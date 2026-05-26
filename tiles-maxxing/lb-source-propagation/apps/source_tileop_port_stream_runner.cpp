#include "lb_source/diagnostic_telemetry.h"
#include "lb_source/detector_band_handoff.h"
#include "lb_source/source_propagation.h"
#include "lb_source/resumable_band_checkpoint.h"
#include "lb_source/stream_checkpoint.h"
#include "lb_source/tileop_static_reach.h"
#include "lb_source/tileop_port_stream.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/tileop.h"
#include "../../cpp-campaign-v2/src/sha256.h"

namespace {

constexpr std::string_view kRunnerId = "source_tileop_port_stream_runner_v1";
constexpr std::string_view kPhase0Schema = "lb_diagnostic_phase0_v1";
constexpr std::string_view kScheduleDigestAlgorithm =
    "sha256:source_tileop_port_stream_runner_schedule_v1";
constexpr std::string_view kSourceId =
    "tileop_port_stream_diagnostic_source_v1";
constexpr std::string_view kFirstBandSourceMode = "GEO_I_PORT_DIAGNOSTIC";
constexpr std::string_view kGeometryId = "gaussian_octant_tileop_port_v1";
constexpr std::string_view kBuildId = "local_campaign_build";
constexpr std::string_view kOverflowSummary = "none";
constexpr std::string_view kProofStatus = "DIAGNOSTIC_NON_CLAIM";
constexpr std::string_view kResumableMode = "resumable-band";
constexpr std::string_view kOracleId = "tileop_port_stream_v1";
constexpr std::string_view kCommandId =
    "source_tileop_port_stream_runner_fixed_width_v1";
constexpr std::string_view kHarvestStatus = "BAND_HARVEST_PROGRESS_V1";
constexpr std::string_view kReplayStatus = "REPLAYABLE_CONTEXT_V1";
constexpr std::string_view kSupportEnvelopeId =
    "tileop-port-carry-ceil-sqrt-k-v1";
constexpr std::string_view kPortIdentitySchemeId = "tileop-local-port-atom-v1";
constexpr std::string_view kBoundaryPolicyId = "closed-tile-boundary-v1";

struct Config {
  std::uint64_t r_start = 248;
  std::uint64_t r_final = 512;
  std::uint64_t microband_width = 128;
  std::size_t stop_after_microbands = 0;
  std::size_t max_atoms = 1000000;
  std::size_t tileop_threads = 0;
  bool seed_inner_flags = false;
  std::optional<std::string> checkpoint_in;
  std::optional<std::string> checkpoint_out;
  std::optional<std::string> resumable_checkpoint_in;
  std::optional<std::string> resumable_checkpoint_out;
  std::optional<std::string> live_manifest_out;
  std::optional<std::string> progress_out;
  std::optional<std::string> death_out;
};

struct DetectorHandoffWriteResult {
  std::string path;
  std::string sha256_hex;
  std::uint64_t bytes = 0;
  std::uint64_t encode_ms = 0;
  std::uint64_t hash_ms = 0;
  std::uint64_t write_ms = 0;
  std::uint64_t readback_ms = 0;
  std::uint64_t validate_ms = 0;
  std::uint64_t rename_ms = 0;
  std::uint64_t total_ms = 0;
};

struct ScheduleInfo {
  std::uint64_t original_r_start = 0;
  std::uint64_t requested_r_final = 0;
  std::uint64_t microband_width = 0;
  std::vector<std::uint64_t> radii;
  std::string digest_hex;
};

struct RunningMaxima {
  std::uint64_t resident_microband_tiles = 0;
  std::uint64_t resident_tileops = 0;
  std::uint64_t resident_port_atoms = 0;
  std::uint64_t resident_edges = 0;
  std::uint64_t live_frontier_atoms = 0;
  std::uint64_t components = 0;
  std::uint64_t checkpoint_bytes = 0;
};

bool parse_uint64(std::string_view text, std::uint64_t& out) {
  try {
    std::size_t pos = 0;
    const std::uint64_t value = std::stoull(std::string(text), &pos);
    if (pos != text.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

void usage(const char* prog) {
  std::cout
      << "Usage: " << prog << " [OPTIONS]\n\n"
      << "Diagnostic TileOp-port microband stream runner for the LB sidecar.\n"
      << "Outputs are DIAGNOSTIC_NON_CLAIM and are not source-death proofs.\n\n"
      << "Options:\n"
      << "  --r-start R                 starting radius (default 248)\n"
      << "  --r-final R                 final radius (default 512)\n"
      << "  --microband-width W         fixed microband width (default 128)\n"
      << "  --seed-inner-flags          seed first microband from TileOp inner flags\n"
      << "  --checkpoint-in PATH        read LB_SOURCE_STREAM_CHECKPOINT_V1\n"
      << "  --checkpoint-out PATH       write LB_SOURCE_STREAM_CHECKPOINT_V1\n"
      << "  --resumable-checkpoint-in PATH\n"
      << "                              read source-neutral resumable checkpoint\n"
      << "  --resumable-checkpoint-out PATH\n"
      << "                              write source-neutral resumable checkpoint\n"
      << "  --live-manifest-out PATH    write final LB_SOURCE_LIVE_HANDOFF_V1\n"
      << "  --progress-out PATH         write per-microband JSONL progress\n"
      << "  --stop-after-microbands N   stop after N microbands in this process\n"
      << "  --max-atoms N               process_band_live atom/frontier caps\n"
      << "  --tileop-threads N          TileOp worker threads; 0 means hardware auto\n"
      << "  --death-out PATH            unsupported in this diagnostic MVP\n";
}

bool parse_args(int argc, char** argv, Config& config) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      std::exit(EXIT_SUCCESS);
    }

    auto take_value = [&](const std::string& flag,
                          std::string& value) -> bool {
      if (arg.rfind(flag + "=", 0) == 0) {
        value = arg.substr(flag.size() + 1);
        return true;
      }
      if (arg == flag) {
        if (i + 1 >= argc) {
          std::cerr << "missing value for " << flag << "\n";
          std::exit(EXIT_FAILURE);
        }
        value = argv[++i];
        return true;
      }
      return false;
    };

    std::string value;
    if (take_value("--r-start", value)) {
      if (!parse_uint64(value, config.r_start)) {
        std::cerr << "invalid --r-start: " << value << "\n";
        return false;
      }
    } else if (take_value("--r-final", value)) {
      if (!parse_uint64(value, config.r_final)) {
        std::cerr << "invalid --r-final: " << value << "\n";
        return false;
      }
    } else if (take_value("--microband-width", value)) {
      if (!parse_uint64(value, config.microband_width)) {
        std::cerr << "invalid --microband-width: " << value << "\n";
        return false;
      }
    } else if (arg == "--seed-inner-flags") {
      config.seed_inner_flags = true;
    } else if (take_value("--checkpoint-in", value)) {
      if (value.empty()) {
        std::cerr << "--checkpoint-in must not be empty\n";
        return false;
      }
      config.checkpoint_in = value;
    } else if (take_value("--checkpoint-out", value)) {
      if (value.empty()) {
        std::cerr << "--checkpoint-out must not be empty\n";
        return false;
      }
      config.checkpoint_out = value;
    } else if (take_value("--resumable-checkpoint-in", value)) {
      if (value.empty()) {
        std::cerr << "--resumable-checkpoint-in must not be empty\n";
        return false;
      }
      config.resumable_checkpoint_in = value;
    } else if (take_value("--resumable-checkpoint-out", value)) {
      if (value.empty()) {
        std::cerr << "--resumable-checkpoint-out must not be empty\n";
        return false;
      }
      config.resumable_checkpoint_out = value;
    } else if (take_value("--live-manifest-out", value)) {
      if (value.empty()) {
        std::cerr << "--live-manifest-out must not be empty\n";
        return false;
      }
      config.live_manifest_out = value;
    } else if (take_value("--progress-out", value)) {
      if (value.empty()) {
        std::cerr << "--progress-out must not be empty\n";
        return false;
      }
      config.progress_out = value;
    } else if (take_value("--stop-after-microbands", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) || parsed == 0 ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid --stop-after-microbands: " << value << "\n";
        return false;
      }
      config.stop_after_microbands = static_cast<std::size_t>(parsed);
    } else if (take_value("--max-atoms", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) || parsed == 0 ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid --max-atoms: " << value << "\n";
        return false;
      }
      config.max_atoms = static_cast<std::size_t>(parsed);
    } else if (take_value("--tileop-threads", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid --tileop-threads: " << value << "\n";
        return false;
      }
      config.tileop_threads = static_cast<std::size_t>(parsed);
    } else if (take_value("--death-out", value)) {
      if (value.empty()) {
        std::cerr << "--death-out must not be empty\n";
        return false;
      }
      config.death_out = value;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return false;
    }
  }

  if (config.r_start == 0 || config.r_final <= config.r_start ||
      config.microband_width == 0) {
    std::cerr << "--r-start must be positive, --r-final must be greater, "
                 "and --microband-width must be positive\n";
    return false;
  }
  if (config.microband_width <
      lb_source::ceil_sqrt(static_cast<std::uint64_t>(campaign::k_sq_value))) {
    std::cerr << "--microband-width must be at least ceil_sqrt(K_SQ)\n";
    return false;
  }
  if (config.seed_inner_flags && config.checkpoint_in.has_value()) {
    std::cerr << "--seed-inner-flags cannot be combined with "
                 "--checkpoint-in\n";
    return false;
  }
  if (config.seed_inner_flags && config.resumable_checkpoint_in.has_value()) {
    std::cerr << "--seed-inner-flags cannot be combined with "
                 "--resumable-checkpoint-in\n";
    return false;
  }
  if (config.checkpoint_in.has_value() &&
      config.resumable_checkpoint_in.has_value()) {
    std::cerr << "--checkpoint-in cannot be combined with "
                 "--resumable-checkpoint-in\n";
    return false;
  }
  if (config.death_out.has_value()) {
    std::cerr << "--death-out is unsupported in "
              << kRunnerId
              << " diagnostic MVP; no death artifact will be written\n";
    return false;
  }
  return true;
}

