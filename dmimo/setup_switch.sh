#!/bin/bash

echo 5 > /sys/class/net/eth2/device/sriov_numvfs

ip link set dev enp63s0v1 address e2:53:8d:8f:a4:6b
ip link set dev enp63s0v1 arp off

# DU address for RU1
ip link set dev enp63s0v2 address 00:11:22:33:44:68
# DU address for RU2
ip link set dev enp63s0v3 address 00:11:22:33:0a:a0

# Configure VF for DU
ip link set enp63s0np0 vf 4 mac aa:bb:cc:11:22:33
ip link set dev enp63s0v4 address aa:bb:cc:11:22:33

ip link set dev enp63s0np0 mtu 9000
ip link set dev enp63s0v0 mtu 9000
ip link set dev enp63s0v1 mtu 9000
ip link set dev enp63s0v2 mtu 9000
ip link set dev enp63s0v3 mtu 9000

sudo ip link add link enp63s0v0 name enp63s0v0.1 type vlan id 1
sudo ip link add link enp63s0v0 name enp63s0v0.3 type vlan id 3
sudo ip link add link enp63s0v1 name enp63s0v1.1 type vlan id 1
sudo ip link add link enp63s0v1 name enp63s0v1.3 type vlan id 3

sudo ip link add link enp63s0v2 name enp63s0v2.1 type vlan id 1
sudo ip link add link enp63s0v2 name enp63s0v2.3 type vlan id 3
sudo ip link add link enp63s0v3 name enp63s0v3.1 type vlan id 1
sudo ip link add link enp63s0v3 name enp63s0v3.3 type vlan id 3


ip link set enp63s0np0 vf 0 trust on
ip link set enp63s0np0 vf 1 trust on
ip link set enp63s0np0 vf 2 trust on
ip link set enp63s0np0 vf 3 trust on


ethtool -K enp63s0v0 rxvlan off txvlan off
ethtool -K enp63s0v1 rxvlan off txvlan off
ethtool -K enp63s0v2 rxvlan off txvlan off
ethtool -K enp63s0v3 rxvlan off txvlan off

ethtool -K enp63s0v0 gro on tso on gso on
ethtool -K enp63s0v1 gro on tso on gso on
ethtool -K enp63s0v2 gro on tso on gso on
ethtool -K enp63s0v3 gro on tso on gso on

ethtool --set-priv-flags enp63s0v0 rx_striding_rq off
ethtool --set-priv-flags enp63s0v1 rx_striding_rq off
ethtool --set-priv-flags enp63s0v2 rx_striding_rq off
ethtool --set-priv-flags enp63s0v3 rx_striding_rq off

ip link set dev enp63s0v0 up
ip link set dev enp63s0v1 up
ip link set dev enp63s0v2 up
ip link set dev enp63s0v3 up
