#include "cuda_campaign/host_driver.h"
#include "lb_source/detector_band_handoff.h"
#include "lb_source/diagnostic_telemetry.h"
#include "lb_source/tileop_static_reach.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/streaming_compositor.h"
#include "campaign/tileop.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Config {
  std::uint64_t r_start = 60000000;
  std::uint64_t r_final = 60032768;
  std::uint64_t microband_width = 8192;
  std::uint64_t static_reach_resident_width = 0;
  std::size_t chunk_size = 200000;
  std::size_t max_atoms = 1000000000ULL;
  bool skip_full_compositor = false;
  bool enable_full_static_handoff = false;
  bool allow_diagnostic_microband_width = false;
  bool streaming_static_reach = true;
};

struct ColumnBatch {
  struct Column {
    std::int32_t i = 0;
    std::size_t tile_count = 0;
  };
  std::vector<campaign::TileCoord> coords;
  std::vector<Column> columns;
};

struct Phase0Telemetry {
  std::uint64_t max_resident_microband_tiles = 0;
  std::uint64_t max_resident_tileops = 0;
  std::uint64_t max_resident_port_atoms = 0;
  std::uint64_t max_resident_edges = 0;
  std::uint64_t max_live_frontier_atoms = 0;
  std::uint64_t max_components = 0;
  std::uint64_t resident_subsegments = 0;
  std::uint64_t materialization_ms = 0;
  std::uint64_t handoff_hash_ms = 0;

  void observe_tileops(std::size_t count) {
    max_resident_tileops =
        std::max<std::uint64_t>(max_resident_tileops,
                                static_cast<std::uint64_t>(count));
  }

  void observe_microband_tiles(std::size_t count) {
    max_resident_microband_tiles =
        std::max<std::uint64_t>(max_resident_microband_tiles,
                                static_cast<std::uint64_t>(count));
    observe_tileops(count);
  }

  void observe_band(const lb_source::TileOpStaticReachMicrobandResult& band) {
    max_resident_port_atoms =
        std::max(max_resident_port_atoms, band.port_atoms);
    max_resident_edges =
        std::max<std::uint64_t>(max_resident_edges,
                                band.internal_edges + band.seam_edges);
  }

  void observe_stream(
      const lb_source::TileOpStaticReachStreamingResult& stream) {
    max_resident_port_atoms =
        std::max(max_resident_port_atoms, stream.port_atoms);
    max_resident_edges =
        std::max(max_resident_edges, stream.max_resident_edges);
  }

  void observe_frontier(const lb_source::StaticReachSeparator& frontier) {
    max_live_frontier_atoms =
        std::max<std::uint64_t>(
            max_live_frontier_atoms,
            static_cast<std::uint64_t>(frontier.carry_atoms.size()));
    max_components =
        std::max<std::uint64_t>(
            max_components,
            static_cast<std::uint64_t>(frontier.component_partition.size()));
  }
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
      << "CUDA TileOp static-reach equivalence gate. Builds TileOps with the\n"
      << "global full-annulus constants, compares full StreamingCompositor\n"
      << "spanning against stitched radial microbands, and emits JSON.\n\n"
      << "Options:\n"
      << "  --r-start R            start radius (default 60000000)\n"
      << "  --r-final R            final radius (default 60032768)\n"
      << "  --microband-width W    stitched segment width (default 8192)\n"
      << "  --static-reach-resident-width W\n"
      << "                         max resident static-reach subband width\n"
      << "  --chunk-size N         CUDA dispatch chunk size (default 200000)\n"
      << "  --max-atoms N          static reach per-band cap (default 1e9)\n"
      << "  --materialized-static-reach\n"
      << "                         use legacy materialized band graph path\n"
      << "  --skip-full-compositor skip full comparator, diagnostic only\n"
      << "  --enable-full-static-handoff\n"
      << "                         build bounded one-big static handoff comparator\n"
      << "  --allow-diagnostic-microband-width\n"
      << "                         permit W < 4096 and label JSON diagnostic\n";
}

