#!/bin/bash

# ARGS: <PCI_ADDR> <RU_MAC> <RU_VLAN> <RU_CofF> <DU_PRB_SIZE> <DU1_ADDR> <DU1_VLAN> <DU1_CofF> <DU1_PRB_OFFSET> <DU2_ADDR> <DU2_VLAN> <DU2_CofF> <DU2_PRB_OFFSET>

# original
# ../../nhost_dpdk_40_100 -l1 -a 0000:4b:11.0 --file-prefix middlebox -- 0000:4b:11.0 6C:AD:AD:00:0B:6C 6 3460.26 106 00:11:22:33:CC:DD 6 3460.08 83 00:11:22:33:EE:FF 6 3460.08 83

../../nhost_dpdk_40_100 -l1 -a 0000:4b:11.0 --file-prefix middlebox -- 0000:4b:11.0 6C:AD:AD:00:0B:6C 6 3460.26 106 00:11:22:33:CC:DD 6 3440.64 29 00:11:22:33:CC:DD 6 3435.6 15

# DU A
# ../../nhost_dpdk_40_100 -l1 -a 0000:4b:11.0 --file-prefix middlebox -- 0000:4b:11.0 6C:AD:AD:00:0B:6C 6 3460.26 106 00:11:22:33:CC:DD 6 3435.6 15 00:11:22:33:CC:DD 6 3435.6 15

# DU B
# ../../nhost_dpdk_40_100 -l1 -a 0000:4b:11.0 --file-prefix middlebox -- 0000:4b:11.0 6C:AD:AD:00:0B:6C 6 3460.26 106 00:11:22:33:EE:FF 6 3484.92 152 00:11:22:33:CC:DD 6 3435.6 15
