#!/bin/bash

ip link set enp108s0f1np1 vf 0 mac 6a:d4:25:34:69:d0
ip link set dev enp63s0v1 address ae:40:e2:ca:ef:89
ip link set dev enp63s0v0 arp on

ip link set dev enp63s0v0 mtu 1500
ip link set dev enp63s0v1 mtu 1500
ip link set dev enp63s0np0 mtu 1500

ethtool --set-priv-flags enp63s0v0 rx_striding_rq on
ethtool --set-priv-flags enp63s0v1 rx_striding_rq on

ip link del link enp63s0v0 name enp63s0v0.1 
ip link del link enp63s0v0 name enp63s0v0.3 
ip link del link enp63s0v1 name enp63s0v1.1 
ip link del link enp63s0v1 name enp63s0v1.3

ethtool -K enp63s0v0 rxvlan on txvlan on
ethtool -K enp63s0v1 rxvlan on txvlan on

ethtool -L enp63s0v0 combined 11
ethtool -L enp63s0v1 combined 11

ip link set dev enp63s0v0 down
ip link set dev enp63s0v1 down
