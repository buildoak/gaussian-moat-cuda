#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "campaign/grid.h"
#include "campaign/tileop.h"
#include "lb_source/source_propagation.h"

namespace lb_source {

constexpr std::uint8_t kStaticReachInner = 0x1;
constexpr std::uint8_t kStaticReachOuter = 0x2;
constexpr std::uint8_t kStaticReachBoth =
    kStaticReachInner | kStaticReachOuter;

enum class StaticReachSeedPolicy : std::uint8_t {
  kOneBand,
  kFirstBand,
  kInteriorBand,
  kFinalBand,
};

struct StaticReachBandAtom {
  AtomId id = 0;
  std::uint64_t norm_sq = 0;
  std::uint8_t reach = 0;
  bool allow_outer_overshoot_carry = false;
};

struct StaticReachBandInput {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::vector<StaticReachBandAtom> atoms;
  std::vector<std::pair<AtomId, AtomId>> edges;
  bool force_overflow = false;
  StaticReachSeedPolicy seed_policy = StaticReachSeedPolicy::kOneBand;
};

struct StaticReachSeparator {
  std::vector<CarryAtom> carry_atoms;
  std::vector<std::vector<AtomId>> component_partition;
  std::vector<std::uint8_t> reach_per_component;

  friend bool operator==(const StaticReachSeparator&,
                         const StaticReachSeparator&) = default;
};

struct StaticReachProcessOptions {
  std::size_t max_atoms = 65535;
  std::size_t max_carry_atoms = 65535;
  std::size_t max_components = 65535;
};

struct StaticReachProcessResult {
  RejectReason reject = RejectReason::kNone;
  std::string diagnostic;
  std::uint64_t carry_width = 0;
  StaticReachSeparator outgoing;
  bool spanning = false;

  bool accepted() const noexcept { return reject == RejectReason::kNone; }
};

struct TileOpStaticReachInput {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::vector<campaign::TileCoord> coords;
  std::vector<campaign::TileOp> tileops;
  StaticReachSeedPolicy seed_policy = StaticReachSeedPolicy::kOneBand;
};

struct TileOpStaticReachMicrobandResult {
  StaticReachBandInput band;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::uint64_t overflow_tiles = 0;
  std::uint64_t empty_tiles = 0;
  std::uint64_t inner_seed_ports = 0;
  std::uint64_t outer_seed_ports = 0;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

StaticReachSeparator canonicalize_static_reach_separator(
    const StaticReachSeparator& state);

std::string validate_static_reach_separator(
    const StaticReachSeparator& state);

TileOpStaticReachMicrobandResult build_tileop_static_reach_microband(
    const TileOpStaticReachInput& input);

StaticReachProcessResult process_static_reach_band(
    const StaticReachBandInput& band,
    const std::optional<StaticReachSeparator>& incoming = std::nullopt,
    const StaticReachProcessOptions& options = {});

StaticReachProcessResult process_static_reach_bands(
    const std::vector<StaticReachBandInput>& bands,
    const std::optional<StaticReachSeparator>& incoming = std::nullopt,
    const StaticReachProcessOptions& options = {});

}  // namespace lb_source
