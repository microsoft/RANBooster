#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <xdp/xsk.h>
#include <xdp/libxdp.h>
#include <linux/if_link.h>
#include <poll.h>
#include <net/if.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <assert.h>
#include <arpa/inet.h>
#include <linux/netdev.h>

#include "ranbooster_common.h"

#include "xran_compression.h"
#include "xran_fh_o_du.h"

#define VLAN_VID_MASK    0x0FFF

#define BATCH_SIZE 64

#define NUM_FRAMES_PER_SOCKET (8192)
#define NUM_SOCKETS (2)
#define NUM_FRAMES (NUM_FRAMES_PER_SOCKET * NUM_SOCKETS)

#define FRAME_SIZE (XSK_UMEM__DEFAULT_FRAME_SIZE)

#define INVALID_UMEM_FRAME UINT64_MAX

uint8_t booster_mac_addr[ETH_ALEN] = {0};
uint8_t du_mac_addr[ETH_ALEN] = {0};

struct xsk_pkt_fragment {
    uint64_t addr;
    uint32_t size;
    void *data;
    bool eop;
    bool new_packet;
    bool packet_cached;
};

struct vlan_hdr {
    __be16 h_vlan_TCI;   /* VLAN Tag Control Information */
    __be16 h_vlan_encapsulated_proto; /* Ethernet protocol type */
};

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
};

struct xsk_socket_info {
    
    int id;

    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_socket *xsk;

    struct xsk_umem_info *umem;
    
    uint64_t umem_frame_addr[NUM_FRAMES_PER_SOCKET];
	uint32_t umem_frame_free;

    uint32_t outstanding_tx;
};

static void set_sched_fifo(int priority) 
{
    struct sched_param param;
    param.sched_priority = priority;

    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) { // 0 indicates current thread
        perror("sched_setscheduler");
        exit(EXIT_FAILURE);
    }

    printf("Set SCHED_FIFO with priority %d for current thread\n", priority);
}

static inline bool parse_mac_address(const char *mac_str, unsigned char mac_addr[6]) 
{
    
    int values[6];
    if (sscanf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        mac_addr[i] = (unsigned char)values[i];
    }
    return true;
}

void pin_thread_to_core(int core_id) 
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) { // 0 indicates current thread
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }

    printf("Pinned current thread to core %d\n", core_id);
}

static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
	uint64_t frame;
	if (xsk->umem_frame_free == 0)
		return INVALID_UMEM_FRAME;

	frame = xsk->umem_frame_addr[--xsk->umem_frame_free];
	xsk->umem_frame_addr[xsk->umem_frame_free] = INVALID_UMEM_FRAME;
	return frame;
}

static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame)
{
	assert(xsk->umem_frame_free < NUM_FRAMES_PER_SOCKET);
	xsk->umem_frame_addr[xsk->umem_frame_free++] = frame;
}

static uint64_t xsk_umem_free_frames(struct xsk_socket_info *xsk)
{
	return xsk->umem_frame_free;
}



// Setup an XDP socket for a given interface
int setup_xsk_socket(struct xsk_socket_info *xsk_info, const char *ifname, int queue_id, const char *map_name) {

    uint32_t idx;

    struct xsk_socket_config socket_config = {
        .rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
        .tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
        .libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD,
        .xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_DRV_MODE,
        .bind_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY //| XDP_USE_SG //| XDP_ZEROCOPY, // Enable zero-copy
    };

    //int ret = xsk_socket__create(&xsk_info->xsk, ifname, queue_id, xsk_info->umem->umem, &xsk_info->rx, &xsk_info->tx, &socket_config);
    int ret = xsk_socket__create_shared(&xsk_info->xsk, ifname, queue_id, xsk_info->umem->umem, &xsk_info->rx, &xsk_info->tx, &xsk_info->umem->fq, &xsk_info->umem->cq, &socket_config);
    if (ret) {
        fprintf(stderr, "Error creating XSK socket for %s: %s\n", ifname, strerror(-ret));
        return ret;
    }

    int map_fd = bpf_obj_get(map_name);
    if (map_fd < 0) {
        fprintf(stderr, "Error finding map %s : %s\n", map_name, strerror(-map_fd));
        return map_fd;
    }

    printf("The map fd is %d\n", map_fd);
    ret = xsk_socket__update_xskmap(xsk_info->xsk, map_fd);
	if (ret) {
        fprintf(stderr, "Error updating xsk map %s : %s\n", map_name, strerror(-ret));
        return ret;
    }

    printf("Map %s updated with socket %d\n", map_name, map_fd);

    printf("XSK socket created for interface %s (queue %d)\n", ifname, queue_id);

	for (int i = 0; i < NUM_FRAMES_PER_SOCKET; i++) {
        //xsk_info->umem_frame_addr[i] = (xsk_info->id * NUM_FRAMES_PER_SOCKET * FRAME_SIZE) + (i * FRAME_SIZE);
        xsk_info->umem_frame_addr[i] = i * FRAME_SIZE;
    }

    xsk_info->umem_frame_free = NUM_FRAMES_PER_SOCKET;

    ret = xsk_ring_prod__reserve(&xsk_info->umem->fq,
				     XSK_RING_PROD__DEFAULT_NUM_DESCS,
				     &idx);

	if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS) {
        printf("Failed to reserve fill ring items %d\n", ret);
        return -1;
    }

    for (int i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++) {
		*xsk_ring_prod__fill_addr(&xsk_info->umem->fq, idx++) =
			xsk_alloc_umem_frame(xsk_info);
    }


    xsk_ring_prod__submit(&xsk_info->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return 0;
}

