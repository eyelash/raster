cmake_minimum_required(VERSION 3.8)
project(raster)

add_executable(raster main.cpp parser.cpp rasterizer.cpp)
target_compile_features(raster PUBLIC cxx_std_11)
find_package(PNG REQUIRED)
target_link_libraries(raster PNG::PNG)
