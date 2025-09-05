#!/bin/bash

ip link set eth2 vf 0 mac 00:00:00:00:00:00 spoofchk off

ip link set eth2 vf 1 mac 00:00:00:00:00:00 spoofchk off

# RU 8
ip link set eth2 vf 2 mac 00:00:00:00:00:00 spoofchk off

#RU 12
ip link set eth2 vf 3 mac 00:00:00:00:00:00 spoofchk off

# RU 15
ip link set eth2 vf 4 mac 00:00:00:00:00:00 spoofchk off

#RU 20
ip link set eth2 vf 5 mac 00:00:00:00:00:00 spoofchk off