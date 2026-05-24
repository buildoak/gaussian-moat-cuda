#include "lb_source/source_propagation.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using lb_source::AtomId;
using lb_source::BandAtom;
using lb_source::BandInput;
using lb_source::CarryAtom;
using lb_source::LiveHandoffExpectedContext;
using lb_source::LiveHandoffV1;
using lb_source::LiveProcessResult;
using lb_source::LiveSeparator;
using lb_source::LastBandReachabilitySummaryV1;
using lb_source::LastBandSummaryApplyResult;
using lb_source::LastBandComponentSummaryV1;
using lb_source::ProcessResult;
using lb_source::RejectReason;
using lb_source::SeparatorState;

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

std::string separator_debug(const SeparatorState& state) {
  std::ostringstream out;
  out << "{";
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    if (c != 0) {
      out << ",";
    }
    out << (state.source_bit_per_component[c] ? "S" : "N") << "[";
    for (std::size_t i = 0; i < state.component_partition[c].size(); ++i) {
      if (i != 0) {
        out << ",";
      }
      out << state.component_partition[c][i];
    }
    out << "]";
  }
  out << "}";
  return out.str();
}

void expect_same_separator(const ProcessResult& stitched,
                           const ProcessResult& big) {
  CHECK_TRUE(stitched.accepted());
  CHECK_TRUE(big.accepted());
  if (!(stitched.outgoing == big.outgoing)) {
    std::cerr << "stitched=" << separator_debug(stitched.outgoing)
              << " big=" << separator_debug(big.outgoing) << "\n";
    ++g_failures;
    return;
  }
  CHECK_EQ(stitched.terminal_source_dead, big.terminal_source_dead);
  CHECK_EQ(stitched.terminal_source_inventory,
           big.terminal_source_inventory);
}

void expect_live_projection_matches_legacy(const LiveProcessResult& live,
                                           const ProcessResult& legacy) {
  CHECK_TRUE(live.accepted());
  CHECK_TRUE(legacy.accepted());
  CHECK_EQ(live.carry_width, legacy.carry_width);
  CHECK_EQ(live.outgoing, lb_source::live_separator_from_separator(
                              legacy.outgoing));
  CHECK_EQ(live.terminal_source_dead, legacy.terminal_source_dead);
}

bool component_has_source_bit(const SeparatorState& state,
                              const std::vector<AtomId>& ids,
                              bool source_bit) {
  const SeparatorState canonical = lb_source::canonicalize_separator(state);
  for (std::size_t c = 0; c < canonical.component_partition.size(); ++c) {
    if (canonical.component_partition[c] == ids &&
        canonical.source_bit_per_component[c] == source_bit) {
      return true;
    }
  }
  return false;
}

void test_one_band_identity() {
  const BandInput band{
      .k_sq = 36,
      .outer_radius = 12,
      .atoms = {{1, 25, true}, {2, 64, false}, {3, 144, false}},
      .edges = {{1, 2}, {2, 3}},
  };

  const ProcessResult one = lb_source::process_band(band, std::nullopt);
  const ProcessResult seq = lb_source::process_bands({band});
  expect_same_separator(seq, one);
  CHECK_TRUE(component_has_source_bit(one.outgoing, {2, 3}, true));
}

void test_stitched_bands_equal_one_big_band() {
  const std::vector<BandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}},
       .edges = {{1, 2}}},
      {.k_sq = 36,
       .outer_radius = 15,
       .atoms = {{3, 169, false}, {4, 225, false}},
       .edges = {{2, 3}, {3, 4}}},
  };
  const BandInput big{
      .k_sq = 36,
      .outer_radius = 15,
      .atoms = {{1, 25, true}, {2, 100, false}, {3, 169, false},
                {4, 225, false}},
      .edges = {{1, 2}, {2, 3}, {3, 4}},
  };

  expect_same_separator(lb_source::process_bands(bands),
                        lb_source::process_band(big, std::nullopt));
}

void test_false_weld_does_not_invent_source() {
  const std::vector<BandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}, {9, 100, false}},
       .edges = {{1, 2}}},
      {.k_sq = 36,
       .outer_radius = 20,
       .atoms = {{10, 400, false}},
       .edges = {{9, 10}}},
  };
  const BandInput big{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, true}, {2, 100, false}, {9, 100, false},
                {10, 400, false}},
      .edges = {{1, 2}, {9, 10}},
  };

  const ProcessResult stitched = lb_source::process_bands(bands);
  const ProcessResult one_big = lb_source::process_band(big, std::nullopt);
  expect_same_separator(stitched, one_big);
  CHECK_TRUE(stitched.terminal_source_dead);
  CHECK_TRUE(component_has_source_bit(stitched.outgoing, {10}, false));
}

void test_source_carry_loss_is_prevented_by_partition() {
  const std::vector<BandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}, {3, 100, false}},
       .edges = {{1, 2}}},
      {.k_sq = 36,
       .outer_radius = 20,
       .atoms = {{4, 400, false}},
       .edges = {{2, 3}, {3, 4}}},
  };
  const BandInput big{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, true}, {2, 100, false}, {3, 100, false},
                {4, 400, false}},
      .edges = {{1, 2}, {2, 3}, {3, 4}},
  };

  const ProcessResult stitched = lb_source::process_bands(bands);
  expect_same_separator(stitched, lb_source::process_band(big, std::nullopt));
  CHECK_TRUE(component_has_source_bit(stitched.outgoing, {4}, true));
}

void test_non_source_partition_merge_reaches_source_later() {
  const std::vector<BandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}, {3, 100, false},
                 {4, 100, false}},
       .edges = {{1, 2}, {3, 4}}},
      {.k_sq = 36,
       .outer_radius = 20,
       .atoms = {{5, 400, false}},
       .edges = {{2, 3}, {4, 5}}},
  };
  const BandInput big{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, true}, {2, 100, false}, {3, 100, false},
                {4, 100, false}, {5, 400, false}},
      .edges = {{1, 2}, {3, 4}, {2, 3}, {4, 5}},
  };

  const ProcessResult stitched = lb_source::process_bands(bands);
  expect_same_separator(stitched, lb_source::process_band(big, std::nullopt));
  CHECK_TRUE(component_has_source_bit(stitched.outgoing, {5}, true));
}

