cmake_minimum_required(VERSION 3.16)

project(RANBooster_ru_sharing_dpdk)

# Add the executable
add_executable(ru_sharing_dpdk ru_sharing_dpdk.c)

# Add include directories
target_include_directories(ru_sharing_dpdk PRIVATE
  ${RANBOOSTER_HEADERS}
  ${DPDK_INCLUDE_DIRS}
)

target_link_libraries(ru_sharing_dpdk PRIVATE fhi_lib)

# Optionally, set output directory
set_target_properties(ru_sharing_dpdk PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ru_sharing_dpdk
)