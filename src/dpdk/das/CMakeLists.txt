# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.16)

project(RANBooster_das_dpdk)

# Add the executable
add_executable(das_dpdk das_dpdk.c)

# Add include directories
target_include_directories(das_dpdk PRIVATE
  fhi_lib
  ${RANBOOSTER_HEADERS}
  ${DPDK_INCLUDE_DIRS}
)

# Link with DPDK and the xran_fhi library (imported in the main CMakeLists.txt)
target_link_libraries(das_dpdk PRIVATE fhi_lib)

# Optionally, set output directory
set_target_properties(das_dpdk PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/das_dpdk
)