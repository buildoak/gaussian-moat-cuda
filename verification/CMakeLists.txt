cmake_minimum_required(VERSION 3.22)
project(gaussian_moat_verification CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)
find_package(Python3 COMPONENTS Interpreter)

add_library(verification_common INTERFACE)
target_include_directories(verification_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

foreach(tool
    bz_exact_check
    boundary_semantics_test
    exact_global_uf
    source_dead_cert_check
    source_dead_gap_check
    source_prop_oracle
    tile_sample_check
    span_cert_check)
  add_executable(${tool} src/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE verification_common nlohmann_json::nlohmann_json)
endforeach()

add_executable(postflight_check postflight/src/postflight_check.cpp)
target_link_libraries(postflight_check PRIVATE verification_common nlohmann_json::nlohmann_json)

enable_testing()
add_test(NAME boundary_semantics_test COMMAND boundary_semantics_test)
add_test(NAME bz_exact_k36_lower_span
  COMMAND bz_exact_check --k-sq 36 --r-inner 73339843 --r-outer 73372611)
add_test(NAME bz_exact_k36_lower_moat
  COMMAND bz_exact_check --k-sq 36 --r-inner 73359375 --r-outer 73392143)
add_test(NAME exact_global_uf_tiny
  COMMAND exact_global_uf --k-sq 36 --r-inner 100 --r-outer 500 --expect SPANNING)
add_test(NAME no_campaign_includes
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/no_campaign_includes.cmake)

foreach(source_prop_case
    composed-ten-vs-big-equivalence
    composed-vs-big-equivalence
    false-weld
    k32-carry-width-minimum
    non-source-partition-merge
    one-band-identity
    source-only-carry-loss
    terminal-death-inventory)
  add_test(NAME source_prop_fixture_${source_prop_case}
    COMMAND source_prop_oracle
      ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/${source_prop_case}.json)
endforeach()
foreach(bad_source_prop_case
    bad-composed-band-count-too-small
    overflow-reject
    bad-k32-carry-width-too-small)
  add_test(NAME source_prop_fixture_${bad_source_prop_case}
    COMMAND source_prop_oracle
      ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/${bad_source_prop_case}.json)
  set_tests_properties(source_prop_fixture_${bad_source_prop_case} PROPERTIES WILL_FAIL TRUE)
endforeach()
add_test(NAME source_dead_cert_fixture_valid
  COMMAND source_dead_cert_check
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/dead-cert-valid.json)
add_test(NAME source_dead_cert_fixture_first_quadrant_valid
  COMMAND source_dead_cert_check
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/dead-cert-first-quadrant-valid.json)
add_test(NAME source_dead_cert_fixture_summary_nonclaim_valid
  COMMAND source_dead_cert_check
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/dead-cert-summary-nonclaim-valid.json)
add_test(NAME source_dead_gap_fixture_valid
  COMMAND source_dead_gap_check
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/dead-gap-valid.json)
set_tests_properties(source_dead_gap_fixture_valid PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"status\":\"SOURCE_DEAD_GAP_NON_CLAIM_PASS\".*\"bridge_safety\":\"accepted_non_claim\".*\"coordinate_path_obligation\":\"blocked_coordinate_gaussian_prime_path\".*\"bz_schedule_obligation\":\"blocked_schedule_only_non_claim\".*\"terminal_inventory_obligation\":\"blocked_claim_grade_terminal_inventory\".*\"claim_grade\":false")
add_test(NAME source_dead_gap_fixture_target_not_reached_valid
  COMMAND source_dead_gap_check
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/dead-gap-target-not-reached-valid.json)
set_tests_properties(source_dead_gap_fixture_target_not_reached_valid PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"status\":\"SOURCE_DEAD_GAP_NON_CLAIM_PASS\".*\"bridge_safety\":\"accepted_non_claim\".*\"coordinate_path_obligation\":\"blocked_target_not_reached\".*\"bz_schedule_obligation\":\"blocked_schedule_only_non_claim\".*\"terminal_inventory_obligation\":\"blocked_claim_grade_terminal_inventory\".*\"claim_grade\":false")
foreach(bad_source_dead_cert
    dead-cert-bad-accumulator-claim
    dead-cert-bad-artifact-hash
    dead-cert-bad-digest
    dead-cert-bad-endpoint-atom-id
    dead-cert-bad-endpoint-inventory
    dead-cert-bad-inventory-extrema
    dead-cert-bad-k26-bz-status
    dead-cert-bad-k26-endpoint
    dead-cert-bad-k26-origin-path
    dead-cert-bad-k26-source-mode
    dead-cert-bad-listed-nonclaim-status
    dead-cert-bad-negative-guard
    dead-cert-bad-path-inventory
    dead-cert-bad-path-provenance
    dead-cert-bad-pending-metadata
    dead-cert-bad-summary-extrema
    dead-cert-bad-summary-mode-claim
    dead-cert-bad-summary-source-step
    dead-cert-bad-source-path)
  add_test(NAME source_dead_cert_fixture_${bad_source_dead_cert}_reject
    COMMAND source_dead_cert_check
      ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/${bad_source_dead_cert}.json)
  set_tests_properties(source_dead_cert_fixture_${bad_source_dead_cert}_reject
    PROPERTIES WILL_FAIL TRUE)
endforeach()
foreach(bad_source_dead_gap
    dead-gap-bad-bridge-candidate-total
    dead-gap-bad-bridge-counts
    dead-gap-bad-bridge-unsafe
    dead-gap-bad-bz-digest
    dead-gap-bad-bz-evidence
    dead-gap-bad-claim-status
    dead-gap-bad-endpoint-atom
    dead-gap-bad-endpoint-port-tile
    dead-gap-bad-inventory-accumulator-claim
    dead-gap-bad-inventory-obligation-claim
    dead-gap-bad-inventory-obligation-digest
    dead-gap-bad-inventory-tie
    dead-gap-bad-missing-list-omits-bz
    dead-gap-bad-missing-list-omits-terminal-inventory
    dead-gap-bad-origin-prefix-target
    dead-gap-bad-path-obligation-claim
    dead-gap-bad-path-obligation-count
    dead-gap-bad-prefix-witness-hash
    dead-gap-bad-port-atom
    dead-gap-bad-target-bridge-count)
  add_test(NAME source_dead_gap_fixture_${bad_source_dead_gap}_reject
    COMMAND source_dead_gap_check
      ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/source_prop/${bad_source_dead_gap}.json)
  set_tests_properties(source_dead_gap_fixture_${bad_source_dead_gap}_reject
    PROPERTIES WILL_FAIL TRUE)
endforeach()
if(Python3_Interpreter_FOUND)
  add_test(NAME source_prop_schema_contract_py
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_source_prop_schema_contract.py)
  add_test(NAME stats_normalize_sweep_rows_py
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/stats/test_normalize_sweep_rows.py)
  add_test(NAME stats_unittest_py
    COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_CURRENT_SOURCE_DIR}/stats -p test*.py)
  add_test(NAME postflight_check_contract_py
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/postflight/test_postflight_check_contract.py
            --postflight-check $<TARGET_FILE:postflight_check>
            --fixtures-dir ${CMAKE_CURRENT_SOURCE_DIR}/postflight/fixtures)
