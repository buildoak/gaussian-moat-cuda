#include "lb_source/tileop_port_stream.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/tileop.h"
#include "lb_source/tileop_port_graph.h"

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

lb_source::TileOpPortGraphInput graph_input(
    const lb_source::TileOpPortStreamInput& input) {
  return lb_source::TileOpPortGraphInput{
      .k_sq = input.k_sq,
      .outer_radius = input.outer_radius,
      .coords = input.coords,
      .tileops = input.tileops,
      .seed_inner_flags = input.seed_inner_flags,
  };
}

bool band_equals(const lb_source::BandInput& lhs,
                 const lb_source::BandInput& rhs) {
  const auto atoms_equal = [](const std::vector<lb_source::BandAtom>& a,
                              const std::vector<lb_source::BandAtom>& b) {
    if (a.size() != b.size()) {
      return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (a[i].id != b[i].id || a[i].norm_sq != b[i].norm_sq ||
          a[i].certified_source != b[i].certified_source ||
          a[i].allow_outer_overshoot_carry !=
              b[i].allow_outer_overshoot_carry) {
        return false;
      }
    }
    return true;
  };
  return lhs.k_sq == rhs.k_sq && lhs.outer_radius == rhs.outer_radius &&
         atoms_equal(lhs.atoms, rhs.atoms) && lhs.edges == rhs.edges &&
         lhs.force_overflow == rhs.force_overflow;
}

void check_matches_oracle(const lb_source::TileOpPortStreamInput& input) {
  const lb_source::TileOpPortStreamResult stream =
      lb_source::build_tileop_port_microband(input);
  const lb_source::TileOpPortGraphResult oracle =
      lb_source::make_tileop_port_band(graph_input(input));

  CHECK_EQ(stream.accepted(), oracle.accepted());
  CHECK_EQ(stream.diagnostic, oracle.diagnostic);
  CHECK_EQ(stream.port_atoms, oracle.port_atoms);
  CHECK_EQ(stream.internal_edges, oracle.internal_edges);
  CHECK_EQ(stream.seam_edges, oracle.seam_edges);
  CHECK_EQ(stream.overflow_tiles, oracle.overflow_tiles);
  CHECK_EQ(stream.empty_tiles, oracle.empty_tiles);
  CHECK_TRUE(band_equals(stream.band, oracle.band));
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

void test_stream_matches_oracle_for_adjacent_tiles() {
  campaign::TileOp lower{};
  add_port(lower, campaign::Face::I, 1);
  add_port(lower, campaign::Face::O, 1);
  campaign::bit_set(lower.inner_flags, 1);

  campaign::TileOp upper{};
  add_port(upper, campaign::Face::I, 2);
  add_port(upper, campaign::Face::O, 2);

  check_matches_oracle({
      .k_sq = campaign::k_sq_value,
      .outer_radius = 573,
      .coords = {coord(0, 0), coord(0, 1)},
      .tileops = {lower, upper},
      .seed_inner_flags = true,
  });
}

void test_duplicate_tile_matches_oracle() {
  campaign::TileOp op{};
  add_port(op, campaign::Face::I, 1);

  check_matches_oracle({
      .k_sq = campaign::k_sq_value,
      .outer_radius = 100,
      .coords = {coord(0, 0), coord(0, 0)},
      .tileops = {op, op},
  });
}

void test_port_count_mismatch_matches_oracle() {
  campaign::TileOp lower{};
  add_port(lower, campaign::Face::O, 1);
  campaign::TileOp upper{};

  check_matches_oracle({
      .k_sq = campaign::k_sq_value,
      .outer_radius = 100,
      .coords = {coord(0, 0), coord(0, 1)},
      .tileops = {lower, upper},
  });
}

void test_overflow_tile_matches_oracle() {
  campaign::TileOp overflow{};
  overflow.tile_flags = campaign::OVERFLOW_BIT;

  check_matches_oracle({
      .k_sq = campaign::k_sq_value,
      .outer_radius = 100,
      .coords = {coord(0, 0)},
      .tileops = {overflow},
  });
}

void test_empty_tile_matches_oracle() {
  campaign::TileOp empty{};
  empty.tile_flags = campaign::EMPTY_BIT;

  check_matches_oracle({
      .k_sq = campaign::k_sq_value,
      .outer_radius = 100,
      .coords = {coord(0, 0)},
      .tileops = {empty},
  });
}

void test_face_ordinal_stability() {
  campaign::TileOp op{};
  add_port(op, campaign::Face::I, 1);
  add_port(op, campaign::Face::I, 2);
  add_port(op, campaign::Face::O, 3);
  add_port(op, campaign::Face::L, 4);
  add_port(op, campaign::Face::L, 5);
  add_port(op, campaign::Face::R, 6);

  const campaign::TileCoord tile_coord = coord(3, 5);
  const lb_source::TileOpPortDecodedTile decoded =
      lb_source::decode_tileop_ports(tile_coord, op);
  CHECK_TRUE(decoded.accepted());
  CHECK_EQ(decoded.ports.size(), static_cast<std::size_t>(6));

  const std::vector<std::pair<campaign::Face, std::uint8_t>> expected = {
      {campaign::Face::I, 0}, {campaign::Face::I, 1},
      {campaign::Face::O, 0}, {campaign::Face::L, 0},
      {campaign::Face::L, 1}, {campaign::Face::R, 0},
  };
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK_EQ(decoded.ports[i].face,
             static_cast<std::uint8_t>(expected[i].first));
    CHECK_EQ(decoded.ports[i].ordinal, expected[i].second);
    CHECK_EQ(decoded.ports[i].id,
             port_id(tile_coord.i, tile_coord.j, expected[i].first,
                     expected[i].second));
    const std::optional<lb_source::PortAtom> atom =
        lb_source::decode_port_atom_id(decoded.ports[i].id);
    CHECK_TRUE(atom.has_value());
    CHECK_EQ(atom->tile_i, tile_coord.i);
    CHECK_EQ(atom->tile_j, tile_coord.j);
    CHECK_EQ(atom->face, static_cast<std::uint8_t>(expected[i].first));
    CHECK_EQ(atom->ordinal, expected[i].second);
  }
}

