#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "campaign/constants.h"

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
          .k_sq = 36,
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

void test_port_graph_rejects_port_count_mismatch() {
  campaign::TileOp lower{};
  add_port(lower, campaign::Face::O, 1);
  campaign::TileOp upper{};

  const lb_source::TileOpPortGraphResult graph =
      lb_source::make_tileop_port_band({
          .k_sq = 36,
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
          .k_sq = 36,
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
  run("port_graph_rejects_port_count_mismatch",
      test_port_graph_rejects_port_count_mismatch);
  run("port_graph_overflow_forces_source_reject",
      test_port_graph_overflow_forces_source_reject);

  if (g_failures != 0) {
    std::cerr << g_failures << " test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
