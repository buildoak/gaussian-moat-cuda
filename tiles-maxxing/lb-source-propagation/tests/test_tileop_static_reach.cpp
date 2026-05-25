#include "lb_source/tileop_static_reach.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "campaign/constants.h"
#include "campaign/grid.h"
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

lb_source::AtomId coordinate_id(std::int64_t a, std::int64_t b) {
  const std::optional<lb_source::AtomId> id =
      lb_source::coordinate_atom_id(a, b);
  if (!id.has_value()) {
    std::cerr << "test coordinate atom id overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return *id;
}

bool separator_has_reach(const lb_source::StaticReachSeparator& state,
                         std::vector<lb_source::AtomId> atoms,
                         std::uint8_t reach) {
  std::sort(atoms.begin(), atoms.end());
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    if (state.component_partition[c] == atoms &&
        state.reach_per_component[c] == reach) {
      return true;
    }
  }
  return false;
}

void test_stitched_static_reach_equals_one_big_band() {
  const lb_source::AtomId a = coordinate_id(3, 4);
  const lb_source::AtomId b = coordinate_id(6, 8);
  const lb_source::AtomId c = coordinate_id(5, 12);
  const lb_source::AtomId d = coordinate_id(9, 12);
  const std::vector<lb_source::StaticReachBandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{a, 25, lb_source::kStaticReachInner, false},
                 {b, 100, 0, false}},
       .edges = {{a, b}}},
      {.k_sq = 36,
       .outer_radius = 15,
       .atoms = {{c, 169, 0, false},
                 {d, 225, lb_source::kStaticReachOuter, false}},
       .edges = {{b, c}, {c, d}}},
  };
  const lb_source::StaticReachBandInput big{
      .k_sq = 36,
      .outer_radius = 15,
      .atoms = {{a, 25, lb_source::kStaticReachInner, false},
                {b, 100, 0, false},
                {c, 169, 0, false},
                {d, 225, lb_source::kStaticReachOuter, false}},
      .edges = {{a, b}, {b, c}, {c, d}},
  };

  const lb_source::StaticReachProcessResult stitched =
      lb_source::process_static_reach_bands(bands);
  const lb_source::StaticReachProcessResult one_big =
      lb_source::process_static_reach_band(big);
  CHECK_TRUE(stitched.accepted());
  CHECK_TRUE(one_big.accepted());
  CHECK_EQ(stitched.spanning, one_big.spanning);
  CHECK_TRUE(stitched.spanning);
}

void test_neutral_carry_does_not_invent_span() {
  const lb_source::AtomId a = coordinate_id(3, 4);
  const lb_source::AtomId b = coordinate_id(6, 8);
  const lb_source::AtomId c = coordinate_id(0, 10);
  const lb_source::AtomId d = coordinate_id(9, 12);
  const std::vector<lb_source::StaticReachBandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{a, 25, lb_source::kStaticReachInner, false},
                 {b, 100, 0, false},
                 {c, 100, 0, false}},
       .edges = {{a, b}}},
      {.k_sq = 36,
       .outer_radius = 15,
       .atoms = {{d, 225, lb_source::kStaticReachOuter, false}},
       .edges = {{c, d}}},
  };
  const lb_source::StaticReachBandInput big{
      .k_sq = 36,
      .outer_radius = 15,
      .atoms = {{a, 25, lb_source::kStaticReachInner, false},
                {b, 100, 0, false},
                {c, 100, 0, false},
                {d, 225, lb_source::kStaticReachOuter, false}},
      .edges = {{a, b}, {c, d}},
  };

  const lb_source::StaticReachProcessResult stitched =
      lb_source::process_static_reach_bands(bands);
  const lb_source::StaticReachProcessResult one_big =
      lb_source::process_static_reach_band(big);
  CHECK_TRUE(stitched.accepted());
  CHECK_TRUE(one_big.accepted());
  CHECK_EQ(stitched.spanning, one_big.spanning);
  CHECK_TRUE(!stitched.spanning);
  CHECK_TRUE(separator_has_reach(stitched.outgoing, {c, d},
                                 lb_source::kStaticReachOuter));
}

void test_tileop_flags_drive_two_bit_reach() {
  campaign::TileOp lower{};
  add_port(lower, campaign::Face::I, 1);
  add_port(lower, campaign::Face::O, 1);
  campaign::bit_set(lower.inner_flags, 1);

  campaign::TileOp upper{};
  add_port(upper, campaign::Face::I, 2);
  add_port(upper, campaign::Face::O, 2);
  campaign::bit_set(upper.outer_flags, 2);

  const lb_source::TileOpStaticReachMicrobandResult microband =
      lb_source::build_tileop_static_reach_microband({
          .k_sq = campaign::k_sq_value,
          .outer_radius = 573,
          .coords = {coord(0, 0), coord(0, 1)},
          .tileops = {lower, upper},
      });
  CHECK_TRUE(microband.accepted());
  CHECK_EQ(microband.port_atoms, static_cast<std::uint64_t>(4));
  CHECK_EQ(microband.internal_edges, static_cast<std::uint64_t>(2));
  CHECK_EQ(microband.seam_edges, static_cast<std::uint64_t>(1));
  CHECK_EQ(microband.inner_seed_ports, static_cast<std::uint64_t>(2));
  CHECK_EQ(microband.outer_seed_ports, static_cast<std::uint64_t>(2));

  const lb_source::StaticReachProcessResult result =
      lb_source::process_static_reach_band(microband.band);
  CHECK_TRUE(result.accepted());
  CHECK_TRUE(result.spanning);
}

void test_duplicate_boundary_tile_carries_reach_across_microbands() {
  campaign::TileOp first{};
  add_port(first, campaign::Face::I, 1);
  campaign::bit_set(first.inner_flags, 1);

  campaign::TileOp second{};
  add_port(second, campaign::Face::I, 1);
  campaign::bit_set(second.outer_flags, 1);

  const lb_source::TileOpStaticReachMicrobandResult a =
      lb_source::build_tileop_static_reach_microband({
          .k_sq = campaign::k_sq_value,
          .outer_radius = 362,
          .coords = {coord(0, 0)},
          .tileops = {first},
      });
  CHECK_TRUE(a.accepted());
  const lb_source::StaticReachProcessResult first_result =
      lb_source::process_static_reach_band(a.band);
  CHECK_TRUE(first_result.accepted());
  CHECK_TRUE(!first_result.spanning);

  const lb_source::TileOpStaticReachMicrobandResult b =
      lb_source::build_tileop_static_reach_microband({
          .k_sq = campaign::k_sq_value,
          .outer_radius = 368,
          .coords = {coord(0, 0)},
          .tileops = {second},
      });
  CHECK_TRUE(b.accepted());
  const lb_source::StaticReachProcessResult stitched =
      lb_source::process_static_reach_band(b.band, first_result.outgoing);
  CHECK_TRUE(stitched.accepted());
  CHECK_TRUE(stitched.spanning);
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
  run("stitched_static_reach_equals_one_big_band",
      test_stitched_static_reach_equals_one_big_band);
  run("neutral_carry_does_not_invent_span",
      test_neutral_carry_does_not_invent_span);
  run("tileop_flags_drive_two_bit_reach",
      test_tileop_flags_drive_two_bit_reach);
  run("duplicate_boundary_tile_carries_reach_across_microbands",
      test_duplicate_boundary_tile_carries_reach_across_microbands);

  if (g_failures != 0) {
    std::cerr << g_failures << " test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
