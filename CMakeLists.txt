cmake_minimum_required(VERSION 3.22)
project(R2000_Visualizer)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)
include_directories(R2000/include)
# Threads       ############################################################
find_package(Threads REQUIRED)
# Boost Package ############################################################
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.64.0 COMPONENTS filesystem program_options REQUIRED)
include_directories(${Boost_INCLUDE_DIR})
link_directories(${Boost_LIBRARY_DIR})
############################################################################

# PCL Package ##############################################################
find_package(PCL 1.8 REQUIRED)
include_directories(${PCL_INCLUDE_DIRS})
link_directories(${PCL_LIBRARY_DIRS})
add_definitions(${PCL_DEFINITIONS})
############################################################################

add_executable(TcpAcquisition include
        R2000/src/DataLink/DataLinkBuilder.cpp
        R2000/src/DataLink/DataLink.cpp
        R2000/src/DataLink/TCPLink.cpp
        R2000/src/DataLink/UDPLink.cpp
        R2000/src/R2000.cpp
        Applications/TcpAcquisition.cpp)
target_link_libraries(TcpAcquisition
        PRIVATE ${CMAKE_DL_LIBS}
        ${Boost_LIBRARIES}
        ${Boost_PROGRAM_OPTIONS_LIBRARIES}
        ${PCL_LIBRARIES}
        Threads::Threads)

add_executable(UdpAcquisition include
        R2000/src/DataLink/DataLinkBuilder.cpp
        R2000/src/DataLink/DataLink.cpp
        R2000/src/DataLink/TCPLink.cpp
        R2000/src/DataLink/UDPLink.cpp
        R2000/src/R2000.cpp
        Applications/UdpAcquisition.cpp)
target_link_libraries(UdpAcquisition
        PRIVATE ${CMAKE_DL_LIBS}
        ${Boost_LIBRARIES}
        ${Boost_PROGRAM_OPTIONS_LIBRARIES}
        ${PCL_LIBRARIES} Threads::Threads)