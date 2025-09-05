cmake_minimum_required(VERSION 3.16)
project(RANBooster LANGUAGES C CXX)

# Default build type
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(ENV{RANBOOSTER_PATH} ${PROJECT_SOURCE_DIR})

set(RANBOOSTER_SOURCE_CODE ${PROJECT_SOURCE_DIR}/src)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -march=native -Wall -Wextra -Wno-unused-parameter")
else()
  set(CMAKE_BUILD_TYPE Release)
  message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Release")
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=native -Wall -Wextra -Wno-unused-parameter")
endif()

# Allow user to set RTE_SDK on the cmake command line: -DRTE_SDK=/path/to/dpdk
set(RTE_SDK "" CACHE PATH "Path to DPDK SDK (RTE_SDK). Can also be provided via environment variable RTE_SDK.")

# If user didn't pass -DRTE_SDK, try to pick it up from the environment
if (DEFINED ENV{RTE_SDK})
  set(RTE_SDK $ENV{RTE_SDK})
  message(STATUS "RTE_SDK taken from environment: ${RTE_SDK}")
else()
  message(STATUS "RTE_SDK not set (you can pass -DRTE_SDK=/path or set environment RTE_SDK)")
endif()

# (existing pkg-config / DPDK discovery can remain here)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK QUIET dpdk)
if (NOT DPDK_FOUND)
  pkg_check_modules(DPDK QUIET libdpdk)
endif()
if (DPDK_FOUND)
  message(STATUS "DPDK found: include dirs=${DPDK_INCLUDE_DIRS}")
else()
  message(WARNING "DPDK not found via pkg-config; the fhi Makefile may locate it itself using RTE_SDK.")
endif()

# Legacy Makefile directory and artifact
set(FHI_DIR "${CMAKE_SOURCE_DIR}/3p/phy/fhi_lib/lib")
set(FHI_LIB "${FHI_DIR}/build/libxran.a")

# Find make
find_program(MAKE_EXECUTABLE NAMES make gmake)
if (NOT MAKE_EXECUTABLE)
  message(FATAL_ERROR "Could not find 'make' executable on PATH.")
endif()

# Define a target that runs make in a subdirectory
add_custom_target(fhi_lib_make ALL
    COMMAND ${CMAKE_MAKE_PROGRAM} -j -C ${FHI_DIR}
    WORKING_DIRECTORY ${FHI_DIR}
    COMMENT "Building fhi_lib"
)

add_custom_target(fhi_lib_clean
    COMMAND ${CMAKE_MAKE_PROGRAM} -C ${FHI_DIR} xclean
    WORKING_DIRECTORY ${FHI_DIR}
    COMMENT "Cleaning fhi_lib"
)

# Expose an imported/static target that other CMake targets can link against
add_library(fhi_lib STATIC IMPORTED GLOBAL)
set_target_properties(fhi_lib PROPERTIES IMPORTED_LOCATION ${FHI_LIB})
add_dependencies(fhi_lib fhi_lib_make)
target_include_directories(fhi_lib INTERFACE
  ${FHI_DIR}
  ${FHI_DIR}/api
  ${FHI_DIR}/src
)

set(RANBOOSTER_HEADERS ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/lib)

# Optionally expose DPDK include dirs if found
message(${DPDK_INCLUDE_DIRS})
if (DPDK_FOUND)
  target_include_directories(fhi_lib INTERFACE ${DPDK_INCLUDE_DIRS})
  target_link_libraries(fhi_lib INTERFACE ${DPDK_LIBRARIES})
endif()

message(STATUS "Configured legacy build target 'fhi_lib_make' (Makefile at ${FHI_DIR}).")

################ DAS with DPDK support ################
add_subdirectory(${RANBOOSTER_SOURCE_CODE}/dpdk/das)

################ DMIMO with DPDK support ################
add_subdirectory(${RANBOOSTER_SOURCE_CODE}/dpdk/dmimo)

################ PRB Monitor with DPDK support ################
add_subdirectory(${RANBOOSTER_SOURCE_CODE}/dpdk/prb_mon)

################ RU sharing with DPDK support ################
add_subdirectory(${RANBOOSTER_SOURCE_CODE}/dpdk/ru_sharing)