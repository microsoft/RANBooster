#!/bin/bash

ip link set dev enp63s0np0 mtu 1500

echo 0 > /sys/class/net/eth2/device/sriov_numvfs
