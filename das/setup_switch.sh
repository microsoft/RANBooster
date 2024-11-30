#!/bin/bash

ip link set enp108s0f1np1 vf 0 mac 00:11:22:33:44:68
ip link set dev enp63s0v1 address e2:53:8d:8f:a4:6b
ip link set dev enp63s0v1 arp off

ip link set dev enp63s0np0 mtu 9000
ip link set dev enp63s0v0 mtu 9000
ip link set dev enp63s0v1 mtu 9000

sudo ip link add link enp63s0v0 name enp63s0v0.1 type vlan id 1
sudo ip link add link enp63s0v0 name enp63s0v0.3 type vlan id 3
sudo ip link add link enp63s0v1 name enp63s0v1.1 type vlan id 1
sudo ip link add link enp63s0v1 name enp63s0v1.3 type vlan id 3

ip link set enp63s0np0 vf 0 trust on
ip link set enp63s0np0 vf 1 trust on

ethtool -K enp63s0v0 rxvlan off txvlan off
ethtool -K enp63s0v1 rxvlan off txvlan off

ethtool -K enp63s0v0 gro on tso on gso on
ethtool -K enp63s0v1 gro on tso on gso on

ethtool --set-priv-flags enp63s0v0 rx_striding_rq off
ethtool --set-priv-flags enp63s0v1 rx_striding_rq off

ethtool -L enp63s0v0 combined 1
ethtool -L enp63s0v1 combined 1

ip link set dev enp63s0v0 up
ip link set dev enp63s0v1 up
