#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <net/if.h>
#include <unistd.h>
#include <linux/if_link.h>

#define XDP_FLAGS XDP_FLAGS_REPLACE | XDP_FLAGS_DRV_MODE

void parse_mac_address(const char *mac_str, unsigned char mac_addr[6]) {
    int values[6];
    if (sscanf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        fprintf(stderr, "Invalid MAC address format\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < 6; i++) {
        mac_addr[i] = (unsigned char)values[i];
    }
}

int main(int argc, char **argv) {
    const char *filename, *iface, *pin_path;
    struct bpf_object *obj;
    struct bpf_program *prog;
    struct bpf_map *map;
    int prog_fd, ifindex, ret;
    uint16_t ru_vlan, du_vlan;
    uint32_t ru_port_fwd_bitmap;

    if (argc != 11) {
        fprintf(stderr, "Usage: %s <xdp_prog.o> <iface> <pin_path> <ranbooster_mac> <ru vlan> <ru mac> <du vlan> <du mac> <ru port bitmap> <ranbooster_du_mac>\n", argv[0]);
        return 1;
    }

    filename = argv[1];   // XDP program object file
    iface = argv[2];      // Network interface name
    pin_path = argv[3];   // Path where the program should be pinned
    const char *booster_mac = argv[4]; // Middlebox MAC address
    ru_vlan = atoi(argv[5]); // RU vlan
    const char *ru_mac = argv[6]; // RU MAC address
    du_vlan = atoi(argv[7]); // DU vlan
    const char *du_mac = argv[8]; // DU MAC address
    ru_port_fwd_bitmap = atoi(argv[9]); // RU port bitmap
    const char *ranbooster_du_mac = argv[10]; // The DU MAC address configured at the RU that is controlled by the loaded program


    // Get interface index
    ifindex = if_nametoindex(iface);
    if (ifindex == 0) {
        perror("if_nametoindex");
        return 1;
    }

    // Open the BPF object file
    obj = bpf_object__open_file(filename, NULL);
    if (!obj) {
        fprintf(stderr, "Failed to open BPF object file %s: %s\n", filename, strerror(errno));
        return 1;
    }

    // Load the BPF program
    ret = bpf_object__load(obj);
    if (ret) {
        fprintf(stderr, "Failed to load BPF object: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    // Find the XDP program by section name (commonly ".text" or "xdp")
    prog = bpf_object__find_program_by_name(obj, "xdp_dmimo");
    if (!prog) {
        fprintf(stderr, "Failed to find XDP program in object file\n");
        bpf_object__close(obj);
        return 1;
    }

    map = bpf_object__find_map_by_name(obj, "ru_vlan");
    if (!map) {
        fprintf(stderr, "Failed to find global variable map\n");
        bpf_object__close(obj);
        return 1;
    }

    // Update the RU vlan variable value in the map
    int key = 0;
    ret = bpf_map_update_elem(bpf_map__fd(map), &key, &ru_vlan, BPF_ANY);
    if (ret) {
        fprintf(stderr, "Failed to update global variable: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    // Find the MAC address map
    map = bpf_object__find_map_by_name(obj, "booster_mac");
    if (!map) {
        fprintf(stderr, "Failed to find MAC address map\n");
        bpf_object__close(obj);
        return 1;
    }

    // Parse the MAC address from the string
    unsigned char booster_mac_addr[6];
    parse_mac_address(booster_mac, booster_mac_addr);

    // Update the MAC address in the map
    ret = bpf_map_update_elem(bpf_map__fd(map), &key, &booster_mac_addr, BPF_ANY);
    if (ret) {
        fprintf(stderr, "Failed to update MAC address: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    // Find the MAC address map
    map = bpf_object__find_map_by_name(obj, "ru_mac_address");
    if (!map) {
        fprintf(stderr, "Failed to find MAC address map\n");
        bpf_object__close(obj);
        return 1;
    }

    // Parse the MAC address from the string
    unsigned char ru_mac_addr[6];
    parse_mac_address(ru_mac, ru_mac_addr);

    // Update the MAC address in the map
    ret = bpf_map_update_elem(bpf_map__fd(map), &key, &ru_mac_addr, BPF_ANY);
    if (ret) {
        fprintf(stderr, "Failed to update MAC address: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    map = bpf_object__find_map_by_name(obj, "du_vlan");
    if (!map) {
        fprintf(stderr, "Failed to find variable map\n");
        bpf_object__close(obj);
        return 1;
    }

    // Update the RU vlan variable value in the map
    ret = bpf_map_update_elem(bpf_map__fd(map), &key, &du_vlan, BPF_ANY);
    if (ret) {
        fprintf(stderr, "Failed to update variable: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    // Find the MAC address map
    map = bpf_object__find_map_by_name(obj, "du_mac_address");
    if (!map) {
        fprintf(stderr, "Failed to find MAC address map\n");
        bpf_object__close(obj);
        return 1;
    }

    // Parse the MAC address from the string
    unsigned char du_mac_addr[6];
    parse_mac_address(du_mac, du_mac_addr);

    // Update the MAC address in the map
    ret = bpf_map_update_elem(bpf_map__fd(map), &key, &du_mac_addr, BPF_ANY);
    if (ret) {
        fprintf(stderr, "Failed to update MAC address: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }


    
    // Find the MAC address map
    map = bpf_object__find_map_by_name(obj, "ranbooster_du_mac_address");
    if (!map) {
        fprintf(stderr, "Failed to find MAC address map\n");
        bpf_object__close(obj);
        return 1;
    }

    // Parse the MAC address from the string
    unsigned char ranbooster_du_mac_addr[6];
    parse_mac_address(ranbooster_du_mac, ranbooster_du_mac_addr);

    // Update the MAC address in the map
    ret = bpf_map_update_elem(bpf_map__fd(map), &key, &ranbooster_du_mac_addr, BPF_ANY);
    if (ret) {
        fprintf(stderr, "Failed to update MAC address: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    map = bpf_object__find_map_by_name(obj, "ports_to_forward");
    if (!map) {
        fprintf(stderr, "Failed to find port forward bitmap map\n");
        bpf_object__close(obj);
        return 1;
    }

    // Update the RU vlan variable value in the map
    ret = bpf_map_update_elem(bpf_map__fd(map), &key, &ru_port_fwd_bitmap, BPF_ANY);
    if (ret) {
        fprintf(stderr, "Failed to update variable: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    // Pin the program
    ret = bpf_program__pin(prog, pin_path);
    if (ret) {
        fprintf(stderr, "Failed to pin program to %s: %s\n", pin_path, strerror(errno));
        bpf_object__close(obj);
        return 1;
    }
    printf("Program pinned at %s\n", pin_path);

    // Get the program file descriptor
    prog_fd = bpf_program__fd(prog);
    if (prog_fd < 0) {
        fprintf(stderr, "Failed to get program file descriptor\n");
        bpf_object__close(obj);
        return 1;
    }

    // Attach the XDP program to the network interface
    ret = bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS, NULL);
    if (ret) {
        fprintf(stderr, "Failed to attach XDP program to interface %s: %s\n", iface, strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    printf("XDP program successfully loaded and attached to interface %s\n", iface);

    // Clean up
    bpf_object__close(obj);
    return 0;
}
