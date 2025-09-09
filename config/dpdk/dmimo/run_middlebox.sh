#!/bin/bash

# Non-DPDK ARGS: Middlebox PCI port, DU MAC, DU VLAN, RU1 port, RU1 vlan, RU1 MAC, RU1 antenna bitmap, RU2 port, RU2 vlan, RU2 MAC, RU2 antenna bitmap, RU3 port, RU3 vlan, RU3 MAC, RU3 antenna bitmap, RU4 port, RU4 vlan, RU4 MAC, RU4 antenna bitmap
# The antenna bitmap indicates the mappping of the DU logical antennas to the RU logical antennas.
# The mapping should be done in the following way:
# If bit N is 1 (from right to left), then DU port N should be mapped to the next unassigned RU port on the DL, and vice versa on the UL.
# For example, if the bitmap is 136 (binary 10001000), then DU port 3 is mapped to RU port 0, and DU port 7 is mapped to RU port 1 on the DL.
# Similarly, on the UL, RU port 0 is mapped to DU port 3, and RU port 1 is mapped to DU port 7.
$RANBOOSTER_PATH/build/dmimo_dpdk/dmimo_dpdk -l1 \
                -a 0000:4b:00.1 \
                -a 0000:4b:00.2 \
                -a 0000:4b:00.3 \
                -a 0000:4b:00.4 \
                -a 0000:4b:00.5 \
                --file-prefix middlebox \
                -- 0000:4b:00.1 aa:bb:cc:11:22:33 1 \
                0000:4b:00.2 8 6C:AD:AD:00:0B:6A 136 \
                0000:4b:00.3 12 6C:AD:AD:00:0B:42 68 \
                0000:4b:00.4 15 6C:AD:AD:00:0B:68 34 \
                0000:4b:00.5 20 6C:AD:AD:00:0B:50 17
