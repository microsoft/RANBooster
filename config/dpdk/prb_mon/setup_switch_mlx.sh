#!/bin/bash

echo 3 > /sys/class/net/eth0/device/sriov_numvfs

ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off

ip link set enp75s0np0 vf 0 trust on
ip link set enp75s0np0 vf 1 trust on

#DU interface
ip link set enp75s0np0 vf 0 mac 00:00:11:11:22:22 spoofchk off
ip link set enp75s0v0 address 00:00:11:11:22:22

# RU interface
ip link set enp75s0np0 vf 1 mac 00:11:22:33:0B:6E spoofchk off
ip link set enp75s0v1 address 00:11:22:33:0B:6E

ip link set dev enp75s0np0 mtu 9600
ip link set dev enp75s0v0 mtu 9600
ip link set dev enp75s0v1 mtu 9600

ethtool -K enp75s0v0 gro on tso on gso on
ethtool -K enp75s0v1 gro on tso on gso on
