#include "lb_source/source_propagation.h"
#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/tileop.h"

namespace {

struct Config {
  std::uint64_t r_start = 248;
  std::uint64_t r_final = 512;
  std::uint64_t band_width = 128;
  std::size_t max_atoms = 1000000;
  bool seed_inner_flags = false;
  std::optional<std::string> manifest_out;
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
      << "Usage: " << prog << " [OPTIONS]\n"
      << "\n"
      << "Diagnostic TileOp-port source scheduler for the LB sidecar.\n"
      << "It builds CPU TileOps per radial band, converts TileOp ports to\n"
      << "canonical port atoms, and stitches bands through lb_source::process_band.\n"
      << "This is not a SOURCE_ORIGIN_K26 claim.\n"
      << "\n"
      << "Options:\n"
      << "  --r-start R           starting radius (default 248)\n"
      << "  --r-final R           final radius (default 512)\n"
      << "  --band-width W        radial band width (default 128)\n"
      << "  --max-atoms N         hard atom cap for sidecar process_band\n"
      << "                        (default 1000000)\n"
      << "  --seed-inner-flags    seed first band from TileOp inner flags\n"
      << "                        (geo-I diagnostic only)\n"
      << "  --manifest-out PATH   write final carry manifest when source survives\n";
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
    } else if (take_value("--band-width", value)) {
      if (!parse_uint64(value, config.band_width)) {
        std::cerr << "invalid --band-width: " << value << "\n";
        return false;
      }
    } else if (take_value("--max-atoms", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid --max-atoms: " << value << "\n";
        return false;
      }
      config.max_atoms = static_cast<std::size_t>(parsed);
    } else if (arg == "--seed-inner-flags") {
      config.seed_inner_flags = true;
    } else if (take_value("--manifest-out", value)) {
      if (value.empty()) {
        std::cerr << "--manifest-out must not be empty\n";
        return false;
      }
      config.manifest_out = value;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return false;
    }
  }

  if (config.r_start == 0 || config.r_final <= config.r_start ||
      config.band_width == 0) {
    std::cerr << "--r-start must be positive, --r-final must be greater, "
                 "and --band-width must be positive\n";
    return false;
  }
  if (config.band_width < lb_source::ceil_sqrt(campaign::k_sq_value)) {
    std::cerr << "--band-width must be at least ceil_sqrt(K_SQ)\n";
    return false;
  }
  return true;
}

bool has_source_carry(const lb_source::SeparatorState& state) {
  return std::find(state.source_bit_per_component.begin(),
                   state.source_bit_per_component.end(),
                   true) != state.source_bit_per_component.end();
}

std::uint64_t source_carry_atoms(const lb_source::SeparatorState& state) {
  std::uint64_t count = 0;
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    if (state.source_bit_per_component[c]) {
      count += state.component_partition[c].size();
    }
  }
  return count;
}

