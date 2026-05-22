#include "lb_source/source_propagation.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/sieve.h"
#include "campaign/tileop.h"

namespace {

constexpr std::uint64_t kAxisPrime = 251;
constexpr std::uint64_t kRinner = 248;
constexpr std::uint64_t kRouter = 254;

bool proper_contains(const campaign::TileCoord& coord, std::int64_t a,
                     std::int64_t b) {
  return coord.a_lo <= a && a <= coord.a_lo + campaign::S &&
         coord.b_lo <= b && b <= coord.b_lo + campaign::S;
}

std::optional<campaign::TileCoord> find_axis_owner(
    const campaign::Grid& grid) {
  for (const campaign::TileCoord& coord : grid.enumerate_column_tiles(0)) {
    if (proper_contains(coord, 0, static_cast<std::int64_t>(kAxisPrime))) {
      return coord;
    }
  }
  return std::nullopt;
}

lb_source::AtomId atom_id(std::int64_t a, std::int64_t b) {
  return (a << 32) ^ b;
}

std::uint64_t dist_sq(const campaign::Prime& lhs,
                      const campaign::Prime& rhs) {
  const __int128 da = static_cast<__int128>(lhs.a) - rhs.a;
  const __int128 db = static_cast<__int128>(lhs.b) - rhs.b;
  return static_cast<std::uint64_t>(da * da + db * db);
}

bool has_source_component(const lb_source::SeparatorState& state) {
  return std::find(state.source_bit_per_component.begin(),
                   state.source_bit_per_component.end(),
                   true) != state.source_bit_per_component.end();
}

}  // namespace

int main() {
  if (campaign::k_sq_value != 36) {
    std::cerr << "source_prop_cpu_tileop_smoke expects -DK_SQ=36\n";
    return EXIT_FAILURE;
  }

  const auto constants = campaign::CampaignConstants::from_radii(
      kRinner, kRouter, campaign::k_sq_value);
  const campaign::Grid grid =
      campaign::Grid::build(kRinner, kRouter, campaign::k_sq_value);
  const std::string invariant_error = grid.verify_invariants();
  if (!invariant_error.empty()) {
    std::cerr << "grid invariant failed: " << invariant_error << "\n";
    return EXIT_FAILURE;
  }

  const std::optional<campaign::TileCoord> owner = find_axis_owner(grid);
  if (!owner.has_value()) {
    std::cerr << "axis prime owner tile not found\n";
    return EXIT_FAILURE;
  }

  const campaign::TileOp op = campaign::process_tile(*owner, constants, grid);
  if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
    std::cerr << "CPU TileOp producer emitted overflow\n";
    return EXIT_FAILURE;
  }
  if ((op.tile_flags & campaign::EMPTY_BIT) != 0) {
    std::cerr << "CPU TileOp producer emitted empty tile\n";
    return EXIT_FAILURE;
  }

  const std::vector<campaign::Prime> primes =
      campaign::sieve_tile(*owner, constants);
  const auto axis_it = std::find_if(
      primes.begin(), primes.end(), [](const campaign::Prime& prime) {
        return prime.a == 0 &&
               prime.b == static_cast<std::int64_t>(kAxisPrime);
      });
  if (axis_it == primes.end()) {
    std::cerr << "sieve did not emit certified axis seed\n";
    return EXIT_FAILURE;
  }

  lb_source::BandInput band;
  band.k_sq = campaign::k_sq_value;
  band.outer_radius = kRouter;
  band.atoms.reserve(primes.size());
  for (const campaign::Prime& prime : primes) {
    band.atoms.push_back(lb_source::BandAtom{
        atom_id(prime.a, prime.b), prime.norm_sq, false});
  }
  for (std::size_t i = 0; i < primes.size(); ++i) {
    for (std::size_t j = i + 1; j < primes.size(); ++j) {
      if (dist_sq(primes[i], primes[j]) <= campaign::k_sq_value) {
        band.edges.push_back(
            {atom_id(primes[i].a, primes[i].b),
             atom_id(primes[j].a, primes[j].b)});
      }
    }
  }

  const lb_source::SourceSeedApplyResult seed =
      lb_source::apply_source_seeds(
          band, {{"ORIGIN_SOURCE", "omega-axis-prime-251",
                  atom_id(axis_it->a, axis_it->b)}});
  if (!seed.accepted() || seed.applied != 1) {
    std::cerr << "source seed provider failed: " << seed.diagnostic << "\n";
    return EXIT_FAILURE;
  }

  const lb_source::ProcessResult result =
      lb_source::process_band(band, std::nullopt);
  if (!result.accepted()) {
    std::cerr << "source propagation rejected: "
              << lb_source::reject_reason_name(result.reject) << ": "
              << result.diagnostic << "\n";
    return EXIT_FAILURE;
  }
  if (result.terminal_source_dead || !has_source_component(result.outgoing)) {
    std::cerr << "certified source did not survive into carry window\n";
    return EXIT_FAILURE;
  }

  const lb_source::CarryManifest manifest =
      lb_source::make_carry_manifest(band.k_sq, band.outer_radius, result);
  const lb_source::CarryManifestReadResult decoded =
      lb_source::carry_manifest_from_string(
          lb_source::carry_manifest_to_string(manifest));
  if (!decoded.accepted() || !(decoded.manifest == manifest)) {
    std::cerr << "carry manifest round trip failed: " << decoded.diagnostic
              << "\n";
    return EXIT_FAILURE;
  }

  const lb_source::SourceProfileDraft profile{
      .profile_id = "cpu-tileop-smoke-axis-251",
      .metadata = {.source_mode = "ORIGIN_SOURCE",
                   .source_id = "omega-axis-prime-251",
                   .geometry_id = "axis-fixture-r248-r254",
                   .commit_id = "local-smoke",
                   .build_id = "sidecar-cmake",
                   .bz_status = "diagnostic-smoke",
                   .artifact_hash = "manifest-roundtrip"},
      .carry_manifest = manifest,
      .reject = result.reject,
      .diagnostic = result.diagnostic,
      .terminal_source_dead = result.terminal_source_dead,
      .terminal_source_inventory = result.terminal_source_inventory,
  };
  const std::string profile_json =
      lb_source::source_profile_draft_json(profile);
  if (profile_json.find("\"schema\":\"lb_source_profile_draft_v1\"") ==
      std::string::npos) {
    std::cerr << "profile draft JSON missing schema\n";
    return EXIT_FAILURE;
  }

  std::cout << "source_prop_cpu_tileop_smoke PASS primes=" << primes.size()
            << " edges=" << band.edges.size()
            << " carry_atoms=" << result.outgoing.carry_atoms.size()
            << "\n";
  return EXIT_SUCCESS;
}
