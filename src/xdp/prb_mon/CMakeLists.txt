cmake_minimum_required(VERSION 3.16)

project(RANBooster_xdp_prb_mon)

# eBPF sources and objects
set(BPF_EGRESS_SRC prb_mon_egress.c)
set(BPF_INGRESS_SRC prb_mon_ingress.c)
set(BPF_EGRESS_OBJ prb_mon_egress.o)
set(BPF_INGRESS_OBJ prb_mon_ingress.o)

# User-space sources
set(USER_SRC prb_mon_load.c)
set(PRB_MON_SRC prb_mon.c)

# Output binaries
set(USER_PROG prb_mon_load)
set(PRB_MON_PROG prb_mon)

# eBPF include directories
set(BPF_INCLUDES
  ${RANBOOSTER_HEADERS}
  /usr/include
  ${CMAKE_SOURCE_DIR}/3p/phy/fhi_lib/lib/api
)

# Custom command to build eBPF objects with clang
find_program(CLANG_EXECUTABLE NAMES clang REQUIRED)

set(BPF_INCLUDE_FLAGS "")
foreach(inc ${BPF_INCLUDES})
  list(APPEND BPF_INCLUDE_FLAGS -I${inc})
endforeach()

set(BPF_OUTPUT_DIR ${CMAKE_BINARY_DIR}/prb_mon_xdp)

set(BPF_CFLAGS -O2 -g -Wall -target bpf -D__x86_64__ -D__BPF_TARGET__)

add_custom_command(
  OUTPUT ${BPF_OUTPUT_DIR}/${BPF_EGRESS_OBJ}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BPF_OUTPUT_DIR}
  COMMAND ${CLANG_EXECUTABLE} ${BPF_CFLAGS} ${BPF_INCLUDE_FLAGS} -c ${BPF_EGRESS_SRC} -o ${BPF_OUTPUT_DIR}/${BPF_EGRESS_OBJ}
  DEPENDS ${BPF_EGRESS_SRC}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Building eBPF object ${BPF_EGRESS_OBJ} with clang"
)

add_custom_command(
  OUTPUT ${BPF_OUTPUT_DIR}/${BPF_INGRESS_OBJ}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BPF_OUTPUT_DIR}
  COMMAND ${CLANG_EXECUTABLE} ${BPF_CFLAGS} ${BPF_INCLUDE_FLAGS} -c ${BPF_INGRESS_SRC} -o ${BPF_OUTPUT_DIR}/${BPF_INGRESS_OBJ}
  DEPENDS ${BPF_INGRESS_SRC}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Building eBPF object ${BPF_INGRESS_OBJ} with clang"
)

add_custom_target(prb_mon_ebpf_objs ALL DEPENDS
  ${BPF_OUTPUT_DIR}/${BPF_EGRESS_OBJ}
  ${BPF_OUTPUT_DIR}/${BPF_INGRESS_OBJ}
)

# User-space programs (built with gcc, default CMake compiler)
add_executable(${USER_PROG} ${USER_SRC})
add_executable(${PRB_MON_PROG} ${PRB_MON_SRC})

target_include_directories(${USER_PROG} PRIVATE ${RANBOOSTER_HEADERS})
target_include_directories(${PRB_MON_PROG} PRIVATE ${RANBOOSTER_HEADERS})

# Link libraries for user-space programs
target_link_libraries(${USER_PROG} PRIVATE fhi_lib bpf elf)
target_link_libraries(${PRB_MON_PROG} PRIVATE fhi_lib bpf elf xdp)

# Set output directory
set_target_properties(${USER_PROG} ${PRB_MON_PROG} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/prb_mon_xdp
)

# Ensure user-space programs depend on eBPF objects
add_dependencies(${USER_PROG} prb_mon_ebpf_objs)
add_dependencies(${PRB_MON_PROG} prb_mon_ebpf_objs)