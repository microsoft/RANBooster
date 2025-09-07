#!/bin/bash

echo 10 > /sys/class/net/eth0/device/sriov_numvfs

ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off

ip link set dev enp75s0v0 address 00:00:11:11:22:22
ip link set dev enp75s0v1 address 00:00:11:11:22:22
ip link set dev enp75s0v1 arp off
ip link set dev enp75s0v2 address 00:00:11:11:22:22
ip link set dev enp75s0v2 arp off
ip link set dev enp75s0v3 address 00:00:11:11:22:22
ip link set dev enp75s0v3
ip link set dev enp75s0v4 address 00:00:11:11:22:22
ip link set dev enp75s0v4

# DU address for RU1
ip link set dev enp75s0v5 address 00:11:22:33:44:66
# DU address for RU19
ip link set dev enp75s0v6 address 00:11:22:33:0B:3E
# DU address for RU7
ip link set dev enp75s0v7 address 00:11:22:33:0B:6E
# DU address for RU21
ip link set dev enp75s0v8 address 00:11:22:33:0B:40
# DU address for RU3
ip link set dev enp75s0v9 address 66:44:33:22:11:00


ip link set dev enp75s0np0 mtu 3300
ip link set dev enp75s0v0 mtu 3300
ip link set dev enp75s0v1 mtu 3300
ip link set dev enp75s0v2 mtu 3300
ip link set dev enp75s0v3 mtu 3300
ip link set dev enp75s0v4 mtu 3300
ip link set dev enp75s0v5 mtu 3300
ip link set dev enp75s0v6 mtu 3300
ip link set dev enp75s0v7 mtu 3300
ip link set dev enp75s0v8 mtu 3300
ip link set dev enp75s0v9 mtu 3300

# DU VLAN
ip link add link enp75s0v0 name enp75s0v0.1 type vlan id 1
ip link add link enp75s0v1 name enp75s0v1.1 type vlan id 1
ip link add link enp75s0v2 name enp75s0v2.1 type vlan id 1
ip link add link enp75s0v3 name enp75s0v3.1 type vlan id 1
ip link add link enp75s0v4 name enp75s0v4.1 type vlan id 1

# RU VLANs
ip link add link enp75s0v5 name enp75s0v5.1 type vlan id 1
ip link add link enp75s0v6 name enp75s0v6.19 type vlan id 19
ip link add link enp75s0v7 name enp75s0v7.7 type vlan id 7
ip link add link enp75s0v8 name enp75s0v8.21 type vlan id 21
ip link add link enp75s0v9 name enp75s0v9.3 type vlan id 3

ip link set enp75s0np0 vf 0 trust on
ip link set enp75s0np0 vf 1 trust on
ip link set enp75s0np0 vf 2 trust on
ip link set enp75s0np0 vf 3 trust on
ip link set enp75s0np0 vf 4 trust on
ip link set enp75s0np0 vf 5 trust on
ip link set enp75s0np0 vf 6 trust on
ip link set enp75s0np0 vf 7 trust on
ip link set enp75s0np0 vf 8 trust on
ip link set enp75s0np0 vf 9 trust on

ethtool -K enp75s0v0 rxvlan off txvlan off
ethtool -K enp75s0v1 rxvlan off txvlan off
ethtool -K enp75s0v2 rxvlan off txvlan off
ethtool -K enp75s0v3 rxvlan off txvlan off
ethtool -K enp75s0v4 rxvlan off txvlan off
ethtool -K enp75s0v5 rxvlan off txvlan off
ethtool -K enp75s0v6 rxvlan off txvlan off
ethtool -K enp75s0v7 rxvlan off txvlan off
ethtool -K enp75s0v8 rxvlan off txvlan off
ethtool -K enp75s0v9 rxvlan off txvlan off

ethtool -K enp75s0v0 gro on tso on gso on
ethtool -K enp75s0v1 gro on tso on gso on
ethtool -K enp75s0v2 gro on tso on gso on
ethtool -K enp75s0v3 gro on tso on gso on
ethtool -K enp75s0v4 gro on tso on gso on
ethtool -K enp75s0v5 gro on tso on gso on
ethtool -K enp75s0v6 gro on tso on gso on
ethtool -K enp75s0v7 gro on tso on gso on
ethtool -K enp75s0v8 gro on tso on gso on
ethtool -K enp75s0v9 gro on tso on gso on

ethtool -L enp75s0v0 combined 1
ethtool -L enp75s0v1 combined 1
ethtool -L enp75s0v2 combined 1
ethtool -L enp75s0v3 combined 1
ethtool -L enp75s0v4 combined 1
ethtool -L enp75s0v5 combined 1
ethtool -L enp75s0v6 combined 1
ethtool -L enp75s0v7 combined 1
ethtool -L enp75s0v8 combined 1
ethtool -L enp75s0v9 combined 1


ethtool --set-priv-flags enp75s0v0 rx_striding_rq off
ethtool --set-priv-flags enp75s0v1 rx_striding_rq off
ethtool --set-priv-flags enp75s0v2 rx_striding_rq off
ethtool --set-priv-flags enp75s0v3 rx_striding_rq off
ethtool --set-priv-flags enp75s0v4 rx_striding_rq off
ethtool --set-priv-flags enp75s0v5 rx_striding_rq off
ethtool --set-priv-flags enp75s0v6 rx_striding_rq off
ethtool --set-priv-flags enp75s0v7 rx_striding_rq off
ethtool --set-priv-flags enp75s0v8 rx_striding_rq off
ethtool --set-priv-flags enp75s0v9 rx_striding_rq off

ip link set dev enp75s0v0 up
ip link set dev enp75s0v1 up
ip link set dev enp75s0v2 up
ip link set dev enp75s0v3 up
ip link set dev enp75s0v4 up
ip link set dev enp75s0v5 up
ip link set dev enp75s0v6 up
ip link set dev enp75s0v7 up
ip link set dev enp75s0v8 up
ip link set dev enp75s0v9 up
