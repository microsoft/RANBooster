#!/bin/bash

echo 5 > /sys/class/net/eth0/device/sriov_numvfs

ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off


ip link set enp75s0np0 vf 0 trust on
ip link set enp75s0np0 vf 1 trust on
ip link set enp75s0np0 vf 2 trust on
ip link set enp75s0np0 vf 3 trust on
ip link set enp75s0np0 vf 4 trust on

ip link set enp75s0np0 vf 0 mac 00:00:11:11:22:22 spoofchk off
ip link set enp75s0v0 address 00:00:11:11:22:22

# RU 1
ip link set enp75s0np0 vf 1 mac 66:44:33:22:11:00 spoofchk off
ip link set enp75s0v1 address 66:44:33:22:11:00

# RU 19
ip link set enp75s0np0 vf 2 mac 00:11:22:33:0B:3E spoofchk off
ip link set enp75s0v2 address 00:11:22:33:0B:3E

#RU 10
ip link set enp75s0np0 vf 3 mac 00:11:22:33:07:1E spoofchk off
ip link set enp75s0v3 address 00:11:22:33:07:1E

#RU 12
ip link set enp75s0np0 vf 4 mac 00:11:22:33:0B:42 spoofchk off
ip link set enp75s0v4 address 00:11:22:33:0B:42

ip link set dev enp75s0np0 mtu 9600
ip link set dev enp75s0v0 mtu 9600
ip link set dev enp75s0v1 mtu 9600
ip link set dev enp75s0v2 mtu 9600
ip link set dev enp75s0v3 mtu 9600
ip link set dev enp75s0v4 mtu 9600

ethtool -K enp75s0v0 gro on tso on gso on
ethtool -K enp75s0v1 gro on tso on gso on
ethtool -K enp75s0v2 gro on tso on gso on
ethtool -K enp75s0v3 gro on tso on gso on
ethtool -K enp75s0v4 gro on tso on gso on

