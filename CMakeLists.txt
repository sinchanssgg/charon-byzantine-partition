cmake_minimum_required(VERSION 3.16)
project(charon_sim CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)

# ── OMNeT++ paths ─────────────────────────────────────────────────────────────
# Adjust this path if your OMNeT++ is installed elsewhere
set(OMNETPP_ROOT "/opt/omnetpp-6.0.3" CACHE PATH "OMNeT++ root directory")

include_directories(${OMNETPP_ROOT}/include)
link_directories(${OMNETPP_ROOT}/lib)

# ── Collect all source files ──────────────────────────────────────────────────
file(GLOB_RECURSE CHARON_SRCS
    src/charon/*.cpp
    src/crypto/*.cpp
)

# ── Main simulation executable ────────────────────────────────────────────────
add_executable(charon_sim ${CHARON_SRCS})

target_include_directories(charon_sim PRIVATE
    src
    src/charon
    src/crypto
    src/topology
)

target_link_libraries(charon_sim
    OpenSSL::SSL
    OpenSSL::Crypto
    oppsim
    oppenvir
    oppcmdenv
)

# ── Ripple NED generator (standalone tool, no OMNeT++ needed) ─────────────────
add_executable(generate_ripple_ned
    src/topology/generate_ripple_ned.cpp
    src/topology/RippleLoader.cpp
    src/crypto/crypto.cpp
)

target_include_directories(generate_ripple_ned PRIVATE
    src
    src/crypto
    src/topology
)

target_link_libraries(generate_ripple_ned
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ── Compiler warnings ─────────────────────────────────────────────────────────
target_compile_options(charon_sim PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_compile_options(generate_ripple_ned PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# ── Build type defaults ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "OMNeT++ root  : ${OMNETPP_ROOT}")
message(STATUS "OpenSSL       : ${OPENSSL_VERSION}")