void append_json_string(std::ostream& out, std::string_view value) {
  out << '"';
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << ch;
        break;
    }
  }
  out << '"';
}

std::string sha256_hex_string(const std::string& text) {
  return campaign::detail::sha256_hex(text);
}

std::string schedule_digest_hex(std::uint64_t original_r_start,
                                std::uint64_t requested_r_final,
                                std::uint64_t microband_width) {
  std::ostringstream text;
  text << "runner=" << kRunnerId << ";k_sq=" << campaign::k_sq_value
       << ";original_r_start=" << original_r_start
       << ";requested_r_final=" << requested_r_final
       << ";microband_width=" << microband_width
       << ";schedule=v1-fixed-width";
  return sha256_hex_string(text.str());
}

std::vector<std::uint64_t> build_schedule_radii(std::uint64_t original_r_start,
                                                std::uint64_t r_final,
                                                std::uint64_t width) {
  std::vector<std::uint64_t> radii;
  for (std::uint64_t r = original_r_start; r < r_final;) {
    radii.push_back(r);
    const std::uint64_t remaining = r_final - r;
    r += std::min(width, remaining);
  }
  radii.push_back(r_final);
  return radii;
}

std::size_t resolve_tileop_threads(std::size_t requested,
                                   std::size_t work_items) {
  if (work_items == 0) {
    return 1;
  }
  std::size_t threads = requested;
  if (threads == 0) {
    threads = std::thread::hardware_concurrency();
  }
  if (threads == 0) {
    threads = 1;
  }
  return std::min(threads, work_items);
}

std::uint64_t source_carry_atoms(const lb_source::LiveSeparator& separator) {
  std::uint64_t count = 0;
  for (std::size_t c = 0; c < separator.component_partition.size(); ++c) {
    if (!separator.source_bit_per_component[c]) {
      continue;
    }
    count += separator.component_partition[c].size();
  }
  return count;
}

bool has_source_carry(const lb_source::LiveSeparator& separator) {
  return std::find(separator.source_bit_per_component.begin(),
                   separator.source_bit_per_component.end(),
                   true) != separator.source_bit_per_component.end();
}

