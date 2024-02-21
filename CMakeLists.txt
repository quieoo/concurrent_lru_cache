cmake_minimum_required(VERSION 3.13)

project(lruhash)
# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
include_directories(include)


add_library(lru_cache INTERFACE)
target_include_directories(lru_cache INTERFACE include/)
set_target_properties(lru_cache PROPERTIES INTERFACE_COMPILE_FEATURES cxx_std_17)

# skip tests
# set(BUILD_LRU_CACHE_TESTS ON CACHE BOOL "Build lru_cache tests")
# if(BUILD_LRU_CACHE_TESTS)
#   add_subdirectory(tests)
# endif()

add_library(clruhash STATIC src/lruhash.cc)
target_link_libraries(clruhash lru_cache pthread)

add_executable(main src/main.c)
target_link_libraries(main clruhash)

# add_executable(concurrent_lru_cache app.cpp)
# target_link_libraries(concurrent_lru_cache lru_cache)
