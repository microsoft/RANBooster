#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_bus_pci.h>
#include <rte_dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ranbooster_common.h"
#include "xran_compression.h"
#include "xran_fh_o_du.h"

#define RX_RING_SIZE 4096
#define TX_RING_SIZE 4096

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 32
#define BURST_SIZE 32

#define MAX_NUM_DUS 2

#define VLAN_VID_MASK    0x0FFF
#define ETHER_JUMBO_FRAME_SIZE 8600

#define IQ_OFFSET   sizeof(struct rte_ether_hdr) + \
                    sizeof(struct xran_ecpri_hdr) + \
                    sizeof(struct radio_app_common_hdr) + \
                    sizeof(struct data_section_hdr) + \
                    sizeof(struct data_section_compression_hdr)

#define HDR_SIZE    sizeof(struct rte_ether_hdr) + \
                    sizeof(struct xran_ecpri_hdr) + \
                    sizeof(struct radio_app_common_hdr) + \
                    sizeof(struct data_section_hdr)

#define XRAN_CONVERT_NUMPRB(x) ((x) > 255 ? 0 : (x))

// struct prb_9
// {
//     uint8_t resv_exp;
//     int16_t rb0_i_sample:9;
//     int16_t rb0_q_sample:9;
//     int16_t rb1_i_sample:9;
//     int16_t rb1_q_sample:9;
//     int16_t rb2_i_sample:9;
//     int16_t rb2_q_sample:9;
//     int16_t rb3_i_sample:9;
//     int16_t rb3_q_sample:9;
//     int16_t rb4_i_sample:9;
//     int16_t rb4_q_sample:9;
//     int16_t rb5_i_sample:9;
//     int16_t rb5_q_sample:9;
//     int16_t rb6_i_sample:9;
//     int16_t rb6_q_sample:9;
//     int16_t rb7_i_sample:9;
//     int16_t rb7_q_sample:9;
//     int16_t rb8_i_sample:9;
//     int16_t rb8_q_sample:9;
//     int16_t rb9_i_sample:9;
//     int16_t rb9_q_sample:9;
//     int16_t rb10_i_sample:9;
//     int16_t rb10_q_sample:9;
//     int16_t rb11_i_sample:9;
//     int16_t rb11_q_sample:9;

// } __rte_packed;

#define PRB_9_SIZE (NUM_SUBCARRIERS_PRB * IQ_BIT_WIDTH_COMPRESSED * 2) / 8
#define PRB_16_SIZE NUM_SUBCARRIERS_PRB * 2 * 2

struct rte_mbuf *cached_uplane_packets[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0}; 
int cached_uplane_packets_num[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};
struct rte_mbuf *cached_cplane_packets[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0}; 
int cached_cplane_packets_num[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};
int32_t cplane_freq_offset[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};
int cplane_section_type[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mtu = ETHER_JUMBO_FRAME_SIZE,
        .max_lro_pkt_size = ETHER_JUMBO_FRAME_SIZE,
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM | RTE_ETH_RX_OFFLOAD_VLAN_STRIP
    },
    .txmode = {
        .offloads = RTE_ETH_TX_OFFLOAD_VLAN_INSERT | RTE_ETH_TX_OFFLOAD_MULTI_SEGS | RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM | RTE_ETH_TX_OFFLOAD_UDP_CKSUM,
        .mq_mode = RTE_ETH_MQ_TX_NONE,
    },
};


struct ru_config {
    struct rte_ether_addr ru_addr;
    uint16_t vlan;
    double center_of_frequency;
};

struct du_config {
    struct rte_ether_addr du_addr;
    uint16_t vlan;
    uint16_t prb_offset;
    double center_of_frequency;
};

struct middlebox_config {
    int num_dus;
    struct ru_config ru_config;
    uint16_t nic_port_id;
    struct rte_mempool *mbuf_pool;
    struct rte_ether_addr middlebox_addr;
    struct du_config du_configs[MAX_NUM_DUS];
    uint16_t du_prb_size;
};

