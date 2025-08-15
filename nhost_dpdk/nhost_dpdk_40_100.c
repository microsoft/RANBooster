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
#include <pcap/pcap.h>
#include <signal.h>

#include "ranbooster_common.h"
// #include "xran_compression.h"
// #include "xran_fh_o_du.h"

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

#define ROUND2(x) roundf(x * 100) / 100;

#define PRB_9_SIZE (1 + ((NUM_SUBCARRIERS_PRB * IQ_BIT_WIDTH_COMPRESSED * 2) / 8))
#define PRB_16_SIZE (NUM_SUBCARRIERS_PRB * 2 * 2)

struct rte_mbuf *cached_packets[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0}; 
int cached_packets_num[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};
uint8_t iq_buffer[MAX_NUM_DUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS][RTE_ETHER_MAX_JUMBO_FRAME_LEN] = {0};

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

#define PCAP_SNAPLEN 262144  // Maximum number of bytes captured per packet

static pcap_t *pcap = NULL;
static pcap_dumper_t *pcap_dumper = NULL;

static volatile int s_keep_running = 1;  /// To catch Ctrl-C
static volatile int s_ctrlz_pressed = 0;
void CtrlCHandler(int) { s_keep_running = 0; }
void CtrlZHandler(int) { s_ctrlz_pressed = 1; }

// Signal handler for cleanup
void cleanup_and_exit() {
    if (pcap_dumper) {
        pcap_dump_close(pcap_dumper);
        printf("PCAP dumper closed successfully.\n");
    }
    if (pcap) {
        pcap_close(pcap);
        printf("PCAP handle closed successfully.\n");
    }
    printf("Exiting program.\n");
    exit(EXIT_SUCCESS);
}