void test_terminal_death_inventory() {
  const std::vector<BandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}},
       .edges = {{1, 2}}},
      {.k_sq = 36,
       .outer_radius = 20,
       .atoms = {{9, 400, false}},
       .edges = {}},
  };
  const BandInput big{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, true}, {2, 100, false}, {9, 400, false}},
      .edges = {{1, 2}},
  };

  const ProcessResult stitched = lb_source::process_bands(bands);
  const ProcessResult one_big = lb_source::process_band(big, std::nullopt);
  expect_same_separator(stitched, one_big);
  CHECK_TRUE(stitched.terminal_source_dead);
  CHECK_EQ(stitched.terminal_source_inventory, (std::vector<AtomId>{1, 2}));
}

void test_terminal_inventory_survives_extra_bands() {
  const std::vector<BandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}},
       .edges = {{1, 2}}},
      {.k_sq = 36,
       .outer_radius = 20,
       .atoms = {{9, 400, false}},
       .edges = {}},
      {.k_sq = 36,
       .outer_radius = 30,
       .atoms = {{10, 900, false}},
       .edges = {{9, 10}}},
  };

  const ProcessResult stitched = lb_source::process_bands(bands);
  CHECK_TRUE(stitched.accepted());
  CHECK_TRUE(stitched.terminal_source_dead);
  CHECK_EQ(stitched.terminal_source_inventory, (std::vector<AtomId>{1, 2}));
}

void test_non_source_payload_later_merge_is_in_terminal_inventory() {
  const std::vector<BandInput> bands = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}, {7, 25, false},
                 {8, 100, false}},
       .edges = {{1, 2}, {7, 8}}},
      {.k_sq = 36,
       .outer_radius = 20,
       .atoms = {{3, 400, false}},
       .edges = {{2, 8}, {8, 3}}},
      {.k_sq = 36,
       .outer_radius = 30,
       .atoms = {{9, 900, false}},
       .edges = {}},
  };

  const ProcessResult stitched = lb_source::process_bands(bands);
  CHECK_TRUE(stitched.accepted());
  CHECK_TRUE(stitched.terminal_source_dead);
  CHECK_EQ(stitched.terminal_source_inventory,
           (std::vector<AtomId>{1, 2, 3, 7, 8}));
}

void test_process_band_canonicalizes_unsorted_incoming_inventory() {
  SeparatorState incoming;
  incoming.carry_atoms = {{2, 100}, {3, 100}};
  incoming.component_partition = {{3, 2}};
  incoming.source_bit_per_component = {true};
  incoming.component_inventory = {{3, 1, 2}};

  const BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{4, 400, false}},
      .edges = {{2, 4}},
  };

  const ProcessResult result = lb_source::process_band(band, incoming);
  CHECK_TRUE(result.accepted());
  CHECK_TRUE(component_has_source_bit(result.outgoing, {4}, true));
  CHECK_EQ(result.outgoing.component_inventory,
           (std::vector<std::vector<AtomId>>{{1, 2, 3, 4}}));
}

void test_component_inventory_cap_rejects_before_unbounded_growth() {
  SeparatorState incoming;
  incoming.carry_atoms = {{2, 100}};
  incoming.component_partition = {{2}};
  incoming.source_bit_per_component = {true};
  incoming.component_inventory = {{1, 2, 3}};

  const BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{4, 400, false}},
      .edges = {{2, 4}},
  };
  lb_source::ProcessOptions options;
  options.max_atoms = 16;
  options.max_carry_atoms = 16;
  options.max_components = 16;
  options.max_inventory_atoms = 3;

  const ProcessResult result = lb_source::process_band(band, incoming,
                                                       options);
  CHECK_EQ(result.reject, RejectReason::kOverflow);
  CHECK_EQ(result.diagnostic,
           std::string("component inventory exceeds source cap"));
}

void test_live_projection_matches_legacy_without_inventory() {
  const BandInput first{
      .k_sq = 36,
      .outer_radius = 10,
      .atoms = {{1, 25, true}, {2, 100, false}, {3, 100, false}},
      .edges = {{1, 2}},
  };
  const BandInput second{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{4, 400, false}, {5, 400, false}},
      .edges = {{2, 3}, {3, 4}},
  };

  const ProcessResult legacy_first =
      lb_source::process_band(first, std::nullopt);
  const LiveProcessResult live_first =
      lb_source::process_band_live(first, std::nullopt);
  expect_live_projection_matches_legacy(live_first, legacy_first);

  const ProcessResult legacy_second =
      lb_source::process_band(second, legacy_first.outgoing);
  const LiveProcessResult live_second = lb_source::process_band_live(
      second, lb_source::live_separator_from_separator(legacy_first.outgoing));
  expect_live_projection_matches_legacy(live_second, legacy_second);

  const SeparatorState projected =
      lb_source::separator_from_live_separator(live_second.outgoing);
  CHECK_TRUE(projected.component_inventory.empty());
}

void test_live_empty_inventory_bypasses_inventory_cap() {
  LiveSeparator incoming;
  incoming.carry_atoms = {{2, 100}, {3, 100}};
  incoming.component_partition = {{2, 3}};
  incoming.source_bit_per_component = {true};

  const BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{4, 400, false}},
      .edges = {{3, 4}},
  };
  lb_source::ProcessOptions options;
  options.max_atoms = 16;
  options.max_carry_atoms = 16;
  options.max_components = 16;
  options.max_inventory_atoms = 0;

  const LiveProcessResult live =
      lb_source::process_band_live(band, incoming, options);
  CHECK_TRUE(live.accepted());
  CHECK_EQ(live.outgoing, (LiveSeparator{
                              .carry_atoms = {{4, 400}},
                              .component_partition = {{4}},
                              .source_bit_per_component = {true},
                          }));
  CHECK_TRUE(lb_source::separator_from_live_separator(live.outgoing)
                 .component_inventory.empty());
}

