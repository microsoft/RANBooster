#!/bin/bash

ip link set dev enp75s0np0 mtu 1500

echo 0 > /sys/class/net/eth0/device/sriov_numvfs
