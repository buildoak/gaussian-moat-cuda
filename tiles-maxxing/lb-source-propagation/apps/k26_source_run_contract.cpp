#include "lb_source/source_propagation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

constexpr std::uint64_t kSq = 26;
constexpr std::uint64_t kTsuchimuraEndpointA = 943460;
constexpr std::uint64_t kTsuchimuraEndpointB = 376039;
constexpr std::uint64_t kCanonicalEndpointA = 376039;
constexpr std::uint64_t kCanonicalEndpointB = 943460;
constexpr std::uint64_t kEndpointNorm = 1031522101121ULL;
constexpr std::uint64_t kEndpointAtomId = 1615075207964004ULL;
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
      "coordinate-to-port seam bridge report: bridged/unbridged carry, next-band candidate counters, source-only candidate counters, bridged port atoms, and bridge edges",
      "positive source chain to canonical octant representative 376039+943460i, symmetry-equivalent to Tsuchimura endpoint 943460+376039i",
      "negative final guard proof at R_final >= 1015645 under conservative K26 shell",
      "zero overflow across all source/origin proof rows",
      "accepted K26 repaired BZ schedule evidence digest bound into profile metadata",
      "commit, build, run profile, and artifact hashes"};

  constexpr const char* kBlockingGaps[] = {
      "full-run K26 bundle harness exists, but no accepted budgeted K26 source/origin execution has completed on the repaired schedule",
      "source_tileop_port_runner supports explicit variable boundaries and the harness wires rows 1..123 with chunk/resume support, but the repaired schedule has not produced accepted full-run artifacts",
      "current production compositors compute ANY-SPAN/ANY-SHELL-MOAT, not SOURCE_ORIGIN_K26",
      "diagnostic origin-prefix-to-port bridge exists; the tiny smoke has source_unbridged_with_next_band_candidates=0, but K26 row 0 to row 1 currently reports source_unbridged_with_next_band_candidates=57",
      "no accepted seam-bridge theorem or bridge-completeness fix yet proves the K26 source-connected candidate gaps are closed",
      "sidecar inventory count/digest/max-norm/tie-set checks exist, but endpoint-chain and inventory handling are not accepted at 14.5B scale",
      "exact K26 BZ schedule evidence has a stable digest, but no executed source/origin run has bound it into full-run metadata",
      "independent SOURCE_DEAD_CERT draft checker exists, but no full-scale K26 certificate artifact passes it"};

  std::cout << "{"
            << "\"schema\":\"lb_source_k26_run_contract_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_K26\","
            << "\"executable_now\":false,"
            << "\"non_claim\":\"execution contract only; no source/origin run executed\","
            << "\"target\":{\"tsuchimura_endpoint\":{\"a\":"
            << kTsuchimuraEndpointA << ",\"b\":" << kTsuchimuraEndpointB
            << ",\"norm_sq\":" << kEndpointNorm
            << "},\"canonical_octant_endpoint\":{\"a\":"
            << kCanonicalEndpointA << ",\"b\":" << kCanonicalEndpointB
            << ",\"norm_sq\":" << kEndpointNorm
            << "},\"expected_component_size\":" << kExpectedComponentSize
            << "},"
            << "\"geometry\":{\"k_sq\":" << kSq
            << ",\"ceil_sqrt_k\":" << lb_source::ceil_sqrt(kSq)
            << ",\"exact_guard_min_r_final\":1015644,"
            << "\"conservative_guard_min_r_final\":"
            << kConservativeTerminalRadius
            << "},"
            << "\"source_dead_cert_claim_gate\":{"
            << "\"terminal_source_inventory_mode\":\"claim_grade_accumulator\","
            << "\"terminal_source_inventory_accumulator_mode\":\"claim_grade_digest_accumulator\","
            << "\"digest_algorithm\":\"sha256:lb_source_inventory_v1\","
            << "\"required_true_flags\":[\"complete_stream_observed\",\"canonical_order\",\"duplicate_free\",\"retired_component_finalized\",\"overflow_checked\"],"
            << "\"expected_count\":" << kExpectedComponentSize
            << ",\"expected_max_norm_sq\":" << kEndpointNorm
            << ",\"expected_max_norm_atom_ids\":[" << kEndpointAtomId << "]"
            << "},"
            << "\"band_schedule_hint\":{\"preferred_band_width\":"
            << kPreferredBandWidth
            << ",\"bz_schedule\":\"repaired\","
            << "\"repaired_boundary_count\":3,"
            << "\"max_abs_boundary_shift\":1,"
            << "\"nominal_dirty_row_indices\":[15,58,75],"
            << "\"outer_radius_start\":"
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
