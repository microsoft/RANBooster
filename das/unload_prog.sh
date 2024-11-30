#/bin/bash

ip link set dev enp63s0v0 xdp off
ip link set dev enp63s0v1 xdp off
rm /sys/fs/bpf/das
rm /sys/fs/bpf/das2
rm /sys/fs/bpf/das_redirect_map
rm /sys/fs/bpf/das2_redirect_map
