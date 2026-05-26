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
  src/k26_bz_schedule.cpp
  src/resumable_band_checkpoint.cpp
  src/source_propagation.cpp
  src/stream_checkpoint.cpp
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
  ../cpp-campaign-v2/src/streaming_compositor.cpp
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
  src/tileop_live_bridge.cpp
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
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/src
)

add_library(lb_source_tileop_port_stream STATIC
  src/tileop_port_stream.cpp
)
target_compile_options(lb_source_tileop_port_stream PRIVATE -Wall -Wextra -Werror)
target_link_libraries(lb_source_tileop_port_stream
  PUBLIC
    lb_source_propagation
    lb_source_campaign_tileop
)
target_include_directories(lb_source_tileop_port_stream
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(lb_source_tileop_static_reach STATIC
  src/tileop_static_reach.cpp
)
target_compile_options(lb_source_tileop_static_reach PRIVATE -Wall -Wextra -Werror)
target_link_libraries(lb_source_tileop_static_reach
  PUBLIC
    lb_source_tileop_port_stream
)
target_include_directories(lb_source_tileop_static_reach
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(lb_source_detector_band_handoff STATIC
  src/detector_band_handoff.cpp
)
target_compile_options(lb_source_detector_band_handoff PRIVATE
  -Wall -Wextra -Werror)
target_link_libraries(lb_source_detector_band_handoff
  PUBLIC
    lb_source_tileop_static_reach
)
target_include_directories(lb_source_detector_band_handoff
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/src
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

add_executable(k26_execution_plan
  apps/k26_execution_plan.cpp
)
target_link_libraries(k26_execution_plan PRIVATE lb_source_propagation)

add_executable(k26_bz_schedule_check
  apps/k26_bz_schedule_check.cpp
)
target_link_libraries(k26_bz_schedule_check PRIVATE lb_source_propagation)

add_executable(k26_source_run_profile
  apps/k26_source_run_profile.cpp
)
target_link_libraries(k26_source_run_profile PRIVATE lb_source_propagation)

add_executable(k26_source_run_commands
  apps/k26_source_run_commands.cpp
)
target_link_libraries(k26_source_run_commands PRIVATE lb_source_propagation)

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

add_executable(source_tileop_port_stream_runner
  apps/source_tileop_port_stream_runner.cpp
)
target_link_libraries(source_tileop_port_stream_runner
  PRIVATE
    lb_source_tileop_port_stream
)
target_include_directories(source_tileop_port_stream_runner
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../cpp-campaign-v2/src
)

add_executable(tileop_static_reach_equivalence
  apps/tileop_static_reach_equivalence.cpp
)
target_link_libraries(tileop_static_reach_equivalence
  PRIVATE
    lb_source_tileop_static_reach
)

enable_testing()

add_executable(source_prop_tests tests/test_source_propagation.cpp)
target_link_libraries(source_prop_tests PRIVATE lb_source_propagation)

add_executable(stream_checkpoint_tests tests/test_stream_checkpoint.cpp)
target_link_libraries(stream_checkpoint_tests PRIVATE lb_source_propagation)

add_executable(resumable_band_checkpoint_tests
  tests/test_resumable_band_checkpoint.cpp
)
target_link_libraries(resumable_band_checkpoint_tests
  PRIVATE
    lb_source_propagation
)

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

add_executable(tileop_port_stream_tests
  tests/test_tileop_port_stream.cpp
)
target_link_libraries(tileop_port_stream_tests
  PRIVATE
    lb_source_tileop_port_stream
    lb_source_tileop_port_graph
)

add_executable(tileop_static_reach_tests
  tests/test_tileop_static_reach.cpp
)
target_link_libraries(tileop_static_reach_tests
  PRIVATE
    lb_source_tileop_static_reach
)

add_executable(detector_band_handoff_tests
  tests/test_detector_band_handoff.cpp
)
target_link_libraries(detector_band_handoff_tests
  PRIVATE
    lb_source_detector_band_handoff
)

add_test(NAME source_prop_tests COMMAND source_prop_tests)
add_test(NAME stream_checkpoint COMMAND stream_checkpoint_tests)
add_test(NAME resumable_band_checkpoint
  COMMAND resumable_band_checkpoint_tests)
add_test(NAME source_prop_smoke COMMAND source_prop_smoke)
add_test(NAME source_prop_cpu_tileop_smoke
  COMMAND source_prop_cpu_tileop_smoke)
add_test(NAME source_prop_tileop_port_graph_tests
  COMMAND source_prop_tileop_port_graph_tests)
add_test(NAME tileop_port_stream COMMAND tileop_port_stream_tests)
add_test(NAME tileop_static_reach COMMAND tileop_static_reach_tests)
add_test(NAME detector_band_handoff COMMAND detector_band_handoff_tests)
add_test(NAME tileop_static_reach_equivalence_smoke
  COMMAND tileop_static_reach_equivalence
    --r-start 248
    --r-final 512
    --microband-width 128
    --max-atoms 1000000
    --tileop-threads 0)
set_tests_properties(tileop_static_reach_equivalence_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"status\":\"STATIC_REACH_EQUIVALENCE_PASS\".*\"full_compositor_spanning\":(true|false).*\"full_static_reach_spanning\":(true|false).*\"stitched_static_reach_spanning\":(true|false)"
  FAIL_REGULAR_EXPRESSION
    "STATIC_REACH_EQUIVALENCE_FAIL|SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME k26_tsuchimura_preflight
  COMMAND k26_tsuchimura_preflight)
add_test(NAME k26_source_run_contract
  COMMAND k26_source_run_contract)
set_tests_properties(k26_source_run_contract PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_k26_run_contract_v1\".*\"claim_label\":\"SOURCE_ORIGIN_K26\".*\"executable_now\":false.*\"conservative_guard_min_r_final\":1015645.*\"source_dead_cert_claim_gate\":.*\"terminal_source_inventory_mode\":\"claim_grade_accumulator\".*\"terminal_source_inventory_accumulator_mode\":\"claim_grade_digest_accumulator\".*complete_stream_observed.*chunk/resume support"
  FAIL_REGULAR_EXPRESSION
    "\"executable_now\":true|SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME k26_execution_plan
  COMMAND k26_execution_plan)
set_tests_properties(k26_execution_plan PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_k26_execution_plan_v1\".*\"claim_label\":\"SOURCE_ORIGIN_K26\".*\"executable_now\":false.*\"max_dph_usd\":0.37.*\"bz_schedule\":\"repaired\".*\"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE\".*\"accepted_for_schedule\":true.*\"schedule_digest_algorithm\":\"sha256:lb_source_k26_repaired_bz_schedule_v1\".*\"repaired_boundary_count\":3.*\"max_abs_boundary_shift\":1.*\"band_count\":124.*\"last_band_width\":8029.*\"index\":123.*\"r_outer\":1015645.*local sidecar CTest passes"
  FAIL_REGULAR_EXPRESSION
    "\"executable_now\":true|SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME k26_bz_schedule_check
  COMMAND k26_bz_schedule_check)
set_tests_properties(k26_bz_schedule_check PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_k26_bz_schedule_check_v1\".*\"proof_status\":\"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE\".*\"accepted_for_schedule\":true.*\"accepted_for_claim\":false.*\"band_count\":124.*\"schedule_digest_algorithm\":\"sha256:lb_source_k26_repaired_bz_schedule_v1\".*\"schedule_digest_hex\":\"7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95\".*\"repaired_boundary_count\":3.*\"max_abs_boundary_shift\":1"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME k26_source_run_profile
  COMMAND k26_source_run_profile)
set_tests_properties(k26_source_run_profile PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_k26_run_profile_v1\".*\"profile_status\":\"RUN_PROFILE_DRAFT_NON_CLAIM\".*\"executable_now\":false.*\"required_k_sq\":26.*\"bz_schedule\":\"repaired\".*\"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE\".*\"accepted_for_schedule\":true.*\"schedule_digest_algorithm\":\"sha256:lb_source_k26_repaired_bz_schedule_v1\".*\"band_count\":124.*\"prefix_row_index\":0.*\"tileop_port_first_row_index\":1.*\"execution_protocol\":.*\"recommended_continuation_chunk_bands\":8.*\"resume_existing_supported\":true.*\"chunk_ledger\":\"k26-continuation-chunks.jsonl\".*\"chunk_ledger_required_for_checked_bundle\":true.*\"runtime_budget_checker\":\"check_k26_runtime_budget.py\".*\"source_dead_gap_checker\":\"source_dead_gap_check\".*\"source_dead_checker\":\"source_dead_cert_check\".*\"auto_summary_nonclaim_cert\":true.*\"recommended_max_runtime_seconds\":14000.*\"source_dead_cert_claim_gate\":.*\"required_terminal_source_inventory_mode\":\"claim_grade_accumulator\".*\"required_accumulator_mode\":\"claim_grade_digest_accumulator\".*complete_stream_observed.*chunk/resume execution"
  FAIL_REGULAR_EXPRESSION
    "\"executable_now\":true|SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME k26_source_run_commands
  COMMAND k26_source_run_commands)
set_tests_properties(k26_source_run_commands PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_k26_run_commands_v1\".*\"claim_label\":\"SOURCE_ORIGIN_K26\".*\"executable_now\":false.*\"required_k_sq\":26.*endpoint-a 376039 --endpoint-b 943460.*\"schedule_boundary_count\":124.*\"schedule_segment_count\":123.*\"schedule_digest_algorithm\":\"sha256:lb_source_k26_repaired_bz_schedule_v1\".*\"seam_bridge_policy\":\"diagnostic_allow_unbridged\".*\"claim_grade_requires_source_unbridged_unsafe_candidate_atoms\":0.*\"recommended_chunk_bands\":8.*\"resume_existing_supported\":true.*run_k26_full_source_bundle.sh.*\"chunk_ledger\":\"k26-continuation-chunks.jsonl\".*\"chunk_ledger_required_for_checked_bundle\":true.*\"runtime_budget_checker\":\"check_k26_runtime_budget.py\".*\"source_dead_gap_checker\":\"source_dead_gap_check\".*\"source_dead_checker\":\"source_dead_cert_check\".*\"auto_summary_nonclaim_cert\":true.*\"claim_grade_terminal_source_inventory_mode\":\"claim_grade_accumulator\".*\"claim_grade_accumulator_mode\":\"claim_grade_digest_accumulator\".*complete_stream_observed.*\"max_runtime_seconds\":14000.*--continuation-chunk-bands 8 --resume-existing.*--max-runtime-seconds 14000.*\"checked_bundle_command\":\"[^\"]*--source-dead-checker source_dead_cert_check\".*\"supplied_cert_bundle_command\":\"[^\"]*--cert-in k26-source-dead-cert.json --source-dead-checker source_dead_cert_check\""
  FAIL_REGULAR_EXPRESSION
    "\"executable_now\":true|SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME remote_smoke_artifact_checker
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_remote_smoke_artifact_checker.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_remote_smoke_artifacts.sh)
add_test(NAME remote_k26_timing_artifact_checker
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_remote_k26_timing_artifact_checker.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_remote_k26_timing_artifacts.sh)
add_test(NAME vast_sidecar_smoke_guard
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vast_sidecar_smoke_guard.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/vast_sidecar_smoke_guard.sh)
set_tests_properties(vast_sidecar_smoke_guard PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/../..)
add_test(NAME remote_k26_timing_probe
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_remote_k26_timing_probe.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/remote_k26_timing_probe.sh)
add_test(NAME k26_full_run_bundle_checker
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_k26_full_run_bundle_checker.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_k26_full_run_bundle.sh)
add_test(NAME k26_full_run_harness
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_k26_full_run_harness.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_k26_full_source_bundle.sh)
add_test(NAME k26_runtime_budget_checker
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_k26_runtime_budget_checker.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_k26_runtime_budget.py)
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
    --k-sq ${K_SQ}
    --r-final 248
    --band-width 128
    --endpoint-a 0
    --endpoint-b 251
    --manifest-out ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_manifest.txt
    --live-manifest-out ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_live_handoff.txt
    --prefix-witness-out ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt)
set_tests_properties(source_origin_prefix_manifest_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_origin_cpu_runner_v1\".*\"accepted\":true.*\"terminal_source_dead\":false.*\"has_source_carry\":true.*\"live_manifest_written\":true.*\"prefix_witness_written\":true.*\"prefix_witness_targets\":[1-9][0-9]*"
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
    "\"schema\":\"lb_source_tileop_port_runner_v1\".*\"source_mode\":\"GEO_I_PORT_DIAGNOSTIC\".*\"campaign_tiles_processed\":[1-9][0-9]*.*\"tileop_overflows\":0.*\"port_atoms\":[1-9][0-9]*.*\"seam_edges\":[1-9][0-9]*.*\"accepted\":true.*\"terminal_source_dead\":false.*\"has_source_carry\":true.*\"source_carry_atoms\":[1-9][0-9]*.*\"source_frontier_mode\":\"live_frontier_only_non_claim\".*\"source_frontier_count\":[1-9][0-9]*"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_tileop_port_runner_schedule_smoke
  COMMAND source_tileop_port_runner
    --r-start 248
    --r-final 512
    --band-width 128
    --schedule-radii 248,375,512
    --seed-inner-flags)
set_tests_properties(source_tileop_port_runner_schedule_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_tileop_port_runner_v1\".*\"source_mode\":\"GEO_I_PORT_DIAGNOSTIC\".*\"schedule_mode\":\"explicit_radii\".*\"schedule_boundary_count\":3.*\"schedule_min_width\":127.*\"schedule_max_width\":137.*\"campaign_tiles_processed\":[1-9][0-9]*.*\"tileop_overflows\":0.*\"accepted\":true.*\"terminal_source_dead\":false.*\"has_source_carry\":true.*\"source_carry_atoms\":[1-9][0-9]*"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_tileop_port_runner_resume_smoke
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tileop_port_runner_resume.sh
    $<TARGET_FILE:source_tileop_port_runner>)
add_test(NAME source_tileop_port_stream_runner_smoke
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tileop_port_stream_runner.sh
    $<TARGET_FILE:source_tileop_port_stream_runner>)
add_test(NAME source_tileop_port_stream_equivalence
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_tileop_port_stream_equivalence.sh
    $<TARGET_FILE:source_tileop_port_stream_runner>
    $<TARGET_FILE:source_tileop_port_runner>
    --out-dir ${CMAKE_CURRENT_BINARY_DIR}/source_tileop_port_stream_equivalence)
add_test(NAME source_tileop_port_runner_wide_band_equivalence
  COMMAND bash
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_tileop_port_wide_band_equivalence.sh
    $<TARGET_FILE:source_tileop_port_runner>
    --r-start 248
    --segments 2
    --segment-width 128
    --out-dir ${CMAKE_CURRENT_BINARY_DIR}/source_tileop_port_wide_band_equivalence)
add_test(NAME source_tileop_port_runner_manifest_smoke
  COMMAND source_tileop_port_runner
    --r-start 248
    --r-final 512
    --band-width 128
    --live-manifest-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_live_handoff.txt
    --prefix-witness-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt)
set_tests_properties(source_tileop_port_runner_manifest_smoke PROPERTIES
  DEPENDS source_origin_prefix_manifest_smoke
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_tileop_port_runner_v1\".*\"source_mode\":\"ORIGIN_PREFIX_PORT_WITNESS\".*\"live_manifest_source_carry_atoms\":[1-9][0-9]*.*\"prefix_witness_targets\":[1-9][0-9]*.*\"bridged_coordinate_carry_atoms\":[1-9][0-9]*.*\"unbridged_coordinate_carry_atoms\":[0-9][0-9]*.*\"bridged_port_carry_atoms\":[1-9][0-9]*.*\"bridge_edges\":[1-9][0-9]*.*\"accepted\":true.*\"terminal_source_dead\":false.*\"has_source_carry\":true.*\"source_carry_atoms\":[1-9][0-9]*.*\"source_frontier_mode\":\"live_frontier_only_non_claim\".*\"source_frontier_count\":[1-9][0-9]*.*\"source_frontier_digest_algorithm\":\"sha256:lb_source_inventory_v1\".*\"max_source_frontier_norm_sq\":[1-9][0-9]*"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_tileop_port_runner_target_smoke
  COMMAND source_tileop_port_runner
    --r-start 248
    --r-final 512
    --band-width 128
    --target-a 0
    --target-b 251
    --live-manifest-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_live_handoff.txt
    --prefix-witness-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt)
set_tests_properties(source_tileop_port_runner_target_smoke PROPERTIES
  DEPENDS source_origin_prefix_manifest_smoke
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_tileop_port_runner_v1\".*\"source_mode\":\"ORIGIN_PREFIX_PORT_WITNESS\".*\"target\":\\{\"enabled\":true,\"a\":0,\"b\":251,\"norm_sq\":63001,\"seen\":true,\"port_atoms\":[1-9][0-9]*,\"bridge_edges\":[1-9][0-9]*,\"source_reached\":true,\"path_provenance\":\"mixed_coordinate_port_atom_chain_non_claim\",\"atom_path_length\":[1-9][0-9]*,\"atom_path\":\\[[0-9,-]+\\],\"prefix_witness_path\":\\{\"available\":true,\"target_atom_id\":[0-9]+,\"path_points\":[1-9][0-9]*,\"seed_norm_sq\":[1-9][0-9]*,\"target_norm_sq\":[1-9][0-9]*\\},\"coordinate_port_expansions\":\\{\"required_edges\":[1-9][0-9]*,\"available_edges\":[1-9][0-9]*,\"path_points_total\":[1-9][0-9]*,\"expansions\":\\[\\{\"coordinate_atom_id\":[0-9]+,\"port_atom_id\":-[0-9]+,\"path_points\":[1-9][0-9]*,\"coordinate_norm_sq\":[1-9][0-9]*,\"port_witness_norm_sq\":[1-9][0-9]*,\"path\":\\[\\{\"a\":[0-9]+,\"b\":[0-9]+,\"norm_sq\":[1-9][0-9]*\\}.*\\]\\}.*\\]\\}\\}.*\"accepted\":true.*\"terminal_source_dead\":false.*\"has_source_carry\":true"
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")
add_test(NAME source_tileop_port_runner_manifest_full_bridge_reject
  COMMAND bash -c
    "out=\"$($<TARGET_FILE:source_tileop_port_runner> --r-start 248 --r-final 512 --band-width 128 --require-full-bridge --live-manifest-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_live_handoff.txt --prefix-witness-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt 2>&1)\" || { echo \"$out\" | grep -q 'strict seam bridge requires zero unbridged coordinate carry atoms'; exit $?; }; echo \"$out\" | grep -q '\"seam_bridge_policy\":\"require_full_bridge\".*\"unbridged_coordinate_carry_atoms\":0'")
set_tests_properties(source_tileop_port_runner_manifest_full_bridge_reject PROPERTIES
  DEPENDS source_origin_prefix_manifest_smoke)
add_test(NAME source_tileop_port_runner_manifest_schedule_smoke
  COMMAND source_tileop_port_runner
    --r-start 248
    --r-final 512
    --band-width 128
    --schedule-radii 248,375,512
    --live-manifest-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_live_handoff.txt
    --prefix-witness-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt)
set_tests_properties(source_tileop_port_runner_manifest_schedule_smoke PROPERTIES
  DEPENDS source_origin_prefix_manifest_smoke
  PASS_REGULAR_EXPRESSION
    "\"schema\":\"lb_source_tileop_port_runner_v1\".*\"source_mode\":\"ORIGIN_PREFIX_PORT_WITNESS\".*\"schedule_mode\":\"explicit_radii\".*\"schedule_boundary_count\":3.*\"live_manifest_source_carry_atoms\":[1-9][0-9]*.*\"prefix_witness_targets\":[1-9][0-9]*.*\"bridged_coordinate_carry_atoms\":[1-9][0-9]*.*\"accepted\":true.*\"terminal_source_dead\":false.*\"has_source_carry\":true.*\"source_frontier_digest_algorithm\":\"sha256:lb_source_inventory_v1\""
  FAIL_REGULAR_EXPRESSION
    "SOURCE_DEAD_CERT_PASS|MOAT_PROOF_PASS|SPAN_PROOF_PASS")

add_test(NAME source_tileop_port_runner_phase_progress_smoke
  COMMAND bash -c
    "out='${CMAKE_CURRENT_BINARY_DIR}/source_tileop_port_phase_progress.out'; progress='${CMAKE_CURRENT_BINARY_DIR}/source_tileop_port_phase_progress.jsonl'; rm -f \"$out\" \"$progress\"; $<TARGET_FILE:source_tileop_port_runner> --r-start 248 --r-final 512 --band-width 128 --live-manifest-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_live_handoff.txt --prefix-witness-in ${CMAKE_CURRENT_BINARY_DIR}/source_origin_prefix_witness.txt --progress-out \"$progress\" >\"$out\" && grep -q '\"schema\":\"lb_source_tileop_port_phase_v1\".*\"phase\":\"live_manifest_read\".*\"event\":\"begin\"' \"$progress\" && grep -q '\"schema\":\"lb_source_tileop_port_phase_v1\".*\"phase\":\"prefix_witness_read\".*\"event\":\"end\".*\"elapsed_ms\"' \"$progress\" && grep -q '\"schema\":\"lb_source_tileop_port_phase_v1\".*\"phase\":\"tileop_build\".*\"event\":\"begin\".*\"band_index\":0' \"$progress\" && grep -q '\"schema\":\"lb_source_tileop_port_progress_v1\".*\"band_index\":0.*\"tileop_worker_threads\":[1-9][0-9]*' \"$progress\" && grep -q '\"tileop_worker_threads\":[1-9][0-9]*' \"$out\"")
set_tests_properties(source_tileop_port_runner_phase_progress_smoke PROPERTIES
  DEPENDS source_origin_prefix_manifest_smoke)
