cmake_minimum_required(VERSION 3.6)
project(kmOS)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES helloworld.c)
add_executable(kmOS ${SOURCE_FILES})