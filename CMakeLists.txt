cmake_minimum_required(VERSION 2.8)
project(hookplt)
aux_source_directory(. hookplt_src)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g3 -std=c++11 -DNDEBUG -O0")

ADD_EXECUTABLE(hookplt ${hookplt_src})
