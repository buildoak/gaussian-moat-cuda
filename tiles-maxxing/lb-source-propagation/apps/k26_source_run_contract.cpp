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

  constexpr const char* kRequiredEvidence[] = {
      "source/origin seed rule: Omega attaches only to Gaussian primes with norm_sq <= 26",
      "stable coordinate or canonical-port carry atoms; no transient TileOp group labels",
      "per-band H_i manifests: carry_atoms + component_partition + source_bit_per_component",
      "source terminal inventory with count, digest, max norm, and max-coordinate tie set",
      "positive source chain to 943460+376039i",
      "negative final guard proof at R_final >= 1015645 under conservative K26 shell",
      "zero overflow across all source/origin proof rows",
      "external accepted K26 BZ evidence bound into profile metadata",
      "commit, build, run profile, and artifact hashes"};

  constexpr const char* kBlockingGaps[] = {
      "CPU TileOp-fed diagnostic runner exists, but no full-scale K26 source runner feeds the sidecar from campaign TileOps",
      "current production compositors compute ANY-SPAN/ANY-SHELL-MOAT, not SOURCE_ORIGIN_K26",
      "sidecar inventory count/digest/max-norm/tie-set checks exist, but endpoint-chain and inventory handling are not accepted at 14.5B scale",
      "K26 non-square BZ evidence is preflight-only in this branch",
      "independent SOURCE_DEAD_CERT draft checker exists, but no full-scale K26 certificate artifact passes it"};

  std::cout << "{"
            << "\"schema\":\"lb_source_k26_run_contract_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_K26\","
            << "\"executable_now\":false,"
            << "\"non_claim\":\"execution contract only; no source/origin run executed\","
            << "\"target\":{\"endpoint\":{\"a\":" << kEndpointA
            << ",\"b\":" << kEndpointB
            << ",\"norm_sq\":" << kEndpointNorm
            << "},\"expected_component_size\":" << kExpectedComponentSize
            << "},"
            << "\"geometry\":{\"k_sq\":" << kSq
            << ",\"ceil_sqrt_k\":" << lb_source::ceil_sqrt(kSq)
            << ",\"exact_guard_min_r_final\":1015644,"
            << "\"conservative_guard_min_r_final\":"
            << kConservativeTerminalRadius
            << "},"
            << "\"band_schedule_hint\":{\"preferred_band_width\":"
            << kPreferredBandWidth << ",\"outer_radius_start\":"
            << kPreferredBandWidth << ",\"outer_radius_final\":"
            << kConservativeTerminalRadius << ",\"band_count\":"
            << band_count_for(kConservativeTerminalRadius, kPreferredBandWidth)
            << ",\"last_band_width\":"
            << (kConservativeTerminalRadius % kPreferredBandWidth) << "},";
  emit_string_array("required_evidence", kRequiredEvidence,
                    sizeof(kRequiredEvidence) / sizeof(kRequiredEvidence[0]));
  std::cout << ",";
  emit_string_array("blocking_gaps", kBlockingGaps,
                    sizeof(kBlockingGaps) / sizeof(kBlockingGaps[0]));
  std::cout << "}\n";
  return EXIT_SUCCESS;
}
