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

#define MAX_NUM_RUS 2

#define VLAN_VID_MASK    0x0FFF
#define ETHER_JUMBO_FRAME_SIZE 8600

#define IQ_OFFSET   sizeof(struct rte_ether_hdr) + \
                    sizeof(struct xran_ecpri_hdr) + \
                    sizeof(struct radio_app_common_hdr) + \
                    sizeof(struct data_section_hdr) + \
                    sizeof(struct data_section_compression_hdr)

struct rte_mbuf *cached_packets[MAX_NUM_RUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0}; 
int cached_packets_num[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};
uint8_t iq_buffer[MAX_NUM_RUS][NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS][RTE_ETHER_MAX_JUMBO_FRAME_LEN] = {0};

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
    uint16_t nic_port_id;
    struct rte_ether_addr ru_addr;
    struct rte_ether_addr ru_du_addr;
    uint16_t vlan;
    struct rte_mempool *mbuf_pool;
    struct rte_mempool *mbuf_pool_clone;
};

struct middlebox_config {
    int num_rus;
    struct ru_config ru_configs[MAX_NUM_RUS];
    uint16_t nic_port_id;
    struct rte_mempool *mbuf_pool;
    struct rte_ether_addr middlebox_addr;
    struct rte_ether_addr du_addr;
    uint16_t du_vlan;
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

