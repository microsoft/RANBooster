CC = gcc

CFLAGS += -O3 -g -march=native -Wall 
CFLAGS += $(shell pkg-config --cflags libdpdk) -I ../ -I../3p/phy/fhi_lib/lib/api
LDFLAGS += $(shell pkg-config --static --libs libdpdk) -L../3p/phy/fhi_lib/lib/build -lxran 

all: das_dpdk

das_dpdk: das_dpdk.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f das_dpdk das_dpdk.o
