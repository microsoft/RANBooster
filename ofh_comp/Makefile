CXX = icx
CXXFLAGS = -Wall -Wextra -march=native -fPIC -std=c++17

# Target name for the shared library
TARGET = libofhcompress.so

# Source files and object files
SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:.cpp=.o)

# Build the shared library
$(TARGET): $(OBJS)
	$(CXX) -shared -o $@ $(OBJS)

# Rule to build object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
	# Clean up build artifacts

.PHONY: clean
	
clean:
	rm -f $(OBJS) $(TARGET)
