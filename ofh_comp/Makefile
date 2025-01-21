# Compiler and flags
CXX = icx
CXXFLAGS = -Wall -Wextra -march=native -std=c++17 -O3 -fPIE

# Library name
LIB_NAME = libofhcompress.a

# Source files and object files
SRC = $(wildcard *.cpp)
OBJ = $(SRC:.cpp=.o)

# Default target
all: $(LIB_NAME)

# Rule to build the static library
$(LIB_NAME): $(OBJ)
	@echo "Creating static library $(LIB_NAME)..."
	ar rcs $@ $^

# Rule to compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	@echo "Cleaning up..."
	rm -f $(OBJ) $(LIB_NAME)

# Phony targets
.PHONY: all clean