bool parse_args(int argc, char** argv, Config& config) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      std::exit(EXIT_SUCCESS);
    }
    auto take = [&](const std::string& flag, std::string& value) -> bool {
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
    if (take("--r-start", value)) {
      if (!parse_uint64(value, config.r_start)) return false;
    } else if (take("--r-final", value)) {
      if (!parse_uint64(value, config.r_final)) return false;
    } else if (take("--microband-width", value)) {
      if (!parse_uint64(value, config.microband_width)) return false;
    } else if (take("--static-reach-resident-width", value)) {
      if (!parse_uint64(value, config.static_reach_resident_width)) return false;
    } else if (take("--chunk-size", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) || parsed == 0 ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
      }
      config.chunk_size = static_cast<std::size_t>(parsed);
    } else if (take("--max-atoms", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) || parsed == 0 ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
      }
      config.max_atoms = static_cast<std::size_t>(parsed);
    } else if (arg == "--skip-full-compositor") {
      config.skip_full_compositor = true;
    } else if (arg == "--materialized-static-reach") {
      config.streaming_static_reach = false;
    } else if (arg == "--enable-full-static-handoff") {
      config.enable_full_static_handoff = true;
    } else if (arg == "--allow-diagnostic-microband-width") {
      config.allow_diagnostic_microband_width = true;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return false;
    }
  }
  if (config.r_start == 0 || config.r_final <= config.r_start ||
      config.microband_width == 0 || config.chunk_size == 0) {
    return false;
  }
  if (config.static_reach_resident_width == 0) {
    config.static_reach_resident_width = config.microband_width;
  }
  if (config.microband_width < 4096 &&
      !config.allow_diagnostic_microband_width) {
    std::cerr << "--microband-width below 4096 requires "
                 "--allow-diagnostic-microband-width\n";
    return false;
  }
  if (config.static_reach_resident_width < 4096 &&
      !config.allow_diagnostic_microband_width) {
    std::cerr << "--static-reach-resident-width below 4096 requires "
                 "--allow-diagnostic-microband-width\n";
    return false;
  }
  return true;
}

std::uint64_t elapsed_ms(Clock::time_point begin, Clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
          .count());
}

double wall_share(std::uint64_t part_ms, std::uint64_t total_ms) {
  if (total_ms == 0) {
    return 0.0;
  }
  return lb_source::json_safe_finite_double(
      static_cast<double>(part_ms) / static_cast<double>(total_ms));
}

std::vector<std::uint64_t> build_schedule(std::uint64_t r_start,
                                          std::uint64_t r_final,
                                          std::uint64_t width) {
  std::vector<std::uint64_t> radii;
  for (std::uint64_t r = r_start; r < r_final;) {
    radii.push_back(r);
    const std::uint64_t remaining = r_final - r;
    r += std::min(width, remaining);
  }
  radii.push_back(r_final);
  return radii;
}

void emit_json_bool(std::ostream& out, bool value) {
  out << (value ? "true" : "false");
}

void emit_json_string(std::ostream& out, std::string_view value) {
  out << '"';
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      out << '\\' << ch;
    } else if (ch == '\n') {
      out << "\\n";
    } else {
      out << ch;
    }
  }
  out << '"';
}

const char* seed_policy_name(lb_source::StaticReachSeedPolicy policy) {
  switch (policy) {
    case lb_source::StaticReachSeedPolicy::kOneBand:
      return "kOneBand";
    case lb_source::StaticReachSeedPolicy::kFirstBand:
      return "kFirstBand";
    case lb_source::StaticReachSeedPolicy::kInteriorBand:
      return "kInteriorBand";
    case lb_source::StaticReachSeedPolicy::kFinalBand:
      return "kFinalBand";
  }
  return "unknown";
}

std::string hex64(std::uint64_t value) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kHex[value & 0xfU];
    value >>= 4;
  }
  return out;
}

std::string schedule_digest_hex(const std::vector<std::uint64_t>& schedule,
                                std::uint64_t width) {
  std::uint64_t hash = 1469598103934665603ULL;
  auto mix = [&](std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      hash ^= (value >> (8 * i)) & 0xffU;
      hash *= 1099511628211ULL;
    }
  };
  mix(width);
  mix(schedule.size());
  for (const std::uint64_t radius : schedule) {
    mix(radius);
  }
  const std::uint64_t a = hash;
  mix(hash ^ 0x9e3779b97f4a7c15ULL);
  const std::uint64_t b = hash;
  mix(hash ^ 0xbf58476d1ce4e5b9ULL);
  const std::uint64_t c = hash;
  mix(hash ^ 0x94d049bb133111ebULL);
  return hex64(a) + hex64(b) + hex64(c) + hex64(hash);
}

lb_source::DetectorBandHandoffV1 make_final_handoff(
    const lb_source::StaticReachProcessResult& reach,
    const std::vector<std::uint64_t>& schedule,
    const Config& config) {
  lb_source::DetectorBandHandoffV1 handoff;
  handoff.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  handoff.cut_radius = config.r_final;
  handoff.carry_width = reach.carry_width;
  handoff.schedule_index = schedule.empty() ? 0 : schedule.size() - 1;
  handoff.schedule_digest_algorithm =
      "fnv1a64x4:cuda-static-reach-equivalence-schedule-v1";
  handoff.schedule_digest_hex =
      schedule_digest_hex(schedule, config.microband_width);
  handoff.geometry_id = "gaussian-octant-grid-full-static-annulus-v1";
  handoff.oracle_id = "cuda-tileop-static-reach-host-v1";
  handoff.build_id = "local-cmake-k" + std::to_string(campaign::k_sq_value);
  handoff.support_envelope_id = "tileop-port-carry-ceil-sqrt-k-v1";
  handoff.port_identity_scheme_id = "tileop-local-port-atom-v1";
  handoff.boundary_policy_id = "closed-tile-boundary-v1";
  handoff.separator = reach.outgoing;
  return lb_source::canonicalize_detector_band_handoff(handoff);
}

