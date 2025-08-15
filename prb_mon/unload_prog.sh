#/bin/bash

ip link set dev enp75s0v0 xdp off
ip link set dev enp75s0v1 xdp off
rm /sys/fs/bpf/prb_mon_egress
rm /sys/fs/bpf/prb_mon_ingress
rm /sys/fs/bpf/prb_mon_load_egress
rm /sys/fs/bpf/prb_mon_load_ingress
