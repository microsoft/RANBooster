# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.16)

project(RANBooster_dmimo_dpdk)

# Add the executable
add_executable(dmimo_dpdk dmimo_dpdk.c)

# Add include directories
target_include_directories(dmimo_dpdk PRIVATE
  ${RANBOOSTER_HEADERS}
  ${DPDK_INCLUDE_DIRS}
)

target_link_libraries(dmimo_dpdk PRIVATE fhi_lib)

# Optionally, set output directory
set_target_properties(dmimo_dpdk PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/dmimo_dpdk
)