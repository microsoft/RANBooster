#!/bin/bash

ip link set dev eth18 address ae:40:e2:ca:ef:89
ip link set dev eth13 arp on

ip link set dev eth13 mtu 1500
ip link set dev eth18 mtu 1500
ip link set dev eth0 mtu 1500

ethtool --set-priv-flags eth13 rx_striding_rq on
ethtool --set-priv-flags eth18 rx_striding_rq on

ip link del link eth13 name eth13.1 
ip link del link eth13 name eth13.3 
ip link del link eth18 name eth18.1 
ip link del link eth18 name eth18.3

ethtool -K eth13 rxvlan on txvlan on
ethtool -K eth18 rxvlan on txvlan on

ip link set dev eth13 down
ip link set dev eth18 down
