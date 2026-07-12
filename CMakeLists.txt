cmake_minimum_required(VERSION 3.14)
project(pmimalloc)

# ------------------------------------------------------------------------------
# Policies
# ------------------------------------------------------------------------------
function(set_policy POLICY_NAME POLICY_VALUE)
  if(POLICY ${POLICY_NAME})
    cmake_policy(SET ${POLICY_NAME} ${POLICY_VALUE})
  endif()
endfunction()

set_policy(CMP0028 NEW) # Double colon in target name means ALIAS or IMPORTED
                        # target
set_policy(CMP0060 NEW) # Link libraries by full path even in implicit
                        # directories
set_policy(CMP0074 NEW) # find_package uses XXX_ROOT vars using PackageName
set_policy(CMP0144 NEW) # find_package uses XXX_ROOT vars using PACKAGENAME
                        # Uppercase.

# ------------------------------------------------------------------------------
# Enable testing
# ------------------------------------------------------------------------------
include(CTest)
enable_testing()

# ------------------------------------------------------------------------------
# Set C++ standard to C++17
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)

# ------------------------------------------------------------------------------
# Set PIC
# ------------------------------------------------------------------------------
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------
include(GNUInstallDirs)

# ------------------------------------------------------------------------------
# Set the external heap size, device and logging option
# ------------------------------------------------------------------------------
# set( EXT_HEAP_SZ_EXP 25 CACHE STRING "Size of the extra mimalloc heap : 1 << X
# with 24 < X < 31" ) if (${EXT_HEAP_SZ_EXP} LESS 25 OR ${EXT_HEAP_SZ_EXP}
# GREATER 31) message(FATAL_ERROR "Invalid value for EXT_HEAP_SZ_EXP. Value must
# be within the range [25,31]") endif() option( PMIMALLOC_ENABLE_DEVICE OFF)
# option( PMIMALLOC_ENABLE_LOGGING OFF)

# ------------------------------------------------------------------------------
# Ideally we need to build mimalloc source for debugging
# ------------------------------------------------------------------------------
set(PMIMALLOC_WITH_MIMALLOC
    ON
    CACHE BOOL "compile pmimalloc with mimalloc library"
)

# ------------------------------------------------------------------------------
# Create config file
# ------------------------------------------------------------------------------
# configure_file( ${PROJECT_SOURCE_DIR}/cmake/config.hpp.in
# ${PROJECT_SOURCE_DIR}/include/config.hpp @ONLY )

# ------------------------------------------------------------------------------
# Cmake module path for find_package
# ------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# ------------------------------------------------------------------------------
# Setup Output directories
# ------------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/bin
    CACHE PATH "Single Directory for all Executables."
)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/bin
    CACHE PATH "Single Directory for all Libraries"
)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/bin
    CACHE PATH "Single Directory for all static libraries."
)

# ------------------------------------------------------------------------------
# Finding libraries
# ------------------------------------------------------------------------------
find_package(fmt REQUIRED)
message(STATUS "Found fmt: ${FMT_FOUND}")

find_package(NUMA REQUIRED)
message(STATUS "Found NUMA: ${NUMA_FOUND}")

find_package(CUDAToolkit REQUIRED)
message(STATUS "Found CUDA : ${CUDA_FOUND}")

find_package(HWLOC REQUIRED)
message(STATUS "Found HWLOC : ${HWLOC_FOUND}")

# ---------------------------------------------------------------------
# Build type
# ---------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Choose the type of build." FORCE
  )
  set_property(
    CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel"
                                    "RelWithDebInfo"
  )
endif()

# ---------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------
set(ENABLE_LOGGING
    OFF
    CACHE BOOL "print logging info to cerr"
)
if(ENABLE_LOGGING)
  set(PMI_LOG_SRC src/log.cpp)
endif()

# ---------------------------------------------------------------------
# mimalloc must be found or compiled in place
# ---------------------------------------------------------------------
include(external)
# set(MI_OVERRIDE OFF) set(MI_SKIP_COLLECT_ON_EXIT OFF)
get_external_project(
  PROJECT_NAME
  "mimalloc"
  GIT_REPO
  "https://github.com/microsoft/mimalloc"
  GIT_TAG
  "v2.2.7" # mimalloc's master branch no longer exists; pin the v2 API line

  PREFER_LOCAL # we want to debug mimalloc, so prefer building locally
)

if(NOT MI_SKIP_COLLECT_ON_EXIT)
  message(
    FATAL_ERROR "Please rerun cmake using 'cmake -DMI_SKIP_COLLECT_ON_EXIT=ON'"
  )
endif()

# ------------------------------------------------------------------------------
# Choice of backend provider
# ------------------------------------------------------------------------------
set(BACKEND
    "none"
    CACHE STRING "Choose the type of the backend."
)
set_property(CACHE BACKEND PROPERTY STRINGS "libfabric" "ucx" "mpi" "none")

string(TOUPPER ${BACKEND} BACKEND_U)

