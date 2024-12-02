#/bin/bash

ip link set dev enp63s0v0 xdp off
ip link set dev enp63s0v1 xdp off
ip link set dev enp63s0v2 xdp off
ip link set dev enp63s0v3 xdp off
rm /sys/fs/bpf/das_egress
rm /sys/fs/bpf/das_egress2
rm /sys/fs/bpf/das_ingress
rm /sys/fs/bpf/das_ingress2

rm /sys/fs/bpf/das_redirect_map_egress
rm /sys/fs/bpf/das_redirect_map_egress2
rm /sys/fs/bpf/das_redirect_map_ingress
rm /sys/fs/bpf/das_redirect_map_ingress2