std::vector<campaign::TileOp> build_tileops(
    const std::vector<campaign::TileCoord>& coords,
    const campaign::CampaignConstants& constants,
    const campaign::Grid& grid,
    std::uint64_t& overflow_tiles,
    std::size_t worker_threads) {
  std::vector<campaign::TileOp> tileops(coords.size());
  worker_threads = std::max<std::size_t>(1, worker_threads);
  if (coords.empty()) {
    return tileops;
  }
  worker_threads = std::min(worker_threads, coords.size());
  if (worker_threads == 1) {
    for (std::size_t i = 0; i < coords.size(); ++i) {
      campaign::TileOp op = campaign::process_tile(coords[i], constants, grid);
      if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
        ++overflow_tiles;
      }
      tileops[i] = op;
    }
    return tileops;
  }

  std::atomic<std::size_t> next_index{0};
  std::vector<std::uint64_t> overflow_by_worker(worker_threads, 0);
  std::vector<std::exception_ptr> errors(worker_threads);
  std::vector<std::thread> workers;
  workers.reserve(worker_threads);
  for (std::size_t worker = 0; worker < worker_threads; ++worker) {
    workers.emplace_back([&, worker]() {
      try {
        while (true) {
          const std::size_t index = next_index.fetch_add(1);
          if (index >= coords.size()) {
            break;
          }
          campaign::TileOp op =
              campaign::process_tile(coords[index], constants, grid);
          if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
            ++overflow_by_worker[worker];
          }
          tileops[index] = op;
        }
      } catch (...) {
        errors[worker] = std::current_exception();
        next_index.store(coords.size());
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  for (const std::exception_ptr& error : errors) {
    if (error) {
      std::rethrow_exception(error);
    }
  }
  for (const std::uint64_t count : overflow_by_worker) {
    overflow_tiles += count;
  }
  return tileops;
}

lb_source::LiveHandoffV1 make_live_handoff(
    std::uint64_t cut_radius,
    std::string_view source_mode,
    const lb_source::LiveSeparator& separator,
    const ScheduleInfo& schedule) {
  lb_source::LiveHandoffV1 handoff;
  handoff.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  handoff.cut_radius = cut_radius;
  handoff.carry_width = lb_source::ceil_sqrt(handoff.k_sq);
  handoff.source_mode = std::string(source_mode);
  handoff.source_id = std::string(kSourceId);
  handoff.geometry_id = std::string(kGeometryId);
  handoff.build_id = std::string(kBuildId);
  handoff.schedule_digest_algorithm = std::string(kScheduleDigestAlgorithm);
  handoff.schedule_digest_hex = schedule.digest_hex;
  handoff.overflow_summary = std::string(kOverflowSummary);
  handoff.separator = separator;
  return lb_source::canonicalize_live_handoff(handoff);
}

lb_source::StreamCheckpointV1 make_checkpoint(
    std::uint64_t next_r_start,
    std::uint64_t schedule_index,
    const std::string& source_mode,
    const lb_source::LiveHandoffV1& handoff,
    const ScheduleInfo& schedule) {
  lb_source::StreamCheckpointV1 checkpoint;
  checkpoint.runner_id = std::string(kRunnerId);
  checkpoint.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  checkpoint.next_r_start = next_r_start;
  checkpoint.requested_r_final = schedule.requested_r_final;
  checkpoint.carry_width = lb_source::ceil_sqrt(checkpoint.k_sq);
  checkpoint.schedule_index = schedule_index;
  checkpoint.schedule_digest_algorithm = std::string(kScheduleDigestAlgorithm);
  checkpoint.schedule_digest_hex = schedule.digest_hex;
  checkpoint.source_mode = source_mode;
  checkpoint.source_id = std::string(kSourceId);
  checkpoint.geometry_id = std::string(kGeometryId);
  checkpoint.build_id = std::string(kBuildId);
  checkpoint.overflow_summary = std::string(kOverflowSummary);
  checkpoint.handoff = handoff;
  return lb_source::canonicalize_stream_checkpoint(checkpoint);
}

lb_source::ResumableBandCheckpointV1 make_resumable_checkpoint(
    std::uint64_t next_r_start,
    std::uint64_t schedule_index,
    std::uint64_t campaign_tiles_processed,
    std::uint64_t tileop_overflows,
    const std::optional<DetectorHandoffWriteResult>& detector_handoff,
    const ScheduleInfo& schedule) {
  lb_source::ResumableBandCheckpointV1 checkpoint;
  checkpoint.runner_id = std::string(kRunnerId);
  checkpoint.mode = std::string(kResumableMode);
  checkpoint.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  checkpoint.original_r_start = schedule.original_r_start;
  checkpoint.next_r_start = next_r_start;
  checkpoint.requested_r_final = schedule.requested_r_final;
  checkpoint.microband_width = schedule.microband_width;
  checkpoint.carry_width = lb_source::ceil_sqrt(checkpoint.k_sq);
  checkpoint.schedule_index = schedule_index;
  checkpoint.schedule_digest_algorithm = std::string(kScheduleDigestAlgorithm);
  checkpoint.schedule_digest_hex = schedule.digest_hex;
  checkpoint.geometry_id = std::string(kGeometryId);
  checkpoint.build_id = std::string(kBuildId);
  checkpoint.oracle_id = std::string(kOracleId);
  checkpoint.command_id = std::string(kCommandId);
  checkpoint.overflow_summary = std::string(kOverflowSummary);
  checkpoint.proof_status = std::string(kProofStatus);
  checkpoint.harvest_status = std::string(kHarvestStatus);
  checkpoint.replay_status = std::string(kReplayStatus);
  if (detector_handoff.has_value()) {
    checkpoint.detector_handoff_path = detector_handoff->path;
    checkpoint.detector_handoff_sha256 = detector_handoff->sha256_hex;
    checkpoint.detector_handoff_schema =
        std::string(lb_source::kDetectorBandHandoffSchema);
    checkpoint.detector_handoff_bytes = detector_handoff->bytes;
  }
  checkpoint.campaign_tiles_processed = campaign_tiles_processed;
  checkpoint.tileop_overflows = tileop_overflows;
  return checkpoint;
}

lb_source::DetectorBandHandoffV1 make_detector_handoff(
    std::uint64_t cut_radius,
    std::uint64_t schedule_index,
    const lb_source::StaticReachSeparator& separator,
    const ScheduleInfo& schedule) {
  lb_source::DetectorBandHandoffV1 handoff;
  handoff.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  handoff.cut_radius = cut_radius;
  handoff.carry_width = lb_source::ceil_sqrt(handoff.k_sq);
  handoff.schedule_index = schedule_index;
  handoff.schedule_digest_algorithm = std::string(kScheduleDigestAlgorithm);
  handoff.schedule_digest_hex = schedule.digest_hex;
  handoff.geometry_id = std::string(kGeometryId);
  handoff.oracle_id = std::string(kOracleId);
  handoff.build_id = std::string(kBuildId);
  handoff.support_envelope_id = std::string(kSupportEnvelopeId);
  handoff.port_identity_scheme_id = std::string(kPortIdentitySchemeId);
  handoff.boundary_policy_id = std::string(kBoundaryPolicyId);
  handoff.separator = separator;
  return lb_source::canonicalize_detector_band_handoff(handoff);
}

lb_source::DetectorBandHandoffExpectedContext expected_detector_context(
    std::uint64_t cut_radius,
    std::uint64_t schedule_index,
    const ScheduleInfo& schedule) {
  lb_source::DetectorBandHandoffExpectedContext expected;
  expected.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  expected.cut_radius = cut_radius;
  expected.carry_width =
      lb_source::ceil_sqrt(static_cast<std::uint64_t>(campaign::k_sq_value));
  expected.schedule_index = schedule_index;
  expected.schedule_digest_algorithm = std::string(kScheduleDigestAlgorithm);
  expected.schedule_digest_hex = schedule.digest_hex;
  expected.geometry_id = std::string(kGeometryId);
  expected.oracle_id = std::string(kOracleId);
  expected.build_id = std::string(kBuildId);
  expected.support_envelope_id = std::string(kSupportEnvelopeId);
  expected.port_identity_scheme_id = std::string(kPortIdentitySchemeId);
  expected.boundary_policy_id = std::string(kBoundaryPolicyId);
  return expected;
}

lb_source::StaticReachSeedPolicy detector_seed_policy(std::size_t segment,
                                                      std::size_t segments) {
  if (segments == 1) {
    return lb_source::StaticReachSeedPolicy::kOneBand;
  }
  if (segment == 0) {
    return lb_source::StaticReachSeedPolicy::kFirstBand;
  }
  if (segment + 1 == segments) {
    return lb_source::StaticReachSeedPolicy::kFinalBand;
  }
  return lb_source::StaticReachSeedPolicy::kInteriorBand;
}

std::string default_detector_handoff_path(const std::string& checkpoint_path) {
  const std::filesystem::path checkpoint(checkpoint_path);
  const std::filesystem::path parent = checkpoint.parent_path();
  const std::filesystem::path live = parent.empty()
                                         ? std::filesystem::path(
                                               "detector_handoff.current.bin")
                                         : parent /
                                               "detector_handoff.current.bin";
  return live.string();
}

std::vector<std::uint8_t> read_binary_file_or_die(
    const std::string& path,
    std::string_view label) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "cannot open " << label << " path: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
}

DetectorHandoffWriteResult write_detector_handoff_atomically_or_die(
    const lb_source::DetectorBandHandoffV1& handoff,
    const std::string& live_path) {
  const auto total_begin = lb_source::DiagnosticClock::now();
  const auto encode_begin = total_begin;
  const lb_source::DetectorBandHandoffBytesResult encoded =
      lb_source::detector_band_handoff_to_bytes(handoff);
  const auto encode_done = lb_source::DiagnosticClock::now();
  if (!encoded.accepted()) {
    std::cerr << "detector handoff encode rejected: "
              << encoded.diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  }
  const auto hash_begin = lb_source::DiagnosticClock::now();
  const std::string sha256_hex =
      campaign::detail::sha256_hex(encoded.bytes.data(), encoded.bytes.size());
  const auto hash_done = lb_source::DiagnosticClock::now();
  const std::string tmp_path = live_path + ".tmp";
  const auto write_begin = lb_source::DiagnosticClock::now();
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "cannot open detector handoff temp path: " << tmp_path
                << "\n";
      std::exit(EXIT_FAILURE);
    }
    out.write(reinterpret_cast<const char*>(encoded.bytes.data()),
              static_cast<std::streamsize>(encoded.bytes.size()));
    if (!out) {
      std::cerr << "cannot write detector handoff temp path: " << tmp_path
                << "\n";
      std::exit(EXIT_FAILURE);
    }
  }
  const auto write_done = lb_source::DiagnosticClock::now();

  const auto readback_begin = lb_source::DiagnosticClock::now();
  const std::vector<std::uint8_t> readback =
      read_binary_file_or_die(tmp_path, "detector handoff temp");
  const auto readback_done = lb_source::DiagnosticClock::now();
  const auto validate_begin = lb_source::DiagnosticClock::now();
  if (readback.size() != encoded.bytes.size() ||
      campaign::detail::sha256_hex(readback.data(), readback.size()) !=
          sha256_hex) {
    std::cerr << "detector handoff temp read-back mismatch\n";
    std::exit(EXIT_FAILURE);
  }
  const lb_source::DetectorBandHandoffReadResult decoded =
      [&]() {
        ScheduleInfo schedule;
        schedule.digest_hex = handoff.schedule_digest_hex;
        return lb_source::detector_band_handoff_from_bytes(
            readback, expected_detector_context(handoff.cut_radius,
                                                handoff.schedule_index,
                                                schedule));
      }();
  if (!decoded.accepted()) {
    std::cerr << "detector handoff temp validation rejected: "
              << decoded.diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  }
  const auto validate_done = lb_source::DiagnosticClock::now();
  const auto rename_begin = validate_done;
  if (std::rename(tmp_path.c_str(), live_path.c_str()) != 0) {
    std::cerr << "cannot atomically replace detector handoff path: "
              << live_path << "\n";
    std::exit(EXIT_FAILURE);
  }
  const auto rename_done = lb_source::DiagnosticClock::now();
  return {.path = live_path,
          .sha256_hex = sha256_hex,
          .bytes = static_cast<std::uint64_t>(encoded.bytes.size()),
          .encode_ms = lb_source::elapsed_ms(encode_begin, encode_done),
          .hash_ms = lb_source::elapsed_ms(hash_begin, hash_done),
          .write_ms = lb_source::elapsed_ms(write_begin, write_done),
          .readback_ms = lb_source::elapsed_ms(readback_begin, readback_done),
          .validate_ms = lb_source::elapsed_ms(validate_begin, validate_done),
          .rename_ms = lb_source::elapsed_ms(rename_begin, rename_done),
          .total_ms = lb_source::elapsed_ms(total_begin, rename_done)};
}

