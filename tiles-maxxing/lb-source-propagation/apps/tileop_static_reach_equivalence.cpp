#include "lb_source/tileop_static_reach.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/streaming_compositor.h"
#include "campaign/tileop.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Config {
  std::uint64_t r_start = 248;
  std::uint64_t r_final = 512;
  std::uint64_t microband_width = 128;
  std::size_t max_atoms = 1000000;
  std::size_t tileop_threads = 0;
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
      << "Compare stitched TileOp static reach against one full annulus.\n"
      << "This is a diagnostic equivalence gate, not a moat proof.\n\n"
      << "Options:\n"
      << "  --r-start R          starting radius (default 248)\n"
      << "  --r-final R          final radius (default 512)\n"
      << "  --microband-width W  stitched segment width (default 128)\n"
      << "  --max-atoms N        reach state cap (default 1000000)\n"
      << "  --tileop-threads N   TileOp worker threads; 0 means hardware auto\n";
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
  return true;
}

std::uint64_t elapsed_ms(Clock::time_point begin, Clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
          .count());
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

std::vector<campaign::TileOp> build_tileops(
    const std::vector<campaign::TileCoord>& coords,
    const campaign::CampaignConstants& constants,
    const campaign::Grid& grid,
    std::size_t worker_threads) {
  std::vector<campaign::TileOp> tileops(coords.size());
  if (coords.empty()) {
    return tileops;
  }
  worker_threads = std::max<std::size_t>(1, worker_threads);
  worker_threads = std::min(worker_threads, coords.size());
  if (worker_threads == 1) {
    for (std::size_t i = 0; i < coords.size(); ++i) {
      tileops[i] = campaign::process_tile(coords[i], constants, grid);
    }
    return tileops;
  }

  std::atomic<std::size_t> next_index{0};
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
          tileops[index] =
              campaign::process_tile(coords[index], constants, grid);
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
  return tileops;
}

