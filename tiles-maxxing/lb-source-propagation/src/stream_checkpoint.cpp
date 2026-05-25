#include "lb_source/stream_checkpoint.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace lb_source {
namespace {

bool parse_uint64_token(const std::string& token, std::uint64_t& value) {
  if (token.empty() || token[0] == '-') {
    return false;
  }
  const char* begin = token.data();
  const char* end = token.data() + token.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  return ec == std::errc() && ptr == end;
}

bool parse_size_token(const std::string& token, std::size_t& value) {
  std::uint64_t parsed = 0;
  if (!parse_uint64_token(token, parsed) ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

bool valid_manifest_token(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (std::isspace(ch) != 0 || ch < 0x20) {
      return false;
    }
  }
  return true;
}

bool valid_hex_token(std::string_view value) {
  if (!valid_manifest_token(value)) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (std::isxdigit(ch) == 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

StreamCheckpointV1 canonicalize_stream_checkpoint(
    const StreamCheckpointV1& checkpoint) {
  StreamCheckpointV1 canonical = checkpoint;
  canonical.handoff = canonicalize_live_handoff(canonical.handoff);
  return canonical;
}

std::string validate_stream_checkpoint(
    const StreamCheckpointV1& checkpoint,
    const StreamCheckpointExpectedContext& expected) {
  if (!valid_manifest_token(checkpoint.runner_id)) {
    return "missing or invalid runner_id";
  }
  if (!valid_manifest_token(checkpoint.schedule_digest_algorithm)) {
    return "missing or invalid schedule_digest_algorithm";
  }
  if (!valid_hex_token(checkpoint.schedule_digest_hex)) {
    return "missing or invalid schedule_digest_hex";
  }
  if (!valid_manifest_token(checkpoint.source_mode)) {
    return "missing or invalid source_mode";
  }
  if (!valid_manifest_token(checkpoint.source_id)) {
    return "missing or invalid source_id";
  }
  if (!valid_manifest_token(checkpoint.geometry_id)) {
    return "missing or invalid geometry_id";
  }
  if (!valid_manifest_token(checkpoint.build_id)) {
    return "missing or invalid build_id";
  }
  if (!valid_manifest_token(checkpoint.overflow_summary)) {
    return "missing or invalid overflow_summary";
  }

  LiveHandoffExpectedContext handoff_expected;
  handoff_expected.k_sq = checkpoint.k_sq;
  handoff_expected.cut_radius = checkpoint.next_r_start;
  handoff_expected.carry_width = checkpoint.carry_width;
  handoff_expected.source_mode = checkpoint.source_mode;
  handoff_expected.source_id = checkpoint.source_id;
  handoff_expected.geometry_id = checkpoint.geometry_id;
  handoff_expected.build_id = checkpoint.build_id;
  handoff_expected.schedule_digest_algorithm =
      checkpoint.schedule_digest_algorithm;
  handoff_expected.schedule_digest_hex = checkpoint.schedule_digest_hex;
  handoff_expected.overflow_summary = checkpoint.overflow_summary;
  const std::string handoff_validation =
      validate_live_handoff(checkpoint.handoff, handoff_expected);
  if (!handoff_validation.empty()) {
    return handoff_validation;
  }

  if (expected.runner_id && checkpoint.runner_id != *expected.runner_id) {
    return "wrong runner_id";
  }
  if (expected.k_sq && checkpoint.k_sq != *expected.k_sq) {
    return "wrong k_sq";
  }
  if (expected.next_r_start &&
      checkpoint.next_r_start != *expected.next_r_start) {
    return "stale next_r_start";
  }
  if (expected.requested_r_final &&
      checkpoint.requested_r_final != *expected.requested_r_final) {
    return "wrong requested_r_final";
  }
  if (expected.carry_width &&
      checkpoint.carry_width != *expected.carry_width) {
    return "wrong carry width";
  }
  if (expected.schedule_index &&
      checkpoint.schedule_index != *expected.schedule_index) {
    return "wrong schedule_index";
  }
  if (expected.schedule_digest_algorithm &&
      checkpoint.schedule_digest_algorithm !=
          *expected.schedule_digest_algorithm) {
    return "wrong schedule_digest_algorithm";
  }
  if (expected.schedule_digest_hex &&
      checkpoint.schedule_digest_hex != *expected.schedule_digest_hex) {
    return "wrong schedule_digest_hex";
  }
  if (expected.source_mode &&
      checkpoint.source_mode != *expected.source_mode) {
    return "wrong source_mode";
  }
  if (expected.source_id && checkpoint.source_id != *expected.source_id) {
    return "wrong source_id";
  }
  if (expected.geometry_id &&
      checkpoint.geometry_id != *expected.geometry_id) {
    return "wrong geometry_id";
  }
  if (expected.build_id && checkpoint.build_id != *expected.build_id) {
    return "wrong build_id";
  }
  if (expected.overflow_summary &&
      checkpoint.overflow_summary != *expected.overflow_summary) {
    return "wrong overflow_summary";
  }
  return "";
}

std::ostream& write_stream_checkpoint(std::ostream& out,
                                      const StreamCheckpointV1& checkpoint) {
  const StreamCheckpointV1 canonical =
      canonicalize_stream_checkpoint(checkpoint);
  const std::string live_handoff = live_handoff_to_string(canonical.handoff);
  const std::size_t live_handoff_lines =
      static_cast<std::size_t>(std::count(live_handoff.begin(),
                                          live_handoff.end(), '\n'));

  out << "LB_SOURCE_STREAM_CHECKPOINT_V1\n";
  out << "runner_id " << canonical.runner_id << "\n";
  out << "k_sq " << canonical.k_sq << "\n";
  out << "next_r_start " << canonical.next_r_start << "\n";
  out << "requested_r_final " << canonical.requested_r_final << "\n";
  out << "carry_width " << canonical.carry_width << "\n";
  out << "schedule_index " << canonical.schedule_index << "\n";
  out << "schedule_digest_algorithm "
      << canonical.schedule_digest_algorithm << "\n";
  out << "schedule_digest_hex " << canonical.schedule_digest_hex << "\n";
  out << "source_mode " << canonical.source_mode << "\n";
  out << "source_id " << canonical.source_id << "\n";
  out << "geometry_id " << canonical.geometry_id << "\n";
  out << "build_id " << canonical.build_id << "\n";
  out << "overflow_summary " << canonical.overflow_summary << "\n";
  out << "live_handoff_lines " << live_handoff_lines << "\n";
  out << live_handoff;
  out << "END_STREAM_CHECKPOINT\n";
  return out;
}

StreamCheckpointReadResult read_stream_checkpoint(
    std::istream& in, const StreamCheckpointExpectedContext& expected) {
  StreamCheckpointReadResult result;
  std::string token;
  const auto fail = [&](std::string diagnostic) {
    result = {};
    result.diagnostic = std::move(diagnostic);
    return result;
  };
  const auto expect = [&](std::string_view expected_token) -> bool {
    return (in >> token) && token == expected_token;
  };
  const auto read_uint64 = [&](std::uint64_t& value) -> bool {
    return (in >> token) && parse_uint64_token(token, value);
  };
  const auto read_size = [&](std::size_t& value) -> bool {
    return (in >> token) && parse_size_token(token, value);
  };
  const auto read_string = [&](std::string& value) -> bool {
    return static_cast<bool>(in >> value);
  };

  if (!expect("LB_SOURCE_STREAM_CHECKPOINT_V1")) {
    return fail("missing stream checkpoint header");
  }
  if (!expect("runner_id") || !read_string(result.checkpoint.runner_id)) {
    return fail("missing or invalid runner_id");
  }
  if (!expect("k_sq") || !read_uint64(result.checkpoint.k_sq)) {
    return fail("missing or invalid k_sq");
  }
  if (!expect("next_r_start") ||
      !read_uint64(result.checkpoint.next_r_start)) {
    return fail("missing or invalid next_r_start");
  }
  if (!expect("requested_r_final") ||
      !read_uint64(result.checkpoint.requested_r_final)) {
    return fail("missing or invalid requested_r_final");
  }
  if (!expect("carry_width") ||
      !read_uint64(result.checkpoint.carry_width)) {
    return fail("missing or invalid carry_width");
  }
  if (!expect("schedule_index") ||
      !read_uint64(result.checkpoint.schedule_index)) {
    return fail("missing or invalid schedule_index");
  }
  if (!expect("schedule_digest_algorithm") ||
      !read_string(result.checkpoint.schedule_digest_algorithm)) {
    return fail("missing or invalid schedule_digest_algorithm");
  }
  if (!expect("schedule_digest_hex") ||
      !read_string(result.checkpoint.schedule_digest_hex)) {
    return fail("missing or invalid schedule_digest_hex");
  }
  if (!expect("source_mode") ||
      !read_string(result.checkpoint.source_mode)) {
    return fail("missing or invalid source_mode");
  }
  if (!expect("source_id") || !read_string(result.checkpoint.source_id)) {
    return fail("missing or invalid source_id");
  }
  if (!expect("geometry_id") ||
      !read_string(result.checkpoint.geometry_id)) {
    return fail("missing or invalid geometry_id");
  }
  if (!expect("build_id") || !read_string(result.checkpoint.build_id)) {
    return fail("missing or invalid build_id");
  }
  if (!expect("overflow_summary") ||
      !read_string(result.checkpoint.overflow_summary)) {
    return fail("missing or invalid overflow_summary");
  }

  std::size_t live_handoff_lines = 0;
  if (!expect("live_handoff_lines") || !read_size(live_handoff_lines)) {
    return fail("missing or invalid live_handoff_lines");
  }

  std::string line;
  std::getline(in, line);
  std::ostringstream live_text;
  for (std::size_t i = 0; i < live_handoff_lines; ++i) {
    if (!std::getline(in, line)) {
      return fail("truncated live handoff payload");
    }
    live_text << line << '\n';
  }

  LiveHandoffReadResult live =
      live_handoff_from_string(live_text.str());
  if (!live.accepted()) {
    return fail(live.diagnostic);
  }
  result.checkpoint.handoff = std::move(live.handoff);

  if (!expect("END_STREAM_CHECKPOINT")) {
    return fail("missing stream checkpoint END marker");
  }
  if (in >> token) {
    return fail("unexpected trailing stream checkpoint tokens");
  }

  const std::string validation =
      validate_stream_checkpoint(result.checkpoint, expected);
  if (!validation.empty()) {
    return fail(validation);
  }
  result.checkpoint = canonicalize_stream_checkpoint(result.checkpoint);
  return result;
}

StreamCheckpointReadResult read_stream_checkpoint(std::istream& in) {
  return read_stream_checkpoint(in, StreamCheckpointExpectedContext{});
}

std::string stream_checkpoint_to_string(
    const StreamCheckpointV1& checkpoint) {
  std::ostringstream out;
  write_stream_checkpoint(out, checkpoint);
  return out.str();
}

StreamCheckpointReadResult stream_checkpoint_from_string(
    std::string_view text, const StreamCheckpointExpectedContext& expected) {
  std::istringstream in{std::string(text)};
  return read_stream_checkpoint(in, expected);
}

StreamCheckpointReadResult stream_checkpoint_from_string(
    std::string_view text) {
  return stream_checkpoint_from_string(text,
                                       StreamCheckpointExpectedContext{});
}

}  // namespace lb_source
