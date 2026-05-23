#include "lb_source/source_propagation.h"
#include "lb_source/k26_bz_schedule.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kSq = 26;
constexpr std::uint64_t kTsuchimuraEndpointA = 943460;
constexpr std::uint64_t kTsuchimuraEndpointB = 376039;
constexpr std::uint64_t kCanonicalEndpointA = 376039;
constexpr std::uint64_t kCanonicalEndpointB = 943460;
constexpr std::uint64_t kEndpointNorm = 1031522101121ULL;
constexpr std::uint64_t kTerminalRadius = 1015645;
constexpr std::uint64_t kBandWidth = 8192;
constexpr std::uint64_t kPrefixOuter = 8192;
constexpr std::uint64_t kRecommendedChunkBands = 8;

struct ScheduleStats {
  std::uint64_t min_width = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t max_width = 0;
};

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

std::uint64_t repaired_boundary(std::uint64_t nominal) {
  return lb_source::k26_bz::repaired_boundary(nominal);
}

std::vector<std::uint64_t> continuation_schedule_radii() {
  std::vector<std::uint64_t> radii;
  radii.push_back(kPrefixOuter);
  for (std::uint64_t nominal_start = kPrefixOuter;
       nominal_start < kTerminalRadius;) {
    const std::uint64_t remaining = kTerminalRadius - nominal_start;
    const std::uint64_t width =
        remaining < kBandWidth ? remaining : kBandWidth;
    nominal_start += width;
    radii.push_back(repaired_boundary(nominal_start));
  }
  return radii;
}

ScheduleStats schedule_stats(const std::vector<std::uint64_t>& radii) {
  ScheduleStats stats;
  for (std::size_t i = 1; i < radii.size(); ++i) {
    const std::uint64_t width = radii[i] - radii[i - 1];
    if (width < stats.min_width) {
      stats.min_width = width;
    }
    if (width > stats.max_width) {
      stats.max_width = width;
    }
  }
  if (stats.min_width == std::numeric_limits<std::uint64_t>::max()) {
    stats.min_width = 0;
  }
  return stats;
}

std::string csv(const std::vector<std::uint64_t>& values) {
  std::ostringstream out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    out << values[i];
  }
  return out.str();
}

void emit_json_string(const std::string& value) {
  std::cout << "\"";
  for (const char ch : value) {
    if (ch == '\\' || ch == '"') {
      std::cout << '\\';
    }
    std::cout << ch;
  }
  std::cout << "\"";
}