void test_live_still_enforces_non_inventory_caps() {
  LiveSeparator incoming;
  incoming.carry_atoms = {{2, 100}};
  incoming.component_partition = {{2}};
  incoming.source_bit_per_component = {true};

  const BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{4, 400, false}},
      .edges = {{2, 4}},
  };
  lb_source::ProcessOptions options;
  options.max_atoms = 16;
  options.max_carry_atoms = 0;
  options.max_components = 16;
  options.max_inventory_atoms = 1;

  const LiveProcessResult result =
      lb_source::process_band_live(band, incoming, options);
  CHECK_EQ(result.reject, RejectReason::kOverflow);
  CHECK_EQ(result.diagnostic,
           std::string("separator state exceeds source caps"));
}

void test_live_terminal_death_drops_listed_inventory() {
  const BandInput first{
      .k_sq = 36,
      .outer_radius = 10,
      .atoms = {{1, 25, true}, {2, 100, false}},
      .edges = {{1, 2}},
  };
  const BandInput second{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{9, 400, false}},
      .edges = {},
  };

  const ProcessResult legacy_first =
      lb_source::process_band(first, std::nullopt);
  CHECK_TRUE(legacy_first.accepted());
  const ProcessResult legacy_second =
      lb_source::process_band(second, legacy_first.outgoing);
  const LiveProcessResult live_second = lb_source::process_band_live(
      second, lb_source::live_separator_from_separator(legacy_first.outgoing));

  expect_live_projection_matches_legacy(live_second, legacy_second);
  CHECK_TRUE(live_second.terminal_source_dead);
  CHECK_TRUE(lb_source::separator_from_live_separator(live_second.outgoing)
                 .component_inventory.empty());
}

void test_live_rejects_fresh_source_with_incoming_handoff() {
  LiveSeparator incoming;
  incoming.carry_atoms = {{2, 100}};
  incoming.component_partition = {{2}};
  incoming.source_bit_per_component = {true};

  const BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{4, 400, true}},
      .edges = {{2, 4}},
  };

  const LiveProcessResult result =
      lb_source::process_band_live(band, incoming);
  CHECK_EQ(result.reject, RejectReason::kMalformed);
  CHECK_EQ(result.diagnostic,
           std::string(
               "fresh certified source is not allowed with incoming live "
               "handoff"));
}

void test_overflow_reject_is_hard() {
  const BandInput band{
      .k_sq = 36,
      .outer_radius = 10,
      .atoms = {{1, 25, true}, {2, 100, false}, {3, 100, false}},
      .edges = {{1, 2}, {2, 3}},
  };
  lb_source::ProcessOptions options;
  options.max_atoms = 2;

  const ProcessResult result = lb_source::process_band(band, std::nullopt,
                                                       options);
  CHECK_EQ(result.reject, RejectReason::kOverflow);
  CHECK_TRUE(!result.accepted());
  CHECK_TRUE(result.outgoing.carry_atoms.empty());
}

void test_k32_ceil_sqrt_carry_width_is_six() {
  CHECK_EQ(lb_source::ceil_sqrt(0), static_cast<std::uint64_t>(0));
  CHECK_EQ(lb_source::ceil_sqrt(36), static_cast<std::uint64_t>(6));
  CHECK_EQ(lb_source::ceil_sqrt(37), static_cast<std::uint64_t>(7));

  const BandInput band{
      .k_sq = 32,
      .outer_radius = 10,
      .atoms = {{1, 9, true}, {2, 16, false}, {3, 100, false}},
      .edges = {{1, 2}, {2, 3}},
  };
  const ProcessResult result = lb_source::process_band(band, std::nullopt);
  CHECK_TRUE(result.accepted());
  CHECK_EQ(result.carry_width, static_cast<std::uint64_t>(6));
  CHECK_TRUE(component_has_source_bit(result.outgoing, {2, 3}, true));
}

void test_coordinate_atom_ids_are_stable_and_canonical() {
  const std::optional<AtomId> endpoint =
      lb_source::coordinate_atom_id(943460, 376039);
  CHECK_TRUE(endpoint.has_value());

  const std::optional<lb_source::CoordinateAtom> decoded =
      lb_source::decode_coordinate_atom_id(*endpoint);
  CHECK_TRUE(decoded.has_value());
  CHECK_EQ(decoded->a, static_cast<std::int64_t>(943460));
  CHECK_EQ(decoded->b, static_cast<std::int64_t>(376039));
  CHECK_EQ(decoded->norm_sq, static_cast<std::uint64_t>(1031522101121ULL));
  CHECK_EQ(lb_source::coordinate_atom_id(decoded->a, decoded->b), endpoint);

  CHECK_TRUE(!lb_source::coordinate_atom_id(-1, 3).has_value());
  CHECK_TRUE(lb_source::coordinate_atom_id(5, 3).has_value());
  CHECK_TRUE(!lb_source::coordinate_atom_id(0, -1).has_value());
  CHECK_TRUE(!lb_source::decode_coordinate_atom_id(-1).has_value());
}

void test_port_atom_ids_are_stable_and_label_free() {
  const std::optional<AtomId> port =
      lb_source::port_atom_id(3907, 1472, 1, 37);
  CHECK_TRUE(port.has_value());
  CHECK_TRUE(*port < 0);
  CHECK_TRUE(!lb_source::decode_coordinate_atom_id(*port).has_value());

  const std::optional<lb_source::PortAtom> decoded =
      lb_source::decode_port_atom_id(*port);
  CHECK_TRUE(decoded.has_value());
  CHECK_EQ(decoded->tile_i, static_cast<std::int32_t>(3907));
  CHECK_EQ(decoded->tile_j, static_cast<std::int32_t>(1472));
  CHECK_EQ(decoded->face, static_cast<std::uint8_t>(1));
  CHECK_EQ(decoded->ordinal, static_cast<std::uint8_t>(37));
  CHECK_EQ(lb_source::port_atom_id(decoded->tile_i, decoded->tile_j,
                                   decoded->face, decoded->ordinal),
           port);

  const std::optional<AtomId> same_port_different_group_label =
      lb_source::port_atom_id(3907, 1472, 1, 37);
  CHECK_EQ(same_port_different_group_label, port);

  CHECK_TRUE(!lb_source::port_atom_id(-1, 0, 0, 0).has_value());
  CHECK_TRUE(!lb_source::port_atom_id(0, -1, 0, 0).has_value());
  CHECK_TRUE(!lb_source::port_atom_id(1 << 24, 0, 0, 0).has_value());
  CHECK_TRUE(!lb_source::port_atom_id(0, 0, 4, 0).has_value());
  CHECK_TRUE(!lb_source::port_atom_id(0, 0, 0, 256).has_value());
  CHECK_TRUE(!lb_source::decode_port_atom_id(0).has_value());
}

