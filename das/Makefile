# Compiler and flags for the eBPF program
CLANG = clang
BPF_CFLAGS = -O2 -g -Wall -target bpf

# Compiler and flags for the user-space program
CC = icx
USER_CFLAGS = -Wall -g -O2

LDFLAGS = -L/usr/lib -lbpf -lelf 
LDFLAGS_RX_HANDLER = -L/usr/lib -L/usr/local/lib -L../ofh_comp -lofhcompress -lbpf -lelf -lxdp

INCLUDES = -I../ -I/use/include -I../ofh_comp

# Targets
BPF_EGRESS_OBJ = das_egress.o
BPF_INGRESS_OBJ = das_ingress.o
USER_PROG = das_load
RX_HANDLER_PROG = das_rx_handler

# Source files
BPF_EGRESS_SRC = das_egress.c
BPF_INGRESS_SRC = das_ingress.c
USER_SRC = das_load.c
RX_HANDLER_SRC = das_rx_handler.c

# Phony target to build all
.PHONY: all clean

# Default target: build both eBPF and user-space programs
all: $(BPF_EGRESS_OBJ) $(BPF_INGRESS_OBJ) $(USER_PROG) $(RX_HANDLER_PROG)

# Compile the eBPF program
$(BPF_EGRESS_OBJ): $(BPF_EGRESS_SRC)
	$(CLANG) $(BPF_CFLAGS) $(INCLUDES) -c $(BPF_EGRESS_SRC) -o $(BPF_EGRESS_OBJ)

$(BPF_INGRESS_OBJ): $(BPF_INGRESS_SRC)
	$(CLANG) $(BPF_CFLAGS) $(INCLUDES) -c $(BPF_INGRESS_SRC) -o $(BPF_INGRESS_OBJ)


# Compile the user-space loader program
$(USER_PROG): $(USER_SRC)
	$(CC) $(USER_CFLAGS) $(LDFLAGS) -o $(USER_PROG) $(USER_SRC) $(INCLUDES)

# Compile the rx_handler program
$(RX_HANDLER_PROG): $(RX_HANDLER_SRC)
	$(CC) $(USER_CFLAGS) -o $(RX_HANDLER_PROG) $(RX_HANDLER_SRC) $(INCLUDES) $(LDFLAGS_RX_HANDLER)

# Clean target to remove generated files
clean:
	rm -f $(BPF_EGRESS_OBJ) $(BPF_INGRESS_OBJ) $(USER_PROG) $(RX_HANDLER_PROG)

