cmake_minimum_required(VERSION 3.22)
project(lb_source_propagation CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED K_SQ)
  set(K_SQ 36 CACHE STRING "Squared step bound for campaign TileOp smoke")
endif()

add_library(lb_source_propagation STATIC
  src/source_propagation.cpp
)
target_compile_options(lb_source_propagation PRIVATE -Wall -Wextra -Werror)

target_include_directories(lb_source_propagation
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(lb_source_campaign_tileop STATIC
  ../cpp-campaign-v2/src/campaign_constants.cpp
  ../cpp-campaign-v2/src/geo_tests.cpp
  ../cpp-campaign-v2/src/grid.cpp
  ../cpp-campaign-v2/src/primality.cpp
  ../cpp-campaign-v2/src/process_tile.cpp
  ../cpp-campaign-v2/src/sha256.cpp
  ../cpp-campaign-v2/src/sieve.cpp
  ../cpp-campaign-v2/src/tileop.cpp
  ../cpp-campaign-v2/src/union_find.cpp
)
target_compile_options(lb_source_campaign_tileop PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(lb_source_campaign_tileop PUBLIC K_SQ=${K_SQ})
target_include_directories(lb_source_campaign_tileop
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/src
)

add_executable(source_prop_smoke apps/source_prop_smoke.cpp)
target_link_libraries(source_prop_smoke PRIVATE lb_source_propagation)

add_executable(k26_tsuchimura_preflight
  apps/k26_tsuchimura_preflight.cpp
)
target_link_libraries(k26_tsuchimura_preflight PRIVATE lb_source_propagation)

enable_testing()

add_executable(source_prop_tests tests/test_source_propagation.cpp)
target_link_libraries(source_prop_tests PRIVATE lb_source_propagation)

add_executable(source_prop_cpu_tileop_smoke
  tests/test_cpu_tileop_producer.cpp
)
target_link_libraries(source_prop_cpu_tileop_smoke
  PRIVATE
    lb_source_propagation
    lb_source_campaign_tileop
)

add_test(NAME source_prop_tests COMMAND source_prop_tests)
add_test(NAME source_prop_smoke COMMAND source_prop_smoke)
add_test(NAME source_prop_cpu_tileop_smoke
  COMMAND source_prop_cpu_tileop_smoke)
add_test(NAME k26_tsuchimura_preflight
  COMMAND k26_tsuchimura_preflight)
