#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_bus_pci.h>
#include <rte_dev.h>
#include <rte_ring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include <immintrin.h>

#include "ranbooster_common.h"
#include "xran_compression.h"
#include "xran_fh_o_du.h"

#define RX_RING_SIZE 4096
#define TX_RING_SIZE 4096

#define WORKER_RING_SIZE 1024
#define MAX_WORKERS 4

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 32
#define RX_BURST_SIZE 32
#define TX_BURST_SIZE 16
#define TX_OUT_BUFS 192

#define MAX_NUM_RUS 9

#define MAX_OUT_PORTS MAX_NUM_RUS

#define VLAN_VID_MASK    0x0FFF
#define ETHER_JUMBO_FRAME_SIZE 8600

#define IQ_OFFSET   sizeof(struct rte_ether_hdr) + \
                    sizeof(struct xran_ecpri_hdr) + \
                    sizeof(struct radio_app_common_hdr) + \
                    sizeof(struct data_section_hdr) + \
                    sizeof(struct data_section_compression_hdr)

struct rte_mbuf *cached_packets[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS][MAX_NUM_RUS] = {0}; 
rte_atomic32_t cached_packets_num[NUM_ANTENNA_PORTS][NUM_SYMBOLS][NUM_SUBFRAMES][NUM_SLOTS] = {0};

struct rte_ring *worker_ru_rings[MAX_WORKERS] = {0};
struct rte_ring *worker_du_rings[MAX_WORKERS] = {0};

uint8_t iq_buffer[MAX_OUT_PORTS][MAX_NUM_RUS][RTE_ETHER_MAX_JUMBO_FRAME_LEN] = {0};

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mtu = ETHER_JUMBO_FRAME_SIZE,
        .max_lro_pkt_size = ETHER_JUMBO_FRAME_SIZE,
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM | RTE_ETH_RX_OFFLOAD_VLAN_STRIP
    },
    .txmode = {
        .offloads = RTE_ETH_TX_OFFLOAD_VLAN_INSERT | RTE_ETH_TX_OFFLOAD_MULTI_SEGS, // | RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE | RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM | RTE_ETH_TX_OFFLOAD_UDP_CKSUM,
        .mq_mode = RTE_ETH_MQ_TX_NONE
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
    int num_workers;
};

struct worker_config {
    struct middlebox_config *mb_config;
    uint16_t worker_id;
    struct rte_ring *ru_ring;
    struct rte_ring *du_ring;
};

