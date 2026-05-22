#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "campaign/grid.h"
#include "campaign/tileop.h"
#include "lb_source/source_propagation.h"

namespace lb_source {

struct TileOpPortGraphInput {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::vector<campaign::TileCoord> coords;
  std::vector<campaign::TileOp> tileops;
  bool seed_inner_flags = false;
};

struct TileOpPortGraphResult {
  BandInput band;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::uint64_t overflow_tiles = 0;
  std::uint64_t empty_tiles = 0;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

TileOpPortGraphResult make_tileop_port_band(
    const TileOpPortGraphInput& input);

}  // namespace lb_source
