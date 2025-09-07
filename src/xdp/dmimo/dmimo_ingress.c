#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>
#include <stddef.h>
#include <bpf/bpf_endian.h>

#include "ranbooster_common.h"

#define VLAN_VID_MASK    0x0FFF

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
} ru_vlan SEC(".maps");

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
    __type(value, __u8[ETH_ALEN]); 
} du_mac_address SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u8[ETH_ALEN]); 
} ranbooster_du_mac_address SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u32); 
} ports_to_forward SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);          
    __type(key, __u32);              
    __type(value, __u8[ETH_ALEN]); 
} booster_mac SEC(".maps");

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
int xdp_dmimo(struct xdp_md *ctx) {

    struct vlan_hdr *vlan_h = NULL;
    __u16 vlan_tci = 0;
    __u16 vlan_id = 0;
    __u16 ether_type = 0;
    __u32 key = 0;
    
    // Get a pointer to the packet's data and data_end
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // Ensure packet has enough data for Ethernet header
    struct ethhdr *eh = data;
    if ((void *)(eh + 1) > data_end) {
        return XDP_DROP;
    }

    __u8 *du_mac = bpf_map_lookup_elem(&du_mac_address, &key);
    if (!du_mac) {
	    return XDP_DROP;
    }

    __u16 *dvlan = bpf_map_lookup_elem(&du_vlan, &key);
    if (!dvlan) {
	    return XDP_DROP;
    }

    __u8 *ru_mac = bpf_map_lookup_elem(&ru_mac_address, &key);
    if (!ru_mac) {
	    return XDP_DROP;
    }

    __u16 *rvlan = bpf_map_lookup_elem(&ru_vlan, &key);
    if (!rvlan) {
	    return XDP_DROP;
    }

    __u8 *booster_mac_addr = bpf_map_lookup_elem(&booster_mac, &key);
    if (!booster_mac_addr) {
	    return XDP_DROP;
    }

    __u32 *port_fwd = bpf_map_lookup_elem(&ports_to_forward, &key);
    if (!port_fwd) {
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

        if (vlan_id != *dvlan && vlan_id != *rvlan) {
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

    __u16 ru_port_id = bpf_ntohs(ecpri_hdr->ecpri_xtc_id) & 0x000F;

    unsigned int bitmask = 1 << ru_port_id;

    // Check if this packet is coming from the correct RU
    if (bpf_memcmp(ru_mac, eh->h_source, ETH_ALEN) == 0) {
        // This packet is coming from the RU handled by this instance
        // forward to the DU

        if (bitmask & *port_fwd) {
            // Change the VLAN
            if (vlan_h) {
                if ((void *)(vlan_h + 1) > data_end) {                    
                    return XDP_DROP;
                }
                vlan_tci = (vlan_tci & 0xF000) | (*dvlan & 0x0FFF);
                vlan_h->h_vlan_TCI = bpf_htons(vlan_tci);
            }
            __builtin_memcpy(eh->h_dest, du_mac, ETH_ALEN);
            __builtin_memcpy(eh->h_source, booster_mac_addr, ETH_ALEN);
            return XDP_TX;
        } else {
           return XDP_DROP;
        }
    }
    return XDP_DROP;
}

char _license[] SEC("license") = "MIT";