std::uint64_t wall_share_basis_points(std::uint64_t part_ms,
                                      std::uint64_t total_ms) {
  if (total_ms == 0) {
    return 0;
  }
  const long double share =
      static_cast<long double>(part_ms) * 10000.0L /
      static_cast<long double>(total_ms);
  if (share <= 0.0L) {
    return 0;
  }
  if (share >= 10000.0L) {
    return 10000;
  }
  return static_cast<std::uint64_t>(share);
}

std::uint64_t checkpoint_size_bytes(const lb_source::StreamCheckpointV1& cp) {
  return static_cast<std::uint64_t>(
      lb_source::stream_checkpoint_to_string(cp).size());
}

std::uint64_t checkpoint_size_bytes(
    const lb_source::ResumableBandCheckpointV1& cp) {
  return static_cast<std::uint64_t>(
      lb_source::resumable_band_checkpoint_to_string(cp).size());
}

lb_source::StreamCheckpointV1 read_checkpoint_or_die(
    const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open --checkpoint-in path: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }
  const lb_source::StreamCheckpointReadResult result =
      lb_source::read_stream_checkpoint(in);
  if (!result.accepted()) {
    std::cerr << "invalid --checkpoint-in: " << result.diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  }
  return result.checkpoint;
}

lb_source::ResumableBandCheckpointV1 read_resumable_checkpoint_or_die(
    const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open --resumable-checkpoint-in path: " << path
              << "\n";
    std::exit(EXIT_FAILURE);
  }
  const lb_source::ResumableBandCheckpointReadResult result =
      lb_source::read_resumable_band_checkpoint(in);
  if (!result.accepted()) {
    std::cerr << "invalid --resumable-checkpoint-in: "
              << result.diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  }
  return result.checkpoint;
}

void validate_checkpoint_binding_or_die(
    const lb_source::StreamCheckpointV1& checkpoint,
    const Config& config,
    const ScheduleInfo& schedule) {
  const auto fail = [](std::string_view diagnostic) {
    std::cerr << "invalid --checkpoint-in: " << diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  };
  if (checkpoint.runner_id != kRunnerId) {
    fail("wrong runner_id");
  }
  if (checkpoint.k_sq != static_cast<std::uint64_t>(campaign::k_sq_value)) {
    fail("wrong k_sq");
  }
  if (checkpoint.next_r_start != config.r_start) {
    fail("stale next_r_start");
  }
  if (checkpoint.requested_r_final != config.r_final) {
    fail("wrong requested_r_final");
  }
  if (checkpoint.carry_width !=
      lb_source::ceil_sqrt(static_cast<std::uint64_t>(campaign::k_sq_value))) {
    fail("wrong carry width");
  }
  if (checkpoint.schedule_index >= schedule.radii.size() ||
      schedule.radii[checkpoint.schedule_index] != config.r_start) {
    fail("wrong schedule_index");
  }
  if (checkpoint.schedule_digest_algorithm != kScheduleDigestAlgorithm) {
    fail("wrong schedule_digest_algorithm");
  }
  if (checkpoint.schedule_digest_hex != schedule.digest_hex) {
    fail("wrong schedule_digest_hex");
  }
  if (checkpoint.source_mode != kFirstBandSourceMode) {
    fail("wrong source_mode");
  }
  if (checkpoint.source_id != kSourceId) {
    fail("wrong source_id");
  }
  if (checkpoint.geometry_id != kGeometryId) {
    fail("wrong geometry_id");
  }
  if (checkpoint.build_id != kBuildId) {
    fail("wrong build_id");
  }
  if (checkpoint.overflow_summary != kOverflowSummary) {
    fail("wrong overflow_summary");
  }
}

