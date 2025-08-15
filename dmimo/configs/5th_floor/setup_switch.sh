#!/bin/bash


ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off

echo 5 > /sys/class/net/eth0/device/sriov_numvfs

ip link set dev enp75s0v0 address 00:00:11:11:22:22
ip link set dev enp75s0v1 address 00:00:11:11:22:22
ip link set dev enp75s0v1 arp off

# DU address for RU1
ip link set dev enp75s0v2 address 00:11:22:33:44:66
# DU address for RU2
ip link set dev enp75s0v3 address 66:44:33:22:11:00

ip link set dev enp75s0np0 mtu 3300
ip link set dev enp75s0v0 mtu 3300
ip link set dev enp75s0v1 mtu 3300
ip link set dev enp75s0v2 mtu 3300
ip link set dev enp75s0v3 mtu 3300

sudo ip link add link enp75s0v0 name enp75s0v0.1 type vlan id 1
sudo ip link add link enp75s0v0 name enp75s0v0.3 type vlan id 3
sudo ip link add link enp75s0v1 name enp75s0v1.1 type vlan id 1
sudo ip link add link enp75s0v1 name enp75s0v1.3 type vlan id 3

sudo ip link add link enp75s0v2 name enp75s0v2.1 type vlan id 1
sudo ip link add link enp75s0v2 name enp75s0v2.3 type vlan id 3
sudo ip link add link enp75s0v3 name enp75s0v3.1 type vlan id 1
sudo ip link add link enp75s0v3 name enp75s0v3.3 type vlan id 3


ip link set enp75s0np0 vf 0 trust on
ip link set enp75s0np0 vf 1 trust on
ip link set enp75s0np0 vf 2 trust on
ip link set enp75s0np0 vf 3 trust on


ethtool -K enp75s0v0 rxvlan off txvlan off
ethtool -K enp75s0v1 rxvlan off txvlan off
ethtool -K enp75s0v2 rxvlan off txvlan off
ethtool -K enp75s0v3 rxvlan off txvlan off

ethtool -K enp75s0v0 gro on tso on gso on
ethtool -K enp75s0v1 gro on tso on gso on
ethtool -K enp75s0v2 gro on tso on gso on
ethtool -K enp75s0v3 gro on tso on gso on

ethtool -L enp75s0v0 combined 1
ethtool -L enp75s0v1 combined 1
ethtool -L enp75s0v2 combined 1
ethtool -L enp75s0v3 combined 1
ethtool -L enp75s0v4 combined 1

ethtool --set-priv-flags enp75s0v0 rx_striding_rq off
ethtool --set-priv-flags enp75s0v1 rx_striding_rq off
ethtool --set-priv-flags enp75s0v2 rx_striding_rq off
ethtool --set-priv-flags enp75s0v3 rx_striding_rq off

ip link set dev enp75s0v0 up
ip link set dev enp75s0v1 up
ip link set dev enp75s0v2 up
ip link set dev enp75s0v3 up
