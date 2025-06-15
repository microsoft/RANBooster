CFLAGS += -O3 -g -march=native -Wall
CFLAGS += $(shell pkg-config --cflags libdpdk) -I ../
LDFLAGS += $(shell pkg-config --libs libdpdk-libs) -lpcap

all: nhost_dpdk nhost_dpdk_40_100 forwarding

nhost_dpdk: nhost_dpdk.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

nhost_dpdk_40_100: nhost_dpdk_40_100.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

forwarding: forwarding.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f nhost_dpdk nhost_dpdk.o nhost_dpdk_40_100 nhost_dpdk_40_100.o forwarding forwarding.o
