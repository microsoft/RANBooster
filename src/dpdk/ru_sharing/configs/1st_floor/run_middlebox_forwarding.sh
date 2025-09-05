#!/bin/bash

# ARGS: Middlebox port, RU1 MAC, RU1 vlan, DU1 MAC, DU1 VLAN, DU2 MAC, DU2 VLAN

../../forwarding -l1 -a 0000:4b:11.0 --file-prefix middlebox -- 0000:4b:11.0 6C:AD:AD:00:0B:6C 6 00:11:22:33:CC:DD 6 00:11:22:33:EE:FF 6