void set_sched_fifo() {
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_RR);

    if (sched_setscheduler(0, SCHED_RR, &param) == -1) {
        fprintf(stderr, "Failed to set SCHED_FIFO: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = port_conf_default;

    const uint16_t rx_rings = 1, tx_rings = 1;
    int retval;

    if (port >= rte_eth_dev_count_avail())
        return -1;

    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_set_mtu(port, ETHER_JUMBO_FRAME_SIZE);
    if (retval != 0)
        return retval;

    retval = rte_eth_rx_queue_setup(port, 0, RX_RING_SIZE,
        rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0)
        return retval;

    retval = rte_eth_tx_queue_setup(port, 0, TX_RING_SIZE,
        rte_eth_dev_socket_id(port), NULL);
    if (retval < 0)
        return retval;

    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;

    return 0;
}

int get_port_from_pci(const char *pci_address, uint16_t *port_id) {
    int ret = rte_eth_dev_get_port_by_name(pci_address, port_id);
    if (ret != 0) {
        fprintf(stderr, "Error: Could not find port ID for PCI address %s\n", pci_address);
        return ret;
    }
    return 0;
}

static void
parse_mac_address(const char *mac_str, struct rte_ether_addr *mac_addr) {
    if (rte_ether_unformat_addr(mac_str, mac_addr) != 0) {
        rte_exit(EXIT_FAILURE, "Invalid MAC address format: %s\n", mac_str);
    }
}

static void
print_mac_address_from_portid(uint16_t port) {
    struct rte_ether_addr mac_addr;
    rte_eth_macaddr_get(port, &mac_addr);
    printf("Port %u MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           port,
           mac_addr.addr_bytes[0],
           mac_addr.addr_bytes[1],
           mac_addr.addr_bytes[2],
           mac_addr.addr_bytes[3],
           mac_addr.addr_bytes[4],
           mac_addr.addr_bytes[5]);
}

static void
print_mac_address(struct rte_ether_addr mac_addr) {
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_addr.addr_bytes[0],
           mac_addr.addr_bytes[1],
           mac_addr.addr_bytes[2],
           mac_addr.addr_bytes[3],
           mac_addr.addr_bytes[4],
           mac_addr.addr_bytes[5]);
}

void check_link_status(uint16_t port_id) {
    struct rte_eth_link link;
    rte_eth_link_get_nowait(port_id, &link);
    if (!link.link_status) {
        printf("Link is down\n");
    } else {
        printf("Link is up - speed %u Mbps - %s\n",
               link.link_speed,
               (link.link_duplex == RTE_ETH_LINK_FULL_DUPLEX) ? "full-duplex" : "half-duplex");
    }
}

static inline struct rte_mbuf *
mcast_out_pkt(struct rte_mbuf *pkt, struct rte_mempool *pool)
{
        struct rte_mbuf *hdr;

        /* Create new mbuf for the header. */
        hdr = rte_pktmbuf_alloc(pkt->pool);
        assert(hdr != NULL);
        // if ((hdr = rte_pktmbuf_alloc(pkt->pool)) == NULL)
        //         return NULL;

        /* If requested, then make a new clone packet. */
        if ((pkt = rte_pktmbuf_clone(pkt, pool)) == NULL) {
                rte_pktmbuf_free(hdr);
                return NULL;
        }

        /* prepend new header */
        hdr->next = pkt;

        /* update header's fields */
        hdr->pkt_len = (uint16_t)(hdr->data_len + pkt->pkt_len);
        hdr->nb_segs = pkt->nb_segs + 1;

        __rte_mbuf_sanity_check(hdr, 1);
        return hdr;
}