void tx_free_pkt(struct xsk_socket_info *xsk, struct xsk_pkt_fragment *pkt_frags, int num_frags)
{
    int res, completed;
    uint32_t tx_idx, idx_fq;
    uint32_t idx_cq = 0;

    if (num_frags == 0)
		return;

    // Send the packets that are ready
    res = xsk_ring_prod__reserve(&xsk->tx, num_frags, &tx_idx);

    assert(res == num_frags);

    for (int i = 0; i < num_frags; i++) {
        struct xdp_desc *tx_desc;

        tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, tx_idx++);
        tx_desc->addr = pkt_frags[i].addr;
        tx_desc->len = pkt_frags[i].size;
        
        if (!pkt_frags[i].eop) {
            tx_desc->options = XDP_PKT_CONTD;
        } else {
            tx_desc->options = 0;
        }   
    }

    xsk_ring_prod__submit(&xsk->tx, num_frags);
    sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

	/* Collect/free completed TX buffers */
	completed = xsk_ring_cons__peek(&xsk->umem->cq,
					XSK_RING_CONS__DEFAULT_NUM_DESCS,
					&idx_cq);


    if (completed > 0) {
        res = xsk_ring_prod__reserve(&xsk->umem->fq, completed, &idx_fq);
        assert(res == completed);

        for (int i = 0; i < completed; i++) {
		    *xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq++) =
			    *xsk_ring_cons__comp_addr(&xsk->umem->cq, idx_cq++);
        }

        xsk_ring_cons__release(&xsk->umem->cq, completed);
        xsk_ring_prod__submit(&xsk->umem->fq, completed);
    }

}