void validate_schedule_or_die(const std::vector<std::uint64_t>& radii) {
  if (radii.size() != 124) {
    std::cerr << "unexpected K26 continuation boundary count\n";
    std::exit(EXIT_FAILURE);
  }
  if (radii.front() != kPrefixOuter || radii.back() != kTerminalRadius) {
    std::cerr << "K26 continuation endpoints mismatch\n";
    std::exit(EXIT_FAILURE);
  }
  const std::uint64_t carry_width = lb_source::ceil_sqrt(kSq);
  for (std::size_t i = 1; i < radii.size(); ++i) {
    if (radii[i] <= radii[i - 1]) {
      std::cerr << "K26 continuation schedule is not increasing\n";
      std::exit(EXIT_FAILURE);
    }
    if (radii[i] - radii[i - 1] < carry_width) {
      std::cerr << "K26 continuation segment thinner than carry width\n";
      std::exit(EXIT_FAILURE);
    }
  }
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

  const std::vector<std::uint64_t> radii = continuation_schedule_radii();
  validate_schedule_or_die(radii);
  const ScheduleStats stats = schedule_stats(radii);
  if (stats.min_width != 8029 || stats.max_width != 8193) {
    std::cerr << "unexpected K26 continuation schedule width range\n";
    return EXIT_FAILURE;
  }
  const std::vector<std::uint64_t> nominal =
      lb_source::k26_bz::nominal_boundaries();
  const std::vector<std::uint64_t> repaired =
      lb_source::k26_bz::canonical_repaired_boundaries();
  const std::string bz_schedule_digest =
      lb_source::k26_bz::repaired_schedule_digest_hex(nominal, repaired);

  const std::string schedule_csv = csv(radii);
  const std::string prefix_command =
      "source_origin_cpu_runner --k-sq 26 --r-final 8192 --band-width 8192 "
      "--endpoint-a 376039 --endpoint-b 943460 --max-atoms 50000000 "
      "--manifest-out k26-prefix-manifest.txt --prefix-witness-out "
      "k26-prefix-witness.txt";
  const std::string continuation_command =
      "source_tileop_port_runner --r-start 8192 --r-final 1015645 "
      "--band-width 8192 --schedule-radii " +
      schedule_csv +
      " --max-atoms 50000000 "
      "--target-a 376039 --target-b 943460 "
      "--manifest-in k26-prefix-manifest.txt "
      "--prefix-witness-in k26-prefix-witness.txt";
  const std::string bundle_command =
      "run_k26_full_source_bundle.sh --build-dir BUILD_DIR --out-dir OUT_DIR "
      "--continuation-chunk-bands 8 --resume-existing "
      "--timeout-seconds 1200 --max-runtime-seconds 14000 "
      "--source-dead-gap-checker "
      "source_dead_gap_check";
  const std::string checked_bundle_command =
      bundle_command +
      " --cert-in k26-source-dead-cert.json "
      "--source-dead-checker source_dead_cert_check";

  std::cout << "{"
            << "\"schema\":\"lb_source_k26_run_commands_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_K26\","
            << "\"executable_now\":false,"
            << "\"non_claim\":\"command contract only; no sqrt(26) source/origin run executed\","
            << "\"build\":{\"required_k_sq\":26,\"cmake_define\":\"-DK_SQ=26\"},"
            << "\"target\":{\"tsuchimura_endpoint\":{\"a\":"
            << kTsuchimuraEndpointA << ",\"b\":" << kTsuchimuraEndpointB
            << ",\"norm_sq\":" << kEndpointNorm
            << "},\"canonical_octant_endpoint\":{\"a\":"
            << kCanonicalEndpointA << ",\"b\":" << kCanonicalEndpointB
            << ",\"norm_sq\":" << kEndpointNorm << "}},"
            << "\"prefix\":{\"runner\":\"source_origin_cpu_runner\","
            << "\"r_final\":" << kPrefixOuter
            << ",\"band_width\":" << kBandWidth
            << ",\"manifest_out\":\"k26-prefix-manifest.txt\","
            << "\"prefix_witness_out\":\"k26-prefix-witness.txt\","
            << "\"command\":";
  emit_json_string(prefix_command);
  std::cout << "},\"continuation\":{\"runner\":\"source_tileop_port_runner\","
            << "\"r_start\":" << kPrefixOuter
            << ",\"r_final\":" << kTerminalRadius
            << ",\"band_width\":" << kBandWidth
            << ",\"schedule_boundary_count\":" << radii.size()
            << ",\"schedule_segment_count\":" << (radii.size() - 1)
            << ",\"schedule_min_width\":" << stats.min_width
            << ",\"schedule_max_width\":" << stats.max_width
            << ",\"schedule_digest_algorithm\":\""
            << lb_source::k26_bz::schedule_digest_algorithm
            << "\",\"schedule_digest_hex\":\"" << bz_schedule_digest << "\""
            << ",\"seam_bridge_policy\":\"diagnostic_allow_unbridged\""
            << ",\"blocked_if_unbridged_coordinate_carry_atoms\":false"
            << ",\"claim_grade_requires_source_unbridged_unsafe_candidate_atoms\":0"
            << ",\"schedule_radii_csv\":";
  emit_json_string(schedule_csv);
  std::cout << ",\"manifest_in\":\"k26-prefix-manifest.txt\","
            << "\"prefix_witness_in\":\"k26-prefix-witness.txt\","
            << "\"target\":{\"a\":" << kCanonicalEndpointA
            << ",\"b\":" << kCanonicalEndpointB
            << ",\"norm_sq\":" << kEndpointNorm << "},"
            << "\"recommended_chunk_bands\":" << kRecommendedChunkBands
            << ",\"resume_existing_supported\":true,"
            << "\"resume_existing_flag\":\"--resume-existing\","
            << "\"command\":";
  emit_json_string(continuation_command);
  std::cout << "},\"bundle_harness\":{\"runner\":\"run_k26_full_source_bundle.sh\","
            << "\"recommended_continuation_chunk_bands\":"
            << kRecommendedChunkBands
            << ",\"resume_existing_supported\":true,"
            << "\"chunk_ledger\":\"k26-continuation-chunks.jsonl\","
            << "\"chunk_ledger_required_for_checked_bundle\":true,"
            << "\"source_dead_gap_checker\":\"source_dead_gap_check\","
            << "\"source_dead_checker\":\"source_dead_cert_check\","
            << "\"timeout_seconds\":1200,"
            << "\"max_runtime_seconds\":14000,"
            << "\"command\":";
  emit_json_string(bundle_command);
  std::cout << ",\"checked_bundle_command\":";
  emit_json_string(checked_bundle_command);
  std::cout << "},\"repaired_boundaries\":["
            << "{\"nominal\":122880,\"repaired\":122879},"
            << "{\"nominal\":475136,\"repaired\":475135},"
            << "{\"nominal\":622592,\"repaired\":622591}],"
            << "\"acceptance_note\":\"Commands are a reproducible run contract only; SOURCE_DEAD_CERT remains blocked until the full run artifact, BZ evidence binding, seam-bridge rule, and terminal inventory verifier all pass.\""
            << "}\n";
  return EXIT_SUCCESS;
}
