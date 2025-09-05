cmake_minimum_required(VERSION 3.16)

project(RANBooster_prb_mon_dpdk)

# Add the executable
add_executable(prb_mon_dpdk prb_mon_dpdk.c)

# Add include directories
target_include_directories(prb_mon_dpdk PRIVATE
  ${RANBOOSTER_HEADERS}
  ${DPDK_INCLUDE_DIRS}
)

target_link_libraries(prb_mon_dpdk PRIVATE fhi_lib)

# Optionally, set output directory
set_target_properties(prb_mon_dpdk PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/prb_mon_dpdk
)