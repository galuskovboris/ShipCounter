cmake_minimum_required(VERSION 3.20)
project(ShipCounter)

set(CMAKE_C_STANDARD 99)

add_executable(ShipCounter
        main.c
        lodepng.c
)
