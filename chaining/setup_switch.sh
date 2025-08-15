#!/bin/bash

echo 13 > /sys/class/net/eth0/device/sriov_numvfs

# VF12 - DU
echo "Setting VF12"
ip link set enp75s0np0 vf 12 mac aa:bb:cc:11:22:33
ip link set dev enp75s0v12 address aa:bb:cc:11:22:33

# VF0 and VF1 - DAS middlebox MAC addresses
echo "Setting VF0 and VF1"
ip link set dev enp75s0v1 address 86:4d:3b:02:0a:8c
ip link set dev enp75s0v1 arp off

# VF2 - DU address for DAS middlebox vRU1
echo "Setting VF2"
ip link set dev enp75s0v2 address AA:BB:CC:AA:BB:CC

# VF3 - DU address for DAS middlebox vRU2
echo "Setting VF3"
ip link set dev enp75s0v3 address CC:BB:AA:AA:BB:CC

# VF4 and VF5 - dMIMO middlebox MAC addresses for vRU1
echo "Setting VF4 and VF5"
ip link set dev enp75s0v4 address AA:11:BB:22:CC:33
ip link set dev enp75s0v5 address AA:11:BB:22:CC:33
ip link set dev enp75s0v5 arp off

# VF6 - DU address for RU1
echo "Setting VF6"
ip link set dev enp75s0v6 address 00:11:22:33:44:66

# VF7 - DU address for RU3
echo "Setting VF7"
ip link set dev enp75s0v7 address 66:44:33:22:11:00


# VF8 and VF9 - dMIMO middlebox MAC addresses for vRU2
echo "Setting VF8 and VF9"
ip link set dev enp75s0v8 address 1A:2B:3C:1A:2B:3C
ip link set dev enp75s0v9 address 1A:2B:3C:1A:2B:3C
ip link set dev enp75s0v9 arp off
 

# VF10 - DU address for RU19
echo "Setting VF10"
ip link set dev enp75s0v10 address 00:11:22:33:0B:3E


# VF 11 - DU address for RU21
echo "Setting VF11"
ip link set dev enp75s0v11 address 00:11:22:33:0B:40


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
ip link set dev enp75s0v10 mtu 3300
ip link set dev enp75s0v11 mtu 3300

sudo ip link add link enp75s0v0 name enp75s0v0.1 type vlan id 1
#sudo ip link add link enp75s0v0 name enp75s0v0.2 type vlan id 2
#sudo ip link add link enp75s0v0 name enp75s0v0.3 type vlan id 3
sudo ip link add link enp75s0v0 name enp75s0v0.19 type vlan id 19
#sudo ip link add link enp75s0v0 name enp75s0v0.21 type vlan id 21

sudo ip link add link enp75s0v1 name enp75s0v1.1 type vlan id 1
#sudo ip link add link enp75s0v1 name enp75s0v1.2 type vlan id 2
#sudo ip link add link enp75s0v1 name enp75s0v1.3 type vlan id 3
sudo ip link add link enp75s0v1 name enp75s0v1.19 type vlan id 19
#sudo ip link add link enp75s0v1 name enp75s0v1.21 type vlan id 21

sudo ip link add link enp75s0v2 name enp75s0v2.1 type vlan id 1
#sudo ip link add link enp75s0v2 name enp75s0v2.2 type vlan id 2
#sudo ip link add link enp75s0v2 name enp75s0v2.3 type vlan id 3
#sudo ip link add link enp75s0v2 name enp75s0v2.19 type vlan id 19
#sudo ip link add link enp75s0v2 name enp75s0v2.21 type vlan id 21

#sudo ip link add link enp75s0v3 name enp75s0v3.1 type vlan id 1
#sudo ip link add link enp75s0v3 name enp75s0v3.2 type vlan id 2
#sudo ip link add link enp75s0v3 name enp75s0v3.3 type vlan id 3
sudo ip link add link enp75s0v3 name enp75s0v3.19 type vlan id 19
#sudo ip link add link enp75s0v3 name enp75s0v3.21 type vlan id 21

sudo ip link add link enp75s0v4 name enp75s0v4.1 type vlan id 1
#sudo ip link add link enp75s0v4 name enp75s0v4.2 type vlan id 2
sudo ip link add link enp75s0v4 name enp75s0v4.3 type vlan id 3
#sudo ip link add link enp75s0v4 name enp75s0v4.19 type vlan id 19
#sudo ip link add link enp75s0v4 name enp75s0v4.21 type vlan id 21