// Function to dump an mbuf into a pcap file
void dump_mbuf_to_pcap(struct rte_mbuf *mbuf) {
    if (mbuf == NULL || pcap_dumper == NULL) {
        fprintf(stderr, "Invalid mbuf or pcap dumper\n");
        return;
    }

    // Get the packet length
    uint32_t pkt_len = rte_pktmbuf_pkt_len(mbuf);

    // Retrieve the packet data
    uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);

    // Construct the PCAP packet header
    struct pcap_pkthdr pcap_hdr;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    pcap_hdr.ts.tv_sec = tv.tv_sec;          // Timestamp seconds
    pcap_hdr.ts.tv_usec = tv.tv_usec;        // Timestamp microseconds
    pcap_hdr.caplen = pkt_len;               // Length of the captured portion
    pcap_hdr.len = pkt_len;                  // Length of the original packet

    // Write the packet to the pcap file
    pcap_dump((u_char *)pcap_dumper, &pcap_hdr, pkt_data);
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

    const char *output_file = "nhost_dump.pcap";
    char errbuf[PCAP_ERRBUF_SIZE];

    // Open a pcap file for writing
    pcap = pcap_open_dead(DLT_EN10MB, PCAP_SNAPLEN);
    if (pcap == NULL) {
        fprintf(stderr, "Failed to open pcap: %s\n", errbuf);
        return EXIT_FAILURE;
    }

    pcap_dumper = pcap_dump_open(pcap, output_file);
    if (pcap_dumper == NULL) {
        fprintf(stderr, "Failed to open output file %s: %s\n", output_file, pcap_geterr(pcap));
        pcap_close(pcap);
        return EXIT_FAILURE;
    }

    while (true) {
        if (s_keep_running == 0) break;

        struct rte_mbuf *mx_bufs[BURST_SIZE];
        struct rte_mbuf *mx_tx_bufs[128];
        uint16_t mx_tx_idx = 0;
        uint16_t nb_rx = rte_eth_rx_burst(config->nic_port_id, 0, mx_bufs, BURST_SIZE);

        for (int rx_idx = 0; rx_idx < nb_rx; rx_idx++) {
            struct rte_mbuf *buf = mx_bufs[rx_idx];

            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

            struct xran_ecpri_hdr *ecpri_hdr;

            ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr)); 

            uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

            uint8_t ecpri_message_type = ecpri_hdr->cmnhdr.bits.ecpri_mesg_type;

            // Check if this is coming from DU 1
            if (rte_is_same_ether_addr(&config->du_configs[0].du_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                    // Fix ethernet header to forward to RU
                    rte_ether_addr_copy(&config->ru_config.ru_addr, &eth_hdr->dst_addr);
                    rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                    eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                    if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                        buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                        buf->vlan_tci = config->ru_config.vlan;
                    }

                    if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) {
                        
                        struct xran_cp_radioapp_common_header *apphdr;
                        apphdr = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_common_header *, 
                                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));

                        struct xran_cp_radioapp_common_header apphdr_copy = *apphdr;
                        *((uint32_t *)&apphdr_copy) = rte_be_to_cpu_32(*((uint32_t *)apphdr));

                        assert(apphdr_copy.numOfSections == 1);

                        if (apphdr_copy.sectionType == 1) { // Data

                            // printf("cplane data received\n");

                            struct xran_cp_radioapp_section1 *section;
                            section = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_section1 *,
                                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr)
                                            + sizeof(struct xran_cp_radioapp_section1_header));

                            struct xran_cp_radioapp_section1 section_copy = *section;
                            *((uint64_t *) &section_copy) = rte_be_to_cpu_64(*((uint64_t *)section));

                            if (section_copy.hdr.u1.common.numPrbc != 106) {
                                printf("Not expected prbc\n");
                                exit(-1);
                            }

                            section_copy.hdr.u1.common.numPrbc = 0;

                            *((uint64_t *)section) = rte_cpu_to_be_64(*((uint64_t *) &section_copy));

                        } else if (apphdr_copy.sectionType == 3) { // PRACH

                            // printf("cplane prach received\n");

                            struct xran_cp_radioapp_section3 *section;
                            section = rte_pktmbuf_mtod_offset(buf, struct xran_cp_radioapp_section3 *,
                                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr)
                                            + sizeof(struct xran_cp_radioapp_section3_header));

                            struct xran_cp_radioapp_section3 section_copy = *section;
                            *((uint64_t *) &section_copy) = rte_be_to_cpu_64(*((uint64_t *)section));

                            int32_t freqOffset = ((int32_t)rte_be_to_cpu_32(section_copy.freqOffset))>>8;

                            // section_copy.freqOffset = ((int32_t)rte_cpu_to_be_32(freqOffset - 12))>>8;

                            // double du_cof = 3460.08;
                            // double ru_cof = 3460.26;
                            double du_cof = config->du_configs[0].center_of_frequency * 1000;
                            double ru_cof = config->ru_config.center_of_frequency * 1000;

                            double frequency_offset_DU = (((double)freqOffset) * 1000 * 0.5 * SCS_MHZ);
                            double frequency_re0rb0 = du_cof - frequency_offset_DU;
                            double frequency_offset_RU = ru_cof - frequency_re0rb0;
                            double new_freqOffset_test = (frequency_offset_RU / (0.5 * SCS_MHZ)) / 1000;

                            double new_freqOffset = freqOffset - ((ru_cof - du_cof) / (0.5 * SCS_MHZ) / 1000);

                            // printf("du_cof %.15f\n", du_cof);
                            // printf("ru_cof %.15f\n", ru_cof);
                            // printf("original freqOffset %d cast to double %.15f\n", freqOffset, (double) freqOffset);
                            // printf("frequency_offset_DU %.15f\n", frequency_offset_DU);
                            // printf("frequency_re0rb0 %.15f\n", frequency_re0rb0);
                            // printf("frequency_offset_RU %.15f\n", frequency_offset_RU);
                            // printf("new_freqOffset_test %.15f\n\n", new_freqOffset_test);

                            // printf("new_freqOffset %.15f\n", new_freqOffset);

                            // printf("original freqOffset %d new freqoffset test %.15f new freqOffset %.15f\n", freqOffset, new_freqOffset_test, new_freqOffset);

                            assert(new_freqOffset_test == (int) new_freqOffset_test); // make sure its an int

                            // assert(round(new_freqOffset) == round(new_freqOffset_test));

                            section_copy.freqOffset = ((int32_t)rte_cpu_to_be_32((int32_t)new_freqOffset))>>8;

                            *((uint64_t *)section) = rte_cpu_to_be_64(*((uint64_t *) &section_copy));

                        } else {
                            printf("Not supported cplane section type %d\n", apphdr_copy.sectionType);
                            exit(-1);
                        }

                        // Send packet
                        mx_tx_bufs[mx_tx_idx++] = buf;

                    } else if (ecpri_message_type == ECPRI_IQ_DATA) { // Downlink UPlane

                        struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                        struct data_section_hdr data_hdr_copy = *data_hdr;
                        data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                        assert(data_hdr_copy.fields.num_prbu == 106);

                        // printf("uplane downlink received\n");

                        // New packet
                        // struct rte_mbuf *new_buf = rte_pktmbuf_alloc(config->mbuf_pool);
                        struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                        if (new_buf == NULL) {
                            printf("Couldn't allocate new mbuf downlink uplane\n");
                            exit(-1);
                        }
                        new_buf->pkt_len = 7676;
                        new_buf->data_len = 7676;
                        // char *temp = rte_pktmbuf_append(new_buf, (273 - 106) * PRB_9_SIZE); //7676 - new_buf->pkt_len);
                        // if (temp == NULL) {
                        //     printf("Couldn't add data to new buf new_buf->pkt_len %d new_buf->data_len %d\n", new_buf->pkt_len, new_buf->data_len);
                        //     exit(-1);
                        // }
                        // if (new_buf->pkt_len != 7676 && new_buf->data_len != 7676) {
                        //     printf("Data not added to new buf packet new_buf->pkt_len %d new_buf->data_len %d prb 9 size %d\n", new_buf->pkt_len, new_buf->data_len, PRB_9_SIZE);
                        //     exit(-1);
                        // }

                        // new_buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                        // new_buf->vlan_tci = config->ru_config.vlan;

                        // printf("downlink old pkt_len %d and data_len %d and new pkt_len %d and data_len %d\n", buf->pkt_len, buf->data_len, new_buf->pkt_len, new_buf->data_len);

                        // // Get head data
                        // uint8_t *new_head = rte_pktmbuf_mtod(new_buf, uint8_t *);
                        
                        // // Get head data of old
                        // uint8_t *old_head = rte_pktmbuf_mtod(buf, uint8_t *);

                        // // Copy header
                        // memcpy(new_head, old_head, IQ_OFFSET);

                        // Update new num prb
                        uint32_t old_num_prb = data_hdr_copy.fields.num_prbu;
                        uint32_t new_num_prb = 273;
                        struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *, 
                                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                            sizeof(struct radio_app_common_hdr));
                        data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);
                        new_data_hdr->fields.all_bits = rte_cpu_to_be_32(data_hdr_copy.fields.all_bits);
                        
                        // Check compression
                        struct data_section_compression_hdr *comp_hdr;
                        comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                        if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_COMPRESSED) {
                            printf("IQ bit width is %d downlink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                            exit(-1);
                        }

                        // Copy prbs
                        int offset = config->du_configs[0].prb_offset * PRB_9_SIZE; // To do calculate
                        //(273/2) - (106/2) = 83.5 but the shift in half a prb

                        uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);
                        uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);

                        if (old_num_prb >= new_num_prb) {
                            printf("Downlink new and old num prb not correct new = %d old = %d\n", new_num_prb, old_num_prb);
                            exit(-1);
                        }

                        // Check if there is enough data
                        if (buf->pkt_len - IQ_OFFSET < old_num_prb * PRB_9_SIZE) {
                            printf("Downlink not enough data to copy from old pkt: pkt_len %d hdr_size %ld to copy size %ld\n", buf->pkt_len, IQ_OFFSET, old_num_prb * PRB_9_SIZE);
                            exit(-1);
                        }
                        if (new_buf->pkt_len - IQ_OFFSET - offset < old_num_prb * PRB_9_SIZE) {
                            printf("Downlink not enough data to copy into new pkt: pkt_len %d hdr_size %ld offset %d to copy size %ld\n", new_buf->pkt_len, IQ_OFFSET, offset, old_num_prb * PRB_9_SIZE);
                            exit(-1);
                        }

                        memset(new_prbs, 0, new_num_prb * PRB_9_SIZE);
                        memcpy(new_prbs + offset, old_prbs, old_num_prb * PRB_9_SIZE);

                        // Send new packet and free old
                        rte_pktmbuf_free(buf);
                        mx_tx_bufs[mx_tx_idx++] = new_buf;

                    } else { // Not CPlane or UPlane
                        rte_pktmbuf_free(buf);
                        continue;
                    }

                } else { // If this was not going to the middlebox, ignore it
                    rte_pktmbuf_free(buf);
                    continue;
                }

            // Check if this is coming from the RU
            } else if (rte_is_same_ether_addr(&config->ru_config.ru_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                    // Fix ethernet header to forward to RU
                    rte_ether_addr_copy(&config->du_configs[0].du_addr, &eth_hdr->dst_addr);
                    rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                    eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                    if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                        buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                        buf->vlan_tci = config->du_configs[0].vlan;
                    }
                    
                    // Check num prbu
                    struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                    struct data_section_hdr data_hdr_copy = *data_hdr;
                    data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                    if (ru_port_id >= MAX_PDSCH_PUSCH_PORT) { // PRACH

                        // printf("prach uplink received dst %s src %s\n", &eth_hdr->dst_addr, &eth_hdr->src_addr);

                        assert(data_hdr_copy.fields.num_prbu == 12);

                        // uint8_t *prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);
                        // memset(prbs, 0, 12 * PRB_16_SIZE);

                        // Forward the same packet
                        mx_tx_bufs[mx_tx_idx++] = buf;

                    } else { // Data 

                        assert(data_hdr_copy.fields.num_prbu == 0);
                        
                        // printf("data uplink received\n");

                        // New packet
                        // struct rte_mbuf *new_buf = rte_pktmbuf_alloc(config->mbuf_pool);
                        struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                        if (new_buf == NULL) {
                            printf("Couldn't allocate new mbuf uplink uplane\n");
                            exit(-1);
                        }
                        new_buf->pkt_len = 3000;
                        new_buf->data_len = 3000;

                        // printf("uplink old pkt_len %d and data_len %d and new pkt_len %d and data_len %d\n", buf->pkt_len, buf->data_len, new_buf->pkt_len, new_buf->data_len);

                        // // Get head data
                        // uint8_t *new_head = rte_pktmbuf_mtod(new_buf, uint8_t *);
                        
                        // // Get head data of old
                        // uint8_t *old_head = rte_pktmbuf_mtod(buf, uint8_t *);

                        // // Copy header
                        // memcpy(new_head, old_head, IQ_OFFSET);

                        // Update new num prb
                        uint32_t old_num_prb = 273;
                        uint32_t new_num_prb = 106;
                        struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *, 
                                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                            sizeof(struct radio_app_common_hdr));
                        data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);
                        new_data_hdr->fields.all_bits = rte_cpu_to_be_32(data_hdr_copy.fields.all_bits);
                        
                        // Check compression
                        struct data_section_compression_hdr *comp_hdr;
                        comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                        if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_COMPRESSED) {
                            printf("IQ bit width is %d uplink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                            exit(-1);
                        }

                        // Copy prbs
                        int offset = config->du_configs[0].prb_offset * PRB_9_SIZE; // To do calculate
                        //(273/2) - (106/2) = 83.5 but the shift in half a prb

                        uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);
                        uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);

                        if (old_num_prb <= new_num_prb) {
                            printf("Uplink new and old num prb not correct new = %d old = %d\n", new_num_prb, old_num_prb);
                            exit(-1);
                        }

                        // Check if there is enough data
                        if (buf->pkt_len - IQ_OFFSET < new_num_prb * PRB_9_SIZE) {
                            printf("Uplink not enough data to copy from old pkt: pkt_len %d hdr_size %ld to copy size %ld\n", buf->pkt_len, IQ_OFFSET, new_num_prb * PRB_9_SIZE);
                            exit(-1);
                        }
                        if (new_buf->pkt_len - IQ_OFFSET < new_num_prb * PRB_9_SIZE) {
                            printf("Uplink not enough data to copy into new pkt: pkt_len %d hdr_size %ld offset %d to copy size %ld\n", new_buf->pkt_len, IQ_OFFSET, offset, new_num_prb * PRB_9_SIZE);
                            exit(-1);
                        }

                        memset(new_prbs, 0, new_num_prb * PRB_9_SIZE);
                        memcpy(new_prbs, old_prbs + offset, new_num_prb * PRB_9_SIZE);

                        // Send new packet and free old
                        rte_pktmbuf_free(buf);
                        mx_tx_bufs[mx_tx_idx++] = new_buf;

                        // printf("data uplink sent\n");

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
            // for (int i = 0; i < mx_tx_idx; i++) {
            //     dump_mbuf_to_pcap(mx_tx_bufs[i]);
            // }
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

    // Register signal handler
    signal(SIGINT, CtrlCHandler);
    signal(SIGTSTP, CtrlZHandler);

    lcore_main(&config);

    cleanup_and_exit();

    return 0;
}