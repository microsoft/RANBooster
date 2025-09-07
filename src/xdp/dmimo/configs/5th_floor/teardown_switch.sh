#!/bin/bash

ip link set eth2 vf 0 mac 00:00:00:00:00:00 spoofchk off

ip link set dev enp75s0np0 mtu 1500

echo 0 > /sys/class/net/eth0/device/sriov_numvfs
