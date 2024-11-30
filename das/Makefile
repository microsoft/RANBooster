# Compiler and flags for the eBPF program
CLANG = clang
BPF_CFLAGS = -O2 -g -Wall -target bpf

# Compiler and flags for the user-space program
CC = gcc
USER_CFLAGS = -Wall -O2

LDFLAGS = -L/usr/lib -lbpf -lelf
LDFLAGS_RX_HANDLER = -L/usr/lib -L/usr/local/lib -lbpf -lelf -lxdp

INCLUDES = -I../ -I/use/include

# Targets
BPF_OBJ = das.o
USER_PROG = das_load
RX_HANDLER_PROG = das_rx_handler

# Source files
BPF_SRC = das.c
USER_SRC = das_load.c
RX_HANDLER_SRC = das_rx_handler.c

# Phony target to build all
.PHONY: all clean

# Default target: build both eBPF and user-space programs
all: $(BPF_OBJ) $(USER_PROG) $(RX_HANDLER_PROG)

# Compile the eBPF program
$(BPF_OBJ): $(BPF_SRC)
	$(CLANG) $(BPF_CFLAGS) $(INCLUDES) -c $(BPF_SRC) -o $(BPF_OBJ)

# Compile the user-space loader program
$(USER_PROG): $(USER_SRC)
	$(CC) $(USER_CFLAGS) $(LDFLAGS) -o $(USER_PROG) $(USER_SRC) $(INCLUDES)

# Compile the rx_handler program
$(RX_HANDLER_PROG): $(RX_HANDLER_SRC)
	$(CC) $(USER_CFLAGS) $(LDFLAGS_RX_HANDLER) -o $(RX_HANDLER_PROG) $(RX_HANDLER_SRC) $(INCLUDES)

# Clean target to remove generated files
clean:
	rm -f $(BPF_OBJ) $(USER_PROG) $(RX_HANDLER_PROG)

