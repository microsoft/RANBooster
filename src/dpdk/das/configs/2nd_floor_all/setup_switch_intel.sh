#!/bin/bash

ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off

ip link set eth2 vf 1 mac 00:00:11:11:22:22 spoofchk off

# RU 8
ip link set eth2 vf 2 mac 00:11:22:33:0B:6A spoofchk off

#RU 12
ip link set eth2 vf 3 mac 00:11:22:33:0B:42 spoofchk off

# RU 15
ip link set eth2 vf 4 mac 00:11:22:33:0B:68 spoofchk off

#RU 20
ip link set eth2 vf 5 mac 00:11:22:33:0B:50 spoofchk off


# ip link set dev enp75s0np0 mtu 9600
# ip link set dev enp75s0v0 mtu 9600
# ip link set dev enp75s0v1 mtu 9600
# ip link set dev enp75s0v2 mtu 9600
# ip link set dev enp75s0v3 mtu 9600
# ip link set dev enp75s0v4 mtu 9600

# ethtool -K enp75s0v0 gro on tso on gso on
# ethtool -K enp75s0v1 gro on tso on gso on
# ethtool -K enp75s0v2 gro on tso on gso on
# ethtool -K enp75s0v3 gro on tso on gso on
# ethtool -K enp75s0v4 gro on tso on gso on

# ethtool --set-priv-flags enp75s0v0 rx_striding_rq on
# ethtool --set-priv-flags enp75s0v1 rx_striding_rq on
# ethtool --set-priv-flags enp75s0v2 rx_striding_rq on
# ethtool --set-priv-flags enp75s0v3 rx_striding_rq on