void process_rcvd_pkt(struct xsk_socket_info *xsk, struct xsk_pkt_fragment *pkt_frags, int num_frags)
{
    struct xsk_pkt_fragment frags_to_be_freed[BATCH_SIZE];
    struct xsk_pkt_fragment frags_to_be_sent[BATCH_SIZE];
    int num_to_free = 0, num_to_send = 0;
    uint32_t idx_fq;
    int alloc;
    struct vlan_hdr *vlan_h = NULL;
    uint16_t ether_type;
    uint16_t vlan_tci = 0;
    uint16_t vlan_id = 0;

    uint16_t packet_len = 0;
    uint16_t headers_len = 0;
    for (int i = 0; i < num_frags; i++) {

        if (xsk->id == 1) {
            frags_to_be_freed[num_to_free++] = pkt_frags[i];
        } else {
            packet_len += pkt_frags[i].size;

            // Forward those packets to the DU
            if (pkt_frags[i].new_packet) { // This is a header, so modify it

                struct ethhdr *eh = (struct ethhdr *)pkt_frags[i].data;

                void *next_hdr = (__u8 *)eh + sizeof(struct ethhdr);

                ether_type = ntohs(eh->h_proto);

                if (ether_type == ETH_P_8021Q)  {

                    vlan_h = next_hdr;
                    vlan_tci = ntohs(vlan_h->h_vlan_TCI);

                    vlan_id = vlan_tci & VLAN_VID_MASK;

                    next_hdr = (__u8 *)next_hdr + sizeof(struct vlan_hdr);
        
                }

                if (vlan_h && vlan_id != 1) {
                    vlan_tci = (vlan_tci & 0xF000) | (1 & 0x0FFF);
                    vlan_h->h_vlan_TCI = htons(vlan_tci);
                }

                memcpy(eh->h_dest, du_mac_addr, ETH_ALEN);
                memcpy(eh->h_source, booster_mac_addr, ETH_ALEN);

                struct xran_ecpri_hdr *ecpri_hdr = (struct xran_ecpri_hdr *)next_hdr;
                uint16_t ru_port_id = ntohs(ecpri_hdr->ecpri_xtc_id) & 0x000F;

                next_hdr = (__u8 *)next_hdr + sizeof(struct xran_ecpri_hdr);

                struct radio_app_common_hdr *app_common_hdr = (struct radio_app_common_hdr *)next_hdr;

                struct radio_app_common_hdr radio_hdr_cpy = *app_common_hdr;
                radio_hdr_cpy.sf_slot_sym.value = ntohs(radio_hdr_cpy.sf_slot_sym.value);

                uint16_t slot = radio_hdr_cpy.sf_slot_sym.slot_id;
                uint16_t subframe = radio_hdr_cpy.sf_slot_sym.subframe_id;
                uint16_t symbol = radio_hdr_cpy.sf_slot_sym.symb_id;

                next_hdr = (__u8 *)next_hdr + sizeof(struct radio_app_common_hdr);

                struct data_section_hdr *data_sec_hdr = (struct data_section_hdr *)next_hdr;

                struct data_section_hdr data_sec_hdr_cpy = *data_sec_hdr;
                data_sec_hdr_cpy.fields.all_bits = ntohl(data_sec_hdr->fields.all_bits);

                uint32_t start_prbu = data_sec_hdr_cpy.fields.start_prbu;
                uint32_t num_prbu = data_sec_hdr_cpy.fields.num_prbu;

                next_hdr = (__u8 *)next_hdr + sizeof(struct data_section_hdr);

                struct data_section_compression_hdr *cmp_header = (struct data_section_compression_hdr *)next_hdr;

                uint8_t iq_width = cmp_header->ud_comp_hdr.ud_iq_width;
                uint8_t comp_method = cmp_header->ud_comp_hdr.ud_comp_meth;

                headers_len = sizeof(struct ethhdr) + sizeof(struct vlan_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr) + sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr);


                uint8_t iq_samples[8096] = {0};

                if (ru_port_id < 4) {

                    next_hdr = (__u8 *)next_hdr + sizeof(struct data_section_compression_hdr);

                    struct xranlib_decompress_request dec_req = {0};
                    dec_req.data_in = next_hdr;
                    dec_req.numRBs = 106;
                    dec_req.numDataElements = 24;
                    dec_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
                    dec_req.iqWidth = 9;
                    dec_req.reMask = 0xfff;
                    dec_req.csf = 0;
                    dec_req.ScaleFactor = 1;
                    dec_req.len = 28 * 106;

                    struct xranlib_decompress_response dec_resp = {0};
                    dec_resp.data_out = iq_samples;
                    dec_resp.len = 8096;

                    int res = xranlib_decompress(&dec_req, &dec_resp);

                    assert(res == 0);

                    memset(next_hdr, 0, 28*106);

                    struct xranlib_compress_request comp_req = {0};
                    comp_req.data_in = iq_samples;
                    comp_req.numRBs = 106;
                    comp_req.numDataElements = 24;
                    comp_req.compMethod = XRAN_COMPMETHOD_BLKFLOAT;
                    comp_req.iqWidth = 9;
                    comp_req.reMask = 0xfff;
                    comp_req.csf = 0;
                    comp_req.ScaleFactor = 1;
                    comp_req.len = 8096;

                    struct xranlib_compress_response comp_resp = {0};
                    comp_resp.data_out = next_hdr;
                    comp_resp.len = 28 * 106;

                    xranlib_compress(&comp_req, &comp_resp);

                }

                /// For PRACH packets, it is just the headers (36 bytes) + 4 bytes per IQ sample for a total of (12PRBs * 12 REs). The total is 612

                /// For data packets, each PRB has (12 * 18) bits for data + 8 bits for compression header (28 bytes). For the total of 273 PRBs, this is 7644B. By adding 36B for the header, we get 7680B. 

                // Check what the exponent is
                // if (ru_port_id < 4) {
                //     // Get the 12th RB
                //     next_hdr = (__u8 *)next_hdr + sizeof(struct data_section_compression_hdr) + (100 * 28); 

                //     //next_hdr = (__u8 *)next_hdr + sizeof(struct data_section_compression_hdr);
                //     struct compression_params *cp = (struct compression_params *)next_hdr;
                //     uint8_t exponent = cp->exponent;
                //     printf("Exponent for PRB 12 is %d\n", exponent);
                // }
            } 
            if(pkt_frags[i].eop) {
                //printf("The packet size is %d and the header length is %d\n", packet_len, headers_len);
                headers_len = 0;
                packet_len = 0;
            }

            frags_to_be_sent[num_to_send++] = pkt_frags[i];
        }
    }

    tx_free_pkt(xsk, frags_to_be_sent, num_to_send);


    if (num_to_free > 0) {

        alloc = xsk_ring_prod__reserve(&xsk->umem->fq, num_to_free, &idx_fq);

        assert(alloc == num_to_free);

        for (int i = 0; i < num_to_free; i++) {
            *xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq++) = frags_to_be_freed[i].addr;
        }

        xsk_ring_prod__submit(&xsk->umem->fq, num_to_free);
    }

}

