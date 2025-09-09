#!/bin/bash

# Non-DPDK ARGS: Middlebox PCI port, DU MAC, DU VLAN, RU1 port, RU1 vlan, RU1 MAC, RU2 port, RU2 vlan, RU2 MAC, RU3 port, RU3 vlan, RU3 MAC, RU4 port, RU4 vlan, RU4 MAC
$RANBOOSTER_PATH/build/das_dpdk/das_dpdk -l $1 \
    -a 0000:4b:00.1 \
    -a 0000:4b:00.2 \
    -a 0000:4b:00.3 \
    -a 0000:4b:00.4 \
    -a 0000:4b:00.5 \
    --file-prefix middlebox -- \
    0000:4b:00.1 aa:bb:cc:11:22:33 7 \
    0000:4b:00.2 7 6C:AD:AD:00:0B:6E \
    0000:4b:00.3 18 6C:AD:AD:00:0B:56 \
    0000:4b:00.4 19 6C:AD:AD:00:0B:3E \
    0000:4b:00.5 21 6C:AD:AD:00:0B:40