void set_sched_fifo() {
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_RR);

    if (sched_setscheduler(0, SCHED_RR, &param) == -1) {
        fprintf(stderr, "Failed to set SCHED_FIFO: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void print_rte_flow_error(const struct rte_flow_error *error) {
    if (error) {
        printf("Flow error type: %d\n", error->type);
        printf("Flow error cause: %p\n", error->cause);
        printf("Flow error message: %s\n", error->message ? error->message : "(no message)");
    } else {
        printf("No error information available.\n");
    }
}

static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool, int num_queues) {
    struct rte_eth_conf port_conf = port_conf_default;

    int retval;

    if (port >= rte_eth_dev_count_avail())
        return -1;

    // 1 queue for RX and as many as num_queues for TX
    retval = rte_eth_dev_configure(port, 1, num_queues, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_set_mtu(port, ETHER_JUMBO_FRAME_SIZE);
    if (retval != 0)
        return retval;

    retval = rte_eth_rx_queue_setup(port, 0, RX_RING_SIZE,
        rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    assert(retval >= 0);
    if (retval < 0)
        return retval;

    for (int i = 0; i < num_queues; i++) {


        retval = rte_eth_tx_queue_setup(port, i, TX_RING_SIZE,
            rte_eth_dev_socket_id(port), NULL);
        assert(retval >= 0);
        if (retval < 0)
            return retval;
    }

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

__attribute__((unused)) static void
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
    hdr = rte_pktmbuf_alloc(pool);
    assert(hdr != NULL);
    // if ((hdr = rte_pktmbuf_alloc(pkt->pool)) == NULL)
    //         return NULL;

    /* If requested, then make a new clone packet. */
    if ((pkt = rte_pktmbuf_clone(pkt, pool)) == NULL) {
        //rte_pktmbuf_free(hdr);
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

#if defined(__AVX512F__)
#pragma message "Using AVX512"
void sum_arrays_avx512(int16_t *iq_sum, int16_t *iq_sum2, int num_iq) {
    int iq_idx;
    for (iq_idx = 0; iq_idx <= num_iq - 32; iq_idx += 32) {
        // Load 32 int16_t values from each array
        __m512i vec1 = _mm512_loadu_si512((__m512i *)&iq_sum[iq_idx]);
        __m512i vec2 = _mm512_loadu_si512((__m512i *)&iq_sum2[iq_idx]);

        // Add the vectors
        __m512i result = _mm512_add_epi16(vec1, vec2);

        // Store the result back to iq_sum
        _mm512_storeu_si512((__m512i *)&iq_sum[iq_idx], result);
    }

    // Handle any remaining elements
    for (; iq_idx < num_iq; iq_idx++) {
        iq_sum[iq_idx] += iq_sum2[iq_idx];
    }
}
#endif

long sent_du_pkts[MAX_WORKERS] = {0};

int
_handle_du_burst(struct middlebox_config *config, struct rte_mbuf **mx_bufs, int nb_rx, int queue_id)
{

    struct rte_mbuf *mx_tx_bufs[TX_OUT_BUFS] = {0};

    uint16_t mx_tx_idx = 0;
    
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
                if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) {

                    for (int i = 1; i < config->num_rus; i++) {
                        struct rte_mbuf *copy = rte_pktmbuf_copy(buf, buf->pool, 0, buf->pkt_len);
                        assert(copy != NULL);

                        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(copy, struct rte_ether_hdr *);

                        rte_ether_addr_copy(&config->ru_configs[i].ru_addr, &eth->dst_addr);
                        rte_ether_addr_copy(&config->ru_configs[i].ru_du_addr, &eth->src_addr);

                        if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                            copy->ol_flags |= RTE_MBUF_F_TX_VLAN;
                            copy->vlan_tci = config->ru_configs[i].vlan;
                        }

                        mx_tx_bufs[mx_tx_idx++] = copy;
                    }

                    rte_ether_addr_copy(&config->ru_configs[0].ru_addr, &eth_hdr->dst_addr);
                    rte_ether_addr_copy(&config->ru_configs[0].ru_du_addr, &eth_hdr->src_addr);

                    eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                    if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                        buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                        buf->vlan_tci = config->ru_configs[0].vlan;
                    }

                    mx_tx_bufs[mx_tx_idx++] = buf;
                } else if (ecpri_message_type == ECPRI_IQ_DATA) {

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
        uint16_t nb_tx = rte_eth_tx_burst(config->nic_port_id, queue_id, mx_tx_bufs, mx_tx_idx);

        assert(nb_tx == mx_tx_idx);

        sent_du_pkts[queue_id] += nb_tx;
        // if (sent_du_pkts[queue_id] % 100000 == 0) {
        //     printf("Sent %ld DU packets on queue %d\n", sent_du_pkts[queue_id], queue_id);
        // }

        if (unlikely(nb_tx < mx_tx_idx)) {
            for (int i = nb_tx; i < mx_tx_idx; i++) {
                rte_pktmbuf_free(mx_tx_bufs[i]);
            }
        }
    }

    return 0;
}

int
ranbooster_handle_du(void *args, int target_worker)
{
    struct middlebox_config *config = (struct middlebox_config *)args;

    struct rte_mbuf *mx_bufs[RX_BURST_SIZE];
    uint16_t nb_rx = rte_eth_rx_burst(config->nic_port_id, 0, mx_bufs, RX_BURST_SIZE);

    if (nb_rx > 0) {

        if (target_worker > 0) {
            struct rte_ring *worker_ring = worker_du_rings[target_worker - 1];
            assert(rte_ring_enqueue_burst(worker_ring, (void **)mx_bufs, nb_rx, NULL) == nb_rx);
        } else {
            _handle_du_burst(config, mx_bufs, nb_rx, 0);
        }

    }

    return 0;
}

long sent_ru_pkts[NUM_ANTENNA_PORTS] = {0};

int 
process_pkts(struct rte_mbuf **ul_pkts, 
            int num_pkts, 
            bool is_compressed, 
            int num_prbs, 
            struct middlebox_config *config,
            int out_port_idx) 
{
    struct rte_mbuf *bufs_to_be_freed[MAX_NUM_RUS] = {0};
    int num_to_free[MAX_NUM_RUS] = { 0 };

    void *iq_sum_ptr;
    uint8_t tmp_buf[RTE_ETHER_MAX_JUMBO_FRAME_LEN] = {0};

    if (is_compressed) {
        // Use a temporary buffer for storing the sums
        iq_sum_ptr = tmp_buf;
    } else {
        // No compression, so directly use the IQ samples buffer of the first RU
        iq_sum_ptr = rte_pktmbuf_mtod_offset(ul_pkts[0], void *, IQ_OFFSET);
    }

    for (int id = 0; id < config->num_rus; id++) {
                    
        // If there is compression (only for RU ports < 4), decompress them
        if (is_compressed) {
            struct xranlib_decompress_request dec_req = {0};
            dec_req.data_in = rte_pktmbuf_mtod_offset(ul_pkts[id], void *,
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
            dec_resp.data_out = (int16_t *)iq_buffer[out_port_idx][id];
            dec_resp.len = num_prbs * ((IQ_BIT_WIDTH_UNCOMPRESSED * 2 * NUM_SUBCARRIERS_PRB) / 8);
                
            int res = xranlib_decompress(&dec_req, &dec_resp);

            assert(res == 0);
        }
                    
        // Add the IQ samples of the new buffer
        int16_t *iq_sum = iq_sum_ptr;
        int16_t *iq_sum2;
        if (is_compressed) {
            iq_sum2 = (int16_t *)iq_buffer[out_port_idx][id];
        } else {
            iq_sum2 = rte_pktmbuf_mtod_offset(ul_pkts[id], void *,
                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr) + 
                sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr));
        }
        int num_iq = num_prbs * NUM_SUBCARRIERS_PRB * 2;
        #define assert__(x) for ( ; !(x) ; assert(x) )                          
#if defined(__AVX512F__)
        sum_arrays_avx512(iq_sum, iq_sum2, num_iq);
#else
        for (int iq_idx = 0; iq_idx < num_iq; iq_idx++) {
            iq_sum[iq_idx] += iq_sum2[iq_idx];
        }
#endif
        if (id > 0) {
            bufs_to_be_freed[id] = ul_pkts[id];
            num_to_free[id]++;
            ul_pkts[id] = NULL;
        }
    }

        struct rte_mbuf *buf_out = ul_pkts[0];

        if (is_compressed) {
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

        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(buf_out, struct rte_ether_hdr *);

        rte_ether_addr_copy(&config->du_addr, &eth_hdr->dst_addr);
        rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

        if (buf_out->ol_flags & RTE_MBUF_F_RX_VLAN) {
            buf_out->ol_flags |= RTE_MBUF_F_TX_VLAN;
            buf_out->vlan_tci = config->du_vlan;
        }

        uint16_t nb_tx = rte_eth_tx_burst(config->ru_configs[out_port_idx].nic_port_id, out_port_idx, &buf_out, 1);
        assert(nb_tx == 1);
        sent_ru_pkts[out_port_idx]++;

        // if (sent_ru_pkts[out_port_idx] % 10000 == 0) {
        //     printf("Sent %ld RU packets on port %d\n", sent_ru_pkts[out_port_idx], out_port_idx);
        // }
        if (unlikely(nb_tx < 1)) {
            rte_pktmbuf_free(buf_out);
        }

        ul_pkts[0] = NULL;


        for (int idx = 1; idx < config->num_rus; idx++) {
            if (num_to_free[idx] > 0) {
                rte_pktmbuf_free(bufs_to_be_freed[idx]);
            }
            ul_pkts[idx] = NULL;
        }
    
    return 0;
}

int num_errors = 0;

int
ranbooster_handle_rus(void *args)
{
    struct middlebox_config *config = (struct middlebox_config *)args;

    // We now check the RUs
    for (int ru_idx = 0; ru_idx < config->num_rus; ru_idx++) {

        struct rte_mbuf *ru_bufs[TX_BURST_SIZE];
        uint16_t nb_rx = 0;

        nb_rx = rte_eth_rx_burst(config->ru_configs[ru_idx].nic_port_id, 0, ru_bufs, TX_BURST_SIZE);

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
                        num_prbs = NUM_PRB;
                    } else {
                        num_prbs = data_sec_hdr_cpy.fields.num_prbu;
                    }

                    //assert(cached_packets[ru_port_id][symbol][subframe][slot][ru_idx] == NULL);
                    if (cached_packets[ru_port_id][symbol][subframe][slot][ru_idx] != NULL) {
                        //printf("RU %d: This looks wrong for RU port %d, symbol %d, subframe %d and slot %d\n", ru_idx, ru_port_id, symbol, subframe, slot);
                        // printf("The number of cached packets is %d\n", rte_atomic32_read(&cached_packets_num[ru_port_id][symbol][subframe][slot]));
                        rte_atomic32_set(&cached_packets_num[ru_port_id][symbol][subframe][slot], 0);
                        for (int w = 0; w < config->num_rus; w++) {
                            if (cached_packets[ru_port_id][symbol][subframe][slot][w] == NULL) {
                                //printf("RU %d is NULL\n", w);
                            } else {
                                //printf("RU %d is not NULL\n", w);
                                rte_pktmbuf_free(cached_packets[ru_port_id][symbol][subframe][slot][w]);
                                cached_packets[ru_port_id][symbol][subframe][slot][w] = NULL;
                            }
                        }
                        num_errors++;
                        if (num_errors % 1000 == 0) {
                            printf("Number of errors is %d\n", num_errors);
                        }
                        //abort();
                    }

                    cached_packets[ru_port_id][symbol][subframe][slot][ru_idx] = buf;

                    rte_atomic32_inc(&cached_packets_num[ru_port_id][symbol][subframe][slot]);

                    // If we have all the expected packets, then time to modify and send out.
                    // For this to be true, the counter should be equal to the number of RUs
                    // Note: We do not consider losses currently
                    int32_t value = rte_atomic32_read(&cached_packets_num[ru_port_id][symbol][subframe][slot]);

                    if (value == config->num_rus) {

                        rte_atomic32_set(&cached_packets_num[ru_port_id][symbol][subframe][slot], 0);

     
                        if (config->num_workers > 0 && ru_port_id < MAX_PDSCH_PUSCH_PORT) {
                            // Push the processing to the worker threads, if possible
                            int worker_id = ru_port_id % (config->num_workers + 1);

                            if (worker_id > 0) {
                                struct rte_ring *worker_ring = worker_ru_rings[worker_id - 1];
                                assert(rte_ring_enqueue_burst(worker_ring, (void **)cached_packets[ru_port_id][symbol][subframe][slot], config->num_rus, NULL) == config->num_rus);
                            } else {
                                process_pkts(cached_packets[ru_port_id][symbol][subframe][slot], config->num_rus, ru_port_id < MAX_PDSCH_PUSCH_PORT, num_prbs, config, 0);
                            } 
                        } else {
                            // Process the packet locally
                            process_pkts(cached_packets[ru_port_id][symbol][subframe][slot], config->num_rus, ru_port_id < MAX_PDSCH_PUSCH_PORT, num_prbs, config, 0);
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
    }
    return 0;
}

int
process_packets_worker(void *args)
{
    struct worker_config *wconfig = (struct worker_config *)args;
    struct middlebox_config *mb_config = wconfig->mb_config;
    struct rte_ring *ru_ring = wconfig->ru_ring;
    struct rte_ring *du_ring = wconfig->du_ring;
    struct rte_mbuf *bufs[MAX_NUM_RUS] = {0};
    struct rte_mbuf *tx_bufs[TX_BURST_SIZE] = {0};
    int nb_rx = 0;
    int nb_tx = 0;

    // set_sched_fifo();

    printf("Worker %d started\n", wconfig->worker_id);

    while (true) {
        nb_tx = rte_ring_dequeue_burst(du_ring, (void **)tx_bufs, TX_BURST_SIZE, NULL);
        if (nb_tx > 0) {
            _handle_du_burst(mb_config, tx_bufs, nb_tx, wconfig->worker_id + 1);
        }        
        nb_rx = rte_ring_dequeue_burst(ru_ring, (void **)bufs, mb_config->num_rus, NULL);
        if (nb_rx < mb_config->num_rus) {
            assert(nb_rx == 0);
            continue;
        } else {
            process_pkts(bufs, mb_config->num_rus, true, NUM_PRB, mb_config, wconfig->worker_id + 1);
        }
    }
}


int
start_middlebox(void *args) 
{
    struct middlebox_config *config = args;
    // set_sched_fifo();

    int target_core = 0;

    while (true) {
        ranbooster_handle_du(args, target_core);
        target_core = ((target_core + 1) % (config->num_workers + 1));
        ranbooster_handle_rus(args);
    }
    return 0;
}

int
main(int argc, char *argv[]) {
    const char *mb_pci_addr_str;
    const char *ru_du_pci_addr_str;
    struct rte_ether_addr du_addr, ru_addr;
    uint16_t ru_vlan, du_vlan;
    int num_rus;
    struct middlebox_config config = {0};
    struct worker_config worker_configs[MAX_WORKERS] = {0};

    int eal_argc = rte_eal_init(argc, argv);

    printf("Received %d DPDK arguments and we have %d total arguments\n", eal_argc, argc);

    argc -= eal_argc;
    argv += eal_argc;

    int num_lcores = rte_lcore_count();

    // TODO
    if (argc < 10) {
        rte_exit(EXIT_FAILURE, "Usage: %s ...\n", argv[0]);
    }

    num_rus = (argc - 3) / 3;
    config.num_rus = num_rus;

    printf("The num of RUs is %d\n", num_rus);

    mb_pci_addr_str = argv[1];
    parse_mac_address(argv[2], &du_addr);
    du_vlan = atoi(argv[3]);

    get_port_from_pci(mb_pci_addr_str, &config.nic_port_id);
    rte_eth_macaddr_get(config.nic_port_id, &config.middlebox_addr);

    config.du_addr = du_addr;
    config.du_vlan = du_vlan;

    config.mbuf_pool = rte_pktmbuf_pool_create("MB_POOL", NUM_MBUFS * config.num_rus,
        MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
    if (config.mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool for middlebox\n");


    if (port_init(config.nic_port_id, config.mbuf_pool, num_lcores) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", config.nic_port_id);

    printf("Initialized port %d for middlebox\n", config.nic_port_id);
    print_mac_address_from_portid(config.nic_port_id);

    check_link_status(config.nic_port_id);  

    int ru_idx = 0;
    for (int arg_idx = 4; arg_idx < argc; arg_idx += 3) {
        ru_du_pci_addr_str = argv[arg_idx];
        ru_vlan = atoi(argv[arg_idx + 1]);
        memset(&ru_addr, 0, sizeof(ru_addr));
        parse_mac_address(argv[arg_idx + 2], &ru_addr);
        printf("RU %d MAC address: %s\n", ru_idx + 1, argv[arg_idx + 2]);

        get_port_from_pci(ru_du_pci_addr_str, &config.ru_configs[ru_idx].nic_port_id);
        rte_eth_macaddr_get(config.ru_configs[ru_idx].nic_port_id, &config.ru_configs[ru_idx].ru_du_addr);
        config.ru_configs[ru_idx].vlan = ru_vlan;
        config.ru_configs[ru_idx].ru_addr = ru_addr;

        char pool_name[32];
        char pool_name_clone[32];
        snprintf(pool_name, sizeof(pool_name), "RU_POOL_%d", ru_idx);
        snprintf(pool_name_clone, sizeof(pool_name), "RU_POOL_%d_clone", ru_idx);
        config.ru_configs[ru_idx].mbuf_pool = rte_pktmbuf_pool_create(pool_name, NUM_MBUFS,
            MBUF_CACHE_SIZE, 0, ETHER_JUMBO_FRAME_SIZE + RTE_PKTMBUF_HEADROOM + 100, SOCKET_ID_ANY);
        if (config.ru_configs[ru_idx].mbuf_pool == NULL) {
            rte_exit(EXIT_FAILURE, "Cannot create mbuf pool %s\n", pool_name);
        }

        config.ru_configs[ru_idx].mbuf_pool_clone = rte_pktmbuf_pool_create(pool_name_clone, NUM_MBUFS,
            MBUF_CACHE_SIZE, 0, 2 * RTE_PKTMBUF_HEADROOM, SOCKET_ID_ANY);
        if (config.ru_configs[ru_idx].mbuf_pool_clone == NULL) {
            rte_exit(EXIT_FAILURE, "Cannot create mbuf pool %s\n", pool_name);
        }
            

        if (port_init(config.ru_configs[ru_idx].nic_port_id, config.ru_configs[ru_idx].mbuf_pool, num_lcores) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", config.ru_configs[ru_idx].nic_port_id);

        printf("Initialized port %d for RU%d\n", config.ru_configs[ru_idx].nic_port_id, ru_idx+1);
        print_mac_address_from_portid(config.ru_configs[ru_idx].nic_port_id);

        check_link_status(config.ru_configs[ru_idx].nic_port_id);  

        ru_idx++;
    }

    for (int i = 0; i < NUM_ANTENNA_PORTS; i++) {
        for (int j = 0; j < NUM_SYMBOLS; j++) {
            for (int k = 0; k < NUM_SUBFRAMES; k++) {
                for (int l = 0; l < NUM_SLOTS; l++) {
                    rte_atomic32_init(&cached_packets_num[i][j][k][l]);
                }
            }
        }
    }

    config.num_workers = num_lcores - 1;
    if (num_lcores == 1) {
        printf("Single core used, so running one thread\n");
        start_middlebox(&config);
    } else {

        printf("Multi-core config with %d workers is used.\n", config.num_workers);
        
        char ring_name[32];

        for (int i = 0; i < config.num_workers; i++) {

            snprintf(ring_name, sizeof(ring_name), "worker_ru_ring_%d", i);
            worker_ru_rings[i] = rte_ring_create(ring_name, WORKER_RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);

            if (worker_ru_rings[i] == NULL) {
                rte_exit(EXIT_FAILURE, "Error creating ring buffer for worker %d\n", i);
            } else {
                printf("Created RU ring for worker %d\n", i);
            }

            snprintf(ring_name, sizeof(ring_name), "worker_du_ring_%d", i);
            worker_du_rings[i] = rte_ring_create(ring_name, WORKER_RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);

            if (worker_du_rings[i] == NULL) {
                rte_exit(EXIT_FAILURE, "Error creating ring buffer for worker %d\n", i);
            } else {
                printf("Created DU ring for worker %d\n", i);
            }
        }

        unsigned lcore_id = rte_get_next_lcore(-1, 1, 0);

        for (int i = 0; i < config.num_workers; i++) {
            printf("Running TX processing thread %d.\n", lcore_id);
            worker_configs[i].mb_config = &config;
            worker_configs[i].worker_id = i;
            worker_configs[i].ru_ring = worker_ru_rings[i];
            worker_configs[i].du_ring = worker_du_rings[i];        
            rte_eal_remote_launch(process_packets_worker, &worker_configs[i], lcore_id);
            lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
        }

        printf("Running RX handler thread\n");
        start_middlebox(&config);
    }
    
    return 0;
}
