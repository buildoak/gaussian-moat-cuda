#include "lb_source/resumable_band_checkpoint.h"
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

lb_source::ResumableBandCheckpointV1 checkpoint_fixture() {
  lb_source::ResumableBandCheckpointV1 checkpoint;
  checkpoint.runner_id = "source_tileop_port_stream_runner_v1";
  checkpoint.mode = "resumable-band";
  checkpoint.k_sq = 26;
  checkpoint.original_r_start = 248;
  checkpoint.next_r_start = 512;
  checkpoint.requested_r_final = 512;
  checkpoint.microband_width = 128;
  checkpoint.carry_width = 6;
  checkpoint.schedule_index = 3;
  checkpoint.schedule_digest_algorithm =
      "sha256:source_tileop_port_stream_runner_schedule_v1";
  checkpoint.schedule_digest_hex =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  checkpoint.geometry_id = "gaussian_octant_tileop_port_v1";
  checkpoint.build_id = "local_campaign_build";
  checkpoint.oracle_id = "tileop_port_stream_v1";
  checkpoint.command_id = "source_tileop_port_stream_runner_fixed_width_v1";
  checkpoint.overflow_summary = "none";
  checkpoint.proof_status = "DIAGNOSTIC_NON_CLAIM";
  checkpoint.harvest_status = "BAND_HARVEST_PROGRESS_V1";
  checkpoint.replay_status = "REPLAYABLE_CONTEXT_V1";
  checkpoint.campaign_tiles_processed = 12345;
  checkpoint.tileop_overflows = 0;
  return checkpoint;
}

lb_source::ResumableBandCheckpointExpectedContext expected_context() {
  lb_source::ResumableBandCheckpointExpectedContext expected;
  const lb_source::ResumableBandCheckpointV1 checkpoint = checkpoint_fixture();
  expected.runner_id = checkpoint.runner_id;
  expected.mode = checkpoint.mode;
  expected.k_sq = checkpoint.k_sq;
  expected.original_r_start = checkpoint.original_r_start;
  expected.next_r_start = checkpoint.next_r_start;
  expected.requested_r_final = checkpoint.requested_r_final;
  expected.microband_width = checkpoint.microband_width;
  expected.carry_width = checkpoint.carry_width;
  expected.schedule_index = checkpoint.schedule_index;
  expected.schedule_digest_algorithm = checkpoint.schedule_digest_algorithm;
  expected.schedule_digest_hex = checkpoint.schedule_digest_hex;
  expected.geometry_id = checkpoint.geometry_id;
  expected.build_id = checkpoint.build_id;
  expected.oracle_id = checkpoint.oracle_id;
  expected.command_id = checkpoint.command_id;
  expected.overflow_summary = checkpoint.overflow_summary;
  expected.proof_status = checkpoint.proof_status;
  expected.harvest_status = checkpoint.harvest_status;
  expected.replay_status = checkpoint.replay_status;
  return expected;
}

void expect_rejects_before_exposing_checkpoint(
    const lb_source::ResumableBandCheckpointV1& checkpoint,
    const lb_source::ResumableBandCheckpointExpectedContext& expected,
    const std::string& diagnostic) {
  const lb_source::ResumableBandCheckpointReadResult decoded =
      lb_source::resumable_band_checkpoint_from_string(
          lb_source::resumable_band_checkpoint_to_string(checkpoint),
          expected);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, diagnostic);
  CHECK_EQ(decoded.checkpoint, lb_source::ResumableBandCheckpointV1{});
}

void test_round_trip_is_source_neutral() {
  const lb_source::ResumableBandCheckpointV1 checkpoint = checkpoint_fixture();
  const std::string encoded =
      lb_source::resumable_band_checkpoint_to_string(checkpoint);
  CHECK_EQ(encoded,
           std::string(
               "LB_RESUMABLE_BAND_CHECKPOINT_V1\n"
               "runner_id source_tileop_port_stream_runner_v1\n"
               "mode resumable-band\n"
               "k_sq 26\n"
               "original_r_start 248\n"
               "next_r_start 512\n"
               "requested_r_final 512\n"
               "microband_width 128\n"
               "carry_width 6\n"
               "schedule_index 3\n"
               "schedule_digest_algorithm "
               "sha256:source_tileop_port_stream_runner_schedule_v1\n"
               "schedule_digest_hex "
               "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
               "geometry_id gaussian_octant_tileop_port_v1\n"
               "build_id local_campaign_build\n"
               "oracle_id tileop_port_stream_v1\n"
               "command_id source_tileop_port_stream_runner_fixed_width_v1\n"
               "overflow_summary none\n"
               "proof_status DIAGNOSTIC_NON_CLAIM\n"
               "harvest_status BAND_HARVEST_PROGRESS_V1\n"
               "replay_status REPLAYABLE_CONTEXT_V1\n"
               "campaign_tiles_processed 12345\n"
               "tileop_overflows 0\n"
               "END_RESUMABLE_BAND_CHECKPOINT\n"));

  CHECK_TRUE(encoded.find("source_mode") == std::string::npos);
  CHECK_TRUE(encoded.find("source_id") == std::string::npos);
  CHECK_TRUE(encoded.find("LB_SOURCE_LIVE_HANDOFF_V1") == std::string::npos);

  const lb_source::ResumableBandCheckpointReadResult decoded =
      lb_source::resumable_band_checkpoint_from_string(encoded,
                                                       expected_context());
  CHECK_TRUE(decoded.accepted());
  CHECK_EQ(decoded.checkpoint, checkpoint);

  const lb_source::StreamCheckpointReadResult source_decode =
      lb_source::stream_checkpoint_from_string(encoded);
  CHECK_TRUE(!source_decode.accepted());
  CHECK_EQ(source_decode.diagnostic,
           std::string("missing stream checkpoint header"));
}

