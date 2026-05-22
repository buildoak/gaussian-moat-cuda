#include "lb_source/source_propagation.h"
#include "lb_source/k26_bz_schedule.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kSq = 26;
constexpr std::uint64_t kTsuchimuraEndpointA = 943460;
constexpr std::uint64_t kTsuchimuraEndpointB = 376039;
constexpr std::uint64_t kCanonicalEndpointA = 376039;
constexpr std::uint64_t kCanonicalEndpointB = 943460;
constexpr std::uint64_t kEndpointNorm = 1031522101121ULL;
constexpr std::uint64_t kExpectedComponentSize = 14542615005ULL;
constexpr std::uint64_t kConservativeTerminalRadius = 1015645;
constexpr std::uint64_t kPreferredBandWidth = 8192;

std::uint64_t endpoint_norm() {
  const unsigned __int128 norm =
      static_cast<unsigned __int128>(kCanonicalEndpointA) *
          kCanonicalEndpointA +
      static_cast<unsigned __int128>(kCanonicalEndpointB) *
          kCanonicalEndpointB;
  if (norm > std::numeric_limits<std::uint64_t>::max()) {
    std::cerr << "endpoint norm overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return static_cast<std::uint64_t>(norm);
}

std::uint64_t band_count_for(std::uint64_t terminal_radius,
                             std::uint64_t band_width) {
  return (terminal_radius + band_width - 1) / band_width;
}

std::uint64_t repaired_boundary(std::uint64_t nominal) {
  return lb_source::k26_bz::repaired_boundary(nominal);
}

std::int64_t boundary_shift(std::uint64_t nominal) {
  return static_cast<std::int64_t>(repaired_boundary(nominal)) -
         static_cast<std::int64_t>(nominal);
}

void emit_string_array(const char* name, const char* const* values,
                       std::size_t count) {
  std::cout << "\"" << name << "\":[";
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0) std::cout << ",";
    std::cout << "\"" << values[i] << "\"";
  }
  std::cout << "]";
}

void emit_rows() {
  std::cout << "\"rows\":[";
  std::uint64_t nominal_start = 0;
  std::uint64_t index = 0;
  while (nominal_start < kConservativeTerminalRadius) {
    const std::uint64_t remaining = kConservativeTerminalRadius - nominal_start;
    const std::uint64_t nominal_width =
        remaining < kPreferredBandWidth ? remaining : kPreferredBandWidth;
    const std::uint64_t nominal_outer = nominal_start + nominal_width;
    const std::uint64_t r_start = repaired_boundary(nominal_start);
    const std::uint64_t r_outer = repaired_boundary(nominal_outer);
    const std::uint64_t width = r_outer - r_start;
    if (index != 0) std::cout << ",";
    std::cout << "{\"index\":" << index
              << ",\"engine\":\""
              << (index == 0 ? "source_origin_cpu_runner_prefix"
                             : "source_tileop_port_runner")
              << "\",\"nominal_r_start\":" << nominal_start
              << ",\"nominal_r_outer\":" << nominal_outer
              << ",\"r_start\":" << r_start
              << ",\"r_outer\":" << r_outer
              << ",\"width\":" << width
              << ",\"start_shift\":" << boundary_shift(nominal_start)
              << ",\"outer_shift\":" << boundary_shift(nominal_outer)
              << ",\"bz_status\":\"K26_REPAIRED_BZ_SCHEDULE_PASS_NON_SOURCE\""
              << ",\"overflow_policy\":\"reject_source_row\"}";
    nominal_start = nominal_outer;
    ++index;
  }
  std::cout << "]";
}

}  // namespace

