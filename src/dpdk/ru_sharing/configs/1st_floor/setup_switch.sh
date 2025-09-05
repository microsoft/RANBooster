#!/bin/bash

echo 1 > /sys/class/net/eth0/device/sriov_numvfs

# DU 1
ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off
# DU 2
ip link set eth3 vf 0 mac aa:bb:cc:33:22:11 spoofchk off


ip link set enp75s0np0 vf 0 trust on

ip link set enp75s0np0 vf 0 mac 00:00:11:11:22:22 spoofchk off
ip link set enp75s0v0 address 00:00:11:11:22:22

ip link set dev enp75s0np0 mtu 9600
ip link set dev enp75s0v0 mtu 9600

ethtool -K enp75s0v0 gro on tso on gso on
