# Compiler and flags for the eBPF program
CLANG = clang
BPF_CFLAGS = -O2 -g -Wall -target bpf

# Compiler and flags for the user-space program
CC = gcc
USER_CFLAGS = -Wall -O2

LDFLAGS = -L/usr/lib -lbpf -lelf

INCLUDES = -I../ -I/use/include

# Targets
BPF_EGRESS_OBJ = prb_mon_egress.o
BPF_INGRESS_OBJ = prb_mon_ingress.o

USER_PROG = prb_mon_load
USER_MONITOR = prb_mon

# Source files
BPF_EGRESS_SRC = prb_mon_egress.c
BPF_INGRESS_SRC = prb_mon_ingress.c
USER_SRC = prb_mon_load.c
USER_MONITOR_SRC = prb_mon.c

# Phony target to build all
.PHONY: all clean

# Default target: build both eBPF and user-space programs
all: $(BPF_EGRESS_OBJ) $(BPF_INGRESS_OBJ) $(USER_PROG) $(USER_MONITOR)

# Compile the eBPF programs
$(BPF_EGRESS_OBJ): $(BPF_EGRESS_SRC)
	$(CLANG) $(BPF_CFLAGS) $(INCLUDES) -c $(BPF_EGRESS_SRC) -o $(BPF_EGRESS_OBJ)

$(BPF_INGRESS_OBJ): $(BPF_INGRESS_SRC)
	$(CLANG) $(BPF_CFLAGS) $(INCLUDES) -c $(BPF_INGRESS_SRC) -o $(BPF_INGRESS_OBJ)

# Compile the user-space loader program
$(USER_PROG): $(USER_SRC)
	$(CC) $(USER_CFLAGS) $(LDFLAGS) -o $(USER_PROG) $(USER_SRC)

$(USER_MONITOR): $(USER_MONITOR_SRC)
	$(CC) $(USER_CFLAGS) $(LDFLAGS) -o $(USER_MONITOR) $(USER_MONITOR_SRC) $(INCLUDES)

# Clean target to remove generated files
clean:
	rm -f $(BPF_EGRESS_OBJ) $(BPF_INGRESS_OBJ) $(USER_PROG) $(USER_MONITOR)

