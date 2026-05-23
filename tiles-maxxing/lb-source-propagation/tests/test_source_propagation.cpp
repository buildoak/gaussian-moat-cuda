#include "lb_source/source_propagation.h"

#include <cstdlib>
#include <iostream>
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
