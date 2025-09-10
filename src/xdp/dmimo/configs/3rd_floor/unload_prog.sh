#/bin/bash

ip link set dev enp75s0v0 xdp off
ip link set dev enp75s0v1 xdp off
ip link set dev enp75s0v2 xdp off
ip link set dev enp75s0v3 xdp off
rm /sys/fs/bpf/dmimo_egress
rm /sys/fs/bpf/dmimo_egress2
rm /sys/fs/bpf/dmimo_ingress
rm /sys/fs/bpf/dmimo_ingress2