void test_local_label_non_persistence() {
  campaign::TileOp left{};
  add_port(left, campaign::Face::I, 1);
  campaign::TileOp right{};
  add_port(right, campaign::Face::I, 1);

  const lb_source::TileOpPortStreamInput input{
      .k_sq = campaign::k_sq_value,
      .outer_radius = 1000,
      .coords = {coord(0, 0), coord(2, 0)},
      .tileops = {left, right},
  };
  check_matches_oracle(input);

  const lb_source::TileOpPortStreamResult stream =
      lb_source::build_tileop_port_microband(input);
  CHECK_TRUE(stream.accepted());
  CHECK_EQ(stream.band.atoms.size(), static_cast<std::size_t>(2));
  CHECK_TRUE(stream.band.edges.empty());
  CHECK_TRUE(stream.band.atoms[0].id != stream.band.atoms[1].id);
  CHECK_EQ(stream.band.atoms[0].id, port_id(0, 0, campaign::Face::I, 0));
  CHECK_EQ(stream.band.atoms[1].id, port_id(2, 0, campaign::Face::I, 0));
}

void test_seed_inner_flags_parity() {
  campaign::TileOp op{};
  add_port(op, campaign::Face::I, 1);
  add_port(op, campaign::Face::O, 2);
  campaign::bit_set(op.inner_flags, 2);

  const lb_source::TileOpPortStreamInput unseeded{
      .k_sq = campaign::k_sq_value,
      .outer_radius = 100,
      .coords = {coord(0, 0)},
      .tileops = {op},
      .seed_inner_flags = false,
  };
  check_matches_oracle(unseeded);
  const lb_source::TileOpPortStreamResult stream_unseeded =
      lb_source::build_tileop_port_microband(unseeded);
  CHECK_TRUE(std::none_of(stream_unseeded.band.atoms.begin(),
                          stream_unseeded.band.atoms.end(),
                          [](const lb_source::BandAtom& atom) {
                            return atom.certified_source;
                          }));

  lb_source::TileOpPortStreamInput seeded = unseeded;
  seeded.seed_inner_flags = true;
  check_matches_oracle(seeded);
  const lb_source::TileOpPortStreamResult stream_seeded =
      lb_source::build_tileop_port_microband(seeded);
  CHECK_EQ(stream_seeded.band.atoms.size(), static_cast<std::size_t>(2));
  CHECK_TRUE(!stream_seeded.band.atoms[0].certified_source);
  CHECK_TRUE(stream_seeded.band.atoms[1].certified_source);
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
  run("stream_matches_oracle_for_adjacent_tiles",
      test_stream_matches_oracle_for_adjacent_tiles);
  run("duplicate_tile_matches_oracle", test_duplicate_tile_matches_oracle);
  run("port_count_mismatch_matches_oracle",
      test_port_count_mismatch_matches_oracle);
  run("overflow_tile_matches_oracle", test_overflow_tile_matches_oracle);
  run("empty_tile_matches_oracle", test_empty_tile_matches_oracle);
  run("face_ordinal_stability", test_face_ordinal_stability);
  run("local_label_non_persistence", test_local_label_non_persistence);
  run("seed_inner_flags_parity", test_seed_inner_flags_parity);

  if (g_failures != 0) {
    std::cerr << g_failures << " test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
