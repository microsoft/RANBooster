#!/bin/bash

# Non-DPDK ARGS: Middlebox port, RU MAC, RU vlan, RU central frequency, DU1 MAC, DU1 VLAN, DU central frequency, DU2 MAC, DU2 VLAN, DU central frequency
$RANBOOSTER_PATH/build/ru_sharing_dpdk/ru_sharing_dpdk -l $1 \
                -a 0000:4b:00.1 \
                --file-prefix middlebox -- \
                0000:4b:00.1 6C:AD:AD:00:0B:6C 6 3460.26 106 \
                aa:bb:cc:11:22:33 6 3435.6 15 \
                aa:bb:cc:33:22:11 6 3484.20 150