std::vector<campaign::TileOp> build_tileops(
    const std::vector<campaign::TileCoord>& coords,
    const campaign::CampaignConstants& constants,
    const campaign::Grid& grid,
    std::uint64_t& overflow_tiles) {
  std::vector<campaign::TileOp> tileops;
  tileops.reserve(coords.size());
  for (const campaign::TileCoord& coord : coords) {
    campaign::TileOp op = campaign::process_tile(coord, constants, grid);
    if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
      ++overflow_tiles;
    }
    tileops.push_back(op);
  }
  return tileops;
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) {
    return EXIT_FAILURE;
  }

  std::optional<lb_source::SeparatorState> incoming;
  lb_source::ProcessResult last;
  std::uint64_t previous_outer = config.r_start;
  std::uint64_t bands_processed = 0;
  std::uint64_t campaign_tiles_processed = 0;
  std::uint64_t tileop_overflows = 0;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;

  while (previous_outer < config.r_final) {
    const std::uint64_t outer =
        std::min(config.r_final, previous_outer + config.band_width);
    campaign::CampaignConstants constants;
    campaign::Grid grid;
    try {
      constants = campaign::CampaignConstants::from_radii(
          previous_outer, outer, campaign::k_sq_value);
      grid = campaign::Grid::build(previous_outer, outer,
                                   campaign::k_sq_value);
    } catch (const std::exception& ex) {
      std::cerr << "campaign band construction failed: " << ex.what() << "\n";
      return EXIT_FAILURE;
    }
    const std::string invariant_error = grid.verify_invariants();
    if (!invariant_error.empty()) {
      std::cerr << "grid invariant failed: " << invariant_error << "\n";
      return EXIT_FAILURE;
    }

    const std::vector<campaign::TileCoord> coords =
        grid.enumerate_active_tiles();
    campaign_tiles_processed += coords.size();
    std::vector<campaign::TileOp> tileops =
        build_tileops(coords, constants, grid, tileop_overflows);

    const lb_source::TileOpPortGraphResult graph =
        lb_source::make_tileop_port_band({
            .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
            .outer_radius = outer,
            .coords = coords,
            .tileops = std::move(tileops),
            .seed_inner_flags = config.seed_inner_flags &&
                                bands_processed == 0,
        });
    if (!graph.accepted()) {
      std::cerr << "TileOp port graph rejected: " << graph.diagnostic << "\n";
      return EXIT_FAILURE;
    }
    port_atoms += graph.port_atoms;
    internal_edges += graph.internal_edges;
    seam_edges += graph.seam_edges;

    last = lb_source::process_band(
        graph.band, incoming,
        {.max_atoms = config.max_atoms,
         .max_carry_atoms = config.max_atoms,
         .max_components = config.max_atoms});
    ++bands_processed;
    if (!last.accepted()) {
      break;
    }
    if (last.terminal_source_dead) {
      break;
    }
    incoming = last.outgoing;
    previous_outer = outer;
  }

  bool manifest_written = false;
  if (config.manifest_out.has_value()) {
    if (!last.accepted() || last.terminal_source_dead ||
        !has_source_carry(last.outgoing)) {
      std::cerr << "--manifest-out requires accepted live source carry\n";
      return EXIT_FAILURE;
    }
    std::ofstream manifest(*config.manifest_out);
    if (!manifest) {
      std::cerr << "cannot open --manifest-out path: "
                << *config.manifest_out << "\n";
      return EXIT_FAILURE;
    }
    lb_source::write_carry_manifest(
        manifest, lb_source::make_carry_manifest(
                      static_cast<std::uint64_t>(campaign::k_sq_value),
                      config.r_final, last));
    manifest_written = true;
  }

  const bool accepted = last.accepted();
  const bool source_carry =
      accepted && !last.terminal_source_dead && has_source_carry(last.outgoing);

  std::cout << "{"
            << "\"schema\":\"lb_source_tileop_port_runner_v1\","
            << "\"claim_label\":\"SOURCE_TILEOP_PORT_DIAGNOSTIC\","
            << "\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\","
            << "\"source_mode\":\""
            << (config.seed_inner_flags ? "GEO_I_PORT_DIAGNOSTIC" : "NONE")
            << "\","
            << "\"k_sq\":" << campaign::k_sq_value
            << ",\"r_start\":" << config.r_start
            << ",\"r_final\":" << config.r_final
            << ",\"band_width\":" << config.band_width
            << ",\"bands_processed\":" << bands_processed
            << ",\"campaign_tiles_processed\":" << campaign_tiles_processed
            << ",\"tileop_overflows\":" << tileop_overflows
            << ",\"port_atoms\":" << port_atoms
            << ",\"internal_edges\":" << internal_edges
            << ",\"seam_edges\":" << seam_edges
            << ",\"accepted\":" << (accepted ? "true" : "false")
            << ",\"reject\":\"" << lb_source::reject_reason_name(last.reject)
            << "\""
            << ",\"terminal_source_dead\":"
            << (accepted && last.terminal_source_dead ? "true" : "false")
            << ",\"has_source_carry\":"
            << (source_carry ? "true" : "false")
            << ",\"source_carry_atoms\":"
            << (source_carry ? source_carry_atoms(last.outgoing) : 0)
            << ",\"manifest_written\":"
            << (manifest_written ? "true" : "false")
            << ",\"non_claim\":\"TileOp-port scheduler diagnostic; not SOURCE_ORIGIN_K26 or SOURCE_DEAD_CERT\""
            << "}\n";

  return accepted && tileop_overflows == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
