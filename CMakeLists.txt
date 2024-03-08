cmake_minimum_required(VERSION 3.13)

project(lruhash)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_VERBOSE_MAKEFILE ON)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE")

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

add_library(rustqc STATIC src/rustqc_dtable.c)
target_compile_options(rustqc PRIVATE -fPIE -pie -fPIC)
target_link_libraries(rustqc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/lib/quick_cache_rust/librust_c_integration.a dl)


add_executable(main src/main.c)
target_compile_options(main PRIVATE -fPIE -pie)
target_link_libraries(main clruhash rustqc)

# add_executable(concurrent_lru_cache app.cpp)
# target_link_libraries(concurrent_lru_cache lru_cache)
