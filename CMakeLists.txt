cmake_minimum_required(VERSION 3.10.0)
project(cobs VERSION 0.1.0 LANGUAGES C CXX)

add_library(cobs INTERFACE)
target_include_directories(cobs INTERFACE include)
target_compile_features(cobs INTERFACE cxx_std_20)
target_compile_options(cobs INTERFACE 
    "-Wall" 
    "-Wextra" 
    "-Wpedantic"
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_executable(exec_cobs main.cpp)
    target_link_libraries(exec_cobs PRIVATE cobs)
    target_compile_features(exec_cobs PRIVATE cxx_std_20)
    target_compile_options(exec_cobs PRIVATE
        "-Wall" 
        "-Wextra" 
        "-Wpedantic"
    )
endif()