            // Check if this is coming from the DU
            if (rte_is_same_ether_addr(&config->du_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                    if (ecpri_message_type == ECPRI_RT_CONTROL_DATA || ecpri_message_type == ECPRI_IQ_DATA) {

                        rte_pktmbuf_adj(buf, (uint16_t)sizeof(struct rte_ether_hdr));

                        // Send to all the RUs
                        for (int i = 1; i < config->num_rus; i++) {
                            struct rte_mbuf *clone = mcast_out_pkt(buf, config->ru_configs[i].mbuf_pool_clone);
                            assert(clone != NULL);

                            struct rte_ether_hdr *ethdr;

                            ethdr = (struct rte_ether_hdr *)rte_pktmbuf_prepend(clone, (uint16_t)sizeof(*ethdr));

                            rte_ether_addr_copy(&config->ru_configs[i].ru_addr, &ethdr->dst_addr);
                            rte_ether_addr_copy(&config->ru_configs[i].ru_du_addr, &ethdr->src_addr);
                            ethdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                            if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                clone->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                clone->vlan_tci = config->ru_configs[i].vlan;
                            }

                            mx_tx_bufs[mx_tx_idx++] = clone;
                        }

                        struct rte_ether_hdr *ethdr;

                        ethdr = (struct rte_ether_hdr *)rte_pktmbuf_prepend(buf, (uint16_t)sizeof(*ethdr));

                        rte_ether_addr_copy(&config->ru_configs[0].ru_addr, &ethdr->dst_addr);
                        rte_ether_addr_copy(&config->ru_configs[0].ru_du_addr, &ethdr->src_addr);

                        ethdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                        if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                            buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                            buf->vlan_tci = config->ru_configs[0].vlan;
                        }

                        mx_tx_bufs[mx_tx_idx++] = buf;

                    } else {
                        rte_pktmbuf_free(buf);
                        continue;
                    }

                } else { // If this was not going to the middlebox, ignore it
                    rte_pktmbuf_free(buf);
                    continue;
                }

            } else { // If this is not coming from the DU, ignore it
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

        // We now check the RUs
        for (int ru_idx = 0; ru_idx < config->num_rus; ru_idx++) {

            struct rte_mbuf *ru_bufs[BURST_SIZE];
            struct rte_mbuf *ru_tx_bufs[128];
            uint16_t ru_tx_idx = 0;

            nb_rx = rte_eth_rx_burst(config->ru_configs[ru_idx].nic_port_id, 0, ru_bufs, BURST_SIZE);

            for (int rx_idx = 0; rx_idx < nb_rx; rx_idx++) {

                struct rte_mbuf *buf = ru_bufs[rx_idx];

                struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

                // Check if this is coming from the RU
                if (rte_is_same_ether_addr(&config->ru_configs[ru_idx].ru_addr, &eth_hdr->src_addr)) {

                    // Check if this was going to the middlebox
                    if (rte_is_same_ether_addr(&config->ru_configs[ru_idx].ru_du_addr, &eth_hdr->dst_addr)) {

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

                        struct data_section_hdr *data_sec_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));

                        struct data_section_hdr data_sec_hdr_cpy = *data_sec_hdr;
                        data_sec_hdr_cpy.fields.all_bits = rte_be_to_cpu_32(data_sec_hdr->fields.all_bits);

                        int num_prbs = 0;

                        if (ru_port_id < MAX_PDSCH_PUSCH_PORT) {
                            num_prbs = 273;
                        } else {
                            num_prbs = data_sec_hdr_cpy.fields.num_prbu;
                        }

                        cached_packets[ru_idx][ru_port_id][symbol][subframe][slot] = buf;
                        cached_packets_num[ru_port_id][symbol][subframe][slot]++;

                        // If we have all the expected packets, then time to modify and send out.
                        // For this to be true, the counter should be equal to the number of RUs
                        // Note: We do not consider losses currently
                        if (cached_packets_num[ru_port_id][symbol][subframe][slot] == config->num_rus) {
                            struct rte_mbuf *bufs_to_be_freed[MAX_NUM_RUS] = {0};
                            int num_to_free[MAX_NUM_RUS] = { 0 };

                            cached_packets_num[ru_port_id][symbol][subframe][slot] = 0;
                            
                            void *iq_sum_ptr;
                            uint8_t tmp_buf[RTE_ETHER_MAX_JUMBO_FRAME_LEN] = {0};

                            if (ru_port_id < MAX_PDSCH_PUSCH_PORT) {
                                // Use a temporary buffer for storing the sums
                                iq_sum_ptr = tmp_buf;
                            } else {
                                // No compression, so directly use the IQ samples buffer of the first RU
                                iq_sum_ptr = rte_pktmbuf_mtod_offset(cached_packets[0][ru_port_id][symbol][subframe][slot], void *,
                                    IQ_OFFSET);
                            }

                            for (int id = 0; id < config->num_rus; id++) {
                    
                                // If there is compression (only for RU ports < 4), decompress them
                                if (ru_port_id < MAX_PDSCH_PUSCH_PORT) {
                                    struct xranlib_decompress_request dec_req = {0};
                                    dec_req.data_in = rte_pktmbuf_mtod_offset(cached_packets[id][ru_port_id][symbol][subframe][slot], void *,
                                        IQ_OFFSET);
                                    dec_req.numRBs = num_prbs;
                                    dec_req.numDataElements = 24;
                                    dec_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
                                    dec_req.iqWidth = IQ_BIT_WIDTH_COMPRESSED;
                                    dec_req.reMask = 0xfff;
                                    dec_req.csf = 0;
                                    dec_req.ScaleFactor = 1;
                        
                                    // Compressed size
                                    dec_req.len = num_prbs * (((IQ_BIT_WIDTH_COMPRESSED * 2 * NUM_SUBCARRIERS_PRB) / 8) + 1);

                                    struct xranlib_decompress_response dec_resp = {0};
                                    dec_resp.data_out = (int16_t *)iq_buffer[id][ru_port_id][symbol][subframe][slot];
                                    dec_resp.len = num_prbs * ((IQ_BIT_WIDTH_UNCOMPRESSED * 2 * NUM_SUBCARRIERS_PRB) / 8);
                
                                    int res = xranlib_decompress(&dec_req, &dec_resp);

                                    assert(res == 0);
                                }
                    
                                if (id > 0) {
                                    // Add the IQ samples of the new buffer
                                    int16_t *iq_sum = iq_sum_ptr;
                                    int16_t *iq_sum2;
                                    if (ru_port_id < MAX_PDSCH_PUSCH_PORT) {
                                        iq_sum2 = (int16_t *)iq_buffer[id][ru_port_id][symbol][subframe][slot];
                                    } else {
                                        iq_sum2 = rte_pktmbuf_mtod_offset(cached_packets[id][ru_port_id][symbol][subframe][slot], void *,
                                            sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr) + 
                                            sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr));
                                    }
                                    int num_iq = num_prbs * NUM_SUBCARRIERS_PRB * 2;
                                    #define assert__(x) for ( ; !(x) ; assert(x) )
                                    for (int iq_idx = 0; iq_idx < num_iq; iq_idx++) {
                                        iq_sum[iq_idx] += iq_sum2[iq_idx];
                                    }
                                    bufs_to_be_freed[id] = cached_packets[id][ru_port_id][symbol][subframe][slot];
                                    num_to_free[id]++;
                                }

                            }

                            struct rte_mbuf *buf_out = cached_packets[0][ru_port_id][symbol][subframe][slot];

                            if (ru_port_id < MAX_PDSCH_PUSCH_PORT) {
                                struct xranlib_compress_request comp_req = {0};
                                comp_req.data_in = iq_sum_ptr;
                                comp_req.numRBs = num_prbs;
                                comp_req.numDataElements = 24;
                                comp_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
                                comp_req.iqWidth = IQ_BIT_WIDTH_COMPRESSED;
                                comp_req.reMask = 0xfff;
                                comp_req.csf = 0;
                                comp_req.ScaleFactor = 1;
                                comp_req.len = num_prbs * NUM_SUBCARRIERS_PRB * IQ_BIT_WIDTH_UNCOMPRESSED * 2;;

                                struct xranlib_compress_response comp_resp = {0};

                                comp_resp.data_out = rte_pktmbuf_mtod_offset(buf_out, void *,
                                    IQ_OFFSET);
                                comp_resp.len = num_prbs * (((IQ_BIT_WIDTH_COMPRESSED * 2 * NUM_SUBCARRIERS_PRB) / 8) + 1);
                                int res = xranlib_compress(&comp_req, &comp_resp);
                                assert(res == 0);
                            }


                            ru_tx_bufs[ru_tx_idx++] = buf_out;
                            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf_out, struct rte_ether_hdr *);

                            rte_ether_addr_copy(&config->du_addr, &eth_hdr->dst_addr);
                            rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                            if (buf_out->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                buf_out->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                buf_out->vlan_tci = config->du_vlan;
                            }

                            for (int idx = 1; idx < MAX_NUM_RUS; idx++) {
                                if (num_to_free[idx] > 0) {
                                    rte_pktmbuf_free(bufs_to_be_freed[idx]);
                                }
                            }
                        }
                    } else {
                        rte_pktmbuf_free(buf);
                        continue;
                    }
                } else {
                    rte_pktmbuf_free(buf);
                    continue;
                }
            }
            if (ru_tx_idx > 0) {
                uint16_t nb_tx = rte_eth_tx_burst(config->nic_port_id, 0, ru_tx_bufs, ru_tx_idx);
                assert(nb_tx == ru_tx_idx);
                if (unlikely(nb_tx < ru_tx_idx)) {
                    for (int i = nb_tx; i < ru_tx_idx; i++) {
                        rte_pktmbuf_free(ru_tx_bufs[i]);
                    }
                } 
            }
        }
    }
    return 0;
}

