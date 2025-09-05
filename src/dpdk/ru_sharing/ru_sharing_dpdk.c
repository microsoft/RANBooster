#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ether.h>
#include <rte_bus_pci.h>
#include <rte_dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

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

// Store both control and data plane messages on the same cache
#define ECPRI_TYPE 3
struct rte_mbuf *cached_du_packets[MAX_NUM_DUS][ECPRI_TYPE][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0}; 
int cached_du_packets_num[ECPRI_TYPE][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};

int pending_ul_packet[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};

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

static inline
void resize_mbuf(struct rte_mbuf *mbuf, uint16_t new_len) {
    uint16_t current_len = rte_pktmbuf_pkt_len(mbuf);
    int16_t delta = new_len - current_len;

    if (delta > 0) {
        // Append bytes
        void *data = rte_pktmbuf_append(mbuf, delta);
        if (data == NULL) {
            // Handle error
        } else {
            // Initialize the new data if necessary
        }
    } else if (delta < 0) {
        // Trim bytes
        if (rte_pktmbuf_trim(mbuf, -delta) != 0) {
            // Handle error
        }
    }
    // If delta == 0, no adjustment is needed
}

int next_prach_counter = 0;
int next_prach = 0;

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


            // Check if this is coming from the RU
            if (rte_is_same_ether_addr(&config->ru_config.ru_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {


                    // TODO: Handle RU to middlebox packets
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

                    if (ru_port_id >= MAX_PDSCH_PUSCH_PORT) { // PRACH
                        // printf("The number of sections is %d for port id %d and symbol %d\n", apphdr_copy.numOfSections, ru_port_id, symbol);
                        // assert(apphdr_copy.numOfSections == config->num_dus);
                        int section_offset = sizeof(struct rte_ether_hdr) + \
                                        sizeof(struct xran_ecpri_hdr) + \
                                        sizeof(struct radio_app_common_hdr);

                        apphdr_copy.numOfSections = 1;


                        apphdr->field.all_bits = rte_cpu_to_be_32(apphdr_copy.field.all_bits);
        
                        // For now we assume that the section id corresponds to DU id
                        for (int sect_idx = 0; sect_idx < config->num_dus; sect_idx++) {

                            //printf("Now forwarding to DU %d\n", sect_idx);

                            struct rte_mbuf *du_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);

                            assert(du_buf != NULL);

                            // Copy the data section to the correct location
                            if (sect_idx != 0) {
                                uint8_t *prach_payload =  rte_pktmbuf_mtod_offset(du_buf,
                                    uint8_t *, section_offset + sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr));

                                // First section of the new packet
                                uint8_t *new_prb_location = rte_pktmbuf_mtod_offset(du_buf, uint8_t *, IQ_OFFSET);
                                
                                memcpy(new_prb_location, prach_payload, (PRB_16_SIZE * 12));

                            }
                            
                            // assert(data_hdr_copy.fields.num_prbu == 12);
                            // assert(data_hdr_copy.fields.sect_id == 0);

                            // Trim the rest of the sections
                            rte_pktmbuf_trim(du_buf, (config->num_dus - 1) *
                                (sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr) + (PRB_16_SIZE * 12)));

                            section_offset += ((sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr) + (PRB_16_SIZE * 12)));

                            struct rte_ether_hdr *ethhdr = rte_pktmbuf_mtod(du_buf, struct rte_ether_hdr *);

                            rte_ether_addr_copy(&config->du_configs[sect_idx].du_addr, &ethhdr->dst_addr);
                            rte_ether_addr_copy(&config->middlebox_addr, &ethhdr->src_addr);

                            if (du_buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                du_buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                du_buf->vlan_tci = config->du_configs[sect_idx].vlan;
                            }

                            mx_tx_bufs[mx_tx_idx++] = du_buf;
                        }
                        rte_pktmbuf_free(buf);
                        continue;
                    } else { // DATA
                        struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                        
                        struct data_section_hdr data_hdr_copy = *data_hdr;
                        data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                        assert(data_hdr_copy.fields.num_prbu == 0);

                        for (int id = 0; id < config->num_dus; id++) {
                            // Don't free the one we are sending out
                            if (pending_ul_packet[id][ru_port_id][symbol][subframe][slot] == 1) {

                                // printf("Sending something to DU %d, symbol %d subframe %d slot %d\n", id, symbol, subframe, slot);
                                
                                // This DU is expecting a response, so let's send it:
                                struct rte_mbuf *du_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                                assert(du_buf != NULL);

                                struct rte_ether_hdr *ethhdr = rte_pktmbuf_mtod(du_buf, struct rte_ether_hdr *);

                                rte_ether_addr_copy(&config->du_configs[id].du_addr, &ethhdr->dst_addr);
                                rte_ether_addr_copy(&config->middlebox_addr, &ethhdr->src_addr);

                                if (du_buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                    du_buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                    du_buf->vlan_tci = config->ru_config.vlan;
                                }

                                struct data_section_compression_hdr *comp_hdr;
                                comp_hdr = rte_pktmbuf_mtod_offset(du_buf, struct data_section_compression_hdr *, HDR_SIZE);

                                assert(comp_hdr->ud_comp_hdr.ud_iq_width == IQ_BIT_WIDTH_COMPRESSED);

                                // TODO: This is not fully correct, as the DU might have asked for less
                                uint16_t new_size = IQ_OFFSET + config->du_prb_size * PRB_9_SIZE;

                                // Calculate the current packet length
                                uint16_t current_size = rte_pktmbuf_pkt_len(du_buf);

                                // Calculate the number of bytes to trim
                                uint16_t trim_bytes = current_size > new_size ? current_size - new_size : 0;

                                assert(trim_bytes > 0 && rte_pktmbuf_trim(du_buf, trim_bytes) == 0);

                                uint32_t new_num_prb = config->du_prb_size;
                                struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(du_buf, struct data_section_hdr *, 
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                                sizeof(struct radio_app_common_hdr));
                                data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);

                                new_data_hdr->fields.all_bits = rte_be_to_cpu_32(data_hdr_copy.fields.all_bits);

                                int offset = config->du_configs[id].prb_offset * PRB_9_SIZE;

                                // printf("The PRB offset is %d\n", config->du_configs[id].prb_offset);

                                uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET + offset);
                                uint8_t *new_prbs = rte_pktmbuf_mtod_offset(du_buf, uint8_t *, IQ_OFFSET);

                                memcpy(new_prbs, old_prbs, new_num_prb * PRB_9_SIZE);
                                
                                // printf("Sending something to DU %d, symbol %d subframe %d slot %d\n", id, symbol, subframe, slot);
                                mx_tx_bufs[mx_tx_idx++] = du_buf;

                                pending_ul_packet[id][ru_port_id][symbol][subframe][slot] = 0;
                                break;
                            }
                        }

                        rte_pktmbuf_free(buf);
                        continue;
                    }

                    // For now let's free the UL packets
                    continue;
                
                } else {
                    // If this was not going to the middlebox, ignore it
                    rte_pktmbuf_free(buf);
                    continue;
                }
                // Done with this packet, so continue to the next one
                rte_pktmbuf_free(buf);
                continue;
            }


            bool packet_matched = false;
            // Check if this is coming from DUs
            for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                if (rte_is_same_ether_addr(&config->du_configs[du_idx].du_addr, &eth_hdr->src_addr)) {

                    // Check if this was going to the middlebox
                    if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                        // TODO
                        packet_matched = true;
                        struct xran_ecpri_hdr *ecpri_hdr;
                        ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr)); 
                        uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

                        uint8_t ecpri_message_type = ecpri_hdr->cmnhdr.bits.ecpri_mesg_type;

                        struct radio_app_common_hdr *app_common_hdr = rte_pktmbuf_mtod_offset(buf, struct radio_app_common_hdr *,
                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                        struct radio_app_common_hdr radio_hdr_cpy = *app_common_hdr;
                            radio_hdr_cpy.sf_slot_sym.value = rte_be_to_cpu_16(radio_hdr_cpy.sf_slot_sym.value);

                        uint16_t slot = radio_hdr_cpy.sf_slot_sym.slot_id;
                        uint16_t subframe = radio_hdr_cpy.sf_slot_sym.subframe_id;
                        uint16_t symbol = radio_hdr_cpy.sf_slot_sym.symb_id;

                        rte_ether_addr_copy(&config->ru_config.ru_addr, &eth_hdr->dst_addr);
                        rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                        if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                            buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                            buf->vlan_tci = config->ru_config.vlan;
                        }

                        // Remove outdated packets (empty the cache for this entry)
                        if (cached_du_packets[du_idx][ecpri_message_type][ru_port_id][symbol][subframe][slot] != NULL) {

                            for (int id = 0; id < config->num_dus; id++) {
                                if (cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot] != NULL) {
                                    rte_pktmbuf_free(cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot]);
                                }
                                cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot] = NULL;
                            }
                            cached_du_packets_num[ecpri_message_type][ru_port_id][symbol][subframe][slot] = 0;
                        }

                        // Update the cache
                        cached_du_packets[du_idx][ecpri_message_type][ru_port_id][symbol][subframe][slot] = buf;
                        cached_du_packets_num[ecpri_message_type][ru_port_id][symbol][subframe][slot]++;

                        // In the case of UL control packet, don't wait for the other DUs, as there might be nothing.
                        // Just ask for the whole spectrum at once and we will figure out what to do with it once
                        // we get the response (all packets should be cached by then)

                        struct xran_cp_radioapp_common_header *hdr_check = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_common_header *,
                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                        bool is_ul_control = (app_common_hdr->data_feature.data_direction == 0 && ecpri_message_type == ECPRI_RT_CONTROL_DATA && hdr_check->sectionType == 1);
                        bool send_ul_control = (is_ul_control && cached_du_packets_num[ecpri_message_type][ru_port_id][symbol][subframe][slot] == 1);

                        if (is_ul_control) {
                            for (int sym_idx = 0; sym_idx < 14; sym_idx++) {
                                pending_ul_packet[du_idx][ru_port_id][sym_idx][subframe][slot] = 1;
                            }
                        }

                        // We have all the packets, so time to send them down
                        if (cached_du_packets_num[ecpri_message_type][ru_port_id][symbol][subframe][slot] == config->num_dus || send_ul_control) {
                            
                            if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) { // CPlane

                                struct xran_cp_radioapp_common_header *cp_apphdr = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_common_header *,
                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                                assert(cp_apphdr->numOfSections == 1);

                                if (cp_apphdr->sectionType == 1) { // Data

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
                                    
                                    cached_du_packets_num[ecpri_message_type][ru_port_id][symbol][subframe][slot] = 0;
                                    for (int id = 0; id < config->num_dus; id++) {
                                        // Don't free the one we are sending out
                                        if (buf != cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot]) {
                                            rte_pktmbuf_free(cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot]);
                                        }
                                        cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot] = NULL;
                                    }

                                } else if (cp_apphdr->sectionType == 3) { // PRACH
                            
                                    cp_apphdr->numOfSections = config->num_dus;

                                    struct rte_mbuf *du_buf = rte_pktmbuf_copy(buf, buf->pool, 0, UINT32_MAX);

                                    assert(du_buf != NULL);

                                    assert(rte_pktmbuf_append(du_buf, sizeof(struct xran_cp_radioapp_section3) * cp_apphdr->numOfSections - 1) != NULL);

                                    for (int idx = 0; idx < config->num_dus; idx++) {

                                        struct rte_mbuf* actual_buf = cached_du_packets[idx][ecpri_message_type][ru_port_id][symbol][subframe][slot];

                                        assert(actual_buf != NULL);

                                        struct xran_cp_radioapp_section3 *section;
                                        section = rte_pktmbuf_mtod_offset(du_buf, struct xran_cp_radioapp_section3 *,
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                + sizeof(struct xran_cp_radioapp_section3_header) + (idx * sizeof(struct xran_cp_radioapp_section3)));

                                        struct xran_cp_radioapp_section3 *actual_section;
                                        actual_section = rte_pktmbuf_mtod_offset(actual_buf, struct xran_cp_radioapp_section3 *,
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) 
                                                + sizeof(struct xran_cp_radioapp_section3_header));

                                        *section = *actual_section;

                                        struct xran_cp_radioapp_section3 actual_section_copy = *actual_section;
                                        *((uint64_t *) &actual_section_copy) = rte_be_to_cpu_64(*((uint64_t *)actual_section));

                                        int32_t freqOffset = ((int32_t)rte_be_to_cpu_32(actual_section_copy.freqOffset))>>8;

                                        int num_prbs = actual_section_copy.hdr.u1.common.numPrbc;

                                        //printf("The freqOffset to adjust for id %d is %d and the prbs are %d\n", idx, freqOffset, num_prbs);

                                        //printf("The frequency offset is %d\n", freqOffset);
                                        double freqOffsetMHz = ((float)freqOffset * 0.5 * 30) / 1000;
                                        //printf("The absolute frequency offset is %fMHz\n", freqOffsetMHz);

                                        double freq_placement = config->du_configs[next_prach].center_of_frequency + freqOffsetMHz;
                                        //printf("The frequency placement should be %fMHz\n", freq_placement);
                                        double new_distance = freq_placement - config->ru_config.center_of_frequency;
                                        //printf("This is %fMHz distance from the RU center frequency\n", new_distance);
                                        int32_t new_freqOffset = (new_distance * 1000) / (0.5 * 30);
                                        //printf("So the new offset parameter for DU %d should be %d\n", idx, new_freqOffset);

                                        new_freqOffset &= 0xFFFFFF;
                                        actual_section_copy.freqOffset = rte_cpu_to_be_32(new_freqOffset << 8);
                                        actual_section_copy.hdr.u1.common.sectionId = idx;

                                        *((uint64_t *)section) = rte_cpu_to_be_64(*((uint64_t *) &actual_section_copy));

                                        section->freqOffset = actual_section_copy.freqOffset;
                                    }
                                    mx_tx_bufs[mx_tx_idx++] = du_buf;

                                    next_prach_counter = (next_prach_counter + 1) % 256;
                                    if (next_prach_counter == 0) {
                                        next_prach = (next_prach + 1) % 2;
                                    }

                                    cached_du_packets_num[ecpri_message_type][ru_port_id][symbol][subframe][slot] = 0;
                                    for (int id = 0; id < config->num_dus; id++) {
                                        // Don't free the one we are sending out
                                        if (NULL != cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot]) {
                                            rte_pktmbuf_free(cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot]);
                                        }
                                        cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot] = NULL;
                                    }

                                } else {
                                    printf("Not supported cplane section type %d\n", cp_apphdr->sectionType);
                                    exit(-1);
                                }

                                break;

                            } else if (ecpri_message_type == ECPRI_IQ_DATA) { // Downlink UPlane

                                struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));

                                struct data_section_hdr data_hdr_copy = *data_hdr;
                                data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                                assert(data_hdr_copy.fields.num_prbu != 0);

                                // New packet
                                struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                                assert(new_buf != NULL);

                                uint32_t new_num_prb = 273;

                                resize_mbuf(new_buf, IQ_OFFSET + (new_num_prb * PRB_9_SIZE));

                                // Update new num prb
                                uint32_t old_num_prb = data_hdr_copy.fields.num_prbu;
                                struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *, 
                                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                                    sizeof(struct radio_app_common_hdr));
                                data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);
                                new_data_hdr->fields.all_bits = rte_cpu_to_be_32(data_hdr_copy.fields.all_bits);

                                // Copy prbs
                                uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);
                                
                                for (int du_cpy_idx = 0; du_cpy_idx < config->num_dus; du_cpy_idx++) {
                                    uint8_t *old_prbs = rte_pktmbuf_mtod_offset(cached_du_packets[du_cpy_idx][ecpri_message_type][ru_port_id][symbol][subframe][slot],
                                                                            uint8_t *, IQ_OFFSET);
                                    memcpy(new_prbs + config->du_configs[du_cpy_idx].prb_offset * PRB_9_SIZE, old_prbs, 
                                            old_num_prb * PRB_9_SIZE);
                                }

                                // Send new packet
                                mx_tx_bufs[mx_tx_idx++] = new_buf;

                                // Done with this packet, so free the cache
                                cached_du_packets_num[ecpri_message_type][ru_port_id][symbol][subframe][slot] = 0;
                                for (int id = 0; id < config->num_dus; id++) {
                                    rte_pktmbuf_free(cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot]);
                                    cached_du_packets[id][ecpri_message_type][ru_port_id][symbol][subframe][slot] = NULL;
                                }

                                break;
                            } else {
                                printf("Not supported ecpri message type %d\n", ecpri_message_type);
                                exit(-1);
                            }

                            break;
                        } else { // We don't have all the cached packets yet, so move on to the next one
                            break;
                        }

                    } else {
                        // If this was not going to the middlebox, ignore it
                        rte_pktmbuf_free(buf);
                        break;
                    }
                }
            }

            // Unable to match the packet, with the DUs or the RU, so ignore it
            if (!packet_matched) {
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

    config.mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
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