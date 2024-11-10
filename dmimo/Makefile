# Compiler and flags for the eBPF program
CLANG = clang
BPF_CFLAGS = -O2 -g -Wall -target bpf

# Compiler and flags for the user-space program
CC = gcc
USER_CFLAGS = -Wall -O2

LDFLAGS = -L/usr/lib -lbpf -lelf

INCLUDES = -I../ -I/use/include

# Targets
BPF_OBJ = dmimo.o
USER_PROG = dmimo_load

# Source files
BPF_SRC = dmimo.c
USER_SRC = dmimo_load.c

# Phony target to build all
.PHONY: all clean

# Default target: build both eBPF and user-space programs
all: $(BPF_OBJ) $(USER_PROG)

# Compile the eBPF program
$(BPF_OBJ): $(BPF_SRC)
	$(CLANG) $(BPF_CFLAGS) $(INCLUDES) -c $(BPF_SRC) -o $(BPF_OBJ)

# Compile the user-space loader program
$(USER_PROG): $(USER_SRC)
	$(CC) $(USER_CFLAGS) $(LDFLAGS) -o $(USER_PROG) $(USER_SRC)

# Clean target to remove generated files
clean:
	rm -f $(BPF_OBJ) $(USER_PROG)

