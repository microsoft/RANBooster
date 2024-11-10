#/bin/bash

#./dmimo_load dmimo.o <iface_name> <pin_name> <ru_vlan> <ru_mac> <du_vlan> <du_mac> <ru port bitmap>

./dmimo_load dmimo.o eth13 /sys/fs/bpf/dmimo 1 6C:AD:AD:00:0A:9E 1 00:11:22:33:44:68 51
./dmimo_load dmimo.o eth18 /sys/fs/bpf/dmimo2 3 6C:AD:AD:00:0A:A0 1 00:11:22:33:44:68 204

