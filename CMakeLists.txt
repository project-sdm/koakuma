cmake_minimum_required(VERSION 3.11)
project(koakuma LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED 23)
set(CMAKE_CXX_STANDARD_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_executable(koakuma src/main.cpp)

if(MSVC)
    target_compile_options(koakuma PRIVATE /W4 /WX)
else()
    target_compile_options(koakuma PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

