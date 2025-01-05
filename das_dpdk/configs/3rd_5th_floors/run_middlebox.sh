#!/bin/bash

# ARGS: Middlebox port, RU1_DU port, RU1 vlan, RU1 MAC, RU2_DU port RU2 vlan, RU2 MAC, DU MAC, DU VLAN

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../../../ofh_comp ../../das_dpdk -l1 -a 0000:4b:00.1 -a 0000:4b:00.2 -a 0000:4b:00.3 --file-prefix middlebox -- 0000:4b:00.1 0000:4b:00.2 1 6C:AD:AD:00:08:86 0000:4b:00.3 19 6C:AD:AD:00:0B:3E aa:bb:cc:11:22:33 1 
