#include "lb_source/resumable_band_checkpoint.h"

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

std::uint64_t ceil_sqrt_u64(std::uint64_t n) {
  std::uint64_t lo = 1;
  std::uint64_t hi =
      n < 4294967296ULL ? n : static_cast<std::uint64_t>(4294967296ULL);
  while (lo < hi) {
    const std::uint64_t mid = lo + (hi - lo) / 2;
    if (mid >= n / mid + (n % mid != 0 ? 1 : 0)) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}

bool is_claim_pass_token(std::string_view value) {
  return value == "SOURCE_DEAD_CERT_PASS" || value == "MOAT_PROOF_PASS" ||
         value == "SPAN_PROOF_PASS";
}

template <class T>
std::string check_expected(const std::optional<T>& expected, const T& actual,
                           std::string_view diagnostic) {
  if (expected && actual != *expected) {
    return std::string(diagnostic);
  }
  return "";
}

}  // namespace

std::string validate_resumable_band_checkpoint(
    const ResumableBandCheckpointV1& checkpoint,
    const ResumableBandCheckpointExpectedContext& expected) {
  if (!valid_manifest_token(checkpoint.runner_id)) {
    return "missing or invalid runner_id";
  }
  if (checkpoint.mode != "resumable-band") {
    return "wrong mode";
  }
  if (checkpoint.k_sq == 0) {
    return "missing or invalid k_sq";
  }
  if (checkpoint.original_r_start == 0) {
    return "missing or invalid original_r_start";
  }
  if (checkpoint.next_r_start <= checkpoint.original_r_start) {
    return "next_r_start must advance original_r_start";
  }
  if (checkpoint.requested_r_final < checkpoint.next_r_start) {
    return "requested_r_final precedes next_r_start";
  }
  if (checkpoint.microband_width == 0) {
    return "missing or invalid microband_width";
  }
  const std::uint64_t expected_carry_width = ceil_sqrt_u64(checkpoint.k_sq);
  if (checkpoint.carry_width != expected_carry_width) {
    return "carry_width does not match k_sq";
  }
  if (checkpoint.microband_width < checkpoint.carry_width) {
    return "microband_width is smaller than carry_width";
  }
  // The neutral checkpoint is replay state for a fixed-width schedule, so the
  // schedule index must locate the exact next cut without source state.
  const unsigned __int128 schedule_offset =
      static_cast<unsigned __int128>(checkpoint.schedule_index) *
      static_cast<unsigned __int128>(checkpoint.microband_width);
  const unsigned __int128 scheduled_next =
      static_cast<unsigned __int128>(checkpoint.original_r_start) +
      schedule_offset;
  const std::uint64_t expected_next =
      scheduled_next > checkpoint.requested_r_final
          ? checkpoint.requested_r_final
          : static_cast<std::uint64_t>(scheduled_next);
  if (checkpoint.schedule_index == 0 ||
      scheduled_next >
          static_cast<unsigned __int128>(checkpoint.requested_r_final) +
              checkpoint.microband_width ||
      checkpoint.next_r_start != expected_next) {
    return "wrong schedule_index";
  }
  if (!valid_manifest_token(checkpoint.schedule_digest_algorithm)) {
    return "missing or invalid schedule_digest_algorithm";
  }
  if (!valid_hex_token(checkpoint.schedule_digest_hex)) {
    return "missing or invalid schedule_digest_hex";
  }
  if (!valid_manifest_token(checkpoint.geometry_id)) {
    return "missing or invalid geometry_id";
  }
  if (!valid_manifest_token(checkpoint.build_id)) {
    return "missing or invalid build_id";
  }
  if (!valid_manifest_token(checkpoint.oracle_id)) {
    return "missing or invalid oracle_id";
  }
  if (!valid_manifest_token(checkpoint.command_id)) {
    return "missing or invalid command_id";
  }
  if (!valid_manifest_token(checkpoint.overflow_summary)) {
    return "missing or invalid overflow_summary";
  }
  if (!valid_manifest_token(checkpoint.proof_status) ||
      is_claim_pass_token(checkpoint.proof_status)) {
    return "missing or invalid proof_status";
  }
  if (!valid_manifest_token(checkpoint.harvest_status)) {
    return "missing or invalid harvest_status";
  }
  if (!valid_manifest_token(checkpoint.replay_status)) {
    return "missing or invalid replay_status";
  }

  if (const std::string diagnostic =
          check_expected(expected.runner_id, checkpoint.runner_id,
                         "wrong runner_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.mode, checkpoint.mode, "wrong mode");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.k_sq, checkpoint.k_sq, "wrong k_sq");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.original_r_start,
                         checkpoint.original_r_start,
                         "wrong original_r_start");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.next_r_start, checkpoint.next_r_start,
                         "stale next_r_start");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.requested_r_final,
                         checkpoint.requested_r_final,
                         "wrong requested_r_final");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.microband_width,
                         checkpoint.microband_width,
                         "wrong microband_width");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.carry_width, checkpoint.carry_width,
                         "wrong carry width");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.schedule_index, checkpoint.schedule_index,
                         "wrong schedule_index");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.schedule_digest_algorithm,
                         checkpoint.schedule_digest_algorithm,
                         "wrong schedule_digest_algorithm");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.schedule_digest_hex,
                         checkpoint.schedule_digest_hex,
                         "wrong schedule_digest_hex");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.geometry_id, checkpoint.geometry_id,
                         "wrong geometry_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.build_id, checkpoint.build_id,
                         "wrong build_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.oracle_id, checkpoint.oracle_id,
                         "wrong oracle_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.command_id, checkpoint.command_id,
                         "wrong command_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.overflow_summary,
                         checkpoint.overflow_summary,
                         "wrong overflow_summary");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.proof_status, checkpoint.proof_status,
                         "wrong proof_status");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.harvest_status, checkpoint.harvest_status,
                         "wrong harvest_status");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.replay_status, checkpoint.replay_status,
                         "wrong replay_status");
      !diagnostic.empty()) {
    return diagnostic;
  }
  return "";
}

