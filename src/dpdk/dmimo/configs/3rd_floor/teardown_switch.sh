#!/bin/bash

ip link set eth2 vf 0 mac 00:00:00:00:00:00
ip link set eth2 vf 1 mac 00:00:00:00:00:00 spoofchk on
ip link set eth2 vf 2 mac 00:00:00:00:00:00 spoofchk on
ip link set eth2 vf 3 mac 00:00:00:00:00:00 spoofchk on
