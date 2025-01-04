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

#define RX_RING_SIZE 4096
#define TX_RING_SIZE 4096

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 32
#define BURST_SIZE 32

#define MAX_NUM_RUS 2

#define VLAN_VID_MASK    0x0FFF
#define ETHER_JUMBO_FRAME_SIZE 8600

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mtu = ETHER_JUMBO_FRAME_SIZE,
        .max_lro_pkt_size = ETHER_JUMBO_FRAME_SIZE,
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM 
    },
    .txmode = {
        .offloads = RTE_ETH_TX_OFFLOAD_MULTI_SEGS | RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM | RTE_ETH_TX_OFFLOAD_UDP_CKSUM,
        .mq_mode = RTE_ETH_MQ_TX_NONE,
    },
};


struct ru_config {
    uint16_t nic_port_id;
    struct rte_ether_addr ru_addr;
    struct rte_ether_addr ru_du_addr;
    uint16_t vlan;
    uint16_t antenna_port_fwd_bitmap;
    struct rte_mempool *mbuf_pool;
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

            struct rte_vlan_hdr *vlan_h = NULL;
            struct xran_ecpri_hdr *ecpri_hdr;
            uint16_t vlan_id = 0;

            if (eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)) {
                vlan_h = rte_pktmbuf_mtod_offset(buf, struct rte_vlan_hdr *, sizeof(struct rte_ether_hdr));
                vlan_id = rte_be_to_cpu_16(vlan_h->vlan_tci) & VLAN_VID_MASK;
            }

            if (vlan_h) {
                ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_vlan_hdr));
            } else {
                ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr));
            }
            
            uint8_t ecpri_message_type = ecpri_hdr->cmnhdr.bits.ecpri_mesg_type;
            uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

            // Check if this is coming from the DU
            if (rte_is_same_ether_addr(&config->du_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                    if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) {

                        // struct rte_mbuf *tx_bufs[MAX_NUM_RUS];
                        // int tx_idx = 0;

                        //printf("Received control data\n");

                        rte_ether_addr_copy(&config->ru_configs[0].ru_addr, &eth_hdr->dst_addr);
                        rte_ether_addr_copy(&config->ru_configs[0].ru_du_addr, &eth_hdr->src_addr);

                        if (vlan_h) {
                            uint16_t vlan_tci = rte_be_to_cpu_16(vlan_h->vlan_tci);
                            vlan_tci = (vlan_tci & 0xF000) | (config->ru_configs[0].vlan & 0x0FFF);
                            vlan_h->vlan_tci = rte_cpu_to_be_16(vlan_tci);
                        }

                        mx_tx_bufs[mx_tx_idx++] = buf;
    
                        // Send the control data to all the RUs
                        for (int i = 1; i < config->num_rus; i++) {
                            struct rte_mbuf *clone = rte_pktmbuf_copy(buf, buf->pool, 0, buf->pkt_len);
                            assert(clone != NULL);

                            struct rte_ether_hdr *clone_eth_hdr = rte_pktmbuf_mtod(clone, struct rte_ether_hdr *);

                            rte_ether_addr_copy(&config->ru_configs[i].ru_addr, &clone_eth_hdr->dst_addr);
                            rte_ether_addr_copy(&config->ru_configs[i].ru_du_addr, &clone_eth_hdr->src_addr);

                            if (vlan_h) {
                                struct rte_vlan_hdr *clone_vlan_h = rte_pktmbuf_mtod_offset(clone, struct rte_vlan_hdr *, sizeof(struct rte_ether_hdr));
                                uint16_t vlan_tci = rte_be_to_cpu_16(clone_vlan_h->vlan_tci);
                                vlan_tci = (vlan_tci & 0xF000) | (config->ru_configs[i].vlan & 0x0FFF);
                                clone_vlan_h->vlan_tci = rte_cpu_to_be_16(vlan_tci);
                            }

                            mx_tx_bufs[mx_tx_idx++] = clone;

                        }

                    } else if (ecpri_message_type == ECPRI_IQ_DATA) {

                        // Find the correct RU to send the packet to
                        for (int ru_idx = 0; ru_idx < config->num_rus; ru_idx++) {
             
                            if ((config->ru_configs[ru_idx].antenna_port_fwd_bitmap & (1 << ru_port_id)) > 0) {
                                rte_ether_addr_copy(&config->ru_configs[ru_idx].ru_addr, &eth_hdr->dst_addr);
                                rte_ether_addr_copy(&config->ru_configs[ru_idx].ru_du_addr, &eth_hdr->src_addr);

                                if (vlan_h) {
                                    uint16_t vlan_tci = rte_be_to_cpu_16(vlan_h->vlan_tci);
                                    vlan_tci = (vlan_tci & 0xF000) | (config->ru_configs[ru_idx].vlan & 0x0FFF);
                                    vlan_h->vlan_tci = rte_cpu_to_be_16(vlan_tci);
                                }

                                mx_tx_bufs[mx_tx_idx++] = buf;
                                break;
                            }
                        }
                    } else {
                        assert(false);
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
            //printf("Sending out %d\n", tx_idx);
            uint16_t nb_tx = rte_eth_tx_burst(config->nic_port_id, 0, mx_tx_bufs, mx_tx_idx);
            assert(nb_tx == mx_tx_idx);
            if (unlikely(nb_tx < mx_tx_idx)) {
                for (int i = nb_tx; i < mx_tx_idx; i++) {
                    rte_pktmbuf_free(mx_tx_bufs[i]);
                }
            }
        }


        //  uint64_t start_tsc = rte_rdtsc();


        // We now check the RUs
        for (int ru_idx = 0; ru_idx < config->num_rus; ru_idx++) {

            struct rte_mbuf *ru_bufs[BURST_SIZE];
            struct rte_mbuf *ru_tx_bufs[128];
            uint16_t ru_tx_idx = 0;

            nb_rx = rte_eth_rx_burst(config->ru_configs[ru_idx].nic_port_id, 0, ru_bufs, BURST_SIZE);
            ru_tx_idx = 0;

            
            // if (nb_rx > 4) {
            //     printf("Got %d packets from RU %d\n", nb_rx, ru_idx);
            // }

            for (int rx_idx = 0; rx_idx < nb_rx; rx_idx++) {

                struct rte_mbuf *buf = ru_bufs[rx_idx];

                struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

                struct rte_vlan_hdr *vlan_h = NULL;
                struct xran_ecpri_hdr *ecpri_hdr;
                uint16_t vlan_id;

                if (eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)) {
                    vlan_h = rte_pktmbuf_mtod_offset(buf, struct rte_vlan_hdr *, sizeof(struct rte_ether_hdr));
                    vlan_id = rte_be_to_cpu_16(vlan_h->vlan_tci) & VLAN_VID_MASK;
                }

                if (vlan_h) {
                    ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_vlan_hdr));
                } else {
                    ecpri_hdr = rte_pktmbuf_mtod_offset(buf, struct xran_ecpri_hdr *, sizeof(struct rte_ether_hdr));
                }
            
                uint8_t ecpri_message_type = ecpri_hdr->cmnhdr.bits.ecpri_mesg_type;
                uint16_t ru_port_id = rte_be_to_cpu_16(ecpri_hdr->ecpri_xtc_id) & 0x000F;

                // Check if this is coming from the RU
                if (rte_is_same_ether_addr(&config->ru_configs[ru_idx].ru_addr, &eth_hdr->src_addr)) {

                    // Check if this was going to the middlebox
                    if (rte_is_same_ether_addr(&config->ru_configs[ru_idx].ru_du_addr, &eth_hdr->dst_addr)) {

                        if ((config->ru_configs[ru_idx].antenna_port_fwd_bitmap & (1 << ru_port_id)) > 0) {

                            rte_ether_addr_copy(&config->du_addr, &eth_hdr->dst_addr);
                            rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                            if (vlan_h) {
                                uint16_t vlan_tci = rte_be_to_cpu_16(vlan_h->vlan_tci);
                                vlan_tci = (vlan_tci & 0xF000) | (config->du_vlan & 0x0FFF);
                                vlan_h->vlan_tci = rte_cpu_to_be_16(vlan_tci);
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
                } else {
                    rte_pktmbuf_free(buf);
                    continue;
                }
            }
            if (ru_tx_idx > 0) {
                //printf("We will send out %d packets\n", tx_idx);
                uint16_t nb_tx = rte_eth_tx_burst(config->ru_configs[ru_idx].nic_port_id, 0, ru_tx_bufs, ru_tx_idx);
                assert(nb_tx == ru_tx_idx);
                if (unlikely(nb_tx < ru_tx_idx)) {
                    for (int i = nb_tx; i < ru_tx_idx; i++) {
                        rte_pktmbuf_free(ru_tx_bufs[i]);
                    }
                } 
            }

        }

        
    // uint64_t end_tsc = rte_rdtsc();
    // uint64_t elapsed_tsc = end_tsc - start_tsc;
    // double elapsed_us = (elapsed_tsc * 1e6) / rte_get_tsc_hz();
    // if (elapsed_us > 0.2)
    // printf("Elapsed time: %.2f milliseconds\n", elapsed_us);
        
    }
}

