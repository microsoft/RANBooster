#/bin/bash

ip link set dev enp75s0v0 xdp off
ip link set dev enp75s0v1 xdp off
ip link set dev enp75s0v2 xdp off
ip link set dev enp75s0v3 xdp off
ip link set dev enp75s0v4 xdp off
ip link set dev enp75s0v5 xdp off
ip link set dev enp75s0v6 xdp off
ip link set dev enp75s0v7 xdp off
ip link set dev enp75s0v8 xdp off
ip link set dev enp75s0v9 xdp off
ip link set dev enp75s0v10 xdp off
ip link set dev enp75s0v11 xdp off

rm /sys/fs/bpf/das_egress
rm /sys/fs/bpf/das_egress2
rm /sys/fs/bpf/das_ingress
rm /sys/fs/bpf/das_ingress2

rm /sys/fs/bpf/das_redirect_map_egress
rm /sys/fs/bpf/das_redirect_map_egress2
rm /sys/fs/bpf/das_redirect_map_ingress
rm /sys/fs/bpf/das_redirect_map_ingress2

rm /sys/fs/bpf/dmimo_egress_vru1
rm /sys/fs/bpf/dmimo_egress2_vru1
rm /sys/fs/bpf/dmimo_ingress_vru1
rm /sys/fs/bpf/dmimo_ingress2_vru1

rm /sys/fs/bpf/dmimo_egress_vru2
rm /sys/fs/bpf/dmimo_egress2_vru2
rm /sys/fs/bpf/dmimo_ingress_vru2
rm /sys/fs/bpf/dmimo_ingress2_vru2
