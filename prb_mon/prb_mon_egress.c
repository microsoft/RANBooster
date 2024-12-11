#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>
#include <stddef.h>
#include <bpf/bpf_endian.h>

#include "ranbooster_common.h"

/*
 This module handles the egress traffic, i.e., traffic going from the DU to the RUs.
*/

#define VLAN_VID_MASK    0x0FFF

__u8 booster_mac_addr[] = {0xe2, 0x53, 0x8d, 0x8f, 0xa4, 0x6b};

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
} vlan SEC(".maps");

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

SEC("xdp.frags")
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

    __u16 *vlan_num = bpf_map_lookup_elem(&vlan, &key);
    if (!vlan_num) {
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

        if (vlan_id != *vlan_num) {
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

    // Check if this packet is coming from the DU
    if (bpf_memcmp(du_mac, eh->h_source, ETH_ALEN) == 0) {

        // Check if this is going to the middlebox
        if (bpf_memcmp(booster_mac_addr, eh->h_dest, ETH_ALEN) == 0) {
            // Change the MAC address to send to the correct destination
            __builtin_memcpy(eh->h_dest, ru_mac, ETH_ALEN);
            __builtin_memcpy(eh->h_source, ranbooster_du_mac, ETH_ALEN);

            if (ecpri_message_type == ECPRI_RT_CONTROL_DATA) { // If this is a C-plane message
                return XDP_TX;
            } else if (ecpri_message_type == ECPRI_IQ_DATA) {   
                
                // TODO: Get the load and forward
                return XDP_TX;
            } else {
                return XDP_DROP;
            }
        }
    }
    return XDP_DROP;
}

char _license[] SEC("license") = "MIT";
