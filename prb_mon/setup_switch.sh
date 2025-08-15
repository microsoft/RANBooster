#!/bin/bash
echo 2 > /sys/class/net/eth0/device/sriov_numvfs

ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off

ip link set enp75s0np0 vf 0 mac 00:00:11:11:22:22 spoofchk off
ip link set enp75s0v0 address 00:00:11:11:22:22

# DU address for RU7
ip link set dev enp75s0v1 address 00:11:22:33:0B:6E


ip link set dev enp75s0np0 mtu 3300
ip link set dev enp75s0v0 mtu 3300
ip link set dev enp75s0v1 mtu 3300

ip link add link enp75s0v0 name enp75s0v0.1 type vlan id 1
ip link add link enp75s0v0 name enp75s0v0.7 type vlan id 7

ip link add link enp75s0v1 name enp75s0v1.1 type vlan id 1
ip link add link enp75s0v1 name enp75s0v1.7 type vlan id 7


ip link set enp75s0np0 vf 0 trust on
ip link set enp75s0np0 vf 1 trust on


ethtool -K enp75s0v0 rxvlan off txvlan off
ethtool -K enp75s0v1 rxvlan off txvlan off

ethtool -K enp75s0v0 gro on tso on gso on
ethtool -K enp75s0v1 gro on tso on gso on

ethtool --set-priv-flags enp75s0v0 rx_striding_rq off
ethtool --set-priv-flags enp75s0v1 rx_striding_rq off

ip link set dev enp75s0v0 up
ip link set dev enp75s0v1 up
