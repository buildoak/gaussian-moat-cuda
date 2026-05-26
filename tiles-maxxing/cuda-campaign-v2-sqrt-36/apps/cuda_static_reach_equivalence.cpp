#include "cuda_campaign/host_driver.h"
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
  std::size_t chunk_size = 200000;
  std::size_t max_atoms = 1000000000ULL;
  bool skip_full_compositor = false;
};

struct ColumnBatch {
  struct Column {
    std::int32_t i = 0;
    std::size_t tile_count = 0;
  };
  std::vector<campaign::TileCoord> coords;
  std::vector<Column> columns;
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
      << "  --chunk-size N         CUDA dispatch chunk size (default 200000)\n"
      << "  --max-atoms N          static reach per-band cap (default 1e9)\n"
      << "  --skip-full-compositor skip full comparator, diagnostic only\n";
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
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return false;
    }
  }
  if (config.r_start == 0 || config.r_final <= config.r_start ||
      config.microband_width == 0 || config.chunk_size == 0) {
    return false;
  }
  return true;
}

std::uint64_t elapsed_ms(Clock::time_point begin, Clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
          .count());
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
    cuda_campaign::DispatchStats& dispatch_stats) {
  campaign::StreamingCompositor compositor;
  compositor.init(grid);

  ColumnBatch batch;
  auto flush = [&]() {
    if (batch.coords.empty()) {
      return;
    }
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

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  const auto total_begin = Clock::now();
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
                                             full_dispatch);
    std::cerr << "phase=full_compositor end tiles=" << full_tiles
              << " columns=" << full_columns
              << " elapsed_ms=" << elapsed_ms(full_begin, Clock::now())
              << "\n";
  }
  const auto full_done = Clock::now();

  const std::vector<std::uint64_t> schedule =
      build_schedule(config.r_start, config.r_final, config.microband_width);
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
  std::uint64_t max_carry_atoms = 0;
  cuda_campaign::DispatchStats stitched_dispatch;
  const auto stitched_begin = Clock::now();

  for (std::size_t segment = 0; segment + 1 < schedule.size(); ++segment) {
    if (stitched_spanning) {
      break;
    }
    const std::uint64_t r_inner = schedule[segment];
    const std::uint64_t r_outer = schedule[segment + 1];
    const auto segment_begin = Clock::now();
    if (segment == 0 || segment + 2 == schedule.size() ||
        segment % 8 == 0) {
      std::cerr << "phase=stitched_segment begin segment=" << segment
                << " segments=" << (schedule.size() - 1)
                << " r_inner=" << r_inner << " r_outer=" << r_outer
                << "\n";
    }
    campaign::Grid grid;
    try {
      grid = campaign::Grid::build(r_inner, r_outer, campaign::k_sq_value);
    } catch (const std::exception& ex) {
      stitched_rejected = true;
      stitched_diagnostic = std::string("segment grid failed: ") + ex.what();
      break;
    }
    const std::string invariant_error = grid.verify_invariants();
    if (!invariant_error.empty()) {
      stitched_rejected = true;
      stitched_diagnostic = "segment grid invariant failed: " + invariant_error;
      break;
    }
    const std::vector<campaign::TileCoord> coords =
        grid.enumerate_active_tiles();
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

    const lb_source::TileOpStaticReachMicrobandResult band =
        lb_source::build_tileop_static_reach_microband({
            .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
            .outer_radius = r_outer,
            .coords = coords,
            .tileops = tileops,
        });
    if (!band.accepted()) {
      stitched_rejected = true;
      stitched_diagnostic = band.diagnostic;
      break;
    }
    const lb_source::StaticReachProcessResult step =
        lb_source::process_static_reach_band(
            band.band, state,
            {.max_atoms = config.max_atoms,
             .max_carry_atoms = config.max_atoms,
             .max_components = config.max_atoms});
    if (!step.accepted()) {
      stitched_rejected = true;
      stitched_diagnostic = step.diagnostic;
      break;
    }
    stitched_tiles += coords.size();
    stitched_port_atoms += band.port_atoms;
    stitched_internal_edges += band.internal_edges;
    stitched_seam_edges += band.seam_edges;
    stitched_inner_seed_ports += band.inner_seed_ports;
    stitched_outer_seed_ports += band.outer_seed_ports;
    max_carry_atoms =
        std::max<std::uint64_t>(max_carry_atoms,
                                step.outgoing.carry_atoms.size());
    stitched_spanning = stitched_spanning || step.spanning;
    state = step.outgoing;
    if (segment == 0 || segment + 2 == schedule.size() ||
        segment % 8 == 0 || stitched_spanning) {
      std::cerr << "phase=stitched_segment end segment=" << segment
                << " tiles=" << coords.size()
                << " carry_atoms=" << step.outgoing.carry_atoms.size()
                << " spanning=" << (stitched_spanning ? "true" : "false")
                << " elapsed_ms=" << elapsed_ms(segment_begin, Clock::now())
                << "\n";
    }
  }
  const auto stitched_done = Clock::now();

  const bool comparable = full_spanning.has_value() && !stitched_rejected;
  const bool equivalent = comparable && *full_spanning == stitched_spanning;
  const char* status = stitched_rejected
                           ? "CUDA_STATIC_REACH_STITCHED_REJECT"
                           : (equivalent ? "CUDA_STATIC_REACH_EQUIVALENCE_PASS"
                                         : "CUDA_STATIC_REACH_EQUIVALENCE_FAIL");

  std::cout << "{"
            << "\"schema\":\"cuda_static_reach_equivalence_v1\""
            << ",\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\""
            << ",\"status\":\"" << status << "\""
            << ",\"k_sq\":" << campaign::k_sq_value
            << ",\"r_start\":" << config.r_start
            << ",\"r_final\":" << config.r_final
            << ",\"microband_width\":" << config.microband_width
            << ",\"segments\":" << (schedule.empty() ? 0 : schedule.size() - 1)
            << ",\"chunk_size\":" << config.chunk_size
            << ",\"full_tiles\":" << full_tiles
            << ",\"full_columns\":" << full_columns
            << ",\"stitched_tiles\":" << stitched_tiles
            << ",\"stitched_port_atoms\":" << stitched_port_atoms
            << ",\"stitched_internal_edges\":" << stitched_internal_edges
            << ",\"stitched_seam_edges\":" << stitched_seam_edges
            << ",\"stitched_inner_seed_ports\":" << stitched_inner_seed_ports
            << ",\"stitched_outer_seed_ports\":" << stitched_outer_seed_ports
            << ",\"max_carry_atoms\":" << max_carry_atoms
            << ",\"stitched_rejected\":";
  emit_json_bool(std::cout, stitched_rejected);
  std::cout << ",\"stitched_diagnostic\":\"";
  for (const char ch : stitched_diagnostic) {
    if (ch == '"' || ch == '\\') std::cout << '\\';
    if (ch == '\n') {
      std::cout << "\\n";
    } else {
      std::cout << ch;
    }
  }
  std::cout << "\",\"full_spanning\":";
  if (full_spanning.has_value()) {
    emit_json_bool(std::cout, *full_spanning);
  } else {
    std::cout << "null";
  }
  std::cout << ",\"stitched_spanning\":";
  emit_json_bool(std::cout, stitched_spanning);
  std::cout << ",\"equivalent\":";
  emit_json_bool(std::cout, equivalent);
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
            << "}"
            << ",\"timings_ms\":{"
            << "\"full\":" << elapsed_ms(full_begin, full_done)
            << ",\"stitched\":" << elapsed_ms(stitched_begin, stitched_done)
            << ",\"total\":" << elapsed_ms(total_begin, stitched_done)
            << "}}\n";

  return equivalent ? EXIT_SUCCESS : EXIT_FAILURE;
}
