cmake_minimum_required(VERSION 3.22)
project(lb_source_propagation CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED K_SQ)
  set(K_SQ 36 CACHE STRING "Squared step bound for campaign TileOp smoke")
endif()

find_package(OpenMP QUIET)

add_library(lb_source_propagation STATIC
  ../cpp-campaign-v2/src/sha256.cpp
  src/source_propagation.cpp
)
target_compile_options(lb_source_propagation PRIVATE -Wall -Wextra -Werror)

target_include_directories(lb_source_propagation
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/src
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
if(NOT OpenMP_CXX_FOUND)
  target_compile_options(lb_source_campaign_tileop PRIVATE -Wno-unknown-pragmas)
endif()
target_compile_definitions(lb_source_campaign_tileop PUBLIC K_SQ=${K_SQ})
target_include_directories(lb_source_campaign_tileop
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/src
)
if(OpenMP_CXX_FOUND)
  target_link_libraries(lb_source_campaign_tileop PUBLIC OpenMP::OpenMP_CXX)
endif()

add_library(lb_source_tileop_port_graph STATIC
  src/tileop_port_graph.cpp
)
target_compile_options(lb_source_tileop_port_graph PRIVATE -Wall -Wextra -Werror)
target_link_libraries(lb_source_tileop_port_graph
  PUBLIC
    lb_source_propagation
    lb_source_campaign_tileop
)
target_include_directories(lb_source_tileop_port_graph
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(source_prop_smoke apps/source_prop_smoke.cpp)
target_link_libraries(source_prop_smoke PRIVATE lb_source_propagation)

add_executable(k26_tsuchimura_preflight
  apps/k26_tsuchimura_preflight.cpp
)
target_link_libraries(k26_tsuchimura_preflight PRIVATE lb_source_propagation)

add_executable(k26_source_run_contract
  apps/k26_source_run_contract.cpp
)
target_link_libraries(k26_source_run_contract PRIVATE lb_source_propagation)

add_executable(source_origin_cpu_runner
  apps/source_origin_cpu_runner.cpp
)
target_link_libraries(source_origin_cpu_runner
  PRIVATE
    lb_source_propagation
    lb_source_campaign_tileop
)

add_executable(source_tileop_cpu_runner
  apps/source_tileop_cpu_runner.cpp
)
target_link_libraries(source_tileop_cpu_runner
  PRIVATE
    lb_source_propagation
    lb_source_campaign_tileop
)

add_executable(source_tileop_port_runner
  apps/source_tileop_port_runner.cpp
)
target_link_libraries(source_tileop_port_runner
  PRIVATE
    lb_source_tileop_port_graph
)

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

add_executable(source_prop_tileop_port_graph_tests
  tests/test_tileop_port_graph.cpp
)
target_link_libraries(source_prop_tileop_port_graph_tests
  PRIVATE
    lb_source_tileop_port_graph
)

add_test(NAME source_prop_tests COMMAND source_prop_tests)
add_test(NAME source_prop_smoke COMMAND source_prop_smoke)
add_test(NAME source_prop_cpu_tileop_smoke
  COMMAND source_prop_cpu_tileop_smoke)
add_test(NAME source_prop_tileop_port_graph_tests
  COMMAND source_prop_tileop_port_graph_tests)
add_test(NAME k26_tsuchimura_preflight
  COMMAND k26_tsuchimura_preflight)
add_test(NAME k26_source_run_contract
  COMMAND k26_source_run_contract)
set_tests_properties(k26_source_run_contract PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_k26_run_contract_v1\".*\"claim_label\":\"SOURCE_ORIGIN_K26\".*\"executable_now\":false.*\"conservative_guard_min_r_final\":1015645"
  FAIL_REGULAR_EXPRESSION
    "\"executable_now\":true|SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_origin_cpu_runner_smoke
  COMMAND source_origin_cpu_runner
    --k-sq 26
    --r-final 12
    --band-width 6
    --endpoint-a 0
    --endpoint-b 3)
set_tests_properties(source_origin_cpu_runner_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_origin_cpu_runner_v1\".*\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\".*\"source_reached\":true.*\"accepted\":true.*\"source_inventory_digest_algorithm\":\"sha256:lb_source_inventory_v1\".*\"source_path_length\":1.*\"source_path\":\\[\\{\"a\":0,\"b\":3,\"norm_sq\":9\\}\\]"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_origin_cpu_runner_dead_cert_smoke
  COMMAND source_origin_cpu_runner
    --k-sq 2
    --r-final 16
    --band-width 2
    --endpoint-a 0
    --endpoint-b 3
    --cert-out ${CMAKE_CURRENT_BINARY_DIR}/source_origin_dead_cert_smoke.json)
set_tests_properties(source_origin_cpu_runner_dead_cert_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_origin_cpu_runner_v1\".*\"terminal_source_dead\":true.*\"source_path_length\":3.*\"cert_written\":true"
  FAIL_REGULAR_EXPRESSION
    "MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_origin_prefix_manifest_smoke
  COMMAND source_origin_cpu_runner
    --k-sq 36
    --r-final 248
    --band-width 128
    --endpoint-a 0
    --endpoint-b 251
    --manifest-out ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_manifest.txt
    --prefix-witness-out ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt)
set_tests_properties(source_origin_prefix_manifest_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_origin_cpu_runner_v1\".*\"accepted\":true.*\"terminal_source_dead\":false.*\"has_source_carry\":true.*\"manifest_written\":true.*\"prefix_witness_written\":true.*\"prefix_witness_targets\":[1-9][0-9]*"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_tileop_cpu_runner_smoke
  COMMAND source_tileop_cpu_runner
    --r-start 248
    --r-final 512
    --band-width 128
    --seed-a 0
    --seed-b 251
    --endpoint-a 0
    --endpoint-b 251)
set_tests_properties(source_tileop_cpu_runner_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_tileop_cpu_runner_v1\".*\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\".*\"source_mode\":\"CERTIFIED_SEED\".*\"campaign_tiles_processed\":[1-9][0-9]*.*\"tileop_overflows\":0.*\"source_reached\":true.*\"accepted\":true.*\"source_path_length\":[1-9][0-9]*"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_tileop_cpu_runner_manifest_smoke
  COMMAND source_tileop_cpu_runner
    --r-start 248
    --r-final 512
    --band-width 128
    --endpoint-a 0
    --endpoint-b 251
    --manifest-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_manifest.txt
    --prefix-witness-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt)
set_tests_properties(source_tileop_cpu_runner_manifest_smoke PROPERTIES
  DEPENDS source_origin_prefix_manifest_smoke
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_tileop_cpu_runner_v1\".*\"source_mode\":\"ORIGIN_PREFIX_WITNESS\".*\"manifest_source_carry_atoms\":[1-9][0-9]*.*\"prefix_witness_targets\":[1-9][0-9]*.*\"campaign_tiles_processed\":[1-9][0-9]*.*\"tileop_overflows\":0.*\"source_reached\":true.*\"accepted\":true.*\"source_path_length\":[1-9][0-9]*"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_tileop_port_runner_smoke
  COMMAND source_tileop_port_runner
    --r-start 248
    --r-final 512
    --band-width 128
    --seed-inner-flags)
set_tests_properties(source_tileop_port_runner_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_tileop_port_runner_v1\".*\"source_mode\":\"GEO_I_PORT_DIAGNOSTIC\".*\"campaign_tiles_processed\":[1-9][0-9]*.*\"tileop_overflows\":0.*\"port_atoms\":[1-9][0-9]*.*\"seam_edges\":[1-9][0-9]*.*\"accepted\":true.*\"terminal_source_dead\":true.*\"manifest_written\":false"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
