#!/bin/bash

# ARGS: Middlebox port, RU1 MAC, RU1 vlan, DU1 MAC, DU1 VLAN, DU2 MAC, DU2 VLAN

../../nhost_dpdk -l1 -a 0000:4b:00.1 --file-prefix middlebox -- 0000:4b:00.1 6C:AD:AD:00:0B:6C 6 3460.26 106 aa:bb:cc:11:22:33 6 3435.6 15 aa:bb:cc:33:22:11 6 3460.08 83
