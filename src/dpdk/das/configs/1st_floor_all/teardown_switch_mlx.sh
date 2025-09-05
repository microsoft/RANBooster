#!/bin/bash

echo 0 > /sys/class/net/eth0/device/sriov_numvfs

ip link set eth2 vf 0 mac 00:00:00:00:00:00
#ip link set eth2 vf 1 mac 00:00:00:00:00:00 spoofchk on
#ip link set eth2 vf 2 mac 00:00:00:00:00:00 spoofchk on
#ip link set eth2 vf 3 mac 00:00:00:00:00:00 spoofchk on
