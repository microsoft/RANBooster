#/bin/bash

#./das_load dmimo.o <iface_name> <pin_name> <ru_vlan> <ru_mac> <du_vlan> <du_mac>

./das_load das.o enp63s0v0 /sys/fs/bpf/das /sys/fs/bpf/das_redirect_map 1 6C:AD:AD:00:0A:9E 1 00:11:22:33:44:68
./das_load das.o enp63s0v1 /sys/fs/bpf/das2 /sys/fs/bpf/das2_redirect_map 3 6C:AD:AD:00:0A:A0 1 00:11:22:33:44:68
./das_rx_handler enp63s0v0 /sys/fs/bpf/das_redirect_map enp63s0v1 /sys/fs/bpf/das2_redirect_map 00:11:22:33:44:68 e2:53:8d:8f:a4:6b