void validate_resumable_checkpoint_binding_or_die(
    const lb_source::ResumableBandCheckpointV1& checkpoint,
    const Config& config,
    const ScheduleInfo& schedule) {
  lb_source::ResumableBandCheckpointExpectedContext expected;
  expected.runner_id = std::string(kRunnerId);
  expected.mode = std::string(kResumableMode);
  expected.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  expected.original_r_start = schedule.original_r_start;
  expected.next_r_start = config.r_start;
  expected.requested_r_final = config.r_final;
  expected.microband_width = config.microband_width;
  expected.carry_width =
      lb_source::ceil_sqrt(static_cast<std::uint64_t>(campaign::k_sq_value));
  expected.schedule_index = checkpoint.schedule_index;
  expected.schedule_digest_algorithm = std::string(kScheduleDigestAlgorithm);
  expected.schedule_digest_hex = schedule.digest_hex;
  expected.geometry_id = std::string(kGeometryId);
  expected.build_id = std::string(kBuildId);
  expected.oracle_id = std::string(kOracleId);
  expected.command_id = std::string(kCommandId);
  expected.overflow_summary = std::string(kOverflowSummary);
  expected.proof_status = std::string(kProofStatus);
  expected.harvest_status = std::string(kHarvestStatus);
  expected.replay_status = std::string(kReplayStatus);
  if (checkpoint.schedule_index > 0) {
    expected.require_detector_handoff_reference = true;
  }
  const std::string validation =
      lb_source::validate_resumable_band_checkpoint(checkpoint, expected);
  if (!validation.empty()) {
    std::cerr << "invalid --resumable-checkpoint-in: " << validation << "\n";
    std::exit(EXIT_FAILURE);
  }
  if (checkpoint.schedule_index >= schedule.radii.size() ||
      schedule.radii[checkpoint.schedule_index] != config.r_start) {
    std::cerr << "invalid --resumable-checkpoint-in: wrong schedule_index\n";
    std::exit(EXIT_FAILURE);
  }
}

lb_source::DetectorBandHandoffV1 read_detector_handoff_for_checkpoint_or_die(
    const lb_source::ResumableBandCheckpointV1& checkpoint,
    const ScheduleInfo& schedule) {
  const std::vector<std::uint8_t> bytes =
      read_binary_file_or_die(checkpoint.detector_handoff_path,
                              "--resumable-checkpoint-in detector handoff");
  if (bytes.size() != checkpoint.detector_handoff_bytes) {
    std::cerr << "invalid --resumable-checkpoint-in: wrong "
                 "detector_handoff_bytes\n";
    std::exit(EXIT_FAILURE);
  }
  const std::string actual_sha256 =
      campaign::detail::sha256_hex(bytes.data(), bytes.size());
  if (actual_sha256 != checkpoint.detector_handoff_sha256) {
    std::cerr << "invalid --resumable-checkpoint-in: wrong "
                 "detector_handoff_sha256\n";
    std::exit(EXIT_FAILURE);
  }
  const lb_source::DetectorBandHandoffReadResult result =
      lb_source::detector_band_handoff_from_bytes(
          bytes, expected_detector_context(checkpoint.next_r_start,
                                           checkpoint.schedule_index,
                                           schedule));
  if (!result.accepted()) {
    std::cerr << "invalid --resumable-checkpoint-in detector handoff: "
              << result.diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  }
  return result.handoff;
}

ScheduleInfo build_schedule_info(
    const Config& config,
    const std::optional<lb_source::StreamCheckpointV1>& checkpoint,
    const std::optional<lb_source::ResumableBandCheckpointV1>&
        resumable_checkpoint) {
  ScheduleInfo schedule;
  schedule.requested_r_final = config.r_final;
  schedule.microband_width = config.microband_width;
  if (checkpoint.has_value()) {
    const unsigned __int128 offset =
        static_cast<unsigned __int128>(checkpoint->schedule_index) *
        static_cast<unsigned __int128>(config.microband_width);
    if (offset > config.r_start) {
      std::cerr << "invalid --checkpoint-in: wrong schedule_index\n";
      std::exit(EXIT_FAILURE);
    }
    schedule.original_r_start =
        config.r_start - static_cast<std::uint64_t>(offset);
  } else if (resumable_checkpoint.has_value()) {
    schedule.original_r_start = resumable_checkpoint->original_r_start;
  } else {
    schedule.original_r_start = config.r_start;
  }
  schedule.radii = build_schedule_radii(schedule.original_r_start,
                                        schedule.requested_r_final,
                                        schedule.microband_width);
  schedule.digest_hex =
      schedule_digest_hex(schedule.original_r_start, schedule.requested_r_final,
                          schedule.microband_width);
  return schedule;
}

}  // namespace

