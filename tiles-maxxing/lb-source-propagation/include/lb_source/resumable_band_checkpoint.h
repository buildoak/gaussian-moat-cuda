#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace lb_source {

struct ResumableBandCheckpointV1 {
  std::string runner_id;
  std::string mode;
  std::uint64_t k_sq = 0;
  std::uint64_t original_r_start = 0;
  std::uint64_t next_r_start = 0;
  std::uint64_t requested_r_final = 0;
  std::uint64_t microband_width = 0;
  std::uint64_t carry_width = 0;
  std::uint64_t schedule_index = 0;
  std::string schedule_digest_algorithm;
  std::string schedule_digest_hex;
  std::string geometry_id;
  std::string build_id;
  std::string oracle_id;
  std::string command_id;
  std::string overflow_summary;
  std::string proof_status;
  std::string harvest_status;
  std::string replay_status;
  std::string detector_handoff_path;
  std::string detector_handoff_sha256;
  std::string detector_handoff_schema;
  std::uint64_t detector_handoff_bytes = 0;
  std::uint64_t campaign_tiles_processed = 0;
  std::uint64_t tileop_overflows = 0;

  friend bool operator==(const ResumableBandCheckpointV1&,
                         const ResumableBandCheckpointV1&) = default;
};

struct ResumableBandCheckpointExpectedContext {
  std::optional<std::string> runner_id;
  std::optional<std::string> mode;
  std::optional<std::uint64_t> k_sq;
  std::optional<std::uint64_t> original_r_start;
  std::optional<std::uint64_t> next_r_start;
  std::optional<std::uint64_t> requested_r_final;
  std::optional<std::uint64_t> microband_width;
  std::optional<std::uint64_t> carry_width;
  std::optional<std::uint64_t> schedule_index;
  std::optional<std::string> schedule_digest_algorithm;
  std::optional<std::string> schedule_digest_hex;
  std::optional<std::string> geometry_id;
  std::optional<std::string> build_id;
  std::optional<std::string> oracle_id;
  std::optional<std::string> command_id;
  std::optional<std::string> overflow_summary;
  std::optional<std::string> proof_status;
  std::optional<std::string> harvest_status;
  std::optional<std::string> replay_status;
  bool require_detector_handoff_reference = false;
  std::optional<std::string> detector_handoff_path;
  std::optional<std::string> detector_handoff_sha256;
  std::optional<std::string> detector_handoff_schema;
  std::optional<std::uint64_t> detector_handoff_bytes;
};

struct ResumableBandCheckpointReadResult {
  ResumableBandCheckpointV1 checkpoint;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

std::string validate_resumable_band_checkpoint(
    const ResumableBandCheckpointV1& checkpoint,
    const ResumableBandCheckpointExpectedContext& expected = {});

std::ostream& write_resumable_band_checkpoint(
    std::ostream& out, const ResumableBandCheckpointV1& checkpoint);

ResumableBandCheckpointReadResult read_resumable_band_checkpoint(
    std::istream& in);

ResumableBandCheckpointReadResult read_resumable_band_checkpoint(
    std::istream& in,
    const ResumableBandCheckpointExpectedContext& expected);

std::string resumable_band_checkpoint_to_string(
    const ResumableBandCheckpointV1& checkpoint);

ResumableBandCheckpointReadResult resumable_band_checkpoint_from_string(
    std::string_view text);

ResumableBandCheckpointReadResult resumable_band_checkpoint_from_string(
    std::string_view text,
    const ResumableBandCheckpointExpectedContext& expected);

}  // namespace lb_source
