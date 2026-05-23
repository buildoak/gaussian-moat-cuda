#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/sieve.h"
#include "campaign/tileop.h"

namespace {

int g_failures = 0;

#define CHECK_TRUE(expr)                                                    \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::cerr << __FILE__ << ":" << __LINE__ << " check failed: " #expr  \
                << "\n";                                                   \
      ++g_failures;                                                         \
      return;                                                               \
    }                                                                       \
  } while (false)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    const auto& actual_value = (a);                                           \
    const auto& expected_value = (b);                                         \
    if (!(actual_value == expected_value)) {                                  \
      std::cerr << __FILE__ << ":" << __LINE__ << " check failed: " #a       \
                << " == " #b << "\n";                                       \
      ++g_failures;                                                          \
      return;                                                                \
    }                                                                        \
  } while (false)

campaign::TileCoord coord(std::int32_t i, std::int32_t j) {
  return campaign::TileCoord{
      .i = i,
      .j = j,
      .a_lo = static_cast<std::int64_t>(campaign::S) * i,
      .b_lo = static_cast<std::int64_t>(campaign::S) * j,
  };
}

void add_port(campaign::TileOp& op, campaign::Face face,
              std::uint8_t label) {
  const int f = static_cast<int>(face);
  const int offset = campaign::face_offset(op, face) + op.n[f];
  op.face_groups[offset] = label;
  ++op.n[f];
}

lb_source::AtomId port_id(std::int32_t i, std::int32_t j,
                          campaign::Face face, std::uint8_t ordinal) {
  const std::optional<lb_source::AtomId> id =
      lb_source::port_atom_id(i, j, static_cast<std::uint64_t>(face),
                              ordinal);
  if (!id.has_value()) {
    std::cerr << "test port id overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return *id;
}

bool component_has_source(const lb_source::SeparatorState& state,
                          lb_source::AtomId id) {
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    const auto& component = state.component_partition[c];
    if (std::find(component.begin(), component.end(), id) !=
        component.end()) {
      return state.source_bit_per_component[c];
    }
  }
  return false;
}

std::vector<lb_source::AtomId> encoded_ports_for_label(
    const campaign::TileCoord& tile_coord,
    const campaign::TileOp& op,
    std::uint8_t label) {
  std::vector<lb_source::AtomId> out;
  for (int face_idx = 0; face_idx < campaign::NUM_FACES; ++face_idx) {
    const campaign::Face face = static_cast<campaign::Face>(face_idx);
    const int offset = campaign::face_offset(op, face);
    for (std::uint8_t ordinal = 0; ordinal < op.n[face_idx]; ++ordinal) {
      if (op.face_groups[offset + ordinal] != label) {
        continue;
      }
      out.push_back(port_id(tile_coord.i, tile_coord.j, face, ordinal));
    }
  }
  return out;
}

struct BridgeFixture {
  campaign::TileCoord tile_coord;
  campaign::CampaignConstants constants;
  campaign::TileOp op;
  campaign::Prime prime;
  std::vector<campaign::Prime> primes;
  lb_source::CoordinatePortBridgeResult bridge;
};

std::optional<BridgeFixture> first_bridgeable_real_tileop() {
  const campaign::CampaignConstants constants =
      campaign::CampaignConstants::from_radii(248, 512,
                                              campaign::k_sq_value);
  const campaign::Grid grid =
      campaign::Grid::build(248, 512, campaign::k_sq_value);
  const std::vector<campaign::TileCoord> coords = grid.enumerate_active_tiles();

  for (const campaign::TileCoord& tile_coord : coords) {
    const campaign::TileOp op =
        campaign::process_tile(tile_coord, constants, grid);
    if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0 ||
        (op.tile_flags & campaign::EMPTY_BIT) != 0) {
      continue;
    }
    const std::vector<campaign::Prime> primes =
        campaign::sieve_tile(tile_coord, constants);

    for (const campaign::Prime& prime : primes) {
      const lb_source::CoordinatePortBridgeResult bridge =
          lb_source::bridge_coordinate_prime_to_ports({
              .coord = tile_coord,
              .constants = constants,
              .tileop = op,
              .target = prime,
              .primes = primes,
          });
      if (bridge.accepted()) {
        return BridgeFixture{tile_coord, constants, op, prime, primes, bridge};
      }
    }
  }
  return std::nullopt;
}

