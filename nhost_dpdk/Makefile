CFLAGS += -O3 -g -march=native -Wall
CFLAGS += $(shell pkg-config --cflags libdpdk) -I ../
LDFLAGS += $(shell pkg-config --libs libdpdk-libs)

all: nhost_dpdk forwarding

nhost_dpdk: nhost_dpdk.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

forwarding: forwarding.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f nhost_dpdk nhost_dpdk.o nhost_dpdk_40_100 nhost_dpdk_40_100.o forwarding forwarding.o
