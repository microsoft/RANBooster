#!/bin/bash

# ARGS: Middlebox port, RU1_DU port, RU1 vlan, RU1 MAC, RU1 port bitmap, RU2_DU port RU2 vlan, RU2 MAC, RU2 port bitmap, DU MAC, DU VLAN

../../dmimo_dpdk/dmimo_dpdk -l3 -a 0000:4b:00.7 -a 0000:4b:01.0 -a 0000:4b:01.1 --file-prefix vru2 -- 0000:4b:00.7 0000:4b:01.0 19 6C:AD:AD:00:0B:3E 51 0000:4b:01.1 21 6C:AD:AD:00:0B:40 204 CC:BB:AA:CC:BB:AA 19 