void test_port_graph_reaches_adjacent_tile() {
  campaign::TileOp lower{};
  add_port(lower, campaign::Face::I, 1);
  add_port(lower, campaign::Face::O, 1);
  campaign::bit_set(lower.inner_flags, 1);

  campaign::TileOp upper{};
  add_port(upper, campaign::Face::I, 2);
  add_port(upper, campaign::Face::O, 2);

  const lb_source::TileOpPortGraphResult graph =
      lb_source::make_tileop_port_band({
          .k_sq = campaign::k_sq_value,
          .outer_radius = 573,
          .coords = {coord(0, 0), coord(0, 1)},
          .tileops = {lower, upper},
          .seed_inner_flags = true,
      });
  CHECK_TRUE(graph.accepted());
  CHECK_EQ(graph.port_atoms, static_cast<std::uint64_t>(4));
  CHECK_EQ(graph.internal_edges, static_cast<std::uint64_t>(2));
  CHECK_EQ(graph.seam_edges, static_cast<std::uint64_t>(1));
  CHECK_TRUE(!graph.band.force_overflow);

  const lb_source::ProcessResult result =
      lb_source::process_band(graph.band, std::nullopt);
  CHECK_TRUE(result.accepted());
  CHECK_TRUE(component_has_source(
      result.outgoing, port_id(0, 1, campaign::Face::O, 0)));
}

void test_port_graph_carries_outer_support_overshoot() {
  campaign::TileOp lower{};
  add_port(lower, campaign::Face::I, 1);
  add_port(lower, campaign::Face::O, 1);
  campaign::bit_set(lower.inner_flags, 1);

  campaign::TileOp upper{};
  add_port(upper, campaign::Face::I, 2);
  add_port(upper, campaign::Face::O, 2);

  const lb_source::TileOpPortGraphResult graph =
      lb_source::make_tileop_port_band({
          .k_sq = campaign::k_sq_value,
          .outer_radius = 512,
          .coords = {coord(0, 0), coord(0, 1)},
          .tileops = {lower, upper},
          .seed_inner_flags = true,
      });
  CHECK_TRUE(graph.accepted());

  const lb_source::ProcessResult result =
      lb_source::process_band(graph.band, std::nullopt);
  CHECK_TRUE(result.accepted());
  CHECK_TRUE(!result.terminal_source_dead);
  CHECK_TRUE(component_has_source(
      result.outgoing, port_id(0, 1, campaign::Face::O, 0)));
}

void test_port_graph_rejects_port_count_mismatch() {
  campaign::TileOp lower{};
  add_port(lower, campaign::Face::O, 1);
  campaign::TileOp upper{};

  const lb_source::TileOpPortGraphResult graph =
      lb_source::make_tileop_port_band({
          .k_sq = campaign::k_sq_value,
          .outer_radius = 100,
          .coords = {coord(0, 0), coord(0, 1)},
          .tileops = {lower, upper},
      });
  CHECK_TRUE(!graph.accepted());
  CHECK_EQ(graph.diagnostic, std::string("I/O TileOp port count mismatch"));
}

void test_port_graph_overflow_forces_source_reject() {
  campaign::TileOp overflow{};
  overflow.tile_flags = campaign::OVERFLOW_BIT;

  const lb_source::TileOpPortGraphResult graph =
      lb_source::make_tileop_port_band({
          .k_sq = campaign::k_sq_value,
          .outer_radius = 100,
          .coords = {coord(0, 0)},
          .tileops = {overflow},
      });
  CHECK_TRUE(graph.accepted());
  CHECK_EQ(graph.overflow_tiles, static_cast<std::uint64_t>(1));
  CHECK_TRUE(graph.band.force_overflow);

  const lb_source::ProcessResult result =
      lb_source::process_band(graph.band, std::nullopt);
  CHECK_EQ(result.reject, lb_source::RejectReason::kOverflow);
}

