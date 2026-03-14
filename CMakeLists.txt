cmake_minimum_required(VERSION 3.16)
project(charon_sim CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(OpenSSL REQUIRED)

# OMNeT++ paths — adjust if your install differs
set(OMNETPP_ROOT "/opt/omnetpp-6.0.3" CACHE PATH "OMNeT++ root")
include_directories(${OMNETPP_ROOT}/include)
link_directories(${OMNETPP_ROOT}/lib)

file(GLOB_RECURSE SRCS src/*.cpp)

add_executable(charon_sim ${SRCS})
target_include_directories(charon_sim PRIVATE src)
target_link_libraries(charon_sim
    OpenSSL::SSL OpenSSL::Crypto
    oppsim oppenvir oppcmdenv)
