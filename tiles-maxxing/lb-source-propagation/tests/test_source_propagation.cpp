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
  run("overflow_reject_is_hard", test_overflow_reject_is_hard);
  run("k32_ceil_sqrt_carry_width_is_six",
      test_k32_ceil_sqrt_carry_width_is_six);
  run("associativity_across_band_grouping",
      test_associativity_across_band_grouping);

  if (g_failures != 0) {
    std::cerr << g_failures << " test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
