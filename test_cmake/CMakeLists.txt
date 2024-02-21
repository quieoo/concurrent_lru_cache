cmake_minimum_required(VERSION 3.5)
project(MyProject)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)

# Include directories
include_directories(include)

# Create a static library for lib1
# add_library(lib1 STATIC src/lib1.cpp)

# Create a static library for lib2
add_library(lib2 STATIC src/lib2.cpp)
# target_link_libraries(lib2 lib1)

# Create a shared library for app.cc
add_library(app SHARED src/app.cc)
target_link_libraries(app lib2)

# Create an executable for main.c
add_executable(main src/main.c)
target_link_libraries(main app)