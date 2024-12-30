#/bin/bash

#../../dmimo_load dmimo.o <iface_name> <pin_name> <ranbooster_mac> <ru_vlan> <ru_mac> <du_vlan> <du_mac> <ru port bitmap>

# Load the modules that handle the egress traffic
../../dmimo_load ../../dmimo_egress.o enp75s0v0 /sys/fs/bpf/dmimo_egress 86:4D:3B:02:0A:8C 19 6C:AD:AD:00:0B:3E 19 aa:bb:cc:11:22:33 51 00:11:22:33:44:66
../../dmimo_load ../../dmimo_egress.o enp75s0v1 /sys/fs/bpf/dmimo_egress2 86:4D:3B:02:0A:8C 21 6C:AD:AD:00:0B:40 19 aa:bb:cc:11:22:33 204 66:44:33:22:11:00

# Load the modules that handle the ingress traffic
../../dmimo_load ../../dmimo_ingress.o enp75s0v2 /sys/fs/bpf/dmimo_ingress 86:4D:3B:02:0A:8C 19 6C:AD:AD:00:0B:3E 19 aa:bb:cc:11:22:33 51 00:11:22:33:44:66
../../dmimo_load ../../dmimo_ingress.o enp75s0v3 /sys/fs/bpf/dmimo_ingress2 86:4D:3B:02:0A:8C 21 6C:AD:AD:00:0B:40 19 aa:bb:cc:11:22:33 204 66:44:33:22:11:00