int
main(int argc, char *argv[]) {
    const char *mb_pci_addr_str, *ru1_du_pci_addr_str, *ru2_du_pci_addr_str;
    struct rte_ether_addr ru1_addr, ru2_addr, du_addr;
    uint16_t ru1_vlan, ru2_vlan, du_vlan;
    uint16_t ru1_port_fwd_bitmap, ru2_port_fwd_bitmap;
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
    ru1_port_fwd_bitmap = atoi(argv[5]);
    ru2_du_pci_addr_str = argv[6];
    ru2_vlan = atoi(argv[7]);
    parse_mac_address(argv[8], &ru2_addr);
    printf("RU2 MAC address: %s\n", argv[8]);
    ru2_port_fwd_bitmap = atoi(argv[9]);
    parse_mac_address(argv[10], &du_addr);
    du_vlan = atoi(argv[11]);

    get_port_from_pci(mb_pci_addr_str, &config.nic_port_id);
    rte_eth_macaddr_get(config.nic_port_id, &config.middlebox_addr);

    config.num_rus = MAX_NUM_RUS;

    get_port_from_pci(ru1_du_pci_addr_str, &config.ru_configs[0].nic_port_id);
    rte_eth_macaddr_get(config.ru_configs[0].nic_port_id, &config.ru_configs[0].ru_du_addr);
    config.ru_configs[0].vlan = ru1_vlan;
    config.ru_configs[0].antenna_port_fwd_bitmap = ru1_port_fwd_bitmap;
    config.ru_configs[0].ru_addr = ru1_addr;

    get_port_from_pci(ru2_du_pci_addr_str, &config.ru_configs[1].nic_port_id);
    rte_eth_macaddr_get(config.ru_configs[1].nic_port_id, &config.ru_configs[1].ru_du_addr);
    config.ru_configs[1].vlan = ru2_vlan;
    config.ru_configs[1].antenna_port_fwd_bitmap = ru2_port_fwd_bitmap;
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
        snprintf(pool_name, sizeof(pool_name), "RU_POOL_%d", i);
        config.ru_configs[i].mbuf_pool = rte_pktmbuf_pool_create(pool_name, NUM_MBUFS,
            MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
        if (config.ru_configs[i].mbuf_pool == NULL) {
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