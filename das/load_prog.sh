#/bin/bash

#./das_load bpf.o <iface_name> <pin_name> <redirect_map_pin_name> <middlebox_mac> <ru_vlan> <ru_mac> <du_vlan> <du_mac> <du_mac_configured_in_the_ru>

./das_load das_egress.o enp75s0v0 /sys/fs/bpf/das_egress /sys/fs/bpf/das_redirect_map_egress 86:4D:3B:02:0A:8C 1 6C:AD:AD:00:08:86 1 aa:bb:cc:11:22:33 00:11:22:33:44:66
./das_load das_egress.o enp75s0v1 /sys/fs/bpf/das_egress2 /sys/fs/bpf/das_redirect_map_egress2 86:4D:3B:02:0A:8C 19 6C:AD:AD:00:0B:3E 1 aa:bb:cc:11:22:33 00:11:22:33:0B:3E

./das_load das_ingress.o enp75s0v2 /sys/fs/bpf/das_ingress /sys/fs/bpf/das_redirect_map_ingress 86:4D:3B:02:0A:8C 1 6C:AD:AD:00:08:86 1 aa:bb:cc:11:22:33 00:11:22:33:44:66
./das_load das_ingress.o enp75s0v3 /sys/fs/bpf/das_ingress2 /sys/fs/bpf/das_redirect_map_ingress2 86:4D:3B:02:0A:8C 19 6C:AD:AD:00:0B:3E 1 aa:bb:cc:11:22:33 00:11:22:33:0B:3E

#./das_rx_handler dmimo.o <ingress_iface_name1> <redirect_map_pin_name1> <ingress_iface_name2> <redirect_map_pin_name2> <du_mac> <ranbooster_mac>
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../ofh_comp ./das_rx_handler enp75s0v2 /sys/fs/bpf/das_redirect_map_ingress enp75s0v3 /sys/fs/bpf/das_redirect_map_ingress2 aa:bb:cc:11:22:33 86:4D:3B:02:0A:8C