void test_associativity_across_band_grouping() {
  const std::vector<BandInput> atomized = {
      {.k_sq = 36,
       .outer_radius = 10,
       .atoms = {{1, 25, true}, {2, 100, false}, {7, 100, false}},
       .edges = {{1, 2}}},
      {.k_sq = 36,
       .outer_radius = 15,
       .atoms = {{3, 169, false}, {4, 225, false}},
       .edges = {{2, 3}, {7, 4}}},
      {.k_sq = 36,
       .outer_radius = 20,
       .atoms = {{5, 324, false}, {6, 400, false}},
       .edges = {{3, 5}, {5, 6}, {4, 6}}},
  };
  const BandInput first_two{
      .k_sq = 36,
      .outer_radius = 15,
      .atoms = {{1, 25, true}, {2, 100, false}, {7, 100, false},
                {3, 169, false}, {4, 225, false}},
      .edges = {{1, 2}, {2, 3}, {7, 4}},
  };
  const BandInput last_two{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{3, 169, false}, {4, 225, false}, {5, 324, false},
                {6, 400, false}},
      .edges = {{2, 3}, {7, 4}, {3, 5}, {5, 6}, {4, 6}},
  };
  const BandInput big{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, true}, {2, 100, false}, {7, 100, false},
                {3, 169, false}, {4, 225, false}, {5, 324, false},
                {6, 400, false}},
      .edges = {{1, 2}, {2, 3}, {7, 4}, {3, 5}, {5, 6}, {4, 6}},
  };

  const ProcessResult three = lb_source::process_bands(atomized);
  const ProcessResult grouped_left =
      lb_source::process_bands({first_two, atomized[2]});
  const ProcessResult first = lb_source::process_band(atomized[0],
                                                      std::nullopt);
  CHECK_TRUE(first.accepted());
  const ProcessResult grouped_right = lb_source::process_bands(
      {last_two}, first.outgoing);
  const ProcessResult one_big = lb_source::process_band(big, std::nullopt);

  expect_same_separator(three, one_big);
  expect_same_separator(grouped_left, one_big);
  expect_same_separator(grouped_right, one_big);
}

void test_source_seed_provider_marks_certified_atoms() {
  BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, false}, {2, 100, false}, {3, 400, false}},
      .edges = {{1, 2}, {2, 3}},
  };

  const lb_source::SourceSeedApplyResult applied =
      lb_source::apply_source_seeds(
          band, {{"ORIGIN_SOURCE", "omega-axis-prime", 1}});
  CHECK_TRUE(applied.accepted());
  CHECK_EQ(applied.applied, static_cast<std::size_t>(1));
  CHECK_TRUE(band.atoms[0].certified_source);

  const ProcessResult result = lb_source::process_band(band, std::nullopt);
  CHECK_TRUE(result.accepted());
  CHECK_TRUE(component_has_source_bit(result.outgoing, {3}, true));
}

void test_source_seed_provider_rejects_uncertified_modes() {
  BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, false}},
      .edges = {},
  };

  const lb_source::SourceSeedApplyResult bad_mode =
      lb_source::apply_source_seeds(band, {{"GEO_I", "not-a-source", 1}});
  CHECK_TRUE(!bad_mode.accepted());
  CHECK_EQ(bad_mode.diagnostic,
           std::string("invalid source seed mode: GEO_I"));

  const lb_source::SourceSeedApplyResult missing =
      lb_source::apply_source_seeds(
          band, {{"CERTIFIED_SEED", "prior-cert", 99}});
  CHECK_TRUE(!missing.accepted());
  CHECK_EQ(missing.diagnostic,
           std::string("source seed references missing atom"));
}

void test_carry_manifest_round_trip_is_canonical() {
  lb_source::CarryManifest manifest;
  manifest.k_sq = 36;
  manifest.outer_radius = 20;
  manifest.carry_width = 6;
  manifest.separator.carry_atoms = {{4, 400}, {2, 100}, {3, 169}};
  manifest.separator.component_partition = {{3, 2}, {4}};
  manifest.separator.source_bit_per_component = {true, false};
  manifest.separator.component_inventory = {{3, 1, 2}, {9, 4}};

  const std::string encoded = lb_source::carry_manifest_to_string(manifest);
  CHECK_EQ(encoded,
           std::string(
               "LB_SOURCE_CARRY_MANIFEST_V1\n"
               "k_sq 36\n"
               "outer_radius 20\n"
               "carry_width 6\n"
               "carry_atoms 3\n"
               "carry_atom 2 100\n"
               "carry_atom 3 169\n"
               "carry_atom 4 400\n"
               "components 2\n"
               "component 1 2 2 3 3 1 2 3\n"
               "component 0 1 4 2 4 9\n"
               "END\n"));

  const lb_source::CarryManifestReadResult decoded =
      lb_source::carry_manifest_from_string(encoded);
  CHECK_TRUE(decoded.accepted());

  lb_source::CarryManifest expected = manifest;
  expected.separator = lb_source::canonicalize_separator(expected.separator);
  CHECK_EQ(decoded.manifest, expected);
  CHECK_EQ(lb_source::carry_manifest_to_string(decoded.manifest), encoded);
}

void test_carry_manifest_rejects_malformed_partition() {
  const std::string encoded =
      "LB_SOURCE_CARRY_MANIFEST_V1\n"
      "k_sq 36\n"
      "outer_radius 20\n"
      "carry_width 6\n"
      "carry_atoms 1\n"
      "carry_atom 1 25\n"
      "components 1\n"
      "component 1 1 2 1 2\n"
      "END\n";

  const lb_source::CarryManifestReadResult decoded =
      lb_source::carry_manifest_from_string(encoded);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic,
           std::string("component references non-carry atom"));
}

