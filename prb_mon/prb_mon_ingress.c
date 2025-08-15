#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>
#include <stddef.h>
#include <bpf/bpf_endian.h>

#include "ranbooster_common.h"

#define VLAN_VID_MASK    0x0FFF

__u8 booster_mac_addr[] = {0x00, 0x00, 0x11, 0x11, 0x22, 0x22};

__u8 broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u8[ETH_ALEN]); 
} ru_mac_address SEC(".maps");


struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u16); 
} du_vlan SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u16); 
} ru_vlan SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u8[ETH_ALEN]); 
} du_mac_address_ru SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u8[ETH_ALEN]); 
} du_mac_address_local SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20); // 1MB ring buffer
} comp_load SEC(".maps");

struct vlan_hdr {
    __be16 h_vlan_TCI;   /* VLAN Tag Control Information */
    __be16 h_vlan_encapsulated_proto; /* Ethernet protocol type */
};

static __inline int bpf_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (p1[i] < p2[i]) ? -1 : 1; // Return -1 or 1 based on comparison
        }
    }
    return 0; // Return 0 if equal
}

SEC("xdp")
int xdp_prb_mon(struct xdp_md *ctx) {

    struct vlan_hdr *vlan_h = NULL;
    __u16 vlan_tci = 0;
    __u16 vlan_id = 0;
    __u16 ether_type = 0;
    __u32 key = 0;
    __u16 symbol = 0;
    __u8 *iq_samples_head;
    unsigned int num_prbs;
    
    // Get a pointer to the packet's data and data_end
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // Ensure packet has enough data for Ethernet header
    struct ethhdr *eh = data;
    if ((void *)(eh + 1) > data_end) {
        return XDP_DROP;
    }

    __u8 *du_mac = bpf_map_lookup_elem(&du_mac_address_ru, &key);
    if (!du_mac) {
	    return XDP_DROP;
    }

    __u8 *ranbooster_du_mac = bpf_map_lookup_elem(&du_mac_address_local, &key);
    if (!ranbooster_du_mac) {
	    return XDP_DROP;
    }

    __u8 *ru_mac = bpf_map_lookup_elem(&ru_mac_address, &key);
    if (!ru_mac) {
	    return XDP_DROP;
    }

    __u16 *du_vlan_num = bpf_map_lookup_elem(&du_vlan, &key);
    if (!du_vlan_num) {
	    return XDP_DROP;
    }

    __u16 *ru_vlan_num = bpf_map_lookup_elem(&ru_vlan, &key);
    if (!ru_vlan_num) {
	    return XDP_DROP;
    }

    void *next_hdr = (__u8 *)eh + sizeof(struct ethhdr);

    ether_type = bpf_ntohs(eh->h_proto);

    if (ether_type == ETH_P_8021Q)  {

        if ((__u8 *)next_hdr + sizeof(struct vlan_hdr) > (__u8 *)data_end) {
            return XDP_DROP;
        }

        vlan_h = next_hdr;
        vlan_tci = bpf_ntohs(vlan_h->h_vlan_TCI);

        vlan_id = vlan_tci & VLAN_VID_MASK;

        if (vlan_id != *ru_vlan_num) {
            return XDP_DROP;
        }
        
        next_hdr = (__u8 *)next_hdr + sizeof(struct vlan_hdr);
        ether_type = bpf_ntohs(vlan_h->h_vlan_encapsulated_proto);
    } else if (ether_type == 44798) { // Packets we get from loopback can be dropped
        return XDP_DROP;   
    }

    struct xran_ecpri_hdr *ecpri_hdr = (struct xran_ecpri_hdr *)next_hdr;

    if ((void *)(ecpri_hdr + 1) > data_end) {
        return XDP_DROP;
    }

    __u8 ecpri_message_type = ecpri_hdr->cmnhdr.bits.ecpri_mesg_type;
    __u16 ru_port_id = bpf_ntohs(ecpri_hdr->ecpri_xtc_id) & 0x000F;

    // Check if this packet is coming from the correct RU
    if (bpf_memcmp(ru_mac, eh->h_source, ETH_ALEN) == 0) {
    
        __builtin_memcpy(eh->h_dest, ranbooster_du_mac, ETH_ALEN);
        __builtin_memcpy(eh->h_source, booster_mac_addr, ETH_ALEN);

        if (vlan_h) {
            if ((void *)(vlan_h + 1) > data_end) {                        
                return XDP_DROP;
            }
            vlan_tci = (vlan_tci & 0xF000) | (*du_vlan_num & 0x0FFF);
            vlan_h->h_vlan_TCI = bpf_htons(vlan_tci);
        }

        return XDP_TX;

        if (ecpri_message_type == ECPRI_IQ_DATA) {

            if (ru_port_id != SELECTED_RU_PORT_ID) {
                return XDP_TX;
            }

            next_hdr = (__u8 *)next_hdr + sizeof(struct xran_ecpri_hdr);

            struct radio_app_common_hdr *app_common_hdr = (struct radio_app_common_hdr *)next_hdr;

            if ((void *)(app_common_hdr + 1) > data_end) {
                return XDP_DROP;
            }

            struct radio_app_common_hdr radio_hdr_cpy = *app_common_hdr;
                
            radio_hdr_cpy.sf_slot_sym.value = bpf_ntohs(radio_hdr_cpy.sf_slot_sym.value);
            symbol = radio_hdr_cpy.sf_slot_sym.symb_id;

            if (symbol != MONITORING_SYMBOL_INGRESS) {
                return XDP_TX;
            }

            next_hdr = (__u8 *)next_hdr + sizeof(struct radio_app_common_hdr);
            struct data_section_hdr *data_sec_hdr = (struct data_section_hdr *)next_hdr;

            if ((void *)(data_sec_hdr + 1) > data_end) {
                return XDP_DROP;
            }

            struct data_section_hdr data_sec_hdr_cpy = *data_sec_hdr;
            data_sec_hdr_cpy.fields.all_bits = bpf_ntohl(data_sec_hdr->fields.all_bits);
            num_prbs = data_sec_hdr_cpy.fields.num_prbu;

            if (num_prbs == 0) {
                num_prbs = 273;
            }

            next_hdr = (__u8 *)next_hdr + sizeof(struct data_section_hdr);

            struct data_section_compression_hdr *cmp_header = (struct data_section_compression_hdr *)next_hdr;

            if ((void *)(cmp_header + 1) > data_end) {
                return XDP_DROP;
            }

            iq_samples_head = next_hdr + sizeof(struct data_section_compression_hdr);

            struct prb_stats_event *e = bpf_ringbuf_reserve(&comp_load, sizeof(struct prb_stats_event), 0);
            
            if (!e)
                return XDP_TX;

            for (int rb_id = 0; rb_id < MAX_NUM_RBS; rb_id++) {
                    
                if (rb_id >= num_prbs) {
                    break;
                }

                if ((void *)(iq_samples_head + COMPRESSED_RB_SIZE_BYTES) > data_end) {
                    break;
                }

                struct compression_params *comp_params = (struct compression_params *)iq_samples_head;

                e->total_num_prbs++;

                if (comp_params->exponent > 2) {
                    e->num_prbs_used += 1;
                }

                iq_samples_head += COMPRESSED_RB_SIZE_BYTES; // 28B for each RB
            }

            bpf_ringbuf_submit(e, 0);

            return XDP_TX;
        } else {
            return XDP_DROP;
        }
    }
    return XDP_DROP;
}

char _license[] SEC("license") = "MIT";
