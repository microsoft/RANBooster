/*
 * Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License
 */

#include "ranbooster_common.h"

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
#include <stdatomic.h>
#include <pthread.h>


#define RX_RING_SIZE 4096
#define TX_RING_SIZE 4096

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 32
#define BURST_SIZE 32

#define MONITORING_SYMBOL_INGRESS (0)
#define MONITORING_SYMBOL_EGRESS (1)

#define IQ_OFFSET   sizeof(struct rte_ether_hdr) + \
                    sizeof(struct xran_ecpri_hdr) + \
                    sizeof(struct radio_app_common_hdr) + \
                    sizeof(struct data_section_hdr) + \
                    sizeof(struct data_section_compression_hdr)

#define VLAN_VID_MASK    0x0FFF
#define ETHER_JUMBO_FRAME_SIZE 8600

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
    struct ru_config ru_configs;
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

static atomic_uint total_dl_prbs_used = 0;
static atomic_uint dl_prbs_count = 0;

static atomic_uint total_ul_prbs_used = 0;
static atomic_uint ul_prbs_count = 0;

void* stats_thread_func(void* arg) {
    while (1) {
        sleep(1); // Sleep for 1 second

        unsigned int total_dl_prbs = atomic_load(&total_dl_prbs_used);
        unsigned int dl_count = atomic_load(&dl_prbs_count);

        unsigned int total_ul_prbs = atomic_load(&total_ul_prbs_used);
        unsigned int ul_count = atomic_load(&ul_prbs_count);

        if (dl_count > 0) {
            double average_prbs = (double)total_dl_prbs / dl_count;
            printf("Average DL PRBs used: %.2f\n", average_prbs);

            // Reset counters
            atomic_store(&total_dl_prbs_used, 0);
            atomic_store(&dl_prbs_count, 0);
        }

        if (ul_count > 0) {
            double average_prbs = (double)total_ul_prbs / ul_count;
            //printf("Average UL PRBs used: %.2f\n", average_prbs);

            // Reset counters
            atomic_store(&total_ul_prbs_used, 0);
            atomic_store(&ul_prbs_count, 0);
        }
    }
    return NULL;
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
            uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

            // Check if this is coming from the DU
            if (rte_is_same_ether_addr(&config->du_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                    if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) {

                        rte_ether_addr_copy(&config->ru_configs.ru_addr, &eth_hdr->dst_addr);
                        rte_ether_addr_copy(&config->ru_configs.ru_du_addr, &eth_hdr->src_addr);
                        
                        if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                            buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                            buf->vlan_tci = config->ru_configs.vlan;
                        }

                        mx_tx_bufs[mx_tx_idx++] = buf;
                    } else if (ecpri_message_type == ECPRI_IQ_DATA) {

                        rte_ether_addr_copy(&config->ru_configs.ru_addr, &eth_hdr->dst_addr);
                        rte_ether_addr_copy(&config->ru_configs.ru_du_addr, &eth_hdr->src_addr);
                        
                        if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                            buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                            buf->vlan_tci = config->ru_configs.vlan;
                        }

                        if (ru_port_id == SELECTED_RU_PORT_ID) {
                            struct radio_app_common_hdr *app_common_hdr = rte_pktmbuf_mtod_offset(buf, struct radio_app_common_hdr *,
                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));
                        
                            struct radio_app_common_hdr radio_hdr_cpy = *app_common_hdr;
                            radio_hdr_cpy.sf_slot_sym.value = rte_be_to_cpu_16(radio_hdr_cpy.sf_slot_sym.value);
                            uint16_t symbol = radio_hdr_cpy.sf_slot_sym.symb_id;
                            
                            if (symbol == MONITORING_SYMBOL_EGRESS) {
                                struct data_section_hdr *data_sec_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));

                                struct data_section_hdr data_sec_hdr_cpy = *data_sec_hdr;
                                data_sec_hdr_cpy.fields.all_bits = rte_be_to_cpu_32(data_sec_hdr->fields.all_bits);

                                int num_prbs = data_sec_hdr_cpy.fields.num_prbu;
                                if (num_prbs == 0) {
                                    num_prbs = 273;
                                }

                                uint8_t *iq_samples_head = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);

                                int num_prbs_dl_used = 0;

                                for (int rb_id = 0; rb_id < num_prbs; rb_id++) {

                                    

                                    union compression_params *comp_params = (union compression_params *)iq_samples_head;

                                    if (comp_params->blockFlPoint.exponent > 0) {
                                        num_prbs_dl_used++;
                                    }

                                    iq_samples_head += COMPRESSED_RB_SIZE_BYTES;
                                }
                                atomic_fetch_add(&total_dl_prbs_used, num_prbs_dl_used);
                                atomic_fetch_add(&dl_prbs_count, 1);

                            }
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


        //  uint64_t start_tsc = rte_rdtsc();


        // We now check the RU
        struct rte_mbuf *ru_bufs[BURST_SIZE];
        struct rte_mbuf *ru_tx_bufs[128];
        int ru_tx_idx = 0;

        nb_rx = rte_eth_rx_burst(config->ru_configs.nic_port_id, 0, ru_bufs, BURST_SIZE);

        for (int rx_idx = 0; rx_idx < nb_rx; rx_idx++) {

            struct rte_mbuf *buf = ru_bufs[rx_idx];

            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

            struct xran_ecpri_hdr *ecpri_hdr;

            ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr));
            
            uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

            // Check if this is coming from the RU
            if (rte_is_same_ether_addr(&config->ru_configs.ru_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->ru_configs.ru_du_addr, &eth_hdr->dst_addr)) {

                    rte_ether_addr_copy(&config->du_addr, &eth_hdr->dst_addr);
                    rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                    if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                        buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                        buf->vlan_tci = config->du_vlan;
                    }

                    if (ru_port_id == SELECTED_RU_PORT_ID) {
                            struct radio_app_common_hdr *app_common_hdr = rte_pktmbuf_mtod_offset(buf, struct radio_app_common_hdr *,
                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr));
                        
                            struct radio_app_common_hdr radio_hdr_cpy = *app_common_hdr;
                            radio_hdr_cpy.sf_slot_sym.value = rte_be_to_cpu_16(radio_hdr_cpy.sf_slot_sym.value);
                            uint16_t symbol = radio_hdr_cpy.sf_slot_sym.symb_id;
                            
                            if (symbol == MONITORING_SYMBOL_INGRESS) {
                                struct data_section_hdr *data_sec_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                                    sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));

                                struct data_section_hdr data_sec_hdr_cpy = *data_sec_hdr;
                                data_sec_hdr_cpy.fields.all_bits = rte_be_to_cpu_32(data_sec_hdr->fields.all_bits);

                                int num_prbs = data_sec_hdr_cpy.fields.num_prbu;
                                if (num_prbs == 0) {
                                    num_prbs = 273;
                                }

                                uint8_t *iq_samples_head = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);

                                int num_prbs_ul_used = 0;

                                for (int rb_id = 0; rb_id < num_prbs; rb_id++) {

                                    union compression_params *comp_params = (union compression_params *)iq_samples_head;

                                    if (comp_params->blockFlPoint.exponent > 0) {
                                        num_prbs_ul_used++;
                                    }

                                    iq_samples_head += COMPRESSED_RB_SIZE_BYTES;
                                }
                                atomic_fetch_add(&total_ul_prbs_used, num_prbs_ul_used);
                                atomic_fetch_add(&ul_prbs_count, 1);

                            }
                        }

                    ru_tx_bufs[ru_tx_idx++] = buf;
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
            //printf("We will send out %d packets\n", tx_idx);
            uint16_t nb_tx = rte_eth_tx_burst(config->ru_configs.nic_port_id, 0, ru_tx_bufs, ru_tx_idx);
            assert(nb_tx == ru_tx_idx);
            if (unlikely(nb_tx < ru_tx_idx)) {
                for (int i = nb_tx; i < ru_tx_idx; i++) {
                    rte_pktmbuf_free(ru_tx_bufs[i]);
                }
            } 
        }

    }
}

