#!/bin/bash

# ARGS: Middlebox port, RU1_DU port, RU1 vlan, RU1 MAC, RU1 port bitmap, RU2_DU port RU2 vlan, RU2 MAC, RU2 port bitmap, DU MAC, DU VLAN

../../dmimo_dpdk/dmimo_dpdk -l2 -a 0000:4b:00.4 -a 0000:4b:00.5 -a 0000:4b:00.6 --file-prefix vru1 -- 0000:4b:00.4 0000:4b:00.5 1 6C:AD:AD:00:08:86 51 0000:4b:00.6 3 6C:AD:AD:00:08:8C 204 AA:BB:CC:AA:BB:CC 1 