std::vector<campaign::TileOp> dispatch_all_tileops(
    const std::vector<campaign::TileCoord>& coords,
    cuda_campaign::TileBatchDispatcher& dispatcher,
    cuda_campaign::DispatchStats& stats) {
  std::vector<campaign::TileOp> tileops(coords.size());
  if (!coords.empty()) {
    dispatcher.dispatch(coords.data(), coords.size(), tileops.data(), &stats);
  }
  return tileops;
}

bool full_compositor_spanning(
    const campaign::Grid& grid,
    cuda_campaign::TileBatchDispatcher& dispatcher,
    const Config& config,
    std::uint64_t& tiles,
    std::uint64_t& columns,
    cuda_campaign::DispatchStats& dispatch_stats,
    Phase0Telemetry& telemetry) {
  campaign::StreamingCompositor compositor;
  compositor.init(grid);

  ColumnBatch batch;
  auto flush = [&]() {
    if (batch.coords.empty()) {
      return;
    }
    telemetry.observe_tileops(batch.coords.size());
    cuda_campaign::DispatchStats local_stats;
    const std::vector<campaign::TileOp> tileops =
        dispatch_all_tileops(batch.coords, dispatcher, local_stats);
    dispatch_stats.tiles += local_stats.tiles;
    dispatch_stats.chunks += local_stats.chunks;
    dispatch_stats.slabs += local_stats.slabs;
    dispatch_stats.k1_cand_overflow_count += local_stats.k1_cand_overflow_count;
    dispatch_stats.k4_prime_overflow_count +=
        local_stats.k4_prime_overflow_count;
    dispatch_stats.k4_group_overflow_count +=
        local_stats.k4_group_overflow_count;
    dispatch_stats.k5_port_overflow_count += local_stats.k5_port_overflow_count;

    std::size_t offset = 0;
    for (const ColumnBatch::Column& column : batch.columns) {
      compositor.ingest_column(
          column.i,
          std::span<const campaign::TileOp>(tileops.data() + offset,
                                            column.tile_count));
      offset += column.tile_count;
    }
    batch.coords.clear();
    batch.columns.clear();
  };

  for (std::int32_t i = grid.i_min; i <= grid.i_max; ++i) {
    const std::vector<campaign::TileCoord> column =
        grid.enumerate_column_tiles(i);
    if (column.empty()) {
      continue;
    }
    if (!batch.coords.empty() &&
        batch.coords.size() + column.size() > config.chunk_size) {
      flush();
    }
    batch.columns.push_back(ColumnBatch::Column{i, column.size()});
    batch.coords.insert(batch.coords.end(), column.begin(), column.end());
    tiles += column.size();
    ++columns;
  }
  flush();
  return compositor.finalize() == campaign::Verdict::kSpanning;
}

struct FullStaticReachComparator {
  bool rejected = false;
  std::string diagnostic;
  std::uint64_t tiles = 0;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::uint64_t inner_seed_ports = 0;
  std::uint64_t outer_seed_ports = 0;
  bool spanning = false;
  std::optional<lb_source::DetectorBandHandoffV1> handoff;
};

