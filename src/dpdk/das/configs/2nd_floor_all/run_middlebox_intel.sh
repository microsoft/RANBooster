#!/bin/bash

# ARGS: Middlebox port, DU MAC, DU VLAN, RU1_DU port, RU1 vlan, RU1 MAC, RU2_DU port RU2 vlan, RU2 MAC, RU3 vlan, RU3 MAC

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../../../ofh_comp ../../das_dpdk -l $1 -a 0000:86:09.1 -a 0000:86:09.2 -a 0000:86:09.3 -a 0000:86:09.4 -a 0000:86:09.5 --file-prefix middlebox -- 0000:86:09.1 aa:bb:cc:11:22:33 1 0000:86:09.2 8 6C:AD:AD:00:0B:6A 0000:86:09.3 12 6C:AD:AD:00:0B:42 0000:86:09.4 15 6C:AD:AD:00:0B:68 0000:86:09.5 20 6C:AD:AD:00:0B:50