# set(BACKEND_ ${BACKEND_U} CACHE INTERNAL "" FORCE) set(PMIMALLOC_DEVICE
# "PMIMALLOC_DEVICE_${BACKEND_}" CACHE INTERNAL "" FORCE)
if(${BACKEND} STREQUAL "none")
  set(BACKEND_SOURCE src/numa.cpp ${PMI_LOG_SRC})
else()
  set(BACKEND_SOURCE src/${BACKEND}/backend.cpp src/numa.cpp ${PMI_LOG_SRC})
endif()
add_library(pmimalloc_${BACKEND} SHARED ${BACKEND_SOURCE})
if(ENABLE_LOGGING)
  target_compile_definitions(
    pmimalloc_${BACKEND} PUBLIC PMIMALLOC_WITH_MIMALLOC ENABLE_LOGGING
                                WITH_${BACKEND_U}
  )
else()
  target_compile_definitions(
    pmimalloc_${BACKEND} PUBLIC PMIMALLOC_WITH_MIMALLOC WITH_${BACKEND_U}
  )
endif()
target_include_directories(
  pmimalloc_${BACKEND} PUBLIC include src/${BACKEND} ext/mimalloc/include
)
target_link_libraries(
  pmimalloc_${BACKEND}
  PUBLIC fmt::fmt-header-only NUMA::libnuma CUDA::cudart
  PUBLIC mimalloc
)

install(
  TARGETS pmimalloc_${BACKEND}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/pmimalloc
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(${BACKEND} STREQUAL "libfabric")
  # ----------------------------
  # libfabric backend
  # ----------------------------
  find_package(Libfabric REQUIRED)
  message(STATUS "Found Libfabric : ${LIBFABRIC_FOUND}")
  find_package(Boost REQUIRED)
  message(STATUS "Found Boost : ${BOOST_FOUND}")

  target_link_libraries(
    pmimalloc_${BACKEND}
    PRIVATE Boost::boost
    PUBLIC libfabric::libfabric
  )

elseif(${BACKEND} STREQUAL "ucx")
  # ----------------------------
  # ucx backend
  # ----------------------------
  find_package(UCX REQUIRED)
  message(STATUS "Found UCX : ${UCX_FOUND}")

  target_link_libraries(pmimalloc_${BACKEND} PUBLIC UCX::ucx)

elseif(${BACKEND} STREQUAL "mpi")
  # ----------------------------
  # mpi backend
  # ----------------------------
  find_package(MPI REQUIRED)
  message(STATUS "Found MPI : ${MPI_FOUND}")

  # create backend library
  target_link_libraries(pmimalloc_${BACKEND} PUBLIC MPI::MPI_CXX)

elseif(${BACKEND} STREQUAL "none")
  message("No backend selected.")
endif()

add_library(pmimalloc ALIAS pmimalloc_${BACKEND})

# ---------------------------------------------------------------------
# NUMA setup
# ---------------------------------------------------------------------
# if(UNIX AND NOT APPLE) find_package(NUMA REQUIRED)
# target_link_libraries(pmimalloc PRIVATE NUMA::libnuma) set(NUMA_THROWS OFF
# CACHE BOOL "True if numa_tools may throw during initialization")
# if(NUMA_THROWS) target_compile_definitions(pmimalloc PRIVATE NUMA_THROWS)
# endif() set(NUMA_FOR_LOCAL OFF CACHE BOOL "Use numa_tools for local node
# allocations") if(NUMA_FOR_LOCAL) target_compile_definitions(pmimalloc PRIVATE
# NUMA_FOR_LOCAL) endif() else() message( WARNING "${CMAKE_PROJECT_NAME}
# configured without NUMA support on Mac") endif()

# target_link_libraries(pmimalloc PRIVATE libfabric::libfabric Boost::boost)
# target_compile_definitions(pmimalloc PRIVATE WITH_LIBFABRIC)
# target_sources(pmimalloc PRIVATE ${BACKEND_SOURCE})
# target_include_directories(pmimalloc PUBLIC src/${BACKEND})

# target_link_libraries(pmimalloc PRIVATE UCX::ucx)
# target_compile_definitions(pmimalloc PRIVATE WITH_UCX)
# target_sources(pmimalloc PRIVATE ${BACKEND_SOURCE})
# target_include_directories(pmimalloc PUBLIC src/${BACKEND})

# target_link_libraries(pmimalloc PRIVATE MPI::MPI_CXX)
# target_compile_definitions(pmimalloc PRIVATE WITH_MPI)
# target_sources(pmimalloc PRIVATE ${BACKEND_SOURCE})
# target_include_directories(pmimalloc PUBLIC src/${BACKEND})

# ------------------------------------------------------------------------------
# CTESTs
# ------------------------------------------------------------------------------
# create a `ctest` test that executes the pinned allocator note that ctest
# doesn't use a shell, so we can't redirect the output to a file using the usual
# '>' operator add_test(NAME pmimalloc COMMAND "$<TARGET_FILE:pmimalloc>")

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

add_subdirectory(examples)
