cmake_minimum_required(VERSION 3.22)

project(ePlace3D VERSION 1.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

add_subdirectory(PlaceCommon)

add_subdirectory(Optimization)

add_subdirectory(Parser)

add_subdirectory(PlaceDB)

add_subdirectory(Placer)

add_subdirectory(Plot)

add_subdirectory(CTS)

# add_subdirectory(Eigen)

add_subdirectory(FFT)