int
lcore_main(void *args) 
{
    struct middlebox_config *config = (struct middlebox_config *)args;

    while (true) {
        struct rte_mbuf *mx_bufs[BURST_SIZE];
        struct rte_mbuf *mx_tx_bufs[128];
        uint16_t mx_tx_idx = 0;
        uint16_t nb_rx = rte_eth_rx_burst(config->nic_port_id, 0, mx_bufs, BURST_SIZE);

        for (int rx_idx = 0; rx_idx < nb_rx; rx_idx++) {
            struct rte_mbuf *buf = mx_bufs[rx_idx];

            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

            struct xran_ecpri_hdr *ecpri_hdr;

            ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr)); 

            uint8_t ecpri_message_type = ecpri_hdr->cmnhdr.bits.ecpri_mesg_type;

            // Check if this is coming from DUs
            for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                if (rte_is_same_ether_addr(&config->du_configs[du_idx].du_addr, &eth_hdr->src_addr)) {

                    // Check if this was going to the middlebox
                    if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                        if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) {

                            struct xran_ecpri_hdr *ecpri_hdr;
                            ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr));
                            uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;
                            
                            struct xran_cp_radioapp_common_header *apphdr;
                            apphdr = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_common_header *, 
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                            struct xran_cp_radioapp_common_header apphdr_copy = *apphdr;
                            *((uint32_t *)&apphdr_copy) = rte_be_to_cpu_32(*((uint32_t *)apphdr));

                            uint16_t slot = apphdr_copy.field.slotId;
                            uint16_t subframe = apphdr_copy.field.subframeId;
                            uint16_t symbol = apphdr_copy.field.startSymbolId;

                            cached_cplane_packets[du_idx][ru_port_id][symbol][subframe][slot] = buf;
                            cached_cplane_packets_num[ru_port_id][symbol][subframe][slot]++;

                            if (cached_cplane_packets_num[ru_port_id][symbol][subframe][slot] == config->num_dus) {

                                cached_cplane_packets_num[ru_port_id][symbol][subframe][slot] = 0;

                                for (int id = 0; id < config->num_dus; id++) {

                                    struct rte_mbuf * du_buf = cached_cplane_packets[id][ru_port_id][symbol][subframe][slot];

                                    struct xran_cp_radioapp_common_header *du_apphdr;
                                    du_apphdr = rte_pktmbuf_mtod_offset(du_buf, struct xran_cp_radioapp_common_header *, 
                                                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                                    struct xran_cp_radioapp_common_header du_apphdr_copy = *du_apphdr;
                                    du_apphdr_copy = *du_apphdr;
                                    *((uint32_t *)&du_apphdr_copy) = rte_be_to_cpu_32(*((uint32_t *)du_apphdr));

                                    assert(apphdr_copy.numOfSections == 1);

                                    if (apphdr_copy.sectionType == 1) { // Data

                                        struct xran_cp_radioapp_section1 *section;
                                        section = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_section1 *,
                                                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                        + sizeof(struct xran_cp_radioapp_section1_header));

                                        struct xran_cp_radioapp_section1 section_copy = *section;
                                        *((uint64_t *) &section_copy) = rte_be_to_cpu_64(*((uint64_t *)section));

                                        assert(section_copy.hdr.u.s1.beamId == 0);

                                        if (section_copy.hdr.u1.common.numPrbc != config->du_prb_size) {
                                            printf("Not expected prbc\n");
                                            exit(-1);
                                        }

                                        cplane_section_type[id][ru_port_id][symbol][subframe][slot] = 1;

                                    } else if (apphdr_copy.sectionType == 3) { // PRACH

                                        struct xran_cp_radioapp_section3 *section;
                                        section = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_section3 *,
                                                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                        + sizeof(struct xran_cp_radioapp_section3_header));

                                        struct xran_cp_radioapp_section3 section_copy = *section;
                                        *((uint64_t *) &section_copy) = rte_be_to_cpu_64(*((uint64_t *)section));

                                        assert(section_copy.hdr.u.s3.beamId == 0);

                                        int32_t freqOffset = ((int32_t)rte_be_to_cpu_32(section->freqOffset))>>8;

                                        cplane_freq_offset[id][ru_port_id][symbol][subframe][slot] = freqOffset;
                                        cplane_section_type[id][ru_port_id][symbol][subframe][slot] = 3;

                                    } else {
                                        printf("Not supported cplane section type %d\n", apphdr_copy.sectionType);
                                        exit(-1);
                                    }
                                }

                                // New packet
                                struct rte_mbuf *new_buf = rte_pktmbuf_alloc(config->mbuf_pool);
                                if (new_buf == NULL) {
                                    printf("Couldn't allocate new mbuf uplink uplane\n");
                                    exit(-1);
                                }
                                new_buf->pkt_len = sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                        + sizeof(struct xran_cp_radioapp_section1_header)
                                                        + sizeof(struct xran_cp_radioapp_section1);
                                new_buf->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                        + sizeof(struct xran_cp_radioapp_section1_header)
                                                        + sizeof(struct xran_cp_radioapp_section1);
                                
                                struct rte_ether_hdr *ethdr = rte_pktmbuf_mtod(new_buf, struct rte_ether_hdr *);

                                rte_ether_addr_copy(&config->ru_config.ru_addr, &ethdr->dst_addr);
                                rte_ether_addr_copy(&config->middlebox_addr, &ethdr->src_addr);

                                ethdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                                if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                    buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                    buf->vlan_tci = config->ru_config.vlan;
                                }

                                struct xran_ecpri_hdr *new_ecpri_hdr = rte_pktmbuf_mtod_offset(new_buf, 
                                                    struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr));
                                *((uint32_t *)new_ecpri_hdr) = *((uint32_t *)ecpri_hdr);
                                
                                struct xran_cp_radioapp_common_header *new_apphdr;
                                new_apphdr = rte_pktmbuf_mtod_offset(new_buf, struct xran_cp_radioapp_common_header *, 
                                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));
                                *((uint32_t *)new_apphdr) = *((uint32_t *)apphdr);

                                struct xran_cp_radioapp_section1_header *new_section_hdr;
                                new_section_hdr = rte_pktmbuf_mtod_offset(new_buf, struct xran_cp_radioapp_section1 *,
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));
                                new_section_hdr->cmnhdr.field.dataDirection = ; // What do I do here
                                new_section_hdr->cmnhdr.field.filterIndex = ; // What do I do here
                                new_section_hdr->cmnhdr.field.frameId = apphdr_copy.field.frameId;
                                new_section_hdr->cmnhdr.field.payloadVer = apphdr_copy.field.payloadVer;
                                new_section_hdr->cmnhdr.field.slotId = slot;
                                new_section_hdr->cmnhdr.field.startSymbolId = symbol;
                                new_section_hdr->cmnhdr.field.subframeId = subframe;
                                new_section_hdr->reserved = 0;
                                new_section_hdr->udComp.udCompMeth = XRAN_COMPMETHOD_BLKFLOAT;
                                new_section_hdr->udComp.udIqWidth = IQ_BIT_WIDTH_COMPRESSED;
                                *((uint32_t *)new_section_hdr) = rte_be_to_cpu_32(*((uint32_t *)new_section_hdr));

                                struct xran_cp_radioapp_section1 *new_section;
                                new_section = rte_pktmbuf_mtod_offset(new_buf, struct xran_cp_radioapp_section1 *,
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                + sizeof(struct xran_cp_radioapp_section1_header));
                                new_section->hdr.u1.common.numPrbc = 273;
                                new_section->hdr.u1.common.rb = 0;
                                new_section->hdr.u1.common.sectionId = ; // What do I do here
                                new_section->hdr.u1.common.startPrbc = 0;
                                new_section->hdr.u1.common.symInc = 0;
                                new_section->hdr.u.s1.beamId = ;
                                new_section->hdr.u.s1.ef = 0;
                                new_section->hdr.u.s1.numSymbol = 1;
                                new_section->hdr.u.s1.reMask = 0b111111111111;
                                *((uint32_t *)new_section) = rte_be_to_cpu_32(*((uint32_t *)new_section));

                                // Send packet
                                mx_tx_bufs[mx_tx_idx++] = new_buf;

                                // Free old bufs
                                for (int free_idx = 1; free_idx < config->num_dus; free_idx++) {
                                    rte_pktmbuf_free(cached_cplane_packets[free_idx][ru_port_id][symbol][subframe][slot]);
                                }
                            }

                        } else if (ecpri_message_type == ECPRI_IQ_DATA) { // Downlink UPlane

                            struct xran_ecpri_hdr *ecpri_hdr;
                            ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr));
                            uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

                            struct radio_app_common_hdr *app_common_hdr = rte_pktmbuf_mtod_offset(buf, struct radio_app_common_hdr *,
                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                            struct radio_app_common_hdr radio_hdr_cpy = *app_common_hdr;
                            radio_hdr_cpy.sf_slot_sym.value = rte_be_to_cpu_16(radio_hdr_cpy.sf_slot_sym.value);

                            uint16_t slot = radio_hdr_cpy.sf_slot_sym.slot_id;
                            uint16_t subframe = radio_hdr_cpy.sf_slot_sym.subframe_id;
                            uint16_t symbol = radio_hdr_cpy.sf_slot_sym.symb_id;

                            struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                            struct data_section_hdr data_hdr_copy;
                                data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                            if (data_hdr_copy.fields.num_prbu != config->du_prb_size) {
                                printf("numprbu is unexpected %d\n", data_hdr->fields.num_prbu);
                                exit(-1);
                            }

                            cached_uplane_packets[du_idx][ru_port_id][symbol][subframe][slot] = buf;
                            cached_uplane_packets_num[ru_port_id][symbol][subframe][slot]++;

                            if (cached_uplane_packets_num[ru_port_id][symbol][subframe][slot] == config->num_dus) {
                                
                                cached_uplane_packets_num[ru_port_id][symbol][subframe][slot] = 0;

                                // New packet
                                struct rte_mbuf *new_buf = rte_pktmbuf_alloc(config->mbuf_pool);
                                if (new_buf == NULL) {
                                    printf("Couldn't allocate new mbuf downlink uplane\n");
                                    exit(-1);
                                }
                                new_buf->pkt_len = 7680;
                                new_buf->data_len = 7680;
                                // printf("downlink old pkt_len %d and data_len %d and new pkt_len %d and data_len %d\n", buf->pkt_len, buf->data_len, new_buf->pkt_len, new_buf->data_len);

                                // Get head data
                                uint8_t *new_head = rte_pktmbuf_mtod(new_buf, uint8_t *);
                                
                                // Get head data of old
                                uint8_t *du1_head = rte_pktmbuf_mtod(cached_uplane_packets[0][ru_port_id][symbol][subframe][slot],
                                                                        uint8_t *);

                                // Copy header
                                memcpy(new_head, du1_head, IQ_OFFSET);

                                // Update new num prb
                                uint32_t old_num_prb = data_hdr_copy.fields.num_prbu;
                                uint32_t new_num_prb = 273;
                                struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *, 
                                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                                    sizeof(struct radio_app_common_hdr));
                                data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);
                                new_data_hdr->fields.all_bits = rte_be_to_cpu_32(data_hdr_copy.fields.all_bits);
                                
                                // Check compression
                                struct data_section_compression_hdr *comp_hdr;
                                comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                                if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_COMPRESSED) {
                                    printf("IQ bit width is %d downlink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                                    exit(-1);
                                }

                                // Copy prbs
                                uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);
                                uint8_t *du1_prbs = rte_pktmbuf_mtod_offset(cached_uplane_packets[0][ru_port_id][symbol][subframe][slot],
                                                                            uint8_t *, IQ_OFFSET);
                                uint8_t *du2_prbs = rte_pktmbuf_mtod_offset(cached_uplane_packets[0][ru_port_id][symbol][subframe][slot],
                                                                            uint8_t *, IQ_OFFSET);

                                memset(new_prbs, 0, new_num_prb * PRB_9_SIZE);
                                for (int du_cpy_idx = 0; du_cpy_idx < config->num_dus; du_cpy_idx++) {
                                    memcpy(new_prbs, du1_prbs + config->du_configs[du_cpy_idx].prb_offset * PRB_9_SIZE, 
                                            old_num_prb * PRB_9_SIZE);
                                }

                                // Send new packet and free old
                                mx_tx_bufs[mx_tx_idx++] = new_buf;

                                for (int free_idx = 1; free_idx < config->num_dus; free_idx++) {
                                    rte_pktmbuf_free(cached_uplane_packets[free_idx][ru_port_id][symbol][subframe][slot]);
                                }

                            }
                        
                        } else { // Not CPlane or UPlane
                            rte_pktmbuf_free(buf);
                            continue;
                        }

                    } else { // If this was not going to the middlebox, ignore it
                        rte_pktmbuf_free(buf);
                        continue;
                    }

                }
            }

            // Check if this is coming from the RU
            if (rte_is_same_ether_addr(&config->ru_config.ru_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                    // Fix ethernet header to forward to RU
                    rte_pktmbuf_adj(buf, (uint16_t)sizeof(struct rte_ether_hdr));

                    struct rte_ether_hdr *ethdr;
                    ethdr = (struct rte_ether_hdr *)rte_pktmbuf_prepend(buf, (uint16_t)sizeof(*ethdr));

                    rte_ether_addr_copy(&config->du_configs[0].du_addr, &ethdr->dst_addr);
                    rte_ether_addr_copy(&config->middlebox_addr, &ethdr->src_addr);

                    ethdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                    struct xran_ecpri_hdr *ecpri_hdr;
                    ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr));
                    uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

                    // Check num prbu
                    struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                    struct data_section_hdr data_hdr_copy;
                        data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                    if (data_hdr_copy.fields.num_prbu == 12) { // PRACH

                        printf("Shouldn't have uplink 12 prbs anymore.\n");
                        exit(-1);

                    } else if (data_hdr_copy.fields.num_prbu == 0) { // Data 

                        // For each DU sent out a packet
                        for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                            struct radio_app_common_hdr *apphdr;
                            apphdr = rte_pktmbuf_mtod_offset(buf, struct radio_app_common_hdr *, 
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                            struct radio_app_common_hdr apphdr_copy = *apphdr;
                            *((uint32_t *)&apphdr_copy) = rte_be_to_cpu_32(*((uint32_t *)apphdr));

                            uint16_t slot = apphdr_copy.sf_slot_sym.slot_id;
                            uint16_t subframe = apphdr_copy.sf_slot_sym.subframe_id;
                            uint16_t symbol = apphdr_copy.sf_slot_sym.symb_id;

                            // New packet
                            struct rte_mbuf *new_buf = rte_pktmbuf_alloc(config->mbuf_pool);
                            if (new_buf == NULL) {
                                printf("Couldn't allocate new mbuf uplink uplane\n");
                                exit(-1);
                            }

                            // Get head data of old
                            uint8_t *old_head = rte_pktmbuf_mtod(buf, uint8_t *);

                            // Get head data
                            uint8_t *new_head = rte_pktmbuf_mtod(new_buf, uint8_t *);

                            // Check compression
                            struct data_section_compression_hdr *comp_hdr;
                            comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                            if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_COMPRESSED) {
                                printf("IQ bit width is %d uplink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                                exit(-1);
                            }

                            if (cplane_section_type[du_idx][ru_port_id][symbol][subframe][slot] == 1) { // data

                                new_buf->pkt_len = IQ_OFFSET + config->du_prb_size * PRB_16_SIZE;
                                new_buf->data_len = IQ_OFFSET + config->du_prb_size * PRB_16_SIZE;

                                // Copy header
                                memcpy(new_head, old_head, IQ_OFFSET);
                                // fix section id?

                                // Change compression
                                struct data_section_compression_hdr *new_comp_hdr;
                                new_comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);
                                new_comp_hdr->ud_comp_hdr.ud_iq_width = IQ_BIT_WIDTH_UNCOMPRESSED; // NO NEED TO CHANGE ORDER?

                                // Update new num prb
                                uint32_t old_num_prb = 273;
                                uint32_t new_num_prb = 12;
                                struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *, 
                                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                                    sizeof(struct radio_app_common_hdr));
                                data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);
                                new_data_hdr->fields.all_bits = rte_be_to_cpu_32(data_hdr_copy.fields.all_bits);
                                
                                // Copy prbs
                                double freqOffset = cplane_freq_offset[du_idx][ru_port_id][symbol][subframe][slot];
                                double frequency_offset = freqOffset * 0.5 * SCS_MHZ;
                                double frequency_PRB0_bottom_DUA = config->du_configs[du_idx].center_of_frequency - frequency_offset - (0.5 * SCS_MHZ);
                                double frequency_PRB0_bottom_RU = config->ru_config.center_of_frequency - (SCS_MHZ * NUM_SUBCARRIERS_PRB * (273/2));
                                double offset_prb_idx = (frequency_PRB0_bottom_DUA - frequency_PRB0_bottom_RU) / (SCS_MHZ * NUM_SUBCARRIERS_PRB);
                                assert((int) (offset_prb_idx) == offset_prb_idx);
                                int offset = (int) (offset_prb_idx) * PRB_16_SIZE;

                                uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);
                                uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);

                                // Uncompress prbs
                                struct xranlib_decompress_request dec_req = {0};
                                dec_req.data_in = rte_pktmbuf_mtod_offset(buf, void *,
                                    IQ_OFFSET + offset);
                                dec_req.numRBs = new_num_prb;
                                dec_req.numDataElements = 24; // What is this?
                                dec_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
                                dec_req.iqWidth = IQ_BIT_WIDTH_COMPRESSED;
                                dec_req.reMask = 0xfff;
                                dec_req.csf = 0;
                                dec_req.ScaleFactor = 1;
                    
                                // Compressed size
                                dec_req.len = new_num_prb * (((IQ_BIT_WIDTH_COMPRESSED * 2 * NUM_SUBCARRIERS_PRB) / 8) + 1);

                                struct xranlib_decompress_response dec_resp = {0};
                                dec_resp.data_out = (int16_t *)new_prbs; // Decompress into the packet
                                dec_resp.len = new_num_prb * ((IQ_BIT_WIDTH_UNCOMPRESSED * 2 * NUM_SUBCARRIERS_PRB) / 8);
            
                                int res = xranlib_decompress(&dec_req, &dec_resp);

                            } else if (cplane_section_type[du_idx][ru_port_id][symbol][subframe][slot] == 3) { // PRACH
                                
                                new_buf->pkt_len = IQ_OFFSET + config->du_prb_size * PRB_9_SIZE;
                                new_buf->data_len = IQ_OFFSET + config->du_prb_size * PRB_9_SIZE;
                                // printf("uplink old pkt_len %d and data_len %d and new pkt_len %d and data_len %d\n", buf->pkt_len, buf->data_len, new_buf->pkt_len, new_buf->data_len);

                                // Copy header
                                memcpy(new_head, old_head, IQ_OFFSET);

                                // Update new num prb
                                uint32_t old_num_prb = 273;
                                uint32_t new_num_prb = config->du_prb_size;
                                struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *, 
                                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                                    sizeof(struct radio_app_common_hdr));
                                data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);
                                new_data_hdr->fields.all_bits = rte_be_to_cpu_32(data_hdr_copy.fields.all_bits);

                                // Copy prbs
                                int offset = config->du_configs[du_idx].prb_offset * PRB_9_SIZE;

                                uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);
                                uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);

                                if (old_num_prb <= new_num_prb) {
                                    printf("Uplink new and old num prb not correct new = %d old = %d\n", new_num_prb, old_num_prb);
                                    exit(-1);
                                }

                                // Check if there is enough data
                                if (buf->pkt_len - IQ_OFFSET < new_num_prb * PRB_9_SIZE) {
                                    printf("Downlink not enough data to copy from old pkt: pkt_len %d hdr_size %ld to copy size %ld\n", buf->pkt_len, IQ_OFFSET, new_num_prb * PRB_9_SIZE);
                                    exit(-1);
                                }
                                if (new_buf->pkt_len - IQ_OFFSET - offset < new_num_prb * PRB_9_SIZE) {
                                    printf("Downlink not enough data to copy into new pkt: pkt_len %d hdr_size %ld offset %d to copy size %ld\n", new_buf->pkt_len, IQ_OFFSET, offset, new_num_prb * PRB_9_SIZE);
                                    exit(-1);
                                }

                                memset(new_prbs, 0, new_num_prb * PRB_9_SIZE);
                                memcpy(new_prbs, old_prbs + offset, new_num_prb * PRB_9_SIZE);

                            }

                            // Send new packet
                            mx_tx_bufs[mx_tx_idx++] = new_buf;

                            // Free old packet
                            rte_pktmbuf_free(buf);

                        }

                    } else {
                        printf("numprbu is unexpected %d\n", data_hdr->fields.num_prbu);
                        exit(-1);
                    }
                    
                } else { // If this was not going to the middlebox, ignore it
                    rte_pktmbuf_free(buf);
                    continue;
                }
            
            } else { // If this is not coming from the RU or DU, ignore it
                rte_pktmbuf_free(buf);
                continue;
            }

        }

        if (mx_tx_idx > 0) {
            uint16_t nb_tx = rte_eth_tx_burst(config->nic_port_id, 0, mx_tx_bufs, mx_tx_idx);
            assert(nb_tx == mx_tx_idx);
            if (unlikely(nb_tx < mx_tx_idx)) {
                for (int i = nb_tx; i < mx_tx_idx; i++) {
                    rte_pktmbuf_free(mx_tx_bufs[i]);
                }
            }
        }

    }
    return 0;
}

