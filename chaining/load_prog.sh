#/bin/bash

# Load DAS middlebox rules

#./das_load bpf.o <iface_name> <pin_name> <redirect_map_pin_name> <middlebox_mac> <ru_vlan> <ru_mac> <du_vlan> <du_mac> <du_mac_configured_in_the_ru>

../das/das_load ../das/das_egress.o enp75s0v0 /sys/fs/bpf/das_egress /sys/fs/bpf/das_redirect_map_egress 86:4D:3B:02:0A:8C 1 AA:11:BB:22:CC:33 1 aa:bb:cc:11:22:33 AA:BB:CC:AA:BB:CC
../das/das_load ../das/das_egress.o enp75s0v1 /sys/fs/bpf/das_egress2 /sys/fs/bpf/das_redirect_map_egress2 86:4D:3B:02:0A:8C 19 1A:2B:3C:1A:2B:3C 1 aa:bb:cc:11:22:33 CC:BB:AA:AA:BB:CC

../das/das_load ../das/das_ingress.o enp75s0v2 /sys/fs/bpf/das_ingress /sys/fs/bpf/das_redirect_map_ingress 86:4D:3B:02:0A:8C 1 AA:11:BB:22:CC:33 1 aa:bb:cc:11:22:33 AA:BB:CC:AA:BB:CC
../das/das_load ../das/das_ingress.o enp75s0v3 /sys/fs/bpf/das_ingress2 /sys/fs/bpf/das_redirect_map_ingress2 86:4D:3B:02:0A:8C 19 1A:2B:3C:1A:2B:3C 1 aa:bb:cc:11:22:33 CC:BB:AA:AA:BB:CC


# Load dMIMO middlebox rules

#../../dmimo_load dmimo.o <iface_name> <pin_name> <ranbooster_mac> <ru_vlan> <ru_mac> <du_vlan> <du_mac> <ru port bitmap>

# For vRU1

# Load the modules that handle the egress traffic
../dmimo/dmimo_load ../dmimo/dmimo_egress.o enp75s0v4 /sys/fs/bpf/dmimo_egress_vru1 AA:11:BB:22:CC:33 1 6C:AD:AD:00:08:86 1 AA:BB:CC:AA:BB:CC 51 00:11:22:33:44:66
../dmimo/dmimo_load ../dmimo/dmimo_egress.o enp75s0v5 /sys/fs/bpf/dmimo_egress2_vru1 AA:11:BB:22:CC:33 3 6C:AD:AD:00:08:8C 1 AA:BB:CC:AA:BB:CC 204 66:44:33:22:11:00

# Load the modules that handle the ingress traffic
../dmimo/dmimo_load ../dmimo/dmimo_ingress.o enp75s0v6 /sys/fs/bpf/dmimo_ingress_vru1 AA:11:BB:22:CC:33 1 6C:AD:AD:00:08:86 1 AA:BB:CC:AA:BB:CC 51 00:11:22:33:44:66
../dmimo/dmimo_load ../dmimo/dmimo_ingress.o enp75s0v7 /sys/fs/bpf/dmimo_ingress2_vru1 AA:11:BB:22:CC:33 3 6C:AD:AD:00:08:8C 1 AA:BB:CC:AA:BB:CC 204 66:44:33:22:11:00



# For vRU2

# Load the modules that handle the egress traffic
../dmimo/dmimo_load ../dmimo/dmimo_egress.o enp75s0v8 /sys/fs/bpf/dmimo_egress_vru2 1A:2B:3C:1A:2B:3C 19 6C:AD:AD:00:0B:3E 19 CC:BB:AA:AA:BB:CC 51 00:11:22:33:0B:3E
../dmimo/dmimo_load ../dmimo/dmimo_egress.o enp75s0v9 /sys/fs/bpf/dmimo_egress2_vru2 1A:2B:3C:1A:2B:3C 21 6C:AD:AD:00:0B:40 19 CC:BB:AA:AA:BB:CC 204 00:11:22:33:0B:40

# Load the modules that handle the ingress traffic
../dmimo/dmimo_load ../dmimo/dmimo_ingress.o enp75s0v10 /sys/fs/bpf/dmimo_ingress_vru2 1A:2B:3C:1A:2B:3C 19 6C:AD:AD:00:0B:3E 19 CC:BB:AA:AA:BB:CC 51 00:11:22:33:0B:3E
../dmimo/dmimo_load ../dmimo/dmimo_ingress.o enp75s0v11 /sys/fs/bpf/dmimo_ingress2_vru2 1A:2B:3C:1A:2B:3C 21 6C:AD:AD:00:0B:40 19 CC:BB:AA:AA:BB:CC 204 00:11:22:33:0B:40


# Run userspace DAS

#./das_rx_handler dmimo.o <ingress_iface_name1> <redirect_map_pin_name1> <ingress_iface_name2> <redirect_map_pin_name2> <du_mac> <ranbooster_mac>
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../ofh_comp taskset -c 0 ../das/das_rx_handler enp75s0v2 /sys/fs/bpf/das_redirect_map_ingress enp75s0v3 /sys/fs/bpf/das_redirect_map_ingress2 aa:bb:cc:11:22:33 86:4D:3B:02:0A:8C