void rx_packets(struct xsk_socket_info *xsk)
{
    static bool new_packet = true;
    uint32_t idx_rx = 0;
    struct xsk_pkt_fragment xsk_pkt_frags[BATCH_SIZE] = {0};

    int rcvd = xsk_ring_cons__peek(&xsk->rx, BATCH_SIZE, &idx_rx);
    
    if (!rcvd) 
        return;

    for (int i = 0; i < rcvd; i++) {
        // Get the next descriptor from the RX ring of the socket
        const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++);

        // Get the data from the ring
        xsk_pkt_frags[i].data = xsk_umem__get_data(xsk->umem->buffer, desc->addr);
        xsk_pkt_frags[i].addr = desc->addr;
        xsk_pkt_frags[i].size = desc->len;
        xsk_pkt_frags[i].new_packet = new_packet;

        // Mark this as a new packet or not
        new_packet = false;
        xsk_pkt_frags[i].eop = !(desc->options & XDP_PKT_CONTD);
        if (xsk_pkt_frags[i].eop) {
            new_packet = true;
        }
    }

    // Release all the received slots from the RX ring of the socket
    xsk_ring_cons__release(&xsk->rx, rcvd);

    // Process the received packets
    process_rcvd_pkt(xsk, xsk_pkt_frags, rcvd);

}

int main(int argc, char **argv) {

    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    uint64_t pbuffer_size;
    
    DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	DECLARE_LIBXDP_OPTS(xdp_program_opts, xdp_opts, 0);

    if (argc < 7) {
        fprintf(stderr, "Usage: %s <ifname1> <redirect_map_name1> <ifname2> <redirect_map_name2> <du_mac_address> <ranbooster_mac_address>\n", argv[0]);
        return 1;
    }

    const char *ifname1 = argv[1];
    const char *map_name1 = argv[2];
    const char *ifname2 = argv[3];
    const char *map_name2 = argv[4];
    const char *du_mac = argv[5];
    const char *ranbooster_mac = argv[6];

    if (!parse_mac_address(du_mac, du_mac_addr)) {
        exit(-1);
    }

    if (!parse_mac_address(ranbooster_mac, booster_mac_addr)) {
        exit(-1);
    }
    struct xsk_umem_info umem[NUM_SOCKETS] = {0};

    struct xsk_socket_info xsk_info[NUM_SOCKETS] = {0};
    struct xsk_umem_config umem_config = {
        .fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS * NUM_SOCKETS,
        .comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS * NUM_SOCKETS,
        .frame_size = FRAME_SIZE,
        .frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
        .flags = 0,
    };

	if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
		fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK) \"%s\"\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

    set_sched_fifo(90);
    pin_thread_to_core(30); 

    printf("Set rlimit to unlimited\n"); 

	pbuffer_size = NUM_FRAMES * FRAME_SIZE;

    for (int i = 0; i < NUM_SOCKETS; i++) {


	    if (posix_memalign(&umem[i].buffer,
			   getpagesize(),
			   pbuffer_size)) {
		    fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n",
		        strerror(errno));
		    exit(EXIT_FAILURE);
	    }

        printf("Allocated memory for umem %d\n", i);

        if (xsk_umem__create(&umem[i].umem, umem[i].buffer,
                            pbuffer_size,
                            &umem[i].fq, &umem[i].cq, &umem_config)) {
            perror("xsk_umem__create");
            return 1;
        }

        printf("xsk_umem__create() for umem %d: Success\n", i);

        xsk_info[i].umem = &umem[i];
        xsk_info[i].id = i;
    }

    // Setup XDP sockets for both interfaces
    if (setup_xsk_socket(&xsk_info[0], ifname1, 0, map_name1) < 0 ||
        setup_xsk_socket(&xsk_info[1], ifname2, 0, map_name2) < 0) {
        return 1;
    }

    printf("Listening on %s and %s with AF_XDP\n", ifname1, ifname2);

    // Event loop
    struct pollfd fds[] = {
        { .fd = xsk_socket__fd(xsk_info[0].xsk), .events = POLLIN },
        { .fd = xsk_socket__fd(xsk_info[1].xsk), .events = POLLIN },
    };

    while (1) {
        int poll_ret = poll(fds, 2, 1000);
        if (poll_ret < 0) {
            perror("poll");
            break;
        }

        if (poll_ret > 0) {
            if (fds[0].revents & POLLIN)
                rx_packets(&xsk_info[0]);
            if (fds[1].revents & POLLIN)
                rx_packets(&xsk_info[1]);
        }
    }

    // Cleanup
    for (int i = 0; i < NUM_SOCKETS; i++) {
        xsk_socket__delete(xsk_info[i].xsk);
        xsk_umem__delete(umem[i].umem);
        free(umem[i].buffer);
    }    
    return 0;
}