FullStaticReachComparator full_static_reach_handoff(
    const campaign::Grid& grid,
    const std::vector<std::uint64_t>& schedule,
    cuda_campaign::TileBatchDispatcher& dispatcher,
    const Config& config,
    cuda_campaign::DispatchStats& dispatch_stats,
    Phase0Telemetry& telemetry) {
  FullStaticReachComparator result;
  const std::vector<campaign::TileCoord> coords = grid.enumerate_active_tiles();
  telemetry.observe_tileops(coords.size());
  cuda_campaign::DispatchStats local_stats;
  const std::vector<campaign::TileOp> tileops =
      dispatch_all_tileops(coords, dispatcher, local_stats);
  dispatch_stats.tiles += local_stats.tiles;
  dispatch_stats.chunks += local_stats.chunks;
  dispatch_stats.slabs += local_stats.slabs;
  dispatch_stats.k1_cand_overflow_count += local_stats.k1_cand_overflow_count;
  dispatch_stats.k4_prime_overflow_count +=
      local_stats.k4_prime_overflow_count;
  dispatch_stats.k4_group_overflow_count +=
      local_stats.k4_group_overflow_count;
  dispatch_stats.k5_port_overflow_count += local_stats.k5_port_overflow_count;

  const auto materialization_begin = Clock::now();
  const lb_source::TileOpStaticReachMicrobandResult band =
      lb_source::build_tileop_static_reach_microband({
          .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
          .outer_radius = config.r_final,
          .coords = coords,
          .tileops = tileops,
          .seed_policy = lb_source::StaticReachSeedPolicy::kOneBand,
      });
  telemetry.materialization_ms += elapsed_ms(materialization_begin, Clock::now());
  telemetry.observe_band(band);
  if (!band.accepted()) {
    result.rejected = true;
    result.diagnostic = band.diagnostic;
    return result;
  }
  const lb_source::StaticReachProcessResult reach =
      lb_source::process_static_reach_band(
          band.band, std::nullopt,
          {.max_atoms = config.max_atoms,
           .max_carry_atoms = config.max_atoms,
           .max_components = config.max_atoms});
  if (!reach.accepted()) {
    result.rejected = true;
    result.diagnostic = reach.diagnostic;
    return result;
  }
  telemetry.observe_frontier(reach.outgoing);

  result.tiles = coords.size();
  result.port_atoms = band.port_atoms;
  result.internal_edges = band.internal_edges;
  result.seam_edges = band.seam_edges;
  result.inner_seed_ports = band.inner_seed_ports;
  result.outer_seed_ports = band.outer_seed_ports;
  result.spanning = reach.spanning;
  result.handoff = make_final_handoff(reach, schedule, config);
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  const auto total_begin = Clock::now();
  Phase0Telemetry telemetry;
  campaign::CampaignConstants constants;
  try {
    constants = campaign::CampaignConstants::from_radii(
        config.r_start, config.r_final, campaign::k_sq_value);
  } catch (const std::exception& ex) {
    std::cerr << "campaign constants failed: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }

  cuda_campaign::DispatchConfig dispatch_config;
  dispatch_config.host_chunk_tiles = config.chunk_size;
  dispatch_config.collect_stage_timings = false;
  cuda_campaign::TileBatchDispatcher dispatcher(constants, dispatch_config);

  std::optional<bool> full_spanning;
  std::uint64_t full_tiles = 0;
  std::uint64_t full_columns = 0;
  cuda_campaign::DispatchStats full_dispatch;
  const auto full_begin = Clock::now();
  if (!config.skip_full_compositor) {
    std::cerr << "phase=full_compositor begin r_start=" << config.r_start
              << " r_final=" << config.r_final << "\n";
    campaign::Grid full_grid;
    try {
      full_grid = campaign::Grid::build(config.r_start, config.r_final,
                                        campaign::k_sq_value);
    } catch (const std::exception& ex) {
      std::cerr << "full grid failed: " << ex.what() << "\n";
      return EXIT_FAILURE;
    }
    const std::string invariant_error = full_grid.verify_invariants();
    if (!invariant_error.empty()) {
      std::cerr << "full grid invariant failed: " << invariant_error << "\n";
      return EXIT_FAILURE;
    }
    full_spanning = full_compositor_spanning(full_grid, dispatcher, config,
                                             full_tiles, full_columns,
                                             full_dispatch, telemetry);
    std::cerr << "phase=full_compositor end tiles=" << full_tiles
              << " columns=" << full_columns
              << " elapsed_ms=" << elapsed_ms(full_begin, Clock::now())
              << "\n";
  }
  const auto full_done = Clock::now();

  const std::vector<std::uint64_t> schedule =
      build_schedule(config.r_start, config.r_final, config.microband_width);
  std::optional<FullStaticReachComparator> full_static;
  cuda_campaign::DispatchStats full_static_dispatch;
  const auto full_static_begin = Clock::now();
  if (config.enable_full_static_handoff) {
    std::cerr << "phase=full_static_handoff begin r_start=" << config.r_start
              << " r_final=" << config.r_final << "\n";
    campaign::Grid full_grid;
    try {
      full_grid = campaign::Grid::build(config.r_start, config.r_final,
                                        campaign::k_sq_value);
    } catch (const std::exception& ex) {
      full_static = FullStaticReachComparator{
          .rejected = true,
          .diagnostic = std::string("full static grid failed: ") + ex.what()};
    }
    if (!full_static.has_value()) {
      const std::string invariant_error = full_grid.verify_invariants();
      if (!invariant_error.empty()) {
        full_static = FullStaticReachComparator{
            .rejected = true,
            .diagnostic =
                "full static grid invariant failed: " + invariant_error};
      } else {
        full_static = full_static_reach_handoff(full_grid, schedule, dispatcher,
                                                config, full_static_dispatch,
                                                telemetry);
      }
    }
    std::cerr << "phase=full_static_handoff end rejected="
              << (full_static->rejected ? "true" : "false")
              << " elapsed_ms="
              << elapsed_ms(full_static_begin, Clock::now()) << "\n";
  }
  const auto full_static_done = Clock::now();

  std::optional<lb_source::StaticReachSeparator> state;
  bool stitched_spanning = false;
  bool stitched_rejected = false;
  std::string stitched_diagnostic;
  std::uint64_t stitched_tiles = 0;
  std::uint64_t stitched_port_atoms = 0;
  std::uint64_t stitched_internal_edges = 0;
  std::uint64_t stitched_seam_edges = 0;
  std::uint64_t stitched_inner_seed_ports = 0;
  std::uint64_t stitched_outer_seed_ports = 0;
  std::uint64_t interior_inner_seed_ports = 0;
  std::uint64_t interior_outer_seed_ports = 0;
  std::uint64_t max_carry_atoms = 0;
  std::uint64_t final_carry_width = 0;
  cuda_campaign::DispatchStats stitched_dispatch;
  const auto stitched_begin = Clock::now();

  const std::size_t segment_count =
      schedule.empty() ? 0 : schedule.size() - 1;
  for (std::size_t segment = 0; segment < segment_count; ++segment) {
    const std::uint64_t r_inner = schedule[segment];
    const std::uint64_t r_outer = schedule[segment + 1];
    const auto segment_begin = Clock::now();
    auto emit_segment_end = [&](std::string_view result,
                                std::uint64_t tiles,
                                std::uint64_t carry_atoms) {
      std::cerr << "phase=stitched_segment end segment=" << segment
                << " result=" << result << " tiles=" << tiles
                << " carry_atoms=" << carry_atoms
                << " spanning=" << (stitched_spanning ? "true" : "false")
                << " elapsed_ms=" << elapsed_ms(segment_begin, Clock::now())
                << "\n";
    };
    std::cerr << "phase=stitched_segment begin segment=" << segment
              << " segments=" << (schedule.size() - 1)
              << " r_inner=" << r_inner << " r_outer=" << r_outer
              << " resident_width=" << config.static_reach_resident_width
              << "\n";
    std::uint64_t segment_tiles = 0;
    std::uint64_t segment_carry_atoms = 0;
    for (std::uint64_t resident_inner = r_inner; resident_inner < r_outer;) {
      const std::uint64_t resident_outer =
          std::min<std::uint64_t>(resident_inner +
                                      config.static_reach_resident_width,
                                  r_outer);
      const bool first_global_resident =
          segment == 0 && resident_inner == r_inner;
      const bool final_global_resident =
          segment + 1 == segment_count && resident_outer == r_outer;
      lb_source::StaticReachSeedPolicy resident_seed_policy =
          lb_source::StaticReachSeedPolicy::kInteriorBand;
      if (first_global_resident && final_global_resident) {
        resident_seed_policy = lb_source::StaticReachSeedPolicy::kOneBand;
      } else if (first_global_resident) {
        resident_seed_policy = lb_source::StaticReachSeedPolicy::kFirstBand;
      } else if (final_global_resident) {
        resident_seed_policy = lb_source::StaticReachSeedPolicy::kFinalBand;
      }

      campaign::Grid grid;
      try {
        grid = campaign::Grid::build(resident_inner, resident_outer,
                                     campaign::k_sq_value);
      } catch (const std::exception& ex) {
        stitched_rejected = true;
        stitched_diagnostic =
            std::string("resident grid failed: ") + ex.what();
        emit_segment_end("rejected_grid", segment_tiles, segment_carry_atoms);
        break;
      }
      const std::string invariant_error = grid.verify_invariants();
      if (!invariant_error.empty()) {
        stitched_rejected = true;
        stitched_diagnostic =
            "resident grid invariant failed: " + invariant_error;
        emit_segment_end("rejected_grid_invariant", segment_tiles,
                         segment_carry_atoms);
        break;
      }
      const std::vector<campaign::TileCoord> coords =
          grid.enumerate_active_tiles();
      telemetry.observe_microband_tiles(coords.size());
      ++telemetry.resident_subsegments;
      cuda_campaign::DispatchStats local_stats;
      const std::vector<campaign::TileOp> tileops =
          dispatch_all_tileops(coords, dispatcher, local_stats);
      stitched_dispatch.tiles += local_stats.tiles;
      stitched_dispatch.chunks += local_stats.chunks;
      stitched_dispatch.slabs += local_stats.slabs;
      stitched_dispatch.k1_cand_overflow_count +=
          local_stats.k1_cand_overflow_count;
      stitched_dispatch.k4_prime_overflow_count +=
          local_stats.k4_prime_overflow_count;
      stitched_dispatch.k4_group_overflow_count +=
          local_stats.k4_group_overflow_count;
      stitched_dispatch.k5_port_overflow_count +=
          local_stats.k5_port_overflow_count;

      lb_source::StaticReachProcessResult step;
      std::uint64_t resident_port_atoms = 0;
      std::uint64_t resident_internal_edges = 0;
      std::uint64_t resident_seam_edges = 0;
      std::uint64_t resident_inner_seed_ports = 0;
      std::uint64_t resident_outer_seed_ports = 0;
      const auto materialization_begin = Clock::now();
      if (config.streaming_static_reach) {
        const lb_source::TileOpStaticReachStreamingResult stream =
            lb_source::process_tileop_static_reach_microband_streaming(
                {.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
                 .outer_radius = resident_outer,
                 .coords = coords,
                 .tileops = tileops,
                 .seed_policy = resident_seed_policy},
                state,
                {.max_atoms = config.max_atoms,
                 .max_carry_atoms = config.max_atoms,
                 .max_components = config.max_atoms});
        telemetry.materialization_ms +=
            elapsed_ms(materialization_begin, Clock::now());
        telemetry.observe_stream(stream);
        if (!stream.accepted()) {
          stitched_rejected = true;
          stitched_diagnostic = stream.process.diagnostic;
          emit_segment_end("rejected_stream", segment_tiles,
                           segment_carry_atoms);
          break;
        }
        step = stream.process;
        resident_port_atoms = stream.port_atoms;
        resident_internal_edges = stream.internal_edges;
        resident_seam_edges = stream.seam_edges;
        resident_inner_seed_ports = stream.inner_seed_ports;
        resident_outer_seed_ports = stream.outer_seed_ports;
      } else {
        const lb_source::TileOpStaticReachMicrobandResult band =
            lb_source::build_tileop_static_reach_microband({
                .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
                .outer_radius = resident_outer,
                .coords = coords,
                .tileops = tileops,
                .seed_policy = resident_seed_policy,
            });
        telemetry.materialization_ms +=
            elapsed_ms(materialization_begin, Clock::now());
        telemetry.observe_band(band);
        if (!band.accepted()) {
          stitched_rejected = true;
          stitched_diagnostic = band.diagnostic;
          emit_segment_end("rejected_materialization", segment_tiles,
                           segment_carry_atoms);
          break;
        }
        step = lb_source::process_static_reach_band(
            band.band, state,
            {.max_atoms = config.max_atoms,
             .max_carry_atoms = config.max_atoms,
             .max_components = config.max_atoms});
        if (!step.accepted()) {
          stitched_rejected = true;
          stitched_diagnostic = step.diagnostic;
          emit_segment_end("rejected_process", segment_tiles,
                           segment_carry_atoms);
          break;
        }
        resident_port_atoms = band.port_atoms;
        resident_internal_edges = band.internal_edges;
        resident_seam_edges = band.seam_edges;
        resident_inner_seed_ports = band.inner_seed_ports;
        resident_outer_seed_ports = band.outer_seed_ports;
      }

      stitched_tiles += coords.size();
      segment_tiles += coords.size();
      stitched_port_atoms += resident_port_atoms;
      stitched_internal_edges += resident_internal_edges;
      stitched_seam_edges += resident_seam_edges;
      stitched_inner_seed_ports += resident_inner_seed_ports;
      stitched_outer_seed_ports += resident_outer_seed_ports;
      if (resident_seed_policy ==
          lb_source::StaticReachSeedPolicy::kInteriorBand) {
        interior_inner_seed_ports += resident_inner_seed_ports;
        interior_outer_seed_ports += resident_outer_seed_ports;
      }
      segment_carry_atoms =
          static_cast<std::uint64_t>(step.outgoing.carry_atoms.size());
      telemetry.observe_frontier(step.outgoing);
      max_carry_atoms = std::max<std::uint64_t>(max_carry_atoms,
                                                segment_carry_atoms);
      final_carry_width = step.carry_width;
      stitched_spanning = stitched_spanning || step.spanning;
      state = std::move(step.outgoing);
      resident_inner = resident_outer;
    }
    if (stitched_rejected) {
      break;
    }
    emit_segment_end("accepted", segment_tiles, segment_carry_atoms);
  }
  const auto stitched_done = Clock::now();

  std::string final_handoff_sha256;
  std::string full_static_handoff_sha256;
  std::optional<bool> handoff_equal;
  if (!stitched_rejected && state.has_value()) {
    const auto handoff_hash_begin = Clock::now();
    const lb_source::StaticReachProcessResult stitched_final{
        .reject = lb_source::RejectReason::kNone,
        .carry_width = final_carry_width,
        .outgoing = *state,
        .spanning = stitched_spanning,
    };
    const lb_source::DetectorBandHandoffV1 stitched_handoff =
        make_final_handoff(stitched_final, schedule, config);
    final_handoff_sha256 =
        lb_source::detector_band_handoff_sha256_hex(stitched_handoff);
    if (full_static.has_value() && !full_static->rejected &&
        full_static->handoff.has_value()) {
      full_static_handoff_sha256 =
          lb_source::detector_band_handoff_sha256_hex(*full_static->handoff);
      handoff_equal = !final_handoff_sha256.empty() &&
                      final_handoff_sha256 == full_static_handoff_sha256 &&
                      stitched_handoff == *full_static->handoff;
    }
    telemetry.handoff_hash_ms += elapsed_ms(handoff_hash_begin, Clock::now());
  }

  const bool interior_seed_clean =
      interior_inner_seed_ports == 0 && interior_outer_seed_ports == 0;
  const bool overflow_clean =
      full_dispatch.k1_cand_overflow_count == 0 &&
      full_dispatch.k4_prime_overflow_count == 0 &&
      full_dispatch.k4_group_overflow_count == 0 &&
      full_dispatch.k5_port_overflow_count == 0 &&
      stitched_dispatch.k1_cand_overflow_count == 0 &&
      stitched_dispatch.k4_prime_overflow_count == 0 &&
      stitched_dispatch.k4_group_overflow_count == 0 &&
      stitched_dispatch.k5_port_overflow_count == 0 &&
      full_static_dispatch.k1_cand_overflow_count == 0 &&
      full_static_dispatch.k4_prime_overflow_count == 0 &&
      full_static_dispatch.k4_group_overflow_count == 0 &&
      full_static_dispatch.k5_port_overflow_count == 0;
  const bool comparable = full_spanning.has_value() && !stitched_rejected;
  const bool verdict_equal = comparable && *full_spanning == stitched_spanning;
  const bool full_static_rejected =
      full_static.has_value() && full_static->rejected;
  const bool handoff_gate_pass =
      !config.enable_full_static_handoff ||
      (handoff_equal.has_value() && *handoff_equal);
  const bool pass = verdict_equal && handoff_gate_pass && interior_seed_clean &&
                    overflow_clean && !full_static_rejected;
  const char* status =
      stitched_rejected
          ? "CUDA_STATIC_REACH_STITCHED_REJECT"
          : (full_static_rejected
                 ? "CUDA_STATIC_REACH_FULL_STATIC_REJECT"
                 : (pass ? "CUDA_STATIC_REACH_EQUIVALENCE_PASS"
                         : "CUDA_STATIC_REACH_EQUIVALENCE_FAIL"));

  const lb_source::RssSnapshot rss = lb_source::rss_snapshot();
  const std::uint64_t full_ms = elapsed_ms(full_begin, full_done);
  const std::uint64_t full_static_ms =
      elapsed_ms(full_static_begin, full_static_done);
  const std::uint64_t stitched_ms = elapsed_ms(stitched_begin, stitched_done);
  const std::uint64_t pre_handoff_total_ms =
      elapsed_ms(total_begin, stitched_done);
  const std::uint64_t total_ms = elapsed_ms(total_begin, Clock::now());

  std::cout << "{"
            << "\"schema\":\"cuda_static_reach_equivalence_v1\""
            << ",\"phase0_schema\":\"cuda_static_reach_equivalence_phase0_telemetry_v1\""
            << ",\"runner_id\":\"cuda_static_reach_equivalence_v1\""
            << ",\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\""
            << ",\"status\":\"" << status << "\""
            << ",\"k_sq\":" << campaign::k_sq_value
            << ",\"r_start\":" << config.r_start
            << ",\"r_final\":" << config.r_final
            << ",\"microband_width\":" << config.microband_width
            << ",\"static_reach_resident_width\":"
            << config.static_reach_resident_width
            << ",\"static_reach_materialization_mode\":\""
            << (config.streaming_static_reach ? "streaming_dsu"
                                               : "materialized_band")
            << "\""
            << ",\"segments\":" << (schedule.empty() ? 0 : schedule.size() - 1)
            << ",\"resident_subsegments\":"
            << telemetry.resident_subsegments
            << ",\"microband_width_class\":\""
            << (config.microband_width < 4096 ? "diagnostic_sub_4096"
                                               : "production")
            << "\",\"diagnostic_microband_width_override\":";
  emit_json_bool(std::cout, config.allow_diagnostic_microband_width);
  std::cout << ",\"enable_full_static_handoff\":";
  emit_json_bool(std::cout, config.enable_full_static_handoff);
  const lb_source::StaticReachSeedPolicy stitched_first_policy =
      segment_count == 1 ? lb_source::StaticReachSeedPolicy::kOneBand
                         : lb_source::StaticReachSeedPolicy::kFirstBand;
  const lb_source::StaticReachSeedPolicy stitched_final_policy =
      segment_count == 1 ? lb_source::StaticReachSeedPolicy::kOneBand
                         : lb_source::StaticReachSeedPolicy::kFinalBand;
  std::cout << ",\"seed_policy\":{\"full\":\""
            << seed_policy_name(lb_source::StaticReachSeedPolicy::kOneBand)
            << "\",\"stitched_first\":\""
            << seed_policy_name(stitched_first_policy)
            << "\",\"stitched_interior\":\""
            << seed_policy_name(lb_source::StaticReachSeedPolicy::kInteriorBand)
            << "\",\"stitched_final\":\""
            << seed_policy_name(stitched_final_policy)
            << "\"}"
            << ",\"chunk_size\":" << config.chunk_size
            << ",\"full_tiles\":" << full_tiles
            << ",\"full_columns\":" << full_columns
            << ",\"full_static_tiles\":"
            << (full_static.has_value() ? full_static->tiles : 0)
            << ",\"full_static_port_atoms\":"
            << (full_static.has_value() ? full_static->port_atoms : 0)
            << ",\"full_static_internal_edges\":"
            << (full_static.has_value() ? full_static->internal_edges : 0)
            << ",\"full_static_seam_edges\":"
            << (full_static.has_value() ? full_static->seam_edges : 0)
            << ",\"stitched_tiles\":" << stitched_tiles
            << ",\"stitched_port_atoms\":" << stitched_port_atoms
            << ",\"stitched_internal_edges\":" << stitched_internal_edges
            << ",\"stitched_seam_edges\":" << stitched_seam_edges
            << ",\"stitched_inner_seed_ports\":" << stitched_inner_seed_ports
            << ",\"stitched_outer_seed_ports\":" << stitched_outer_seed_ports
            << ",\"interior_inner_seed_ports\":" << interior_inner_seed_ports
            << ",\"interior_outer_seed_ports\":" << interior_outer_seed_ports
            << ",\"max_carry_atoms\":" << max_carry_atoms
            << ",\"max_live_frontier_ports\":" << max_carry_atoms
            << ",\"rss_bytes\":" << rss.current_bytes
            << ",\"peak_rss_bytes\":" << rss.peak_bytes
            << ",\"max_resident_microband_tiles\":"
            << telemetry.max_resident_microband_tiles
            << ",\"max_resident_tileops\":" << telemetry.max_resident_tileops
            << ",\"max_resident_port_atoms\":"
            << telemetry.max_resident_port_atoms
            << ",\"max_resident_edges\":" << telemetry.max_resident_edges
            << ",\"max_live_frontier_atoms\":"
            << telemetry.max_live_frontier_atoms
            << ",\"max_components\":" << telemetry.max_components
            << ",\"wall_share_full_compositor\":"
            << wall_share(full_ms, total_ms)
            << ",\"wall_share_full_static\":"
            << wall_share(full_static_ms, total_ms)
            << ",\"wall_share_stitched\":" << wall_share(stitched_ms, total_ms)
            << ",\"wall_share_materialization\":"
            << wall_share(telemetry.materialization_ms, total_ms)
            << ",\"wall_share_handoff_hash\":"
            << wall_share(telemetry.handoff_hash_ms, total_ms)
            << ",\"stitched_rejected\":";
  emit_json_bool(std::cout, stitched_rejected);
  std::cout << ",\"stitched_diagnostic\":";
  emit_json_string(std::cout, stitched_diagnostic);
  std::cout << ",\"full_static_rejected\":";
  emit_json_bool(std::cout, full_static_rejected);
  std::cout << ",\"full_static_diagnostic\":";
  emit_json_string(std::cout, full_static.has_value()
                                  ? full_static->diagnostic
                                  : std::string_view(""));
  std::cout << ",\"full_spanning\":";
  if (full_spanning.has_value()) {
    emit_json_bool(std::cout, *full_spanning);
  } else {
    std::cout << "null";
  }
  std::cout << ",\"stitched_spanning\":";
  emit_json_bool(std::cout, stitched_spanning);
  std::cout << ",\"full_static_spanning\":";
  if (full_static.has_value() && !full_static->rejected) {
    emit_json_bool(std::cout, full_static->spanning);
  } else {
    std::cout << "null";
  }
  std::cout << ",\"verdict_equal\":";
  emit_json_bool(std::cout, verdict_equal);
  std::cout << ",\"equivalent\":";
  emit_json_bool(std::cout, pass);
  std::cout << ",\"handoff_equal\":";
  if (handoff_equal.has_value()) {
    emit_json_bool(std::cout, *handoff_equal);
  } else {
    std::cout << "null";
  }
  std::cout << ",\"full_static_handoff_sha256\":";
  if (!full_static_handoff_sha256.empty()) {
    emit_json_string(std::cout, full_static_handoff_sha256);
  } else {
    std::cout << "null";
  }
  std::cout << ",\"final_handoff_sha256\":";
  if (!final_handoff_sha256.empty()) {
    emit_json_string(std::cout, final_handoff_sha256);
  } else {
    std::cout << "null";
  }
  std::cout << ",\"overflow_counters\":{"
            << "\"full_k1\":" << full_dispatch.k1_cand_overflow_count
            << ",\"full_k4_prime\":"
            << full_dispatch.k4_prime_overflow_count
            << ",\"full_k4_group\":"
            << full_dispatch.k4_group_overflow_count
            << ",\"full_k5_port\":"
            << full_dispatch.k5_port_overflow_count
            << ",\"stitched_k1\":"
            << stitched_dispatch.k1_cand_overflow_count
            << ",\"stitched_k4_prime\":"
            << stitched_dispatch.k4_prime_overflow_count
            << ",\"stitched_k4_group\":"
            << stitched_dispatch.k4_group_overflow_count
            << ",\"stitched_k5_port\":"
            << stitched_dispatch.k5_port_overflow_count
            << ",\"full_static_k1\":"
            << full_static_dispatch.k1_cand_overflow_count
            << ",\"full_static_k4_prime\":"
            << full_static_dispatch.k4_prime_overflow_count
            << ",\"full_static_k4_group\":"
            << full_static_dispatch.k4_group_overflow_count
            << ",\"full_static_k5_port\":"
            << full_static_dispatch.k5_port_overflow_count
            << "}"
            << ",\"timings_ms\":{"
            << "\"full\":" << full_ms
            << ",\"full_static\":" << full_static_ms
            << ",\"stitched\":" << stitched_ms
            << ",\"materialization\":" << telemetry.materialization_ms
            << ",\"handoff_hash\":" << telemetry.handoff_hash_ms
            << ",\"pre_handoff_total\":" << pre_handoff_total_ms
            << ",\"total\":" << total_ms
            << "}}\n";

  return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
