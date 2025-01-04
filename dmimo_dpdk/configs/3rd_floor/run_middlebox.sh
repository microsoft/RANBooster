#!/bin/bash

# ARGS: Middlebox port, RU1_DU port, RU1 vlan, RU1 MAC, RU1 port bitmap, RU2_DU port RU2 vlan, RU2 MAC, RU2 port bitmap, DU MAC, DU VLAN

../../dmimo_dpdk -l1 -a 0000:4b:00.1 -a 0000:4b:00.2 -a 0000:4b:00.3 --file-prefix middlebox -- 0000:4b:00.1 0000:4b:00.2 19 6C:AD:AD:00:0B:3E 51 0000:4b:00.3 21 6C:AD:AD:00:0B:40 204 aa:bb:cc:11:22:33 1 
