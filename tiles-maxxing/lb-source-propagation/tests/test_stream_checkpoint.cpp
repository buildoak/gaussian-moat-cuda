#include "lb_source/stream_checkpoint.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

lb_source::LiveHandoffV1 live_handoff_fixture() {
  lb_source::LiveHandoffV1 handoff;
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

lb_source::StreamCheckpointV1 checkpoint_fixture() {
  lb_source::StreamCheckpointV1 checkpoint;
  checkpoint.runner_id = "microband-runner";
  checkpoint.k_sq = 36;
  checkpoint.next_r_start = 20;
  checkpoint.requested_r_final = 1015645;
  checkpoint.carry_width = 6;
  checkpoint.schedule_index = 7;
  checkpoint.schedule_digest_algorithm =
      "sha256:lb_source_k26_repaired_bz_schedule_v1";
  checkpoint.schedule_digest_hex = "abcdef0123456789";
  checkpoint.source_mode = "ORIGIN_SOURCE";
  checkpoint.source_id = "omega-axis-prime";
  checkpoint.geometry_id = "full-octant";
  checkpoint.build_id = "debug-build";
  checkpoint.overflow_summary = "none";
  checkpoint.handoff = live_handoff_fixture();
  return checkpoint;
}

lb_source::StreamCheckpointExpectedContext expected_context() {
  lb_source::StreamCheckpointExpectedContext expected;
  expected.runner_id = "microband-runner";
  expected.k_sq = 36;
  expected.next_r_start = 20;
  expected.requested_r_final = 1015645;
  expected.carry_width = 6;
  expected.schedule_index = 7;
  expected.schedule_digest_algorithm =
      "sha256:lb_source_k26_repaired_bz_schedule_v1";
  expected.schedule_digest_hex = "abcdef0123456789";
  expected.source_mode = "ORIGIN_SOURCE";
  expected.source_id = "omega-axis-prime";
  expected.geometry_id = "full-octant";
  expected.build_id = "debug-build";
  expected.overflow_summary = "none";
  return expected;
}

void expect_rejects_before_exposing_checkpoint(
    const lb_source::StreamCheckpointV1& checkpoint,
    const lb_source::StreamCheckpointExpectedContext& expected,
    const std::string& diagnostic) {
  const lb_source::StreamCheckpointReadResult decoded =
      lb_source::stream_checkpoint_from_string(
          lb_source::stream_checkpoint_to_string(checkpoint), expected);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, diagnostic);
  CHECK_EQ(decoded.checkpoint, lb_source::StreamCheckpointV1{});
}

void test_stream_checkpoint_round_trip_is_canonical() {
  const lb_source::StreamCheckpointV1 checkpoint = checkpoint_fixture();
  const std::string encoded =
      lb_source::stream_checkpoint_to_string(checkpoint);
  CHECK_EQ(encoded,
           std::string(
               "LB_SOURCE_STREAM_CHECKPOINT_V1\n"
               "runner_id microband-runner\n"
               "k_sq 36\n"
               "next_r_start 20\n"
               "requested_r_final 1015645\n"
               "carry_width 6\n"
               "schedule_index 7\n"
               "schedule_digest_algorithm "
               "sha256:lb_source_k26_repaired_bz_schedule_v1\n"
               "schedule_digest_hex abcdef0123456789\n"
               "source_mode ORIGIN_SOURCE\n"
               "source_id omega-axis-prime\n"
               "geometry_id full-octant\n"
               "build_id debug-build\n"
               "overflow_summary none\n"
               "live_handoff_lines 19\n"
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
               "END\n"
               "END_STREAM_CHECKPOINT\n"));

  const lb_source::StreamCheckpointReadResult decoded =
      lb_source::stream_checkpoint_from_string(encoded, expected_context());
  CHECK_TRUE(decoded.accepted());

  lb_source::StreamCheckpointV1 expected = checkpoint;
  expected = lb_source::canonicalize_stream_checkpoint(expected);
  CHECK_EQ(decoded.checkpoint, expected);
  CHECK_EQ(lb_source::stream_checkpoint_to_string(decoded.checkpoint),
           encoded);

  const lb_source::LiveHandoffReadResult live_decode =
      lb_source::live_handoff_from_string(encoded);
  CHECK_TRUE(!live_decode.accepted());
  CHECK_EQ(live_decode.diagnostic,
           std::string("missing live handoff header"));
}

void test_stream_checkpoint_rejects_expected_context_mismatch() {
  const lb_source::StreamCheckpointV1 checkpoint = checkpoint_fixture();

  lb_source::StreamCheckpointExpectedContext expected = expected_context();
  expected.k_sq = 37;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong k_sq");

  expected = expected_context();
  expected.carry_width = 7;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong carry width");

  expected = expected_context();
  expected.next_r_start = 19;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "stale next_r_start");

  expected = expected_context();
  expected.requested_r_final = 1015644;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong requested_r_final");

  expected = expected_context();
  expected.schedule_index = 8;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong schedule_index");

  expected = expected_context();
  expected.schedule_digest_algorithm = "sha256:other";
  expect_rejects_before_exposing_checkpoint(
      checkpoint, expected, "wrong schedule_digest_algorithm");

  expected = expected_context();
  expected.schedule_digest_hex = "bbbb";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong schedule_digest_hex");

  expected = expected_context();
  expected.source_mode = "WIRED_SOURCE";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong source_mode");

  expected = expected_context();
  expected.source_id = "other-source";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong source_id");

  expected = expected_context();
  expected.build_id = "release-build";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong build_id");

  expected = expected_context();
  expected.geometry_id = "quadrant";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong geometry_id");

  expected = expected_context();
  expected.overflow_summary = "overflow";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong overflow_summary");
}

void test_stream_checkpoint_rejects_nested_handoff_mismatch() {
  const lb_source::StreamCheckpointExpectedContext expected =
      expected_context();

  lb_source::StreamCheckpointV1 checkpoint = checkpoint_fixture();
  checkpoint.handoff.k_sq = 37;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "carry width does not match k_sq");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.cut_radius = 19;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "stale cut radius");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.carry_width = 7;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "carry width does not match k_sq");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.source_mode = "WIRED_SOURCE";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong source_mode");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.source_id = "nested-other-source";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong source_id");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.geometry_id = "quadrant";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong geometry_id");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.build_id = "release-build";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong build_id");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.schedule_digest_algorithm = "sha256:other";
  expect_rejects_before_exposing_checkpoint(
      checkpoint, expected, "wrong schedule_digest_algorithm");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.schedule_digest_hex = "bbbb";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong schedule_digest_hex");

  checkpoint = checkpoint_fixture();
  checkpoint.handoff.overflow_summary = "overflow";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong overflow_summary");
}

}  // namespace

int main() {
  const auto run = [](const char* name, void (*test)()) {
    const int before = g_failures;
    test();
    if (g_failures == before) {
      std::cout << "[PASS] " << name << "\n";
    }
  };

  run("stream_checkpoint_round_trip_is_canonical",
      test_stream_checkpoint_round_trip_is_canonical);
  run("stream_checkpoint_rejects_expected_context_mismatch",
      test_stream_checkpoint_rejects_expected_context_mismatch);
  run("stream_checkpoint_rejects_nested_handoff_mismatch",
      test_stream_checkpoint_rejects_nested_handoff_mismatch);

  if (g_failures != 0) {
    std::cerr << g_failures << " stream checkpoint test(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