int
main(int argc, char *argv[]) {
    const char *mb_pci_addr_str;
    struct rte_ether_addr ru_addr, du1_addr, du2_addr;
    uint16_t ru_vlan, du1_vlan, du2_vlan, du1_prb_offset, du2_prb_offset, du_prb_size_in;
    double du1_CofF, du2_CofF;
    struct middlebox_config config;

    int eal_argc = rte_eal_init(argc, argv);

    printf("Received %d DPDK arguments and we have %d total arguments\n", eal_argc, argc);

    argc -= eal_argc;
    argv += eal_argc;


    if (argc < 3) {
        rte_exit(EXIT_FAILURE, "Usage: %s <PCI_ADDR> <RU_MAC> <RU_VLAN> <RU_CofF> <DU_PRB_SIZE> <DU1_ADDR> <DU1_VLAN> <DU1_CofF> <DU1_PRB_OFFSET> <DU2_ADDR> <DU2_VLAN> <DU2_CofF> <DU2_PRB_OFFSET>\n", argv[0]);
    }

    mb_pci_addr_str = argv[1];
    parse_mac_address(argv[2], &ru_addr);
    printf("RU MAC address: %s\n", argv[2]);
    ru_vlan = atoi(argv[3]);
    du_prb_size_in = atoi(argv[4]);
    parse_mac_address(argv[5], &du1_addr);
    printf("DU1 MAC address: %s\n", argv[5]);
    du1_vlan = atoi(argv[6]);
    du1_CofF = atof(argv[7]);
    du1_prb_offset = atoi(argv[8]);
    parse_mac_address(argv[9], &du2_addr);
    printf("DU2 MAC address: %s\n", argv[9]);
    du2_vlan = atoi(argv[10]);
    du2_CofF = atof(argv[11]);
    du2_prb_offset = atoi(argv[12]);

    get_port_from_pci(mb_pci_addr_str, &config.nic_port_id);
    rte_eth_macaddr_get(config.nic_port_id, &config.middlebox_addr);

    config.num_dus = MAX_NUM_DUS;

    config.ru_config.vlan = ru_vlan;
    config.ru_config.ru_addr = ru_addr;
    config.ru_config.center_of_frequency = du1_prb_offset;

    config.du_configs[0].du_addr = du1_addr;
    config.du_configs[0].vlan = du1_vlan;
    config.du_configs[0].prb_offset = du1_prb_offset;
    config.du_configs[0].center_of_frequency = du1_CofF;
    config.du_configs[1].du_addr = du2_addr;
    config.du_configs[1].vlan = du2_vlan;
    config.du_configs[1].prb_offset = du2_prb_offset;
    config.du_configs[1].center_of_frequency = du2_CofF;

    config.du_prb_size = du_prb_size_in;

    config.mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS*32,
        MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
    if (config.mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool for middlebox\n");

    if (port_init(config.nic_port_id, config.mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", config.nic_port_id);

    printf("Initialized port %d for middlebox\n", config.nic_port_id);
    print_mac_address_from_portid(config.nic_port_id);

    check_link_status(config.nic_port_id);  

    if (rte_lcore_count() > 1)
        printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

    set_sched_fifo();

    lcore_main(&config);

    return 0;
}