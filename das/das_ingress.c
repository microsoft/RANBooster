#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>
#include <stddef.h>
#include <bpf/bpf_endian.h>

#include "ranbooster_common.h"

#define VLAN_VID_MASK    0x0FFF
#define MAX_ENTRIES 11

__u8 booster_mac_addr[] = {0xe2, 0x53, 0x8d, 0x8f, 0xa4, 0x6b};

__u8 broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, MAX_ENTRIES);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} redirect_map SEC(".maps");

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

int counter = 0;

SEC("xdp")
int xdp_das(struct xdp_md *ctx) {

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

    // Check if this packet is coming from the DU
    if (bpf_memcmp(ru_mac, eh->h_source, ETH_ALEN) == 0) {
        // Send the packet to the userspace application to merge it
        // __builtin_memcpy(eh->h_dest, du_mac, ETH_ALEN);
        // __builtin_memcpy(eh->h_source, booster_mac_addr, ETH_ALEN);
        // return XDP_TX;
        // if (vlan_id == 1) {
        //     __u16 ru_port_id = bpf_ntohs(ecpri_hdr->ecpri_xtc_id) & 0x000F;
        //     bpf_printk("The antenna is %d\n", ru_port_id);
        // }

        // if (vlan_id == 1) {
        //     counter++;
        //     if (counter % 1024 == 0) {
        //         bpf_printk("Counter for vlan1 is %d\n", counter);
        //     }
        // }

        return bpf_redirect_map(&redirect_map, 0, XDP_DROP);
    }
    return XDP_DROP;
}

char _license[] SEC("license") = "GPL";
