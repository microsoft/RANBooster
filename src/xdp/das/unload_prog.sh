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
rm /sys/fs/bpf/das_egress
rm /sys/fs/bpf/das_egress2
rm /sys/fs/bpf/das_egress3
rm /sys/fs/bpf/das_ingress
rm /sys/fs/bpf/das_ingress2
rm /sys/fs/bpf/das_ingress3

rm /sys/fs/bpf/das_redirect_map_egress
rm /sys/fs/bpf/das_redirect_map_egress2
rm /sys/fs/bpf/das_redirect_map_egress3
rm /sys/fs/bpf/das_redirect_map_ingress
rm /sys/fs/bpf/das_redirect_map_ingress2
rm /sys/fs/bpf/das_redirect_map_ingress3
