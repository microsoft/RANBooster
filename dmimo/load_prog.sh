#/bin/bash

#./dmimo_load dmimo.o <iface_name> <pin_name> <ru_vlan> <ru_mac> <du_vlan> <du_mac> <ru port bitmap>

# Load the modules that handle the egress traffic
./dmimo_load dmimo_egress.o enp63s0v0 /sys/fs/bpf/dmimo_egress 1 6C:AD:AD:00:0A:9E 1 aa:bb:cc:11:22:33 51 00:11:22:33:44:68
./dmimo_load dmimo_egress.o enp63s0v1 /sys/fs/bpf/dmimo_egress2 3 6C:AD:AD:00:0A:A0 1 aa:bb:cc:11:22:33 204 00:11:22:33:0a:a0

# Load the modules that handle the ingress traffic
./dmimo_load dmimo_ingress.o enp63s0v2 /sys/fs/bpf/dmimo_ingress 1 6C:AD:AD:00:0A:9E 1 aa:bb:cc:11:22:33 51 00:11:22:33:44:68
./dmimo_load dmimo_ingress.o enp63s0v3 /sys/fs/bpf/dmimo_ingress2 3 6C:AD:AD:00:0A:A0 1 aa:bb:cc:11:22:33 204 00:11:22:33:0a:a0


