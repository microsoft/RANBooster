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
#include <sys/time.h>
#include <math.h>

#include "ranbooster_common.h"

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

#define PRB_9_SIZE (1 + ((NUM_SUBCARRIERS_PRB * IQ_BIT_WIDTH_COMPRESSED * 2) / 8))
#define PRB_16_SIZE (NUM_SUBCARRIERS_PRB * 2 * 2)

int du_active[MAX_NUM_DUS] = {0};

struct rte_mbuf *cached_uplane_packets[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0}; 
int cached_uplane_packets_num[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};
struct rte_mbuf *cached_cplane_packets[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0}; 
int cached_cplane_packets_num[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};

int prach_prb_offset[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};
int prach_num_prbcs[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};

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

int indexofSmallestElement(double array[], int size)
{
    int index = 0;

    for(int i = 1; i < size; i++)
    {
        if(array[i] < array[index])
            index = i;              
    }

    return index;
}

int indexofLargestElement(double array[], int size)
{
    int index = 0;

    for(int i = 1; i < size; i++)
    {
        if(array[i] > array[index])
            index = i;              
    }

    return index;
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

            // Set DU as active
            for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {
                if (rte_is_same_ether_addr(&config->du_configs[du_idx].du_addr, &eth_hdr->src_addr)) {
                    if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {
                        du_active[du_idx] = 1;
                    }
                }
            }

            // Check if all DUs are active
            int active_num_dus = 0;
            for (int active_du_idx = 0; active_du_idx < MAX_NUM_DUS; active_du_idx++) {
                active_num_dus += du_active[active_du_idx];
            }
            
            if (active_num_dus != config->num_dus) {
                rte_pktmbuf_free(buf);
                continue;
            } 

            struct xran_ecpri_hdr *ecpri_hdr;
            ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr)); 
            uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

            uint8_t ecpri_message_type = ecpri_hdr->cmnhdr.bits.ecpri_mesg_type;

            struct xran_cp_radioapp_common_header *apphdr;
            apphdr = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_common_header *, 
                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

            struct xran_cp_radioapp_common_header apphdr_copy = *apphdr;
            *((uint32_t *)&apphdr_copy) = rte_be_to_cpu_32(*((uint32_t *)apphdr));

            uint16_t slot = apphdr_copy.field.slotId;
            uint16_t subframe = apphdr_copy.field.subframeId;
            uint16_t symbol = apphdr_copy.field.startSymbolId;
            // uint16_t frameId = apphdr_copy.field.frameId;

            // Check if this is coming from DUs
            for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                if (rte_is_same_ether_addr(&config->du_configs[du_idx].du_addr, &eth_hdr->src_addr)) {

                    // Check if this was going to the middlebox
                    if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                        // struct timeval tv;
                        // gettimeofday(&tv, NULL);
                        // printf("DU %d ru port %d frame %d symbol %d subframe %d slot %d timestamp seconds %ld microseconds %ld\n", 
                        //         du_idx, ru_port_id, frameId, symbol, slot, tv.tv_sec, tv.tv_usec);

                        // Fix ethernet header to forward to RU
                        rte_ether_addr_copy(&config->ru_config.ru_addr, &eth_hdr->dst_addr);
                        rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                        eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                        if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                            buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                            buf->vlan_tci = config->ru_config.vlan;
                        }

                        if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) { // CPlane

                            cached_cplane_packets[du_idx][ru_port_id][symbol][subframe][slot] = buf;
                            cached_cplane_packets_num[ru_port_id][symbol][subframe][slot]++;

                            assert(apphdr_copy.numOfSections == 1);

                            if (apphdr_copy.sectionType == 1) { // Data

                                // Only send if this is the first packet
                                if (cached_cplane_packets_num[ru_port_id][symbol][subframe][slot] == 1) {

                                    // Send packet
                                    struct xran_cp_radioapp_section1 *section;
                                    section = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_section1 *,
                                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                    + sizeof(struct xran_cp_radioapp_section1_header));

                                    struct xran_cp_radioapp_section1 section_copy = *section;
                                    *((uint64_t *) &section_copy) = rte_be_to_cpu_64(*((uint64_t *)section));

                                    if (section_copy.hdr.u1.common.numPrbc != config->du_prb_size) {
                                        printf("Not expected prbc\n");
                                        exit(-1);
                                    }

                                    section_copy.hdr.u1.common.numPrbc = 0;

                                    *((uint64_t *)section) = rte_cpu_to_be_64(*((uint64_t *) &section_copy));

                                    // Send packet
                                    mx_tx_bufs[mx_tx_idx++] = buf;

                                }

                            } else if (apphdr_copy.sectionType == 3) { // PRACH

                                if (cached_cplane_packets_num[ru_port_id][symbol][subframe][slot] == config->num_dus) {

                                    struct rte_mbuf *bufs_to_be_freed[MAX_NUM_DUS] = {0};
                                    int num_to_free[MAX_NUM_DUS] = {0};

                                    cached_cplane_packets_num[ru_port_id][symbol][subframe][slot] = 0;

                                    double du_frequency_offset[MAX_NUM_DUS] = {0};

                                    for (int id = 0; id < config->num_dus; id++) {

                                        struct rte_mbuf *du_buf = cached_cplane_packets[id][ru_port_id][symbol][subframe][slot];

                                        // Get freq offset
                                        struct xran_cp_radioapp_section3 *section;
                                        section = rte_pktmbuf_mtod_offset(du_buf, struct xran_cp_radioapp_section3 *,
                                                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                        + sizeof(struct xran_cp_radioapp_section3_header));

                                        struct xran_cp_radioapp_section3 section_copy = *section;
                                        *((uint64_t *) &section_copy) = rte_be_to_cpu_64(*((uint64_t *)section));

                                        int32_t freqOffset = ((int32_t)rte_be_to_cpu_32(section_copy.freqOffset))>>8;

                                        double du_cof = config->du_configs[du_idx].center_of_frequency;
                                        double ru_cof = config->ru_config.center_of_frequency;

                                        double frequency_offset_DU = (double)freqOffset * 0.5 * SCS_MHZ;
                                        double frequency_re0rb0 = du_cof - frequency_offset_DU;
                                        double frequency_offset_RU = ru_cof - frequency_re0rb0;
                                        double new_freqOffset = frequency_offset_RU / (0.5 * SCS_MHZ);

                                        assert((double)((int) round(new_freqOffset)) == round(new_freqOffset)); // make sure its an int

                                        du_frequency_offset[id] = frequency_offset_RU;
                                        
                                        if (id > 0) {
                                            bufs_to_be_freed[id] = cached_cplane_packets[id][ru_port_id][symbol][subframe][slot];
                                            num_to_free[id]++;
                                        }

                                    }

                                    struct rte_mbuf *buf_out = cached_cplane_packets[0][ru_port_id][symbol][subframe][slot];

                                    struct xran_cp_radioapp_section3 *section;
                                    section = rte_pktmbuf_mtod_offset(buf_out, struct xran_cp_radioapp_section3 *,
                                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                    + sizeof(struct xran_cp_radioapp_section3_header));

                                    // Change freq offset to min
                                    struct xran_cp_radioapp_section3 section_copy = *section;
                                    *((uint64_t *) &section_copy) = rte_be_to_cpu_64(*((uint64_t *)section));

                                    int min_frequency_offset_idx = indexofSmallestElement(du_frequency_offset, MAX_NUM_DUS);
                                    double min_frequency_offset = du_frequency_offset[min_frequency_offset_idx];
                                    double new_freqOffset = min_frequency_offset / (0.5 * SCS_MHZ);

                                    section_copy.freqOffset = ((int32_t)rte_cpu_to_be_32((int32_t)round(new_freqOffset)))>>8;

                                    double re0rb0_min = config->ru_config.center_of_frequency - min_frequency_offset;

                                    for (int id = 0; id < MAX_NUM_DUS; id++) {
                                        double re0rb0_du = config->ru_config.center_of_frequency - du_frequency_offset[id];

                                        // Check if all offsets are aligned 
                                        if (id != min_frequency_offset_idx) {
                                            assert(fmod(re0rb0_min - re0rb0_du, SCS_MHZ * 12) == 0);
                                        }
                                        
                                        // Calculate prb offset
                                        double prb_offset = (re0rb0_min - re0rb0_du)/(SCS_MHZ * 12);

                                        assert((double)((int) (prb_offset)) == prb_offset); // make sure its an int

                                        prach_prb_offset[id][ru_port_id][symbol][subframe][slot] = (int) prb_offset;
                                    }

                                    // Get new number of prbs
                                    int max_frequency_offset_idx = indexofLargestElement(du_frequency_offset, MAX_NUM_DUS);
                                    double max_frequency_offset = du_frequency_offset[max_frequency_offset_idx];

                                    double re0rb0_max = config->ru_config.center_of_frequency - max_frequency_offset;

                                    uint32_t num_prbs = (uint32_t)((re0rb0_max - re0rb0_min) / (SCS_MHZ * 12));
                                    num_prbs += 12;

                                    section_copy.hdr.u1.common.numPrbc = num_prbs;
                                    *((uint64_t *)section) = rte_cpu_to_be_64(*((uint64_t *) &section_copy));

                                    prach_num_prbcs[ru_port_id][symbol][subframe][slot] = num_prbs;

                                    // Send packet
                                    mx_tx_bufs[mx_tx_idx++] = buf_out;

                                    // Free old bufs
                                    for (int idx = 1; idx < MAX_NUM_DUS; idx++) {
                                        if (num_to_free[idx] > 0) {
                                            rte_pktmbuf_free(bufs_to_be_freed[idx]);
                                            cached_cplane_packets[idx][ru_port_id][symbol][subframe][slot] = 0;
                                        }
                                    }
                                }
                            } else {
                                printf("Not supported cplane section type %d\n", apphdr_copy.sectionType);
                                exit(-1);
                            }

                        } else if (ecpri_message_type == ECPRI_IQ_DATA) { // Downlink UPlane

                            struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));

                            struct data_section_hdr data_hdr_copy = *data_hdr;
                            data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                            if (data_hdr_copy.fields.num_prbu != config->du_prb_size) {
                                printf("numprbu is unexpected %d\n", data_hdr->fields.num_prbu);
                                exit(-1);
                            }

                            cached_uplane_packets[du_idx][ru_port_id][symbol][subframe][slot] = buf;
                            cached_uplane_packets_num[ru_port_id][symbol][subframe][slot]++;

                            if (cached_uplane_packets_num[ru_port_id][symbol][subframe][slot] == config->num_dus) {
                                
                                // New packet
                                struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                                if (new_buf == NULL) {
                                    printf("Couldn't allocate new mbuf downlink uplane\n");
                                    exit(-1);
                                }
                                new_buf->pkt_len = 7676;
                                new_buf->data_len = 7676;

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

                                memset(new_prbs, 0, new_num_prb * PRB_9_SIZE);
                                
                                for (int du_cpy_idx = 0; du_cpy_idx < config->num_dus; du_cpy_idx++) {
                                    uint8_t *old_prbs = rte_pktmbuf_mtod_offset(cached_uplane_packets[du_cpy_idx][ru_port_id][symbol][subframe][slot],
                                                                            uint8_t *, IQ_OFFSET);
                                    memcpy(new_prbs + config->du_configs[du_cpy_idx].prb_offset * PRB_9_SIZE, old_prbs, 
                                            old_num_prb * PRB_9_SIZE);
                                }

                                // Send new packet
                                mx_tx_bufs[mx_tx_idx++] = new_buf;


                                // Free old packet and reset cache
                                cached_uplane_packets_num[ru_port_id][symbol][subframe][slot] = 0;
                                for (int free_idx = 0; free_idx < config->num_dus; free_idx++) {
                                    rte_pktmbuf_free(cached_uplane_packets[free_idx][ru_port_id][symbol][subframe][slot]);
                                    cached_uplane_packets[free_idx][ru_port_id][symbol][subframe][slot] = 0;
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

                    if (ru_port_id >= MAX_PDSCH_PUSCH_PORT) { // PRACH

                        assert(apphdr_copy.numOfSections == config->num_dus);

                        struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                        
                        struct data_section_hdr data_hdr_copy = *data_hdr;
                        data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                        assert(data_hdr_copy.fields.num_prbu == prach_num_prbcs[ru_port_id][symbol][subframe][slot]);

                        for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                            // New packet
                            struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                            if (new_buf == NULL) {
                                printf("Couldn't allocate new mbuf uplink uplane\n");
                                exit(-1);
                            }
                            new_buf->pkt_len = IQ_OFFSET + (12 * PRB_16_SIZE);
                            new_buf->data_len = IQ_OFFSET + (12 * PRB_16_SIZE);

                            // Check compression
                            struct data_section_compression_hdr *comp_hdr;
                            comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                            if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_UNCOMPRESSED) {
                                printf("IQ bit width is %d uplink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                                exit(-1);
                            }

                            // Change num prb
                            struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *,
                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));

                            data_hdr_copy.fields.num_prbu = 12;

                            new_data_hdr->fields.all_bits  = rte_cpu_to_be_32(data_hdr_copy.fields.all_bits);

                            // Copy section
                            int section_offset = IQ_OFFSET + 
                                (prach_prb_offset[du_idx][ru_port_id][symbol][subframe][slot] * PRB_16_SIZE);

                            uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, section_offset);
                            uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);

                            memcpy(new_prbs, old_prbs, 12 * PRB_16_SIZE);

                            // Fix ethernet header to forward to DU
                            struct rte_ether_hdr *new_eth_hdr = rte_pktmbuf_mtod(new_buf, struct rte_ether_hdr *);

                            rte_ether_addr_copy(&config->du_configs[du_idx].du_addr, &new_eth_hdr->dst_addr);
                            rte_ether_addr_copy(&config->middlebox_addr, &new_eth_hdr->src_addr);

                            new_eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                            if (new_buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                new_buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                new_buf->vlan_tci = config->du_configs[du_idx].vlan;
                            }

                            // Send new packet
                            mx_tx_bufs[mx_tx_idx++] = new_buf;

                        }

                        // Free old packet
                        rte_pktmbuf_free(buf);  

                    } else { // Data 

                        struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                        
                        struct data_section_hdr data_hdr_copy = *data_hdr;
                        data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                        if (data_hdr_copy.fields.num_prbu != 0) {
                            printf("Uplink data num prbu is not expected %d\n", data_hdr_copy.fields.num_prbu);
                            exit(-1);
                        }

                        // For each DU sent out a packet
                        for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                            // Check if received a CPlane packet
                            if (cached_cplane_packets[du_idx][ru_port_id][symbol][subframe][slot] == 0) {
                                continue;
                            } else {
                                // Reset cache
                                cached_cplane_packets[du_idx][ru_port_id][symbol][subframe][slot] = 0;
                                cached_cplane_packets_num[ru_port_id][symbol][subframe][slot] = 0;
                            }

                            // New packet
                            struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                            if (new_buf == NULL) {
                                printf("Couldn't allocate new mbuf uplink uplane\n");
                                exit(-1);
                            }

                            // Check compression
                            struct data_section_compression_hdr *comp_hdr;
                            comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                            if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_COMPRESSED) {
                                printf("IQ bit width is %d uplink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                                exit(-1);
                            }
                                
                            new_buf->pkt_len = IQ_OFFSET + config->du_prb_size * PRB_9_SIZE;
                            new_buf->data_len = IQ_OFFSET + config->du_prb_size * PRB_9_SIZE;

                            if (config->du_prb_size == 12) {
                                assert(new_buf->pkt_len == 3000 && new_buf->data_len == 3000);
                            }

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
                            if (new_buf->pkt_len - IQ_OFFSET < new_num_prb * PRB_9_SIZE) {
                                printf("Downlink not enough data to copy into new pkt: pkt_len %d hdr_size %ld offset %d to copy size %ld\n", new_buf->pkt_len, IQ_OFFSET, offset, new_num_prb * PRB_9_SIZE);
                                exit(-1);
                            }

                            memset(new_prbs, 0, new_num_prb * PRB_9_SIZE);
                            memcpy(new_prbs, old_prbs + offset, new_num_prb * PRB_9_SIZE);

                            // Fix ethernet header to forward to DU
                            struct rte_ether_hdr *new_eth_hdr = rte_pktmbuf_mtod(new_buf, struct rte_ether_hdr *);

                            rte_ether_addr_copy(&config->du_configs[du_idx].du_addr, &new_eth_hdr->dst_addr);
                            rte_ether_addr_copy(&config->middlebox_addr, &new_eth_hdr->src_addr);

                            new_eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                            if (new_buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                new_buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                new_buf->vlan_tci = config->du_configs[du_idx].vlan;
                            }

                            // Send new packet
                            mx_tx_bufs[mx_tx_idx++] = new_buf;

                        }

                        // Free old packet
                        rte_pktmbuf_free(buf);

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
    double du1_CofF, du2_CofF, ru_CofF;
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
    ru_CofF = atof(argv[4]);
    du_prb_size_in = atoi(argv[5]);
    printf("VLAN: %d center of frequency: %f DU PRB size: %d\n",
            ru_vlan, ru_CofF, du_prb_size_in);
    parse_mac_address(argv[6], &du1_addr);
    printf("DU1 MAC address: %s\n", argv[6]);
    du1_vlan = atoi(argv[7]);
    du1_CofF = atof(argv[8]);
    du1_prb_offset = atoi(argv[9]);
    printf("VLAN: %d center of frequency: %f PRB offset: %d\n",
            du1_vlan, du1_CofF, du1_prb_offset);
    parse_mac_address(argv[10], &du2_addr);
    printf("DU2 MAC address: %s\n", argv[10]);
    du2_vlan = atoi(argv[11]);
    du2_CofF = atof(argv[12]);
    du2_prb_offset = atoi(argv[13]);
    printf("VLAN: %d center of frequency: %f PRB offset: %d\n",
            du2_vlan, du2_CofF, du2_prb_offset);

    get_port_from_pci(mb_pci_addr_str, &config.nic_port_id);
    rte_eth_macaddr_get(config.nic_port_id, &config.middlebox_addr);

    config.num_dus = MAX_NUM_DUS;

    config.ru_config.vlan = ru_vlan;
    config.ru_config.ru_addr = ru_addr;
    config.ru_config.center_of_frequency = ru_CofF;

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