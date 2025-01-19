#!/bin/bash

# ARGS: Middlebox port, DU MAC, DU VLAN RU1_DU port, RU1 vlan, RU1 MAC
./prb_mon_dpdk -l1 -a 0000:4b:00.1 -a 0000:4b:00.2 --file-prefix middlebox -- 0000:4b:00.1 aa:bb:cc:11:22:33 1 0000:4b:00.2 7 6C:AD:AD:00:0B:6E
