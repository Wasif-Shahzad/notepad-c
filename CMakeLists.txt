cmake_minimum_required(VERSION 4.0)
project(Notepad C)

set(CMAKE_C_STANDARD 17)
set(EXTRA_FLAGS -Wall -Wextra -pedantic)

add_compile_options(${EXTRA_FLAGS})
add_executable(Notepad main.c)
