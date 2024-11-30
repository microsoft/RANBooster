#/bin/bash

ip link set dev enp63s0v0 xdp off
ip link set dev enp63s0v1 xdp off
rm /sys/fs/bpf/dmimo
rm /sys/fs/bpf/dmimo2