LiveHandoffV1 live_handoff_fixture() {
  LiveHandoffV1 handoff;
  handoff.k_sq = 36;
  handoff.cut_radius = 20;
  handoff.carry_width = 6;
  handoff.source_mode = "ORIGIN_SOURCE";
  handoff.source_id = "omega-axis-prime";
  handoff.geometry_id = "full-octant";
  handoff.build_id = "debug-build";
  handoff.schedule_digest_algorithm =
      "sha256:lb_source_k26_repaired_bz_schedule_v1";
  handoff.schedule_digest_hex = "abcdef0123456789";
  handoff.overflow_summary = "none";
  handoff.separator.carry_atoms = {{4, 16}, {2, 4}, {3, 9}};
  handoff.separator.component_partition = {{3, 2}, {4}};
  handoff.separator.source_bit_per_component = {true, false};
  return handoff;
}

LiveHandoffExpectedContext live_handoff_expected_context() {
  LiveHandoffExpectedContext expected;
  expected.k_sq = 36;
  expected.cut_radius = 20;
  expected.carry_width = 6;
  expected.source_mode = "ORIGIN_SOURCE";
  expected.source_id = "omega-axis-prime";
  expected.geometry_id = "full-octant";
  expected.build_id = "debug-build";
  expected.schedule_digest_algorithm =
      "sha256:lb_source_k26_repaired_bz_schedule_v1";
  expected.schedule_digest_hex = "abcdef0123456789";
  expected.overflow_summary = "none";
  return expected;
}

void test_live_handoff_round_trip_is_canonical() {
  const LiveHandoffV1 handoff = live_handoff_fixture();
  const std::string encoded = lb_source::live_handoff_to_string(handoff);
  CHECK_EQ(encoded,
           std::string(
               "LB_SOURCE_LIVE_HANDOFF_V1\n"
               "k_sq 36\n"
               "cut_radius 20\n"
               "carry_width 6\n"
               "source_mode ORIGIN_SOURCE\n"
               "source_id omega-axis-prime\n"
               "geometry_id full-octant\n"
               "build_id debug-build\n"
               "schedule_digest_algorithm "
               "sha256:lb_source_k26_repaired_bz_schedule_v1\n"
               "schedule_digest_hex abcdef0123456789\n"
               "overflow_summary none\n"
               "carry_atoms 3\n"
               "carry_atom 2 4\n"
               "carry_atom 3 9\n"
               "carry_atom 4 16\n"
               "components 2\n"
               "component 1 2 2 3\n"
               "component 0 1 4\n"
               "END\n"));

  const lb_source::LiveHandoffReadResult decoded =
      lb_source::live_handoff_from_string(encoded,
                                          live_handoff_expected_context());
  CHECK_TRUE(decoded.accepted());

  LiveHandoffV1 expected = handoff;
  expected = lb_source::canonicalize_live_handoff(expected);
  CHECK_EQ(decoded.handoff, expected);
  CHECK_EQ(lb_source::live_handoff_to_string(decoded.handoff), encoded);

  const lb_source::CarryManifestReadResult carry_decode =
      lb_source::carry_manifest_from_string(encoded);
  CHECK_TRUE(!carry_decode.accepted());
  CHECK_EQ(carry_decode.diagnostic,
           std::string("missing carry manifest header"));
}

void test_live_handoff_rejects_wrong_resume_context() {
  const std::string encoded =
      lb_source::live_handoff_to_string(live_handoff_fixture());

  LiveHandoffExpectedContext expected = live_handoff_expected_context();
  expected.cut_radius = 19;
  lb_source::LiveHandoffReadResult decoded =
      lb_source::live_handoff_from_string(encoded, expected);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, std::string("stale cut radius"));

  expected = live_handoff_expected_context();
  expected.k_sq = 37;
  decoded = lb_source::live_handoff_from_string(encoded, expected);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, std::string("wrong k_sq"));

  expected = live_handoff_expected_context();
  expected.carry_width = 7;
  decoded = lb_source::live_handoff_from_string(encoded, expected);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, std::string("wrong carry width"));
}

void test_live_handoff_accepts_diagnostic_source_mode() {
  LiveHandoffV1 handoff = live_handoff_fixture();
  handoff.source_mode = "ORIGIN_PREFIX_PORT_WITNESS";

  const lb_source::LiveHandoffReadResult decoded =
      lb_source::live_handoff_from_string(
          lb_source::live_handoff_to_string(handoff));
  CHECK_TRUE(decoded.accepted());
  CHECK_EQ(decoded.handoff.source_mode,
           std::string("ORIGIN_PREFIX_PORT_WITNESS"));
}

void test_live_handoff_rejects_malformed_separator() {
  LiveHandoffV1 duplicate = live_handoff_fixture();
  duplicate.separator.carry_atoms.push_back({2, 4});
  lb_source::LiveHandoffReadResult decoded =
      lb_source::live_handoff_from_string(
          lb_source::live_handoff_to_string(duplicate));
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, std::string("duplicate carry atom"));

  LiveHandoffV1 missing_coverage = live_handoff_fixture();
  missing_coverage.separator.component_partition = {{2, 3}};
  missing_coverage.separator.source_bit_per_component = {true};
  decoded = lb_source::live_handoff_from_string(
      lb_source::live_handoff_to_string(missing_coverage));
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic,
           std::string("component partition does not cover all carry atoms"));

  LiveHandoffV1 unstable = live_handoff_fixture();
  const AtomId unstable_id = std::numeric_limits<AtomId>::min();
  unstable.separator.carry_atoms = {{unstable_id, 1}};
  unstable.separator.component_partition = {{unstable_id}};
  unstable.separator.source_bit_per_component = {true};
  decoded = lb_source::live_handoff_from_string(
      lb_source::live_handoff_to_string(unstable));
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, std::string("unstable carry atom id"));

  LiveHandoffV1 wrong_norm = live_handoff_fixture();
  wrong_norm.separator.carry_atoms[0].norm_sq = 99;
  decoded = lb_source::live_handoff_from_string(
      lb_source::live_handoff_to_string(wrong_norm));
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic,
           std::string("carry atom norm does not match coordinate atom"));
}