sudo ip link add link enp75s0v5 name enp75s0v5.1 type vlan id 1
#sudo ip link add link enp75s0v5 name enp75s0v5.2 type vlan id 2
sudo ip link add link enp75s0v5 name enp75s0v5.3 type vlan id 3
#sudo ip link add link enp75s0v5 name enp75s0v5.19 type vlan id 19
#sudo ip link add link enp75s0v5 name enp75s0v5.21 type vlan id 21

sudo ip link add link enp75s0v6 name enp75s0v6.1 type vlan id 1
#sudo ip link add link enp75s0v6 name enp75s0v6.2 type vlan id 2
#sudo ip link add link enp75s0v6 name enp75s0v6.3 type vlan id 3
#sudo ip link add link enp75s0v6 name enp75s0v6.19 type vlan id 19
#sudo ip link add link enp75s0v6 name enp75s0v6.21 type vlan id 21

#sudo ip link add link enp75s0v7 name enp75s0v7.1 type vlan id 1
#sudo ip link add link enp75s0v7 name enp75s0v7.2 type vlan id 2
sudo ip link add link enp75s0v7 name enp75s0v7.3 type vlan id 3
#sudo ip link add link enp75s0v7 name enp75s0v7.19 type vlan id 19
#sudo ip link add link enp75s0v7 name enp75s0v7.21 type vlan id 21

#sudo ip link add link enp75s0v8 name enp75s0v8.1 type vlan id 1
#sudo ip link add link enp75s0v8 name enp75s0v8.2 type vlan id 2
#sudo ip link add link enp75s0v8 name enp75s0v8.3 type vlan id 3
sudo ip link add link enp75s0v8 name enp75s0v8.19 type vlan id 19
sudo ip link add link enp75s0v8 name enp75s0v8.21 type vlan id 21

#sudo ip link add link enp75s0v9 name enp75s0v9.1 type vlan id 1
#sudo ip link add link enp75s0v9 name enp75s0v9.2 type vlan id 2
#sudo ip link add link enp75s0v9 name enp75s0v9.3 type vlan id 3
sudo ip link add link enp75s0v9 name enp75s0v9.19 type vlan id 19
sudo ip link add link enp75s0v9 name enp75s0v9.21 type vlan id 21

#sudo ip link add link enp75s0v10 name enp75s0v10.1 type vlan id 1
#sudo ip link add link enp75s0v10 name enp75s0v10.2 type vlan id 2
#sudo ip link add link enp75s0v10 name enp75s0v10.3 type vlan id 3
sudo ip link add link enp75s0v10 name enp75s0v10.19 type vlan id 19
#sudo ip link add link enp75s0v10 name enp75s0v10.21 type vlan id 21

#sudo ip link add link enp75s0v11 name enp75s0v11.1 type vlan id 1
#sudo ip link add link enp75s0v11 name enp75s0v11.2 type vlan id 2
#sudo ip link add link enp75s0v11 name enp75s0v11.3 type vlan id 3
#sudo ip link add link enp75s0v11 name enp75s0v11.19 type vlan id 19
sudo ip link add link enp75s0v11 name enp75s0v11.21 type vlan id 21


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
ip link set enp75s0np0 vf 10 trust on
ip link set enp75s0np0 vf 11 trust on


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
ethtool -K enp75s0v10 rxvlan off txvlan off
ethtool -K enp75s0v11 rxvlan off txvlan off

#ethtool -K enp75s0v0 gro on tso on gso on
#ethtool -K enp75s0v1 gro on tso on gso on
#ethtool -K enp75s0v2 gro on tso on gso on
#ethtool -K enp75s0v3 gro on tso on gso on
#ethtool -K enp75s0v4 gro on tso on gso on
#ethtool -K enp75s0v5 gro on tso on gso on
#ethtool -K enp75s0v6 gro on tso on gso on
#ethtool -K enp75s0v7 gro on tso on gso on
#ethtool -K enp75s0v8 gro on tso on gso on
#ethtool -K enp75s0v9 gro on tso on gso on
#ethtool -K enp75s0v10 gro on tso on gso on
#ethtool -K enp75s0v11 gro on tso on gso on

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
ethtool -L enp75s0v10 combined 1
ethtool -L enp75s0v11 combined 1
ethtool -L enp75s0v12 combined 1

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
ethtool --set-priv-flags enp75s0v10 rx_striding_rq off
ethtool --set-priv-flags enp75s0v11 rx_striding_rq off

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
ip link set dev enp75s0v10 up
ip link set dev enp75s0v11 up
