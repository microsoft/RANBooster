#!/bin/bash

# ARGS: Middlebox port, RU1_DU port, RU1 vlan, RU1 MAC, RU2_DU port RU2 vlan, RU2 MAC, DU MAC, DU VLAN

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../../ofh_comp ../../das_dpdk/das_dpdk -l1 -a 0000:4b:00.1 -a 0000:4b:00.2 -a 0000:4b:00.3 --file-prefix middlebox -- 0000:4b:00.1 0000:4b:00.2 1 AA:11:BB:22:CC:33 0000:4b:00.3 19 1A:2B:3C:1A:2B:3C aa:bb:cc:11:22:33 1 
