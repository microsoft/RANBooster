#/bin/bash

#./prb_mon_load dmimo.o <iface_name> <pin_name> <map_pin_name> <ru_vlan> <ru_mac> <du_mac_middlebox> <du_mac_ru> <du_vlan>

# Load the modules that handle the egress traffic
./prb_mon_load prb_mon_egress.o enp75s0v0 /sys/fs/bpf/prb_mon_egress /sys/fs/bpf/prb_mon_load_egress 7 6C:AD:AD:00:0B:6E aa:bb:cc:11:22:33 00:11:22:33:0B:6E 1

# Load the modules that handle the ingress traffic
./prb_mon_load prb_mon_ingress.o enp75s0v1 /sys/fs/bpf/prb_mon_ingress /sys/fs/bpf/prb_mon_load_ingress 7 6C:AD:AD:00:0B:6E aa:bb:cc:11:22:33 00:11:22:33:0B:6E 1


