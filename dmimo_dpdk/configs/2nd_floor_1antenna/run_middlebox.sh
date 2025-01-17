#!/bin/bash

# ARGS: Middlebox port, DU MAC, DU VLAN RU1_DU port, RU1 vlan, RU1 MAC, RU1 port bitmap, RU2_DU port RU2 vlan, RU2 MAC, RU2 port bitmap

../../dmimo_dpdk -l1 -a 0000:4b:00.1 -a 0000:4b:00.2 -a 0000:4b:00.3 -a 0000:4b:00.4 -a 0000:4b:00.5 --file-prefix middlebox -- 0000:4b:00.1 aa:bb:cc:11:22:33 1 0000:4b:00.2 8 6C:AD:AD:00:0B:6A 136 0000:4b:00.3 12 6C:AD:AD:00:0B:42 68 0000:4b:00.4 15 6C:AD:AD:00:0B:68 34 0000:4b:00.5 20 6C:AD:AD:00:0B:50 17
