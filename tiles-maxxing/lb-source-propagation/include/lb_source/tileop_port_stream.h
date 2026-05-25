#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "campaign/grid.h"
#include "campaign/tileop.h"
#include "lb_source/source_propagation.h"

namespace lb_source {

struct TileOpPortStreamInput {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::vector<campaign::TileCoord> coords;
  std::vector<campaign::TileOp> tileops;
  bool seed_inner_flags = false;
};

struct TileOpDecodedPort {
  AtomId id = 0;
  std::uint8_t face = 0;
  std::uint8_t ordinal = 0;
  std::uint8_t local_label = 0;
  bool certified_source = false;
  bool certified_sink = false;

  friend bool operator==(const TileOpDecodedPort&,
                         const TileOpDecodedPort&) = default;
};

struct TileOpPortDecodedTile {
  campaign::TileCoord coord{};
  bool overflow = false;
  bool empty = false;
  std::uint64_t support_norm_sq = 0;
  std::vector<TileOpDecodedPort> ports;
  std::vector<std::pair<AtomId, AtomId>> internal_edges;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

struct TileOpPortStreamResult {
  BandInput band;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::uint64_t overflow_tiles = 0;
  std::uint64_t empty_tiles = 0;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

TileOpPortDecodedTile decode_tileop_ports(
    const campaign::TileCoord& coord,
    const campaign::TileOp& tileop,
    bool seed_inner_flags = false);

TileOpPortStreamResult build_tileop_port_microband(
    const TileOpPortStreamInput& input);

}  // namespace lb_source