LastBandReachabilitySummaryV1 last_band_summary_fixture(
    const LiveHandoffV1& incoming) {
  LastBandReachabilitySummaryV1 summary;
  summary.k_sq = incoming.k_sq;
  summary.r_start = incoming.cut_radius;
  summary.r_outer = incoming.cut_radius + 10;
  summary.carry_width = incoming.carry_width;
  summary.source_mode = incoming.source_mode;
  summary.source_id = incoming.source_id;
  summary.geometry_id = incoming.geometry_id;
  summary.build_id = incoming.build_id;
  summary.schedule_digest_algorithm = incoming.schedule_digest_algorithm;
  summary.schedule_digest_hex = incoming.schedule_digest_hex;
  summary.overflow_summary = incoming.overflow_summary;
  summary.bridge_policy = "synthetic";
  summary.transfer_summary_present = true;
  return summary;
}

void test_last_band_summary_reproduces_terminal_state() {
  LiveHandoffV1 incoming = live_handoff_fixture();
  incoming.cut_radius = 20;
  incoming.separator.carry_atoms = {{2, 4}, {3, 9}, {4, 16}};
  incoming.separator.component_partition = {{2, 3}, {4}};
  incoming.separator.source_bit_per_component = {true, false};

  LastBandReachabilitySummaryV1 summary =
      last_band_summary_fixture(incoming);
  summary.components = {
      LastBandComponentSummaryV1{
          .boundary_atoms = {2, 3, 10},
          .max_coordinate_norm_sq = 100,
          .max_coordinate_atom_ids = {10},
          .max_support_norm_sq = 100,
          .max_support_atom_ids = {10},
          .bridge_safety = {.coordinate_carry_atoms_checked = 1,
                            .coordinate_carry_atoms_bridged = 1},
      },
      LastBandComponentSummaryV1{
          .boundary_atoms = {4, 11},
          .touches_outer_coordinate_carry = true,
          .max_coordinate_norm_sq = 121,
          .max_coordinate_atom_ids = {11},
          .max_support_norm_sq = 121,
          .max_support_atom_ids = {11},
      },
  };

  const LastBandSummaryApplyResult applied =
      lb_source::apply_last_band_summary(incoming, summary);
  CHECK_TRUE(applied.accepted());
  CHECK_TRUE(applied.has_incoming_source);
  CHECK_TRUE(applied.terminal_source_dead);
  CHECK_TRUE(!applied.has_source_continuation);
  CHECK_EQ(applied.max_source_coordinate_norm_sq,
           static_cast<std::uint64_t>(100));
  CHECK_EQ(applied.max_source_coordinate_atom_ids,
           (std::vector<AtomId>{10}));
  CHECK_EQ(applied.source_bridge_safety.coordinate_carry_atoms_checked,
           static_cast<std::uint64_t>(1));
  CHECK_EQ(applied.source_bridge_safety.coordinate_carry_atoms_bridged,
           static_cast<std::uint64_t>(1));
}

void test_last_band_summary_welds_neutral_carry_into_source() {
  LiveHandoffV1 incoming = live_handoff_fixture();
  incoming.separator.carry_atoms = {{2, 4}, {4, 16}};
  incoming.separator.component_partition = {{2}, {4}};
  incoming.separator.source_bit_per_component = {true, false};

  LastBandReachabilitySummaryV1 summary =
      last_band_summary_fixture(incoming);
  summary.components = {
      LastBandComponentSummaryV1{
          .boundary_atoms = {2, 4, 12},
          .touches_outer_coordinate_carry = true,
          .max_coordinate_norm_sq = 144,
          .max_coordinate_atom_ids = {12},
          .max_support_norm_sq = 144,
          .max_support_atom_ids = {12},
          .bridge_safety = {.coordinate_carry_atoms_checked = 2,
                            .coordinate_carry_atoms_bridged = 1,
                            .coordinate_carry_atoms_unbridged = 1,
                            .coordinate_carry_atoms_unsafe_candidates = 1},
      },
  };

  const LastBandSummaryApplyResult applied =
      lb_source::apply_last_band_summary(incoming, summary);
  CHECK_TRUE(applied.accepted());
  CHECK_TRUE(applied.has_source_continuation);
  CHECK_TRUE(!applied.terminal_source_dead);
  CHECK_EQ(applied.max_source_coordinate_norm_sq,
           static_cast<std::uint64_t>(144));
  CHECK_EQ(applied.max_source_coordinate_atom_ids,
           (std::vector<AtomId>{12}));
  CHECK_EQ(applied.source_bridge_safety.coordinate_carry_atoms_checked,
           static_cast<std::uint64_t>(2));
  CHECK_EQ(
      applied.source_bridge_safety.coordinate_carry_atoms_unsafe_candidates,
      static_cast<std::uint64_t>(1));
}

void test_last_band_port_overhang_is_not_coordinate_max_evidence() {
  LiveHandoffV1 incoming = live_handoff_fixture();
  incoming.separator.carry_atoms = {{2, 4}};
  incoming.separator.component_partition = {{2}};
  incoming.separator.source_bit_per_component = {true};
  const std::optional<AtomId> port = lb_source::port_atom_id(1, 2, 1, 5);
  CHECK_TRUE(port.has_value());

  LastBandReachabilitySummaryV1 summary =
      last_band_summary_fixture(incoming);
  summary.components = {
      LastBandComponentSummaryV1{
          .boundary_atoms = {2, *port},
          .touches_port_overhang = true,
          .max_support_norm_sq = 999,
          .max_support_atom_ids = {*port},
      },
  };

  const LastBandSummaryApplyResult applied =
      lb_source::apply_last_band_summary(incoming, summary);
  CHECK_TRUE(applied.accepted());
  CHECK_TRUE(applied.has_source_continuation);
  CHECK_TRUE(!applied.terminal_source_dead);
  CHECK_EQ(applied.max_source_coordinate_norm_sq,
           static_cast<std::uint64_t>(0));
  CHECK_TRUE(applied.max_source_coordinate_atom_ids.empty());
  CHECK_EQ(applied.max_source_support_norm_sq,
           static_cast<std::uint64_t>(999));
  CHECK_EQ(applied.max_source_support_atom_ids,
           (std::vector<AtomId>{*port}));
}

