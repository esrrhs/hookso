cmake_minimum_required(VERSION 2.8)
project(hookso)
aux_source_directory(. hookso_src)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g3 -std=c++11 -DNDEBUG")

ADD_EXECUTABLE(hookso ${hookso_src})
