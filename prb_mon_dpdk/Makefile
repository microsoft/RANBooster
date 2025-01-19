CFLAGS += -O3 -g -march=native -Wall
CFLAGS += $(shell pkg-config --cflags libdpdk) -I ../
LDFLAGS += $(shell pkg-config --libs libdpdk-libs)

all: prb_mon_dpdk

prb_mon_dpdk: prb_mon_dpdk.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f prb_mon_dpdk prb_mon_dpdk.o
