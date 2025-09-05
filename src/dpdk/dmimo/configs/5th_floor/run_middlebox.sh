#!/bin/bash

# ARGS: Middlebox port, RU1_DU port, RU1 vlan, RU1 MAC, RU1 port bitmap, RU2_DU port RU2 vlan, RU2 MAC, RU2 port bitmap, DU MAC

./dmimo_dpdk -a 0000:86:09.1 -a 0000:86:09.2 -a 0000:86:09.3 --file-prefix middlebox -- 0000:86:09.1 0000:86:09.2 1 6C:AD:AD:00:08:86 51 0000:86:09.3 3 6C:AD:AD:00:08:8C 204 12:9C:FA:A6:A7:29 1 