int main(int argc, char** argv) {
  const lb_source::ElapsedTimer run_timer;
  Config config;
  if (!parse_args(argc, argv, config)) {
    return EXIT_FAILURE;
  }

  std::ofstream progress;
  if (config.progress_out.has_value()) {
    progress.open(*config.progress_out);
    if (!progress) {
      std::cerr << "cannot open --progress-out path: " << *config.progress_out
                << "\n";
      return EXIT_FAILURE;
    }
  }

  std::optional<lb_source::StreamCheckpointV1> checkpoint;
  std::optional<lb_source::ResumableBandCheckpointV1> resumable_checkpoint;
  std::optional<lb_source::LiveSeparator> live_incoming;
  std::optional<lb_source::StaticReachSeparator> detector_incoming;
  std::optional<lb_source::LiveHandoffV1> current_live_handoff;
  std::optional<lb_source::DetectorBandHandoffV1> current_detector_handoff;
  std::optional<DetectorHandoffWriteResult> current_detector_handoff_ref;
  std::string checkpoint_handoff_source = "none";
  std::string source_mode = config.seed_inner_flags
                                ? std::string(kFirstBandSourceMode)
                                : "NONE";
  std::uint64_t schedule_index = 0;
  std::uint64_t detector_handoff_wall_ms = 0;
  std::uint64_t checkpoint_write_ms = 0;
  std::uint64_t materialization_ms = 0;

  if (config.checkpoint_in.has_value()) {
    checkpoint = read_checkpoint_or_die(*config.checkpoint_in);
    if (checkpoint->source_mode != kFirstBandSourceMode ||
        checkpoint->handoff.source_mode != kFirstBandSourceMode) {
      std::cerr << "invalid --checkpoint-in: wrong source_mode\n";
      return EXIT_FAILURE;
    }
    live_incoming = checkpoint->handoff.separator;
    current_live_handoff = checkpoint->handoff;
    source_mode = checkpoint->source_mode;
    schedule_index = checkpoint->schedule_index;
  }

  if (config.resumable_checkpoint_in.has_value()) {
    resumable_checkpoint =
        read_resumable_checkpoint_or_die(*config.resumable_checkpoint_in);
    schedule_index = resumable_checkpoint->schedule_index;
  }

  const ScheduleInfo schedule =
      build_schedule_info(config, checkpoint, resumable_checkpoint);
  if (checkpoint.has_value()) {
    validate_checkpoint_binding_or_die(*checkpoint, config, schedule);
  }
  if (resumable_checkpoint.has_value()) {
    validate_resumable_checkpoint_binding_or_die(*resumable_checkpoint,
                                                 config, schedule);
    if (resumable_checkpoint->schedule_index > 0) {
      current_detector_handoff =
          read_detector_handoff_for_checkpoint_or_die(*resumable_checkpoint,
                                                      schedule);
      detector_incoming = current_detector_handoff->separator;
      current_detector_handoff_ref = {
          .path = resumable_checkpoint->detector_handoff_path,
          .sha256_hex = resumable_checkpoint->detector_handoff_sha256,
          .bytes = resumable_checkpoint->detector_handoff_bytes};
      checkpoint_handoff_source = "checkpoint_in";
    }
  }
  if (schedule.radii.empty() || schedule_index >= schedule.radii.size() ||
      schedule.radii[schedule_index] != config.r_start) {
    std::cerr << "requested --r-start is not aligned with the microband "
                 "schedule\n";
    return EXIT_FAILURE;
  }
  if (config.stop_after_microbands != 0 &&
      config.stop_after_microbands > schedule.radii.size() - 1 -
                                         schedule_index) {
    std::cerr << "--stop-after-microbands exceeds remaining microband count\n";
    return EXIT_FAILURE;
  }

  campaign::CampaignConstants tileop_constants;
  try {
    tileop_constants = campaign::CampaignConstants::from_radii(
        schedule.original_r_start, schedule.requested_r_final,
        campaign::k_sq_value);
  } catch (const std::exception& ex) {
    std::cerr << "campaign run construction failed: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }

  lb_source::LiveProcessResult live_last;
  lb_source::StaticReachProcessResult detector_last;
  std::uint64_t processed_outer = config.r_start;
  std::uint64_t microbands_processed_this_run = 0;
  std::uint64_t campaign_tiles_processed =
      resumable_checkpoint.has_value()
          ? resumable_checkpoint->campaign_tiles_processed
          : 0;
  std::uint64_t tileop_overflows = resumable_checkpoint.has_value()
                                       ? resumable_checkpoint->tileop_overflows
                                       : 0;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::uint64_t max_tileop_threads = 0;
  RunningMaxima maxima;
  const std::size_t segment_count =
      schedule.radii.empty() ? 0 : schedule.radii.size() - 1;
  const std::optional<std::string> detector_handoff_live_path =
      config.resumable_checkpoint_out.has_value()
          ? std::optional<std::string>(default_detector_handoff_path(
                *config.resumable_checkpoint_out))
          : std::nullopt;

  for (std::size_t segment = schedule_index; segment + 1 < schedule.radii.size();
       ++segment) {
    const auto band_begin = lb_source::DiagnosticClock::now();
    const std::uint64_t r_start = schedule.radii[segment];
    const std::uint64_t r_outer = schedule.radii[segment + 1];

    campaign::Grid grid;
    const auto grid_begin = lb_source::DiagnosticClock::now();
    try {
      grid = campaign::Grid::build(r_start, r_outer, campaign::k_sq_value);
    } catch (const std::exception& ex) {
      std::cerr << "campaign microband construction failed: " << ex.what()
                << "\n";
      return EXIT_FAILURE;
    }
    const std::string invariant_error = grid.verify_invariants();
    if (!invariant_error.empty()) {
      std::cerr << "grid invariant failed: " << invariant_error << "\n";
      return EXIT_FAILURE;
    }
    const auto grid_done = lb_source::DiagnosticClock::now();

    const std::vector<campaign::TileCoord> coords =
        grid.enumerate_active_tiles();
    const auto enumerate_done = lb_source::DiagnosticClock::now();
    const lb_source::RssSnapshot rss_after_enumerate =
        lb_source::rss_snapshot();
    campaign_tiles_processed += coords.size();
    const std::size_t tileop_threads =
        resolve_tileop_threads(config.tileop_threads, coords.size());
    max_tileop_threads =
        std::max<std::uint64_t>(max_tileop_threads, tileop_threads);

    std::vector<campaign::TileOp> tileops =
        build_tileops(coords, tileop_constants, grid, tileop_overflows,
                      tileop_threads);
    const auto tileop_done = lb_source::DiagnosticClock::now();
    const lb_source::RssSnapshot rss_after_tileop = lb_source::rss_snapshot();

    const lb_source::TileOpPortStreamResult stream =
        lb_source::build_tileop_port_microband({
            .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
            .outer_radius = r_outer,
            .coords = coords,
            .tileops = tileops,
            .seed_inner_flags = config.seed_inner_flags && segment == 0,
        });
    if (!stream.accepted()) {
      std::cerr << "TileOp port microband rejected: " << stream.diagnostic
                << "\n";
      return EXIT_FAILURE;
    }
    const auto stream_done = lb_source::DiagnosticClock::now();
    const lb_source::RssSnapshot rss_after_stream = lb_source::rss_snapshot();

    const lb_source::TileOpStaticReachMicrobandResult detector_band =
        lb_source::build_tileop_static_reach_microband({
            .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
            .outer_radius = r_outer,
            .coords = coords,
            .tileops = tileops,
            .seed_policy = detector_seed_policy(segment, segment_count),
        });
    if (!detector_band.accepted()) {
      std::cerr << "TileOp static reach microband rejected: "
                << detector_band.diagnostic << "\n";
      return EXIT_FAILURE;
    }
    detector_last = lb_source::process_static_reach_band(
        detector_band.band, detector_incoming,
        {.max_atoms = config.max_atoms,
         .max_carry_atoms = config.max_atoms,
         .max_components = config.max_atoms});
    if (!detector_last.accepted()) {
      std::cerr << "TileOp static reach process rejected: "
                << detector_last.diagnostic << "\n";
      return EXIT_FAILURE;
    }

    live_last = lb_source::process_band_live(
        stream.band, live_incoming,
        {.max_atoms = config.max_atoms,
         .max_carry_atoms = config.max_atoms,
         .max_components = config.max_atoms,
         .max_inventory_atoms = config.max_atoms});
    const auto process_done = lb_source::DiagnosticClock::now();
    const lb_source::RssSnapshot rss_after_process = lb_source::rss_snapshot();
    materialization_ms += lb_source::elapsed_ms(band_begin, process_done);
    lb_source::RssSnapshot rss_after_handoff_write;

    port_atoms += stream.port_atoms;
    internal_edges += stream.internal_edges;
    seam_edges += stream.seam_edges;
    processed_outer = r_outer;
    ++schedule_index;
    ++microbands_processed_this_run;

    const std::uint64_t resident_edges =
        stream.internal_edges + stream.seam_edges;
    maxima.resident_microband_tiles =
        std::max<std::uint64_t>(maxima.resident_microband_tiles, coords.size());
    maxima.resident_tileops =
        std::max<std::uint64_t>(maxima.resident_tileops, tileops.size());
    maxima.resident_port_atoms =
        std::max(maxima.resident_port_atoms, stream.port_atoms);
    maxima.resident_edges = std::max(maxima.resident_edges, resident_edges);

    if (live_last.accepted()) {
      current_live_handoff =
          make_live_handoff(r_outer, source_mode, live_last.outgoing, schedule);
      maxima.live_frontier_atoms =
          std::max<std::uint64_t>(maxima.live_frontier_atoms,
                                  live_last.outgoing.carry_atoms.size());
      maxima.components =
          std::max<std::uint64_t>(
              maxima.components,
              live_last.outgoing.component_partition.size());
      maxima.components =
          std::max<std::uint64_t>(
              maxima.components,
              detector_last.outgoing.component_partition.size());
      current_detector_handoff =
          make_detector_handoff(r_outer, schedule_index,
                                detector_last.outgoing, schedule);
      if (detector_handoff_live_path.has_value()) {
        current_detector_handoff_ref =
            write_detector_handoff_atomically_or_die(
                *current_detector_handoff, *detector_handoff_live_path);
        detector_handoff_wall_ms += current_detector_handoff_ref->total_ms;
        checkpoint_handoff_source = "written";
        rss_after_handoff_write = lb_source::rss_snapshot();
      }
      const lb_source::ResumableBandCheckpointV1 progress_checkpoint =
          make_resumable_checkpoint(
              r_outer, schedule_index, campaign_tiles_processed,
              tileop_overflows, current_detector_handoff_ref, schedule);
      maxima.checkpoint_bytes =
          std::max(maxima.checkpoint_bytes,
                   checkpoint_size_bytes(progress_checkpoint));
    }

    if (progress) {
      const bool accepted = live_last.accepted();
      const bool source_carry =
          accepted && !live_last.terminal_source_dead &&
          has_source_carry(live_last.outgoing);
      progress << "{\"schema\":\"lb_source_tileop_port_stream_progress_v1\""
               << ",\"runner_id\":\"" << kRunnerId << "\""
               << ",\"microband_index\":" << (schedule_index - 1)
               << ",\"r_start\":" << r_start
               << ",\"r_outer\":" << r_outer
               << ",\"tiles\":" << coords.size()
               << ",\"tileop_worker_threads\":" << tileop_threads
               << ",\"port_atoms\":" << stream.port_atoms
               << ",\"internal_edges\":" << stream.internal_edges
               << ",\"seam_edges\":" << stream.seam_edges
               << ",\"outgoing_carry_atoms\":"
               << (accepted ? live_last.outgoing.carry_atoms.size() : 0)
               << ",\"outgoing_components\":"
               << (accepted ? live_last.outgoing.component_partition.size()
                            : 0)
               << ",\"source_carry_atoms\":"
               << (source_carry ? source_carry_atoms(live_last.outgoing) : 0)
               << ",\"grid_ms\":"
               << lb_source::elapsed_ms(grid_begin, grid_done)
               << ",\"enumerate_ms\":"
               << lb_source::elapsed_ms(grid_done, enumerate_done)
               << ",\"tileop_ms\":"
               << lb_source::elapsed_ms(enumerate_done, tileop_done)
               << ",\"microband_build_ms\":"
               << lb_source::elapsed_ms(tileop_done, stream_done)
               << ",\"process_ms\":"
               << lb_source::elapsed_ms(stream_done, process_done)
               << ",\"total_ms\":"
               << lb_source::elapsed_ms(band_begin, process_done)
               << ",\"rss_after_enumerate_bytes\":"
               << rss_after_enumerate.current_bytes
               << ",\"peak_rss_after_enumerate_bytes\":"
               << rss_after_enumerate.peak_bytes
               << ",\"rss_after_tileop_bytes\":"
               << rss_after_tileop.current_bytes
               << ",\"peak_rss_after_tileop_bytes\":"
               << rss_after_tileop.peak_bytes
               << ",\"rss_after_stream_bytes\":"
               << rss_after_stream.current_bytes
               << ",\"peak_rss_after_stream_bytes\":"
               << rss_after_stream.peak_bytes
               << ",\"rss_after_process_bytes\":"
               << rss_after_process.current_bytes
               << ",\"peak_rss_after_process_bytes\":"
               << rss_after_process.peak_bytes
               << ",\"rss_after_handoff_write_bytes\":"
               << rss_after_handoff_write.current_bytes
               << ",\"peak_rss_after_handoff_write_bytes\":"
               << rss_after_handoff_write.peak_bytes
               << ",\"accepted\":" << (accepted ? "true" : "false")
               << ",\"reject\":\""
               << lb_source::reject_reason_name(live_last.reject) << "\""
               << ",\"reject_diagnostic\":";
      append_json_string(progress, live_last.diagnostic);
      progress << ",\"terminal_source_dead\":"
               << (accepted && live_last.terminal_source_dead ? "true"
                                                              : "false")
               << ",\"has_source_carry\":"
               << (source_carry ? "true" : "false")
               << ",\"max_resident_microband_tiles\":"
               << maxima.resident_microband_tiles
               << ",\"max_resident_tileops\":" << maxima.resident_tileops
               << ",\"max_resident_port_atoms\":"
               << maxima.resident_port_atoms
               << ",\"max_resident_edges\":" << maxima.resident_edges
               << ",\"max_live_frontier_atoms\":"
               << maxima.live_frontier_atoms
               << ",\"max_components\":" << maxima.components
               << ",\"max_checkpoint_bytes\":"
               << maxima.checkpoint_bytes << "}\n";
      progress.flush();
    }

    if (!live_last.accepted() || live_last.terminal_source_dead) {
      break;
    }
    live_incoming = live_last.outgoing;
    detector_incoming = detector_last.outgoing;

    if (config.stop_after_microbands != 0 &&
        microbands_processed_this_run >= config.stop_after_microbands) {
      break;
    }
  }

  bool live_manifest_written = false;
  const bool accepted = live_last.accepted();
  const bool source_carry =
      accepted && !live_last.terminal_source_dead &&
      has_source_carry(live_last.outgoing);
  if (config.live_manifest_out.has_value()) {
    if (!source_carry || !current_live_handoff.has_value()) {
      std::cerr << "--live-manifest-out requires accepted live source carry\n";
      return EXIT_FAILURE;
    }
    std::ofstream out(*config.live_manifest_out);
    if (!out) {
      std::cerr << "cannot open --live-manifest-out path: "
                << *config.live_manifest_out << "\n";
      return EXIT_FAILURE;
    }
    lb_source::write_live_handoff(out, *current_live_handoff);
    live_manifest_written = true;
  }

  bool checkpoint_written = false;
  std::uint64_t checkpoint_bytes = 0;
  if (config.checkpoint_out.has_value()) {
    if (!source_carry || !current_live_handoff.has_value()) {
      std::cerr << "--checkpoint-out requires accepted live source carry\n";
      return EXIT_FAILURE;
    }
    const lb_source::StreamCheckpointV1 out_checkpoint =
        make_checkpoint(processed_outer, schedule_index, source_mode,
                        *current_live_handoff, schedule);
    checkpoint_bytes = checkpoint_size_bytes(out_checkpoint);
    const auto checkpoint_write_begin = lb_source::DiagnosticClock::now();
    std::ofstream out(*config.checkpoint_out);
    if (!out) {
      std::cerr << "cannot open --checkpoint-out path: "
                << *config.checkpoint_out << "\n";
      return EXIT_FAILURE;
    }
    lb_source::write_stream_checkpoint(out, out_checkpoint);
    const auto checkpoint_write_done = lb_source::DiagnosticClock::now();
    checkpoint_write_ms +=
        lb_source::elapsed_ms(checkpoint_write_begin, checkpoint_write_done);
    checkpoint_written = true;
  }

  bool resumable_checkpoint_written = false;
  std::uint64_t resumable_checkpoint_bytes = 0;
  if (config.resumable_checkpoint_out.has_value()) {
    if (!accepted) {
      std::cerr << "--resumable-checkpoint-out requires an accepted band run\n";
      return EXIT_FAILURE;
    }
    const lb_source::ResumableBandCheckpointV1 out_checkpoint =
        make_resumable_checkpoint(processed_outer, schedule_index,
                                  campaign_tiles_processed, tileop_overflows,
                                  current_detector_handoff_ref, schedule);
    resumable_checkpoint_bytes = checkpoint_size_bytes(out_checkpoint);
    const auto checkpoint_write_begin = lb_source::DiagnosticClock::now();
    std::ofstream out(*config.resumable_checkpoint_out);
    if (!out) {
      std::cerr << "cannot open --resumable-checkpoint-out path: "
                << *config.resumable_checkpoint_out << "\n";
      return EXIT_FAILURE;
    }
    lb_source::write_resumable_band_checkpoint(out, out_checkpoint);
    const auto checkpoint_write_done = lb_source::DiagnosticClock::now();
    checkpoint_write_ms +=
        lb_source::elapsed_ms(checkpoint_write_begin, checkpoint_write_done);
    resumable_checkpoint_written = true;
  }

  const char* death_summary_status =
      accepted && live_last.terminal_source_dead
          ? "UNSUPPORTED_DIAGNOSTIC_MVP"
          : "NOT_TERMINAL";
  const lb_source::RssSnapshot final_rss = lb_source::rss_snapshot();
  const std::uint64_t wall_ms = run_timer.elapsed_ms();
  const std::uint64_t detector_handoff_encode_ms =
      current_detector_handoff_ref.has_value()
          ? current_detector_handoff_ref->encode_ms
          : 0;
  const std::uint64_t detector_handoff_hash_ms =
      current_detector_handoff_ref.has_value()
          ? current_detector_handoff_ref->hash_ms
          : 0;
  const std::uint64_t detector_handoff_write_ms =
      current_detector_handoff_ref.has_value()
          ? current_detector_handoff_ref->write_ms
          : 0;
  const std::uint64_t detector_handoff_readback_ms =
      current_detector_handoff_ref.has_value()
          ? current_detector_handoff_ref->readback_ms
          : 0;
  const std::uint64_t detector_handoff_validate_ms =
      current_detector_handoff_ref.has_value()
          ? current_detector_handoff_ref->validate_ms
          : 0;
  const std::uint64_t detector_handoff_rename_ms =
      current_detector_handoff_ref.has_value()
          ? current_detector_handoff_ref->rename_ms
          : 0;
  const std::uint64_t detector_handoff_total_ms =
      current_detector_handoff_ref.has_value()
          ? current_detector_handoff_ref->total_ms
          : 0;

  std::cout << "{"
            << "\"schema\":\"lb_source_tileop_port_stream_runner_v1\","
            << "\"phase0_schema\":\"" << kPhase0Schema << "\","
            << "\"runner_id\":\"" << kRunnerId << "\","
            << "\"claim_label\":\"SOURCE_TILEOP_PORT_STREAM_DIAGNOSTIC\","
            << "\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\","
            << "\"resumable_mode\":\"" << kResumableMode << "\","
            << "\"source_mode\":\"" << source_mode << "\","
            << "\"k_sq\":" << campaign::k_sq_value
            << ",\"original_r_start\":" << schedule.original_r_start
            << ",\"r_start\":" << config.r_start
            << ",\"r_final\":" << processed_outer
            << ",\"requested_r_final\":" << config.r_final
            << ",\"microband_width\":" << config.microband_width
            << ",\"schedule_digest_algorithm\":\""
            << kScheduleDigestAlgorithm << "\""
            << ",\"schedule_digest_hex\":\"" << schedule.digest_hex << "\""
            << ",\"schedule_index\":" << schedule_index
            << ",\"microbands_processed\":"
            << microbands_processed_this_run
            << ",\"tileop_worker_threads\":" << max_tileop_threads
            << ",\"campaign_tiles_processed\":" << campaign_tiles_processed
            << ",\"tileop_overflows\":" << tileop_overflows
            << ",\"port_atoms\":" << port_atoms
            << ",\"internal_edges\":" << internal_edges
            << ",\"seam_edges\":" << seam_edges
            << ",\"accepted\":" << (accepted ? "true" : "false")
            << ",\"reject\":\""
            << lb_source::reject_reason_name(live_last.reject)
            << "\",\"reject_diagnostic\":";
  append_json_string(std::cout, live_last.diagnostic);
  std::cout << ",\"terminal_source_dead\":"
            << (accepted && live_last.terminal_source_dead ? "true" : "false")
            << ",\"has_source_carry\":"
            << (source_carry ? "true" : "false")
            << ",\"source_carry_atoms\":"
            << (source_carry ? source_carry_atoms(live_last.outgoing) : 0)
            << ",\"outgoing_carry_atoms\":"
            << (accepted ? live_last.outgoing.carry_atoms.size() : 0)
            << ",\"outgoing_components\":"
            << (accepted ? live_last.outgoing.component_partition.size() : 0)
            << ",\"live_manifest_written\":"
            << (live_manifest_written ? "true" : "false")
            << ",\"checkpoint_written\":"
            << (checkpoint_written ? "true" : "false")
            << ",\"checkpoint_bytes\":" << checkpoint_bytes
            << ",\"resumable_checkpoint_written\":"
            << (resumable_checkpoint_written ? "true" : "false")
            << ",\"resumable_checkpoint_bytes\":"
            << resumable_checkpoint_bytes
            << ",\"checkpoint_write_ms\":" << checkpoint_write_ms
            << ",\"detector_spanning\":"
            << (detector_last.spanning ? "true" : "false")
            << ",\"detector_handoff_written\":"
            << (current_detector_handoff_ref.has_value() ? "true" : "false")
            << ",\"checkpoint_handoff_source\":";
  append_json_string(std::cout, checkpoint_handoff_source);
  std::cout << ",\"detector_handoff_encode_ms\":"
            << detector_handoff_encode_ms
            << ",\"detector_handoff_hash_ms\":" << detector_handoff_hash_ms
            << ",\"detector_handoff_write_ms\":" << detector_handoff_write_ms
            << ",\"detector_handoff_readback_ms\":"
            << detector_handoff_readback_ms
            << ",\"detector_handoff_validate_ms\":"
            << detector_handoff_validate_ms
            << ",\"detector_handoff_rename_ms\":"
            << detector_handoff_rename_ms
            << ",\"detector_handoff_total_ms\":"
            << detector_handoff_total_ms
            << ",\"detector_handoff_sha256\":";
  append_json_string(std::cout,
                     current_detector_handoff_ref.has_value()
                         ? current_detector_handoff_ref->sha256_hex
                         : "");
  std::cout << ",\"detector_handoff_bytes\":"
            << (current_detector_handoff_ref.has_value()
                    ? current_detector_handoff_ref->bytes
                    : 0)
            << ",\"death_summary_status\":\"" << death_summary_status << "\""
            << ",\"death_written\":false"
            << ",\"max_resident_microband_tiles\":"
            << maxima.resident_microband_tiles
            << ",\"max_resident_tileops\":" << maxima.resident_tileops
            << ",\"max_resident_port_atoms\":" << maxima.resident_port_atoms
            << ",\"max_resident_edges\":" << maxima.resident_edges
            << ",\"max_live_frontier_atoms\":" << maxima.live_frontier_atoms
            << ",\"max_components\":" << maxima.components
            << ",\"max_checkpoint_bytes\":" << maxima.checkpoint_bytes
            << ",\"rss_bytes\":" << final_rss.current_bytes
            << ",\"peak_rss_bytes\":" << final_rss.peak_bytes
            << ",\"wall_ms\":" << wall_ms
            << ",\"materialization_ms\":" << materialization_ms
            << ",\"detector_handoff_wall_ms\":"
            << detector_handoff_wall_ms
            << ",\"handoff_wall_share_bp\":"
            << wall_share_basis_points(detector_handoff_wall_ms, wall_ms)
            << ",\"checkpoint_wall_share_bp\":"
            << wall_share_basis_points(checkpoint_write_ms, wall_ms)
            << ",\"materialization_wall_share_bp\":"
            << wall_share_basis_points(materialization_ms, wall_ms)
            << "}\n";

  return accepted ? EXIT_SUCCESS : EXIT_FAILURE;
}
