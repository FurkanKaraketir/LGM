cmake_minimum_required(VERSION 3.16)
project(LinearGraphModeling VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(LinearGraphModeling
    src/main.cpp
    src/canvas.cpp
)

target_link_libraries(LinearGraphModeling PRIVATE Qt6::Widgets)
