cmake_minimum_required(VERSION 3.22)
project(lb_source_propagation CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror)

add_library(lb_source_propagation STATIC
  src/source_propagation.cpp
)

target_include_directories(lb_source_propagation
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(source_prop_smoke apps/source_prop_smoke.cpp)
target_link_libraries(source_prop_smoke PRIVATE lb_source_propagation)

enable_testing()

add_executable(source_prop_tests tests/test_source_propagation.cpp)
target_link_libraries(source_prop_tests PRIVATE lb_source_propagation)

add_test(NAME source_prop_tests COMMAND source_prop_tests)
add_test(NAME source_prop_smoke COMMAND source_prop_smoke)
