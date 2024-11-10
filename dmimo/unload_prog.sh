#/bin/bash

ip link set dev eth13 xdp off
ip link set dev eth18 xdp off
rm /sys/fs/bpf/dmimo
rm /sys/fs/bpf/dmimo2
