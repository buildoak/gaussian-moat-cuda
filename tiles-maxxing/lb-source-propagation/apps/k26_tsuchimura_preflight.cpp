#include "lb_source/source_propagation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

constexpr std::uint64_t kSq = 26;
constexpr std::uint64_t kEndpointA = 943460;
constexpr std::uint64_t kEndpointB = 376039;
constexpr std::uint64_t kEndpointNorm = 1031522101121ULL;
constexpr std::uint64_t kExpectedComponentSize = 14542615005ULL;

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

std::uint64_t first_conservative_terminal_radius() {
  std::uint64_t r = 0;
  while (square_u64(r) <= kEndpointNorm) {
    ++r;
  }
  return r + lb_source::ceil_sqrt(kSq);
}

}  // namespace

int main() {
  if (endpoint_norm() != kEndpointNorm) {
    std::cerr << "endpoint norm mismatch\n";
    return EXIT_FAILURE;
  }
  const std::uint64_t rho = lb_source::ceil_sqrt(kSq);
  if (rho != 6) {
    std::cerr << "ceil_sqrt(26) must be 6, got " << rho << "\n";
    return EXIT_FAILURE;
  }

  const std::uint64_t conservative_r_final =
      first_conservative_terminal_radius();
  if (conservative_r_final != 1015645) {
    std::cerr << "unexpected conservative terminal radius "
              << conservative_r_final << "\n";
    return EXIT_FAILURE;
  }
  if (!endpoint_in_conservative_guard(1015644)) {
    std::cerr << "expected endpoint to remain in conservative guard at 1015644\n";
    return EXIT_FAILURE;
  }
  if (endpoint_in_conservative_guard(conservative_r_final)) {
    std::cerr << "endpoint still in conservative guard at terminal radius\n";
    return EXIT_FAILURE;
  }

  const lb_source::SourceDraftMetadata metadata{
      .source_mode = "ORIGIN_SOURCE",
      .source_id = "omega",
      .geometry_id = "SOURCE_ORIGIN_K26",
      .commit_id = "pending-run",
      .build_id = "pending-remote-smoke",
      .bz_status = "requires-external-k26-bz",
      .artifact_hash = "pending",
  };
  const lb_source::SourceCertificateDraft draft{
      .certificate_id = "k26-tsuchimura-dead-cert-draft",
      .profile_id = "k26-tsuchimura-source-profile",
      .metadata = metadata,
      .k_sq = kSq,
      .terminal_radius = conservative_r_final,
      .negative_guard_pass = false,
      .endpoint = {static_cast<std::int64_t>(kEndpointA),
                   static_cast<std::int64_t>(kEndpointB),
                   kEndpointNorm},
      .source_path = {},
      .terminal_source_inventory = {},
  };

  std::cout << "{"
            << "\"schema\":\"k26_tsuchimura_preflight_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_K26\","
            << "\"endpoint\":{\"a\":" << kEndpointA
            << ",\"b\":" << kEndpointB
            << ",\"norm_sq\":" << kEndpointNorm << "},"
            << "\"expected_component_size\":" << kExpectedComponentSize
            << ",\"k_sq\":" << kSq
            << ",\"ceil_sqrt_k\":" << rho
            << ",\"exact_guard_min_r_final\":1015644,"
            << "\"conservative_guard_min_r_final\":"
            << conservative_r_final
            << ",\"r1015644_endpoint_in_conservative_guard\":true,"
            << "\"source_dead_cert_draft\":"
            << lb_source::source_certificate_draft_json(draft)
            << ",\"non_claim\":\"preflight only; no source/origin run executed\""
            << "}\n";
  return EXIT_SUCCESS;
}
