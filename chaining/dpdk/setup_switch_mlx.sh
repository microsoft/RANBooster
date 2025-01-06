#!/bin/bash

echo 9 > /sys/class/net/eth0/device/sriov_numvfs

ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off


ip link set enp75s0np0 vf 0 trust on
ip link set enp75s0np0 vf 1 trust on
ip link set enp75s0np0 vf 2 trust on
ip link set enp75s0np0 vf 3 trust on
ip link set enp75s0np0 vf 4 trust on
ip link set enp75s0np0 vf 5 trust on
ip link set enp75s0np0 vf 6 trust on
ip link set enp75s0np0 vf 7 trust on
ip link set enp75s0np0 vf 8 trust on

# DAS configs
ip link set enp75s0np0 vf 0 mac 00:00:11:11:22:22 spoofchk off
ip link set enp75s0v0 address 00:00:11:11:22:22


ip link set enp75s0np0 vf 1 mac AA:BB:CC:AA:BB:CC spoofchk off
ip link set enp75s0v1 address AA:BB:CC:AA:BB:CC


ip link set enp75s0np0 vf 2 mac CC:BB:AA:CC:BB:AA spoofchk off
ip link set enp75s0v2 address CC:BB:AA:CC:BB:AA


# vRU 1 configs
ip link set enp75s0np0 vf 3 mac AA:11:BB:22:CC:33 spoofchk off
ip link set enp75s0v3 address AA:11:BB:22:CC:33


ip link set enp75s0np0 vf 4 mac 00:11:22:33:44:66 spoofchk off
ip link set enp75s0v4 address 00:11:22:33:44:66


ip link set enp75s0np0 vf 5 mac 66:44:33:22:11:00 spoofchk off
ip link set enp75s0v5 address 66:44:33:22:11:00


# vRU 2 configs
ip link set enp75s0np0 vf 6 mac 1A:2B:3C:1A:2B:3C spoofchk off
ip link set enp75s0v6 address 1A:2B:3C:1A:2B:3C


ip link set enp75s0np0 vf 7 mac 00:11:22:33:0B:3E spoofchk off
ip link set enp75s0v7 address 00:11:22:33:0B:3E


ip link set enp75s0np0 vf 8 mac 00:11:22:33:0B:40 spoofchk off
ip link set enp75s0v8 address 00:11:22:33:0B:40


ip link set dev enp75s0np0 mtu 9600
ip link set dev enp75s0v0 mtu 9600
ip link set dev enp75s0v1 mtu 9600
ip link set dev enp75s0v2 mtu 9600
ip link set dev enp75s0v3 mtu 9600
ip link set dev enp75s0v4 mtu 9600
ip link set dev enp75s0v5 mtu 9600
ip link set dev enp75s0v6 mtu 9600
ip link set dev enp75s0v7 mtu 9600
ip link set dev enp75s0v8 mtu 9600

ethtool -K enp75s0v0 gro on tso on gso on
ethtool -K enp75s0v1 gro on tso on gso on
ethtool -K enp75s0v2 gro on tso on gso on
ethtool -K enp75s0v3 gro on tso on gso on
ethtool -K enp75s0v4 gro on tso on gso on
ethtool -K enp75s0v5 gro on tso on gso on
ethtool -K enp75s0v6 gro on tso on gso on
ethtool -K enp75s0v7 gro on tso on gso on
ethtool -K enp75s0v8 gro on tso on gso on
