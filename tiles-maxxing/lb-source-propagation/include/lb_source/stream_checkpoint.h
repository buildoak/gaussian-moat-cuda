#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

#include "lb_source/source_propagation.h"

namespace lb_source {

struct StreamCheckpointV1 {
  std::string runner_id;
  std::uint64_t k_sq = 0;
  std::uint64_t next_r_start = 0;
  std::uint64_t requested_r_final = 0;
  std::uint64_t carry_width = 0;
  std::uint64_t schedule_index = 0;
  std::string schedule_digest_algorithm;
  std::string schedule_digest_hex;
  std::string source_mode;
  std::string source_id;
  std::string geometry_id;
  std::string build_id;
  std::string overflow_summary;
  LiveHandoffV1 handoff;

  friend bool operator==(const StreamCheckpointV1&,
                         const StreamCheckpointV1&) = default;
};

struct StreamCheckpointExpectedContext {
  std::optional<std::string> runner_id;
  std::optional<std::uint64_t> k_sq;
  std::optional<std::uint64_t> next_r_start;
  std::optional<std::uint64_t> requested_r_final;
  std::optional<std::uint64_t> carry_width;
  std::optional<std::uint64_t> schedule_index;
  std::optional<std::string> schedule_digest_algorithm;
  std::optional<std::string> schedule_digest_hex;
  std::optional<std::string> source_mode;
  std::optional<std::string> source_id;
  std::optional<std::string> geometry_id;
  std::optional<std::string> build_id;
  std::optional<std::string> overflow_summary;
};

struct StreamCheckpointReadResult {
  StreamCheckpointV1 checkpoint;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

StreamCheckpointV1 canonicalize_stream_checkpoint(
    const StreamCheckpointV1& checkpoint);

std::string validate_stream_checkpoint(
    const StreamCheckpointV1& checkpoint,
    const StreamCheckpointExpectedContext& expected = {});

std::ostream& write_stream_checkpoint(std::ostream& out,
                                      const StreamCheckpointV1& checkpoint);

StreamCheckpointReadResult read_stream_checkpoint(std::istream& in);

StreamCheckpointReadResult read_stream_checkpoint(
    std::istream& in, const StreamCheckpointExpectedContext& expected);

std::string stream_checkpoint_to_string(
    const StreamCheckpointV1& checkpoint);

StreamCheckpointReadResult stream_checkpoint_from_string(
    std::string_view text);

StreamCheckpointReadResult stream_checkpoint_from_string(
    std::string_view text, const StreamCheckpointExpectedContext& expected);

}  // namespace lb_source
