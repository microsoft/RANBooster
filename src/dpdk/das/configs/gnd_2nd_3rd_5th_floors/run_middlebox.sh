#!/bin/bash

# ARGS: Middlebox port, DU MAC, DU VLAN, RU1_DU port, RU1 vlan, RU1 MAC, RU2_DU port RU2 vlan, RU2 MAC, RU3 vlan, RU3 MAC

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../../../ofh_comp ../../das_dpdk -l $1 -a 0000:4b:00.1 -a 0000:4b:00.2 -a 0000:4b:00.3 -a 0000:4b:00.4 -a 0000:4b:00.5 --file-prefix middlebox -- 0000:4b:00.1 aa:bb:cc:11:22:33 1 0000:4b:00.2 3 6C:AD:AD:00:08:8C 0000:4b:00.3 19 6C:AD:AD:00:0B:3E 0000:4b:00.4 10 6C:AD:AD:00:07:1E 0000:4b:00.5 12 6C:AD:AD:00:0B:42