int
main(int argc, char *argv[]) {
    const char *mb_pci_addr_str, *ru1_du_pci_addr_str, *ru2_du_pci_addr_str;
    struct rte_ether_addr ru1_addr, ru2_addr, du_addr;
    uint16_t ru1_vlan, ru2_vlan, du_vlan;
    struct middlebox_config config;

    int eal_argc = rte_eal_init(argc, argv);

    printf("Received %d DPDK arguments and we have %d total arguments\n", eal_argc, argc);

    argc -= eal_argc;
    argv += eal_argc;


    // TODO
    if (argc < 3) {
        rte_exit(EXIT_FAILURE, "Usage: %s <PCI_ADDR> <SRC_MAC> <DST_MAC>\n", argv[0]);
    }

    mb_pci_addr_str = argv[1];
    ru1_du_pci_addr_str = argv[2];
    ru1_vlan = atoi(argv[3]);
    parse_mac_address(argv[4], &ru1_addr);
    printf("RU1 MAC address: %s\n", argv[4]);
    ru2_du_pci_addr_str = argv[5];
    ru2_vlan = atoi(argv[6]);
    parse_mac_address(argv[7], &ru2_addr);
    printf("RU2 MAC address: %s\n", argv[7]);
    parse_mac_address(argv[8], &du_addr);
    du_vlan = atoi(argv[9]);

    get_port_from_pci(mb_pci_addr_str, &config.nic_port_id);
    rte_eth_macaddr_get(config.nic_port_id, &config.middlebox_addr);

    config.num_rus = MAX_NUM_RUS;

    get_port_from_pci(ru1_du_pci_addr_str, &config.ru_configs[0].nic_port_id);
    rte_eth_macaddr_get(config.ru_configs[0].nic_port_id, &config.ru_configs[0].ru_du_addr);
    config.ru_configs[0].vlan = ru1_vlan;
    config.ru_configs[0].ru_addr = ru1_addr;

    get_port_from_pci(ru2_du_pci_addr_str, &config.ru_configs[1].nic_port_id);
    rte_eth_macaddr_get(config.ru_configs[1].nic_port_id, &config.ru_configs[1].ru_du_addr);
    config.ru_configs[1].vlan = ru2_vlan;
    config.ru_configs[1].ru_addr = ru2_addr;

    config.du_addr = du_addr;
    config.du_vlan = du_vlan;

    config.mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
    if (config.mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool for middlebox\n");

    if (port_init(config.nic_port_id, config.mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", config.nic_port_id);

    printf("Initialized port %d for middlebox\n", config.nic_port_id);
    print_mac_address_from_portid(config.nic_port_id);

    check_link_status(config.nic_port_id);  

    for (int i = 0; i < config.num_rus; i++) {
        char pool_name[32];
        char pool_name_clone[32];
        snprintf(pool_name, sizeof(pool_name), "RU_POOL_%d", i);
        snprintf(pool_name_clone, sizeof(pool_name), "RU_POOL_%d_clone", i);
        config.ru_configs[i].mbuf_pool = rte_pktmbuf_pool_create(pool_name, NUM_MBUFS,
            MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
        if (config.ru_configs[i].mbuf_pool == NULL) {
            rte_exit(EXIT_FAILURE, "Cannot create mbuf pool %s\n", pool_name);
        }

        config.ru_configs[i].mbuf_pool_clone = rte_pktmbuf_pool_create(pool_name_clone, NUM_MBUFS,
            MBUF_CACHE_SIZE, 0, 2 * RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
        if (config.ru_configs[i].mbuf_pool_clone == NULL) {
            rte_exit(EXIT_FAILURE, "Cannot create mbuf pool %s\n", pool_name);
        }
            

        if (port_init(config.ru_configs[i].nic_port_id, config.ru_configs[i].mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", config.ru_configs[i].nic_port_id);

        printf("Initialized port %d for RU%d\n", config.ru_configs[i].nic_port_id, i+1);
        print_mac_address_from_portid(config.ru_configs[i].nic_port_id);

        check_link_status(config.ru_configs[i].nic_port_id);  

    }

    if (rte_lcore_count() > 1)
        printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

    set_sched_fifo();

    lcore_main(&config);

    return 0;
}