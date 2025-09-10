#!/bin/bash

# Non-DPDK ARGS: Middlebox port, DU MAC, DU VLAN, RU port, RU vlan, RU MAC
$RANBOOSTER_PATH/build/prb_mon_dpdk/prb_mon_dpdk -l $1 \
                -a 0000:4b:00.1 \
                -a 0000:4b:00.2 \
                --file-prefix middlebox -- \
                0000:4b:00.1 aa:bb:cc:11:22:33 1 \
                0000:4b:00.2 7 6C:AD:AD:00:0B:6E
