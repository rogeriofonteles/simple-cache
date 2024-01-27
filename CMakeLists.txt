cmake_minimum_required(VERSION 3.0.0)
project(order_cache VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "v")
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest REQUIRED)

include(CTest)
include_directories(${GTEST_INCLUDE_DIRS})
enable_testing()

add_library(cache_lib OrderCache.cpp)

add_executable(order_cache main.cpp)
target_link_libraries(order_cache cache_lib)

add_executable(order_tests OrderCacheTests.cpp)
target_link_libraries(order_tests cache_lib ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES} pthread)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