std::ostream& write_resumable_band_checkpoint(
    std::ostream& out, const ResumableBandCheckpointV1& checkpoint) {
  out << "LB_RESUMABLE_BAND_CHECKPOINT_V1\n";
  out << "runner_id " << checkpoint.runner_id << "\n";
  out << "mode " << checkpoint.mode << "\n";
  out << "k_sq " << checkpoint.k_sq << "\n";
  out << "original_r_start " << checkpoint.original_r_start << "\n";
  out << "next_r_start " << checkpoint.next_r_start << "\n";
  out << "requested_r_final " << checkpoint.requested_r_final << "\n";
  out << "microband_width " << checkpoint.microband_width << "\n";
  out << "carry_width " << checkpoint.carry_width << "\n";
  out << "schedule_index " << checkpoint.schedule_index << "\n";
  out << "schedule_digest_algorithm "
      << checkpoint.schedule_digest_algorithm << "\n";
  out << "schedule_digest_hex " << checkpoint.schedule_digest_hex << "\n";
  out << "geometry_id " << checkpoint.geometry_id << "\n";
  out << "build_id " << checkpoint.build_id << "\n";
  out << "oracle_id " << checkpoint.oracle_id << "\n";
  out << "command_id " << checkpoint.command_id << "\n";
  out << "overflow_summary " << checkpoint.overflow_summary << "\n";
  out << "proof_status " << checkpoint.proof_status << "\n";
  out << "harvest_status " << checkpoint.harvest_status << "\n";
  out << "replay_status " << checkpoint.replay_status << "\n";
  out << "campaign_tiles_processed "
      << checkpoint.campaign_tiles_processed << "\n";
  out << "tileop_overflows " << checkpoint.tileop_overflows << "\n";
  out << "END_RESUMABLE_BAND_CHECKPOINT\n";
  return out;
}

