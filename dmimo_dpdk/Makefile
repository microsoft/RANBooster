CFLAGS += -O3 -g -march=native -Wall
CFLAGS += $(shell pkg-config --cflags libdpdk) -I ../
LDFLAGS += $(shell pkg-config --libs libdpdk-libs)

all: dmimo_dpdk

dmimo_dpdk: dmimo_dpdk.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f dmimo_dpdk dmimo_dpdk.o
