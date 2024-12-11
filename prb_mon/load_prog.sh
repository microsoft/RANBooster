#/bin/bash

#./prb_mon_load dmimo.o <iface_name> <pin_name> <vlan> <ru_mac> <du_mac_middlebox> <du_mac_ru>

# Load the modules that handle the egress traffic
./prb_mon_load prb_mon_egress.o enp63s0v0 /sys/fs/bpf/prb_mon_egress 1 6C:AD:AD:00:0A:9E aa:bb:cc:11:22:33 00:11:22:33:44:68

# Load the modules that handle the ingress traffic
./prb_mon_load prb_mon_ingress.o enp63s0v1 /sys/fs/bpf/prb_mon_ingress 1 6C:AD:AD:00:0A:9E aa:bb:cc:11:22:33 00:11:22:33:44:68