int main() {
  if (endpoint_norm() != kEndpointNorm) {
    std::cerr << "endpoint norm mismatch\n";
    return EXIT_FAILURE;
  }
  if (lb_source::ceil_sqrt(kSq) != 6) {
    std::cerr << "ceil_sqrt(26) mismatch\n";
    return EXIT_FAILURE;
  }
  if (band_count_for(kConservativeTerminalRadius, kPreferredBandWidth) !=
      124) {
    std::cerr << "unexpected K26 band count\n";
    return EXIT_FAILURE;
  }
  const std::vector<std::uint64_t> nominal =
      lb_source::k26_bz::nominal_boundaries();
  const std::vector<std::uint64_t> repaired =
      lb_source::k26_bz::canonical_repaired_boundaries();
  const std::string bz_schedule_digest =
      lb_source::k26_bz::repaired_schedule_digest_hex(nominal, repaired);

  constexpr const char* kRunnerRequirements[] = {
      "build sidecar with -DK_SQ=26 before campaign TileOp ingestion",
      "run exact coordinate prefix for row 0 and emit carry manifest plus prefix witness at radius 8192",
      "continue rows 1..123 with source_tileop_port_runner using the repaired variable boundary schedule",
      "seed continuation only from the origin-prefix carry manifest and prefix witness",
      "reject any TileOp overflow row",
      "bind repaired BZ schedule digest/evidence into profile metadata",
      "emit terminal inventory summary without explicit 14.5B-member JSON expansion",
      "emit SOURCE_DEAD_CERT only after positive endpoint path and negative final guard are both verified"};
  constexpr const char* kMissingRunnerFeatures[] = {
      "full K26 source runner has not executed the repaired variable-boundary schedule",
      "coordinate-to-port seam bridge remains diagnostic",
      "TileOp-port target reachability has mixed coordinate/port atom-chain provenance, not a coordinate source-path witness",
      "terminal inventory has only a summary-only non-claim verifier; claim-grade full-run provenance is still missing",
      "no K26 full-run artifact hash or non-pending build metadata exists"};

  std::cout
      << "{"
      << "\"schema\":\"lb_source_k26_run_profile_v1\","
      << "\"claim_label\":\"SOURCE_ORIGIN_K26\","
      << "\"profile_status\":\"RUN_PROFILE_DRAFT_NON_CLAIM\","
      << "\"executable_now\":false,"
      << "\"non_claim\":\"run profile only; no sqrt(26) source/origin run executed\","
      << "\"target\":{\"tsuchimura_endpoint\":{\"a\":"
      << kTsuchimuraEndpointA << ",\"b\":" << kTsuchimuraEndpointB
      << ",\"norm_sq\":" << kEndpointNorm
      << "},\"canonical_octant_endpoint\":{\"a\":"
      << kCanonicalEndpointA << ",\"b\":" << kCanonicalEndpointB
      << ",\"norm_sq\":" << kEndpointNorm
      << "},\"expected_component_size\":" << kExpectedComponentSize << "},"
      << "\"build\":{\"required_k_sq\":" << kSq
      << ",\"cmake_define\":\"-DK_SQ=26\","
      << "\"reason\":\"campaign TileOp constants are compile-time bound\"},"
      << "\"schedule\":{\"bz_schedule\":\"repaired\","
      << "\"bz_evidence\":{\"status\":\"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE\","
      << "\"accepted_for_schedule\":true,"
      << "\"accepted_for_claim\":false,"
      << "\"schedule_digest_algorithm\":\""
      << lb_source::k26_bz::schedule_digest_algorithm
      << "\",\"schedule_digest_hex\":\"" << bz_schedule_digest << "\"},"
      << "\"terminal_radius\":" << kConservativeTerminalRadius
      << ",\"preferred_band_width\":" << kPreferredBandWidth
      << ",\"band_count\":"
      << band_count_for(kConservativeTerminalRadius, kPreferredBandWidth)
      << ",\"repaired_boundary_count\":3,"
      << "\"max_abs_boundary_shift\":1,"
      << "\"nominal_dirty_row_indices\":[15,58,75],"
      << "\"prefix_row_index\":0,"
      << "\"tileop_port_first_row_index\":1,";
  emit_rows();
  std::cout << "},";
  emit_string_array("runner_requirements", kRunnerRequirements,
                    sizeof(kRunnerRequirements) /
                        sizeof(kRunnerRequirements[0]));
  std::cout << ",";
  emit_string_array("missing_runner_features", kMissingRunnerFeatures,
                    sizeof(kMissingRunnerFeatures) /
                        sizeof(kMissingRunnerFeatures[0]));
  std::cout << "}\n";
  return EXIT_SUCCESS;
}