endif()

add_test(NAME span_cert_fixture_valid
  COMMAND span_cert_check ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/span_cert/minimal-valid.json)
foreach(bad_span_cert
    bad-claim-id
    bad-metadata
    bad-point
    bad-region)
  add_test(NAME span_cert_fixture_${bad_span_cert}_reject
    COMMAND span_cert_check ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/span_cert/${bad_span_cert}.json)
  set_tests_properties(span_cert_fixture_${bad_span_cert}_reject PROPERTIES WILL_FAIL TRUE)
endforeach()

add_test(NAME tile_sample_fixture_valid
  COMMAND tile_sample_check
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/valid-manifest.json
    --samples ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/valid.tiles.jsonl)
add_test(NAME tile_sample_fixture_exhausted_quota_valid
  COMMAND tile_sample_check
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/exhausted-quota-manifest.json
    --samples ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/valid.tiles.jsonl)
foreach(bad_tile_sample
    bad-class
    bad-tileop
    duplicate
    out-of-grid)
  add_test(NAME tile_sample_fixture_${bad_tile_sample}_reject
    COMMAND tile_sample_check
      --manifest ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/valid-manifest.json
      --samples ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/${bad_tile_sample}.tiles.jsonl)
  set_tests_properties(tile_sample_fixture_${bad_tile_sample}_reject PROPERTIES WILL_FAIL TRUE)
endforeach()
add_test(NAME tile_sample_fixture_under_quota_reject
  COMMAND tile_sample_check
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/under-quota-manifest.json
    --samples ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tile_samples/valid.tiles.jsonl)
set_tests_properties(tile_sample_fixture_under_quota_reject PROPERTIES WILL_FAIL TRUE)

foreach(postflight_case
    run-contract-pass:RUN_CONTRACT_PASS
    analytical-telemetry-pass:RUN_CONTRACT_PASS
    tile-sample-audit-pass:TILE_SAMPLE_AUDIT_PASS
    span-proof-pass:SPAN_PROOF_PASS
    moat-claim-proof-missing:CLAIM_PROOF_MISSING)
  string(REPLACE ":" ";" postflight_parts "${postflight_case}")
  list(GET postflight_parts 0 postflight_name)
  list(GET postflight_parts 1 postflight_status)
  add_test(NAME postflight_fixture_${postflight_name}
    COMMAND postflight_check
      ${CMAKE_CURRENT_SOURCE_DIR}/postflight/fixtures/${postflight_name}.json)
  set_tests_properties(postflight_fixture_${postflight_name}
    PROPERTIES PASS_REGULAR_EXPRESSION "\"status\":\"${postflight_status}\"|\"status\": \"${postflight_status}\"")
endforeach()
add_test(NAME postflight_fixture_reject_overflow
  COMMAND postflight_check ${CMAKE_CURRENT_SOURCE_DIR}/postflight/fixtures/reject-overflow.json)
set_tests_properties(postflight_fixture_reject_overflow PROPERTIES WILL_FAIL TRUE)
add_test(NAME postflight_fixture_reject_analytical_telemetry
  COMMAND postflight_check ${CMAKE_CURRENT_SOURCE_DIR}/postflight/fixtures/reject-analytical-telemetry.json)
set_tests_properties(postflight_fixture_reject_analytical_telemetry PROPERTIES WILL_FAIL TRUE)
