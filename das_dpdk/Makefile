CFLAGS += -O3 -g -march=native -Wall
CFLAGS += $(shell pkg-config --cflags libdpdk) -I ../ -I../ofh_comp
LDFLAGS += $(shell pkg-config --libs libdpdk-libs) -L../ofh_comp -lofhcompress

all: das_dpdk

das_dpdk: das_dpdk.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f das_dpdk das_dpdk.o
