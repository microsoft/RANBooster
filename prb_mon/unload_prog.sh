#/bin/bash

ip link set dev enp63s0v0 xdp off
ip link set dev enp63s0v1 xdp off
rm /sys/fs/bpf/prb_mon_egress
rm /sys/fs/bpf/prb_mon_ingress
