cmake_minimum_required(VERSION 3.8)
project(raster)

add_executable(raster main.cpp parser.cpp rasterizer.cpp png.cpp)
target_compile_features(raster PUBLIC cxx_std_11)