void test_rejects_expected_context_mismatch() {
  const lb_source::ResumableBandCheckpointV1 checkpoint = checkpoint_fixture();

  lb_source::ResumableBandCheckpointExpectedContext expected =
      expected_context();
  expected.runner_id = "other";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong runner_id");

  expected = expected_context();
  expected.mode = "source-overlay";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong mode");

  expected = expected_context();
  expected.k_sq = 36;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong k_sq");

  expected = expected_context();
  expected.original_r_start = 249;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong original_r_start");

  expected = expected_context();
  expected.next_r_start = 376;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "stale next_r_start");

  expected = expected_context();
  expected.requested_r_final = 640;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong requested_r_final");

  expected = expected_context();
  expected.microband_width = 64;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong microband_width");

  expected = expected_context();
  expected.schedule_index = 2;
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong schedule_index");

  expected = expected_context();
  expected.schedule_digest_hex =
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong schedule_digest_hex");

  expected = expected_context();
  expected.geometry_id = "other_geometry";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong geometry_id");

  expected = expected_context();
  expected.build_id = "release_build";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong build_id");

  expected = expected_context();
  expected.oracle_id = "other_oracle";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong oracle_id");

  expected = expected_context();
  expected.command_id = "other_command";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong command_id");

  expected = expected_context();
  expected.proof_status = "SUMMARY_ONLY_NON_CLAIM";
  expect_rejects_before_exposing_checkpoint(checkpoint, expected,
                                            "wrong proof_status");
}

void test_rejects_structural_invariants() {
  lb_source::ResumableBandCheckpointV1 checkpoint = checkpoint_fixture();
  checkpoint.next_r_start = 248;
  CHECK_EQ(lb_source::validate_resumable_band_checkpoint(checkpoint),
           std::string("next_r_start must advance original_r_start"));

  checkpoint = checkpoint_fixture();
  checkpoint.requested_r_final = 511;
  CHECK_EQ(lb_source::validate_resumable_band_checkpoint(checkpoint),
           std::string("requested_r_final precedes next_r_start"));

  checkpoint = checkpoint_fixture();
  checkpoint.carry_width = 5;
  CHECK_EQ(lb_source::validate_resumable_band_checkpoint(checkpoint),
           std::string("carry_width does not match k_sq"));

  checkpoint = checkpoint_fixture();
  checkpoint.microband_width = 5;
  CHECK_EQ(lb_source::validate_resumable_band_checkpoint(checkpoint),
           std::string("microband_width is smaller than carry_width"));

  checkpoint = checkpoint_fixture();
  checkpoint.schedule_index = 2;
  CHECK_EQ(lb_source::validate_resumable_band_checkpoint(checkpoint),
           std::string("wrong schedule_index"));

  checkpoint = checkpoint_fixture();
  checkpoint.proof_status = "SOURCE_DEAD_CERT_PASS";
  CHECK_EQ(lb_source::validate_resumable_band_checkpoint(checkpoint),
           std::string("missing or invalid proof_status"));
}

void test_rejects_hostile_parse_inputs() {
  const lb_source::ResumableBandCheckpointV1 checkpoint = checkpoint_fixture();
  const std::string encoded =
      lb_source::resumable_band_checkpoint_to_string(checkpoint);

  lb_source::ResumableBandCheckpointReadResult decoded =
      lb_source::resumable_band_checkpoint_from_string(
          "LB_SOURCE_STREAM_CHECKPOINT_V1\n");
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic,
           std::string("missing resumable band checkpoint header"));

  decoded = lb_source::resumable_band_checkpoint_from_string(
      encoded + "extra_token\n");
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic,
           std::string("unexpected trailing resumable band checkpoint tokens"));

  std::string bad_hex = encoded;
  const std::string good_hex =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  bad_hex.replace(bad_hex.find(good_hex), good_hex.size(), "not_hex");
  decoded = lb_source::resumable_band_checkpoint_from_string(bad_hex);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic,
           std::string("missing or invalid schedule_digest_hex"));

  std::string negative = encoded;
  negative.replace(negative.find("k_sq 26"), std::string("k_sq 26").size(),
                   "k_sq -26");
  decoded = lb_source::resumable_band_checkpoint_from_string(negative);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, std::string("missing or invalid k_sq"));

  const std::string max_u64 = "18446744073709551615";
  std::string overflow = encoded;
  overflow.replace(overflow.find("k_sq 26"), std::string("k_sq 26").size(),
                   "k_sq " + max_u64 + "0");
  decoded = lb_source::resumable_band_checkpoint_from_string(overflow);
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, std::string("missing or invalid k_sq"));
}

}  // namespace

int main() {
  test_round_trip_is_source_neutral();
  test_rejects_expected_context_mismatch();
  test_rejects_structural_invariants();
  test_rejects_hostile_parse_inputs();

  if (g_failures != 0) {
    std::cerr << g_failures << " resumable band checkpoint tests failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
