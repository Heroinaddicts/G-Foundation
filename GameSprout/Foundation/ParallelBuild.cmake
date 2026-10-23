# Shared by standalone projects and their subdirectories.
include_guard(DIRECTORY)

cmake_host_system_information(RESULT FOUNDATION_BUILD_CORES QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT FOUNDATION_BUILD_CORES MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Cannot determine the host CPU core count.")
endif()

# Ninja starts jobs automatically; cap compilation at the host core count.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS foundation_compile=${FOUNDATION_BUILD_CORES})
set(CMAKE_JOB_POOL_COMPILE foundation_compile)
set(CMAKE_AUTOGEN_PARALLEL "${FOUNDATION_BUILD_CORES}")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/MP${FOUNDATION_BUILD_CORES}>")

# CMAKE_BUILD_PARALLEL_LEVEL is a build-time environment variable, not a
# configure-time CMake variable. Generate a portable entry for every build tree.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/BuildParallel.cmake.in"
        "${CMAKE_BINARY_DIR}/build-parallel.cmake" @ONLY)
    message(STATUS "Parallel build: ${FOUNDATION_BUILD_CORES} logical cores; cmake -P \"${CMAKE_BINARY_DIR}/build-parallel.cmake\"")
endif()