bool compositor_spanning(const campaign::Grid& grid,
                         const std::vector<campaign::TileOp>& tileops) {
  campaign::StreamingCompositor compositor;
  compositor.init(grid);
  std::size_t offset = 0;
  for (std::int32_t i = grid.i_min; i <= grid.i_max; ++i) {
    const auto [j_low, j_high] = grid.column_bounds(i);
    const std::size_t count =
        j_high >= j_low ? static_cast<std::size_t>(j_high - j_low + 1) : 0;
    compositor.ingest_column(
        i, std::span<const campaign::TileOp>(tileops.data() + offset, count));
    offset += count;
  }
  const campaign::Verdict verdict = compositor.finalize();
  return verdict == campaign::Verdict::kSpanning;
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

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) {
    return EXIT_FAILURE;
  }

  const auto total_begin = Clock::now();
  campaign::CampaignConstants constants;
  try {
    constants = campaign::CampaignConstants::from_radii(
        config.r_start, config.r_final, campaign::k_sq_value);
  } catch (const std::exception& ex) {
    std::cerr << "campaign construction failed: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }

  campaign::Grid full_grid;
  try {
    full_grid = campaign::Grid::build(config.r_start, config.r_final,
                                      campaign::k_sq_value);
  } catch (const std::exception& ex) {
    std::cerr << "full grid construction failed: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }
  const std::string full_invariant_error = full_grid.verify_invariants();
  if (!full_invariant_error.empty()) {
    std::cerr << "full grid invariant failed: " << full_invariant_error
              << "\n";
    return EXIT_FAILURE;
  }

  const auto full_tileop_begin = Clock::now();
  const std::vector<campaign::TileCoord> full_coords =
      full_grid.enumerate_active_tiles();
  const std::vector<campaign::TileOp> full_tileops =
      build_tileops(full_coords, constants, full_grid,
                    resolve_tileop_threads(config.tileop_threads,
                                           full_coords.size()));
  const auto full_tileop_done = Clock::now();

  const lb_source::TileOpStaticReachMicrobandResult full_reach_band =
      lb_source::build_tileop_static_reach_microband({
          .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
          .outer_radius = config.r_final,
          .coords = full_coords,
          .tileops = full_tileops,
      });
  if (!full_reach_band.accepted()) {
    std::cerr << "full static reach band rejected: "
              << full_reach_band.diagnostic << "\n";
    return EXIT_FAILURE;
  }
  const lb_source::StaticReachProcessResult full_reach =
      lb_source::process_static_reach_band(
          full_reach_band.band, std::nullopt,
          {.max_atoms = config.max_atoms,
           .max_carry_atoms = config.max_atoms,
           .max_components = config.max_atoms});
  if (!full_reach.accepted()) {
    std::cerr << "full static reach process rejected: "
              << full_reach.diagnostic << "\n";
    return EXIT_FAILURE;
  }
  const bool full_compositor = compositor_spanning(full_grid, full_tileops);
  const auto full_done = Clock::now();

  const std::vector<std::uint64_t> schedule =
      build_schedule(config.r_start, config.r_final,
                     config.microband_width);
  std::optional<lb_source::StaticReachSeparator> stitched_state;
  bool stitched_spanning = false;
  std::uint64_t stitched_tiles = 0;
  std::uint64_t stitched_port_atoms = 0;
  std::uint64_t stitched_internal_edges = 0;
  std::uint64_t stitched_seam_edges = 0;
  std::uint64_t stitched_inner_seed_ports = 0;
  std::uint64_t stitched_outer_seed_ports = 0;
  std::uint64_t max_stitched_carry_atoms = 0;
  const auto stitched_begin = Clock::now();

  for (std::size_t segment = 0; segment + 1 < schedule.size(); ++segment) {
    const std::uint64_t r_inner = schedule[segment];
    const std::uint64_t r_outer = schedule[segment + 1];
    campaign::Grid grid;
    try {
      grid = campaign::Grid::build(r_inner, r_outer, campaign::k_sq_value);
    } catch (const std::exception& ex) {
      std::cerr << "segment grid construction failed: " << ex.what() << "\n";
      return EXIT_FAILURE;
    }
    const std::string invariant_error = grid.verify_invariants();
    if (!invariant_error.empty()) {
      std::cerr << "segment grid invariant failed: " << invariant_error
                << "\n";
      return EXIT_FAILURE;
    }

    const std::vector<campaign::TileCoord> coords =
        grid.enumerate_active_tiles();
    const std::vector<campaign::TileOp> tileops =
        build_tileops(coords, constants, grid,
                      resolve_tileop_threads(config.tileop_threads,
                                             coords.size()));
    const lb_source::TileOpStaticReachMicrobandResult band =
        lb_source::build_tileop_static_reach_microband({
            .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
            .outer_radius = r_outer,
            .coords = coords,
            .tileops = tileops,
        });
    if (!band.accepted()) {
      std::cerr << "stitched static reach band rejected: "
                << band.diagnostic << "\n";
      return EXIT_FAILURE;
    }
    const lb_source::StaticReachProcessResult step =
        lb_source::process_static_reach_band(
            band.band, stitched_state,
            {.max_atoms = config.max_atoms,
             .max_carry_atoms = config.max_atoms,
             .max_components = config.max_atoms});
    if (!step.accepted()) {
      std::cerr << "stitched static reach process rejected: "
                << step.diagnostic << "\n";
      return EXIT_FAILURE;
    }

    stitched_tiles += coords.size();
    stitched_port_atoms += band.port_atoms;
    stitched_internal_edges += band.internal_edges;
    stitched_seam_edges += band.seam_edges;
    stitched_inner_seed_ports += band.inner_seed_ports;
    stitched_outer_seed_ports += band.outer_seed_ports;
    max_stitched_carry_atoms =
        std::max<std::uint64_t>(max_stitched_carry_atoms,
                                step.outgoing.carry_atoms.size());
    stitched_spanning = stitched_spanning || step.spanning;
    stitched_state = step.outgoing;
    if (stitched_spanning) {
      break;
    }
  }
  const auto stitched_done = Clock::now();

  const bool pass =
      full_compositor == full_reach.spanning &&
      full_reach.spanning == stitched_spanning;

  std::cout << "{"
            << "\"schema\":\"lb_source_tileop_static_reach_equivalence_v1\""
            << ",\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\""
            << ",\"status\":\""
            << (pass ? "STATIC_REACH_EQUIVALENCE_PASS"
                     : "STATIC_REACH_EQUIVALENCE_FAIL")
            << "\",\"k_sq\":" << campaign::k_sq_value
            << ",\"r_start\":" << config.r_start
            << ",\"r_final\":" << config.r_final
            << ",\"microband_width\":" << config.microband_width
            << ",\"segments\":" << (schedule.empty() ? 0 : schedule.size() - 1)
            << ",\"full_tiles\":" << full_coords.size()
            << ",\"stitched_tiles_processed\":" << stitched_tiles
            << ",\"full_port_atoms\":" << full_reach_band.port_atoms
            << ",\"stitched_port_atoms\":" << stitched_port_atoms
            << ",\"full_internal_edges\":" << full_reach_band.internal_edges
            << ",\"stitched_internal_edges\":" << stitched_internal_edges
            << ",\"full_seam_edges\":" << full_reach_band.seam_edges
            << ",\"stitched_seam_edges\":" << stitched_seam_edges
            << ",\"full_inner_seed_ports\":" << full_reach_band.inner_seed_ports
            << ",\"stitched_inner_seed_ports\":" << stitched_inner_seed_ports
            << ",\"full_outer_seed_ports\":" << full_reach_band.outer_seed_ports
            << ",\"stitched_outer_seed_ports\":" << stitched_outer_seed_ports
            << ",\"max_stitched_carry_atoms\":"
            << max_stitched_carry_atoms
            << ",\"full_compositor_spanning\":"
            << (full_compositor ? "true" : "false")
            << ",\"full_static_reach_spanning\":"
            << (full_reach.spanning ? "true" : "false")
            << ",\"stitched_static_reach_spanning\":"
            << (stitched_spanning ? "true" : "false")
            << ",\"full_tileop_ms\":"
            << elapsed_ms(full_tileop_begin, full_tileop_done)
            << ",\"full_total_ms\":" << elapsed_ms(full_tileop_begin, full_done)
            << ",\"stitched_total_ms\":"
            << elapsed_ms(stitched_begin, stitched_done)
            << ",\"total_ms\":" << elapsed_ms(total_begin, stitched_done)
            << "}\n";

  return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