ResumableBandCheckpointReadResult read_resumable_band_checkpoint(
    std::istream& in,
    const ResumableBandCheckpointExpectedContext& expected) {
  ResumableBandCheckpointReadResult result;
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
  const auto read_string = [&](std::string& value) -> bool {
    return static_cast<bool>(in >> value);
  };

  if (!expect("LB_RESUMABLE_BAND_CHECKPOINT_V1")) {
    return fail("missing resumable band checkpoint header");
  }
  if (!expect("runner_id") || !read_string(result.checkpoint.runner_id)) {
    return fail("missing or invalid runner_id");
  }
  if (!expect("mode") || !read_string(result.checkpoint.mode)) {
    return fail("missing or invalid mode");
  }
  if (!expect("k_sq") || !read_uint64(result.checkpoint.k_sq)) {
    return fail("missing or invalid k_sq");
  }
  if (!expect("original_r_start") ||
      !read_uint64(result.checkpoint.original_r_start)) {
    return fail("missing or invalid original_r_start");
  }
  if (!expect("next_r_start") ||
      !read_uint64(result.checkpoint.next_r_start)) {
    return fail("missing or invalid next_r_start");
  }
  if (!expect("requested_r_final") ||
      !read_uint64(result.checkpoint.requested_r_final)) {
    return fail("missing or invalid requested_r_final");
  }
  if (!expect("microband_width") ||
      !read_uint64(result.checkpoint.microband_width)) {
    return fail("missing or invalid microband_width");
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
  if (!expect("geometry_id") ||
      !read_string(result.checkpoint.geometry_id)) {
    return fail("missing or invalid geometry_id");
  }
  if (!expect("build_id") || !read_string(result.checkpoint.build_id)) {
    return fail("missing or invalid build_id");
  }
  if (!expect("oracle_id") ||
      !read_string(result.checkpoint.oracle_id)) {
    return fail("missing or invalid oracle_id");
  }
  if (!expect("command_id") ||
      !read_string(result.checkpoint.command_id)) {
    return fail("missing or invalid command_id");
  }
  if (!expect("overflow_summary") ||
      !read_string(result.checkpoint.overflow_summary)) {
    return fail("missing or invalid overflow_summary");
  }
  if (!expect("proof_status") ||
      !read_string(result.checkpoint.proof_status)) {
    return fail("missing or invalid proof_status");
  }
  if (!expect("harvest_status") ||
      !read_string(result.checkpoint.harvest_status)) {
    return fail("missing or invalid harvest_status");
  }
  if (!expect("replay_status") ||
      !read_string(result.checkpoint.replay_status)) {
    return fail("missing or invalid replay_status");
  }
  if (!expect("campaign_tiles_processed") ||
      !read_uint64(result.checkpoint.campaign_tiles_processed)) {
    return fail("missing or invalid campaign_tiles_processed");
  }
  if (!expect("tileop_overflows") ||
      !read_uint64(result.checkpoint.tileop_overflows)) {
    return fail("missing or invalid tileop_overflows");
  }
  if (!expect("END_RESUMABLE_BAND_CHECKPOINT")) {
    return fail("missing resumable band checkpoint END marker");
  }
  if (in >> token) {
    return fail("unexpected trailing resumable band checkpoint tokens");
  }

  const std::string validation =
      validate_resumable_band_checkpoint(result.checkpoint, expected);
  if (!validation.empty()) {
    return fail(validation);
  }
  return result;
}

ResumableBandCheckpointReadResult read_resumable_band_checkpoint(
    std::istream& in) {
  return read_resumable_band_checkpoint(
      in, ResumableBandCheckpointExpectedContext{});
}

std::string resumable_band_checkpoint_to_string(
    const ResumableBandCheckpointV1& checkpoint) {
  std::ostringstream out;
  write_resumable_band_checkpoint(out, checkpoint);
  return out.str();
}

ResumableBandCheckpointReadResult resumable_band_checkpoint_from_string(
    std::string_view text,
    const ResumableBandCheckpointExpectedContext& expected) {
  std::istringstream in{std::string(text)};
  return read_resumable_band_checkpoint(in, expected);
}

ResumableBandCheckpointReadResult resumable_band_checkpoint_from_string(
    std::string_view text) {
  return resumable_band_checkpoint_from_string(
      text, ResumableBandCheckpointExpectedContext{});
}

}  // namespace lb_source