void test_coordinate_bridge_matches_real_tileop_ports() {
  const std::optional<BridgeFixture> fixture = first_bridgeable_real_tileop();
  CHECK_TRUE(fixture.has_value());
  CHECK_TRUE(!fixture->bridge.port_atoms.empty());
  CHECK_EQ(fixture->bridge.port_expansions.size(),
           fixture->bridge.port_atoms.size());
  CHECK_TRUE(fixture->bridge.tileop_label != 0);
  CHECK_EQ(fixture->bridge.port_atoms,
           encoded_ports_for_label(fixture->tile_coord, fixture->op,
                                   fixture->bridge.tileop_label));
  for (const lb_source::AtomId atom : fixture->bridge.port_atoms) {
    const std::optional<lb_source::PortAtom> decoded =
        lb_source::decode_port_atom_id(atom);
    CHECK_TRUE(decoded.has_value());
    CHECK_EQ(decoded->tile_i, fixture->tile_coord.i);
    CHECK_EQ(decoded->tile_j, fixture->tile_coord.j);
  }
  for (const auto& expansion : fixture->bridge.port_expansions) {
    CHECK_TRUE(std::find(fixture->bridge.port_atoms.begin(),
                         fixture->bridge.port_atoms.end(),
                         expansion.port_atom) !=
               fixture->bridge.port_atoms.end());
    CHECK_TRUE(!expansion.path.empty());
    CHECK_EQ(expansion.path.front().a, fixture->prime.a);
    CHECK_EQ(expansion.path.front().b, fixture->prime.b);
    CHECK_EQ(expansion.path.front().norm_sq, fixture->prime.norm_sq);
    CHECK_EQ(expansion.tileop_label, fixture->bridge.tileop_label);
    for (std::size_t i = 1; i < expansion.path.size(); ++i) {
      const __int128 da = static_cast<__int128>(expansion.path[i].a) -
                          expansion.path[i - 1].a;
      const __int128 db = static_cast<__int128>(expansion.path[i].b) -
                          expansion.path[i - 1].b;
      CHECK_TRUE(da * da + db * db <=
                 static_cast<__int128>(campaign::k_sq_value));
    }
  }
}

void test_coordinate_bridge_rejects_missing_prime() {
  const campaign::CampaignConstants constants =
      campaign::CampaignConstants::from_radii(248, 512,
                                              campaign::k_sq_value);
  const campaign::Grid grid =
      campaign::Grid::build(248, 512, campaign::k_sq_value);
  const campaign::TileCoord tile_coord = grid.enumerate_active_tiles().front();
  const campaign::TileOp op =
      campaign::process_tile(tile_coord, constants, grid);
  const std::vector<campaign::Prime> primes =
      campaign::sieve_tile(tile_coord, constants);

  const lb_source::CoordinatePortBridgeResult bridge =
      lb_source::bridge_coordinate_prime_to_ports({
          .coord = tile_coord,
          .constants = constants,
          .tileop = op,
          .target = campaign::Prime{.a = 1, .b = 1, .norm_sq = 2},
          .primes = primes,
      });
  CHECK_TRUE(!bridge.accepted());
  CHECK_EQ(bridge.diagnostic, std::string("target prime not found in tile sieve"));
}

void test_coordinate_bridge_rejects_stale_tileop() {
  const std::optional<BridgeFixture> fixture = first_bridgeable_real_tileop();
  CHECK_TRUE(fixture.has_value());

  campaign::TileOp stale = fixture->op;
  stale.reserved[0] = 1;
  const lb_source::CoordinatePortBridgeResult bridge =
      lb_source::bridge_coordinate_prime_to_ports({
          .coord = fixture->tile_coord,
          .constants = fixture->constants,
          .tileop = stale,
          .target = fixture->prime,
          .primes = fixture->primes,
      });
  CHECK_TRUE(!bridge.accepted());
  CHECK_EQ(bridge.diagnostic,
           std::string("TileOp bytes do not match coord/constants/primes"));
}

void run(const char* name, void (*fn)()) {
  const int before = g_failures;
  fn();
  if (g_failures == before) {
    std::cout << "PASS " << name << "\n";
  }
}

}  // namespace

int main() {
  run("port_graph_reaches_adjacent_tile",
      test_port_graph_reaches_adjacent_tile);
  run("port_graph_carries_outer_support_overshoot",
      test_port_graph_carries_outer_support_overshoot);
  run("port_graph_rejects_port_count_mismatch",
      test_port_graph_rejects_port_count_mismatch);
  run("port_graph_overflow_forces_source_reject",
      test_port_graph_overflow_forces_source_reject);
  run("coordinate_bridge_matches_real_tileop_ports",
      test_coordinate_bridge_matches_real_tileop_ports);
  run("coordinate_bridge_rejects_missing_prime",
      test_coordinate_bridge_rejects_missing_prime);
  run("coordinate_bridge_rejects_stale_tileop",
      test_coordinate_bridge_rejects_stale_tileop);

  if (g_failures != 0) {
    std::cerr << g_failures << " test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
