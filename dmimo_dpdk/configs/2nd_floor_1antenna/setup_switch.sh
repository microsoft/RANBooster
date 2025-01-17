#!/bin/bash

ip link set eth2 vf 0 mac aa:bb:cc:11:22:33 spoofchk off
ip link set eth2 vf 1 mac 00:00:11:11:22:22 spoofchk off
ip link set eth2 vf 2 mac 00:11:22:33:0B:3E spoofchk off
ip link set eth2 vf 3 mac 00:11:22:33:0B:40 spoofchk off