void test_last_band_summary_rejects_progress_row_only_death() {
  const LiveHandoffV1 incoming = live_handoff_fixture();
  LastBandReachabilitySummaryV1 summary;
  summary.k_sq = incoming.k_sq;
  summary.r_start = incoming.cut_radius;
  summary.r_outer = incoming.cut_radius + 10;
  summary.carry_width = incoming.carry_width;

  LastBandSummaryApplyResult applied =
      lb_source::apply_last_band_summary(incoming, summary);
  CHECK_EQ(applied.reject, RejectReason::kMalformed);
  CHECK_EQ(applied.diagnostic,
           std::string("missing last-band transfer summary"));

  summary.transfer_summary_present = true;
  applied = lb_source::apply_last_band_summary(incoming, summary);
  CHECK_EQ(applied.reject, RejectReason::kMalformed);
  CHECK_EQ(applied.diagnostic,
           std::string("empty last-band transfer summary"));
}

void test_last_band_summary_must_cover_incoming_carry() {
  LiveHandoffV1 incoming = live_handoff_fixture();
  incoming.separator.carry_atoms = {{2, 4}, {3, 9}};
  incoming.separator.component_partition = {{2, 3}};
  incoming.separator.source_bit_per_component = {true};

  LastBandReachabilitySummaryV1 summary =
      last_band_summary_fixture(incoming);
  summary.components = {
      LastBandComponentSummaryV1{
          .boundary_atoms = {2},
          .max_coordinate_norm_sq = 4,
          .max_coordinate_atom_ids = {2},
      },
  };

  const LastBandSummaryApplyResult applied =
      lb_source::apply_last_band_summary(incoming, summary);
  CHECK_EQ(applied.reject, RejectReason::kMalformed);
  CHECK_EQ(applied.diagnostic,
           std::string("last-band summary omits incoming carry atom"));
}

void test_make_carry_manifest_from_process_result() {
  const BandInput band{
      .k_sq = 36,
      .outer_radius = 20,
      .atoms = {{1, 25, true}, {2, 100, false}, {3, 169, false}},
      .edges = {{1, 2}, {2, 3}},
  };

  const ProcessResult result = lb_source::process_band(band, std::nullopt);
  CHECK_TRUE(result.accepted());
  const lb_source::CarryManifest manifest =
      lb_source::make_carry_manifest(band.k_sq, band.outer_radius, result);
  CHECK_EQ(manifest.k_sq, static_cast<std::uint64_t>(36));
  CHECK_EQ(manifest.outer_radius, static_cast<std::uint64_t>(20));
  CHECK_EQ(manifest.carry_width, static_cast<std::uint64_t>(6));
  CHECK_EQ(manifest.separator, result.outgoing);
}

void test_inventory_summary_is_canonical() {
  const lb_source::InventorySummary unsorted =
      lb_source::summarize_inventory({3, 1, 2, 1});
  const lb_source::InventorySummary canonical =
      lb_source::summarize_inventory({1, 2, 3});
  const lb_source::InventorySummary different =
      lb_source::summarize_inventory({1, 2, 4});

  CHECK_EQ(unsorted, canonical);
  CHECK_EQ(canonical.count, static_cast<std::uint64_t>(3));
  CHECK_EQ(canonical.digest_algorithm,
           std::string("sha256:lb_source_inventory_v1"));
  CHECK_EQ(canonical.digest_hex.size(), static_cast<std::size_t>(64));
  CHECK_EQ(canonical.max_norm_sq, static_cast<std::uint64_t>(9));
  CHECK_EQ(canonical.max_norm_atom_ids, (std::vector<AtomId>{3}));
  CHECK_TRUE(canonical.digest_hex != different.digest_hex);
}

void test_draft_profile_and_certificate_json_output() {
  lb_source::CarryManifest manifest;
  manifest.k_sq = 36;
  manifest.outer_radius = 20;
  manifest.carry_width = 6;
  manifest.separator.carry_atoms = {{2, 100}};
  manifest.separator.component_partition = {{2}};
  manifest.separator.source_bit_per_component = {true};
  manifest.separator.component_inventory = {{1, 2}};

  const lb_source::SourceDraftMetadata metadata{
      .source_mode = "ORIGIN_SOURCE",
      .source_id = "omega\"seed",
      .geometry_id = "full-octant",
      .commit_id = "abc123",
      .build_id = "debug",
      .bz_status = "BZ_CLEAN",
      .artifact_hash = "sha256:test",
  };
  const lb_source::SourceProfileDraft profile{
      .profile_id = "profile-1",
      .metadata = metadata,
      .carry_manifest = manifest,
      .reject = RejectReason::kNone,
      .diagnostic = "",
      .terminal_source_dead = true,
      .terminal_source_inventory = {1, 2},
  };
  const std::string profile_json =
      lb_source::source_profile_draft_json(profile);
  CHECK_TRUE(profile_json.find(
                 "\"schema\":\"lb_source_profile_draft_v1\"") !=
             std::string::npos);
  CHECK_TRUE(profile_json.find(
                 "\"terminal_source_inventory_summary\":{\"count\":2,"
                 "\"digest_algorithm\":\"sha256:lb_source_inventory_v1\","
                 "\"digest_hex\":\"") != std::string::npos);
  CHECK_TRUE(profile_json.find("\"max_norm_sq\":4,"
                               "\"max_norm_atom_ids\":[2]") !=
             std::string::npos);
  CHECK_TRUE(profile_json.find("\"terminal_source_inventory\":[1,2]") !=
             std::string::npos);
  CHECK_TRUE(profile_json.find(
                 "\"carry_manifest\":{\"schema\":"
                 "\"lb_source_carry_manifest_v1\"") != std::string::npos);

  const lb_source::SourceCertificateDraft certificate{
      .certificate_id = "cert-1",
      .profile_id = "profile-1",
      .metadata = metadata,
      .k_sq = 36,
      .terminal_radius = 20,
      .negative_guard_pass = true,
      .endpoint = {0, 3, 9},
      .endpoint_atom_id = 3,
      .source_path = {{0, 3, 9}},
      .terminal_source_inventory = {1, 2},
  };
  const std::string cert_json =
      lb_source::source_certificate_draft_json(certificate);
  CHECK_TRUE(cert_json.find(
                 "\"schema\":\"lb_source_dead_cert_draft_v1\"") !=
             std::string::npos);
  CHECK_TRUE(cert_json.find(
                 "\"terminal_source_inventory_summary\":{\"count\":2,"
                 "\"digest_algorithm\":\"sha256:lb_source_inventory_v1\","
                 "\"digest_hex\":\"") != std::string::npos);
  CHECK_TRUE(cert_json.find("\"max_norm_sq\":4,"
                            "\"max_norm_atom_ids\":[2]") !=
             std::string::npos);
  CHECK_TRUE(cert_json.find("\"endpoint\":{\"a\":0,\"b\":3,"
                            "\"norm_sq\":9}") != std::string::npos);
  CHECK_TRUE(cert_json.find("\"endpoint_atom_id\":3") !=
             std::string::npos);
  CHECK_TRUE(cert_json.find("\"source_path_provenance\":"
                            "\"coordinate_gaussian_prime_path\"") !=
             std::string::npos);
  CHECK_TRUE(cert_json.find("\"source_path\":[{\"a\":0,\"b\":3,"
                            "\"norm_sq\":9}]") != std::string::npos);
  CHECK_TRUE(cert_json.find("\"terminal_source_inventory\":[1,2]") !=
             std::string::npos);
}

