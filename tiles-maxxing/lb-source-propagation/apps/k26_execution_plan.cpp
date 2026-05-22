#include "lb_source/source_propagation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

constexpr std::uint64_t kSq = 26;
constexpr std::uint64_t kEndpointA = 943460;
constexpr std::uint64_t kEndpointB = 376039;
constexpr std::uint64_t kEndpointNorm = 1031522101121ULL;
constexpr std::uint64_t kExpectedComponentSize = 14542615005ULL;
constexpr std::uint64_t kConservativeTerminalRadius = 1015645;
constexpr std::uint64_t kPreferredBandWidth = 8192;
constexpr double kMaxDollarsPerHour = 0.37;
constexpr double kMaxTotalDollars = 1.50;

std::uint64_t square_u64(std::uint64_t value) {
  const unsigned __int128 square =
      static_cast<unsigned __int128>(value) * value;
  if (square > std::numeric_limits<std::uint64_t>::max()) {
    std::cerr << "square overflow for " << value << "\n";
    std::exit(EXIT_FAILURE);
  }
  return static_cast<std::uint64_t>(square);
}

std::uint64_t endpoint_norm() {
  const unsigned __int128 norm =
      static_cast<unsigned __int128>(kEndpointA) * kEndpointA +
      static_cast<unsigned __int128>(kEndpointB) * kEndpointB;
  if (norm > std::numeric_limits<std::uint64_t>::max()) {
    std::cerr << "endpoint norm overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return static_cast<std::uint64_t>(norm);
}

bool endpoint_in_conservative_guard(std::uint64_t r_final) {
  const std::uint64_t rho = lb_source::ceil_sqrt(kSq);
  const std::uint64_t guard_inner = r_final > rho ? r_final - rho : 0;
  return kEndpointNorm >= square_u64(guard_inner) &&
         kEndpointNorm <= square_u64(r_final);
}

std::uint64_t band_count_for(std::uint64_t terminal_radius,
                             std::uint64_t band_width) {
  return (terminal_radius + band_width - 1) / band_width;
}

void emit_string_array(const char* name, const char* const* values,
                       std::size_t count) {
  std::cout << "\"" << name << "\":[";
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0) {
      std::cout << ",";
    }
    std::cout << "\"" << values[i] << "\"";
  }
  std::cout << "]";
}

void emit_schedule_rows(std::uint64_t terminal_radius,
                        std::uint64_t band_width) {
  std::cout << "\"rows\":[";
  std::uint64_t r_start = 0;
  std::uint64_t row_index = 0;
  while (r_start < terminal_radius) {
    const std::uint64_t remaining = terminal_radius - r_start;
    const std::uint64_t width =
        remaining < band_width ? remaining : band_width;
    const std::uint64_t r_outer = r_start + width;
    if (row_index != 0) {
      std::cout << ",";
    }
    std::cout << "{\"index\":" << row_index
              << ",\"r_start\":" << r_start
              << ",\"r_outer\":" << r_outer
              << ",\"width\":" << width
              << ",\"required_bz\":\"K26_EXTERNAL_BZ_REQUIRED_OR_DIAGNOSTIC\""
              << ",\"overflow_policy\":\"reject_source_row\"}";
    r_start = r_outer;
    ++row_index;
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
  if (!endpoint_in_conservative_guard(kConservativeTerminalRadius - 1)) {
    std::cerr << "endpoint must still be inside conservative guard at R-1\n";
    return EXIT_FAILURE;
  }
  if (endpoint_in_conservative_guard(kConservativeTerminalRadius)) {
    std::cerr << "endpoint must be outside conservative guard at R\n";
    return EXIT_FAILURE;
  }
  if (band_count_for(kConservativeTerminalRadius, kPreferredBandWidth) !=
      124) {
    std::cerr << "unexpected K26 band count\n";
    return EXIT_FAILURE;
  }
  if (kConservativeTerminalRadius % kPreferredBandWidth != 8029) {
    std::cerr << "unexpected K26 final band width\n";
    return EXIT_FAILURE;
  }

  constexpr const char* kPreRunGates[] = {
      "local sidecar ctest 14/14",
      "local independent verification ctest 43/43",
      "remote 4090 smoke with sidecar ctest 14/14 and verification ctest 43/43",
      "accepted K26 external BZ evidence for every source/origin proof row",
      "accepted coordinate-to-port seam bridge theorem or diagnostic label",
      "accepted terminal inventory count/digest/max-norm handling at 14.5B scale"};
  constexpr const char* kCurrentBlockers[] = {
      "remote 4090 smoke has not passed on current head under the active price cap",
      "full-scale K26 source runner is not accepted",
      "K26 external BZ evidence is not bound to run metadata",
      "coordinate-to-port seam bridge remains diagnostic",
      "no full-scale SOURCE_DEAD_CERT artifact exists"};

  std::cout << "{"
            << "\"schema\":\"lb_source_k26_execution_plan_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_K26\","
            << "\"executable_now\":false,"
            << "\"non_claim\":\"execution plan only; no source/origin run executed\","
            << "\"budget_caps\":{\"max_dph_usd\":" << kMaxDollarsPerHour
            << ",\"max_total_usd\":" << kMaxTotalDollars << "},"
            << "\"target\":{\"endpoint\":{\"a\":" << kEndpointA
            << ",\"b\":" << kEndpointB
            << ",\"norm_sq\":" << kEndpointNorm
            << "},\"expected_component_size\":" << kExpectedComponentSize
            << "},"
            << "\"geometry\":{\"k_sq\":" << kSq
            << ",\"ceil_sqrt_k\":" << lb_source::ceil_sqrt(kSq)
            << ",\"conservative_guard_min_r_final\":"
            << kConservativeTerminalRadius
            << ",\"endpoint_outside_conservative_guard\":true},"
            << "\"schedule\":{\"preferred_band_width\":"
            << kPreferredBandWidth << ",\"band_count\":"
            << band_count_for(kConservativeTerminalRadius, kPreferredBandWidth)
            << ",\"last_band_width\":"
            << (kConservativeTerminalRadius % kPreferredBandWidth) << ",";
  emit_schedule_rows(kConservativeTerminalRadius, kPreferredBandWidth);
  std::cout << "},";
  emit_string_array("pre_run_gates", kPreRunGates,
                    sizeof(kPreRunGates) / sizeof(kPreRunGates[0]));
  std::cout << ",";
  emit_string_array("current_blockers", kCurrentBlockers,
                    sizeof(kCurrentBlockers) / sizeof(kCurrentBlockers[0]));
  std::cout << "}\n";
  return EXIT_SUCCESS;
}
