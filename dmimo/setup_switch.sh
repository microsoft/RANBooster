#!/bin/bash

ip link set dev eth18 address e2:53:8d:8f:a4:6b
ip link set dev eth18 arp off

ip link set dev eth0 mtu 9000
ip link set dev eth13 mtu 9000
ip link set dev eth18 mtu 9000

sudo ip link add link eth13 name eth13.1 type vlan id 1
sudo ip link add link eth13 name eth13.3 type vlan id 3
sudo ip link add link eth18 name eth18.1 type vlan id 1
sudo ip link add link eth18 name eth18.3 type vlan id 3

ip link set eth0 vf 0 trust on
ip link set eth0 vf 1 trust on

ethtool -K eth13 rxvlan off txvlan off
ethtool -K eth18 rxvlan off txvlan off

ethtool -K eth13 gro on tso on gso on
ethtool -K eth18 gro on tso on gso on

ethtool --set-priv-flags eth13 rx_striding_rq off
ethtool --set-priv-flags eth18 rx_striding_rq off

ip link set dev eth13 up
ip link set dev eth18 up