void run(const std::string& name, void (*test)()) {
  const int before = g_failures;
  test();
  if (g_failures == before) {
    std::cout << "PASS " << name << "\n";
  } else {
    std::cout << "FAIL " << name << "\n";
  }
}

}  // namespace

int main() {
  run("one_band_identity", test_one_band_identity);
  run("stitched_bands_equal_one_big_band",
      test_stitched_bands_equal_one_big_band);
  run("false_weld_does_not_invent_source",
      test_false_weld_does_not_invent_source);
  run("source_carry_loss_is_prevented_by_partition",
      test_source_carry_loss_is_prevented_by_partition);
  run("non_source_partition_merge_reaches_source_later",
      test_non_source_partition_merge_reaches_source_later);
  run("terminal_death_inventory", test_terminal_death_inventory);
  run("terminal_inventory_survives_extra_bands",
      test_terminal_inventory_survives_extra_bands);
  run("non_source_payload_later_merge_is_in_terminal_inventory",
      test_non_source_payload_later_merge_is_in_terminal_inventory);
  run("process_band_canonicalizes_unsorted_incoming_inventory",
      test_process_band_canonicalizes_unsorted_incoming_inventory);
  run("component_inventory_cap_rejects_before_unbounded_growth",
      test_component_inventory_cap_rejects_before_unbounded_growth);
  run("live_projection_matches_legacy_without_inventory",
      test_live_projection_matches_legacy_without_inventory);
  run("live_empty_inventory_bypasses_inventory_cap",
      test_live_empty_inventory_bypasses_inventory_cap);
  run("live_still_enforces_non_inventory_caps",
      test_live_still_enforces_non_inventory_caps);
  run("live_terminal_death_drops_listed_inventory",
      test_live_terminal_death_drops_listed_inventory);
  run("live_rejects_fresh_source_with_incoming_handoff",
      test_live_rejects_fresh_source_with_incoming_handoff);
  run("overflow_reject_is_hard", test_overflow_reject_is_hard);
  run("k32_ceil_sqrt_carry_width_is_six",
      test_k32_ceil_sqrt_carry_width_is_six);
  run("coordinate_atom_ids_are_stable_and_canonical",
      test_coordinate_atom_ids_are_stable_and_canonical);
  run("port_atom_ids_are_stable_and_label_free",
      test_port_atom_ids_are_stable_and_label_free);
  run("associativity_across_band_grouping",
      test_associativity_across_band_grouping);
  run("source_seed_provider_marks_certified_atoms",
      test_source_seed_provider_marks_certified_atoms);
  run("source_seed_provider_rejects_uncertified_modes",
      test_source_seed_provider_rejects_uncertified_modes);
  run("carry_manifest_round_trip_is_canonical",
      test_carry_manifest_round_trip_is_canonical);
  run("carry_manifest_rejects_malformed_partition",
      test_carry_manifest_rejects_malformed_partition);
  run("live_handoff_round_trip_is_canonical",
      test_live_handoff_round_trip_is_canonical);
  run("live_handoff_rejects_wrong_resume_context",
      test_live_handoff_rejects_wrong_resume_context);
  run("live_handoff_accepts_diagnostic_source_mode",
      test_live_handoff_accepts_diagnostic_source_mode);
  run("live_handoff_rejects_malformed_separator",
      test_live_handoff_rejects_malformed_separator);
  run("last_band_summary_reproduces_terminal_state",
      test_last_band_summary_reproduces_terminal_state);
  run("last_band_summary_welds_neutral_carry_into_source",
      test_last_band_summary_welds_neutral_carry_into_source);
  run("last_band_port_overhang_is_not_coordinate_max_evidence",
      test_last_band_port_overhang_is_not_coordinate_max_evidence);
  run("last_band_summary_rejects_progress_row_only_death",
      test_last_band_summary_rejects_progress_row_only_death);
  run("last_band_summary_must_cover_incoming_carry",
      test_last_band_summary_must_cover_incoming_carry);
  run("make_carry_manifest_from_process_result",
      test_make_carry_manifest_from_process_result);
  run("inventory_summary_is_canonical",
      test_inventory_summary_is_canonical);
  run("draft_profile_and_certificate_json_output",
      test_draft_profile_and_certificate_json_output);

  if (g_failures != 0) {
    std::cerr << g_failures << " test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
