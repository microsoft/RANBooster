#!/bin/bash

echo 0 > /sys/class/net/eth0/device/sriov_numvfs

ip link set eth2 vf 0 mac 00:00:00:00:00:00
ip link set eth3 vf 0 mac 00:00:00:00:00:00
