cmake_minimum_required(VERSION 3.10)
project(assignment3)

set(CMAKE_CXX_STANDARD 11)

add_executable(a3sdn a3sdn.cpp controller.cpp controller.h switch.cpp switch.h util.cpp util.h)