int
main(int argc, char *argv[]) {
    const char *mb_pci_addr_str, *ru_du_pci_addr_str;
    struct rte_ether_addr du_addr, ru_addr;
    uint16_t du_vlan, ru_vlan;
    struct middlebox_config config;
    int num_rus;
    pthread_t stats_thread;

    int eal_argc = rte_eal_init(argc, argv);

    printf("Received %d DPDK arguments and we have %d total arguments\n", eal_argc, argc);

    argc -= eal_argc;
    argv += eal_argc;

    num_rus = (argc - 3) / 4;
    config.num_rus = num_rus;


    // TODO
    if (argc < 3) {
        rte_exit(EXIT_FAILURE, "Usage: %s <PCI_ADDR> <SRC_MAC> <DST_MAC>\n", argv[0]);
    }

    mb_pci_addr_str = argv[1];
    parse_mac_address(argv[2], &du_addr);
    du_vlan = atoi(argv[3]);

    get_port_from_pci(mb_pci_addr_str, &config.nic_port_id);
    rte_eth_macaddr_get(config.nic_port_id, &config.middlebox_addr);

    config.du_addr = du_addr;
    config.du_vlan = du_vlan;

    ru_du_pci_addr_str = argv[4];
    ru_vlan = atoi(argv[5]);
    printf("RU MAC address: %s\n", argv[5]);
    parse_mac_address(argv[6], &ru_addr);

    get_port_from_pci(ru_du_pci_addr_str, &config.ru_configs.nic_port_id);
    rte_eth_macaddr_get(config.ru_configs.nic_port_id, &config.ru_configs.ru_du_addr);
    config.ru_configs.ru_addr = ru_addr;

    config.ru_configs.vlan = ru_vlan;

    config.mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
    if (config.mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool for middlebox\n");

    if (port_init(config.nic_port_id, config.mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", config.nic_port_id);

    printf("Initialized port %d for middlebox\n", config.nic_port_id);
    print_mac_address_from_portid(config.nic_port_id);

    check_link_status(config.nic_port_id);  

    config.ru_configs.mbuf_pool = rte_pktmbuf_pool_create("RU pool", NUM_MBUFS,
            MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
    if (config.ru_configs.mbuf_pool == NULL) {
        rte_exit(EXIT_FAILURE, "Cannot create RU mbuf pool\n");
    }       

    if (port_init(config.ru_configs.nic_port_id, config.ru_configs.mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", config.ru_configs.nic_port_id);

    printf("Initialized port %d for RU\n", config.ru_configs.nic_port_id);
    print_mac_address_from_portid(config.ru_configs.nic_port_id);

    check_link_status(config.ru_configs.nic_port_id);  

    if (rte_lcore_count() > 1)
        printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

    if (pthread_create(&stats_thread, NULL, stats_thread_func, NULL) != 0) {
        perror("Failed to create stats thread");
        return 1;
    }

    //set_sched_fifo();

    lcore_main(&config);

    return 0;
}
