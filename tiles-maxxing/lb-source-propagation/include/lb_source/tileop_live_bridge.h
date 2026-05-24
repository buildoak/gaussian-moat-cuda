#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/grid.h"
#include "campaign/tileop.h"
#include "lb_source/source_propagation.h"

namespace lb_source {

struct TileOpLiveBridgeResult {
  std::uint64_t coordinate_carry_atoms_with_next_band_candidates = 0;
  std::uint64_t bridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_without_next_band_candidates = 0;
  std::uint64_t unbridged_with_next_band_candidates = 0;
  std::uint64_t unbridged_dead_end_candidate_atoms = 0;
  std::uint64_t unbridged_unsafe_candidate_atoms = 0;
  std::uint64_t bridge_rejected_candidate_atoms = 0;
  std::uint64_t source_coordinate_carry_atoms_with_next_band_candidates = 0;
  std::uint64_t source_bridged_coordinate_carry_atoms = 0;
  std::uint64_t source_unbridged_coordinate_carry_atoms = 0;
  std::uint64_t source_unbridged_without_next_band_candidates = 0;
  std::uint64_t source_unbridged_with_next_band_candidates = 0;
  std::uint64_t source_unbridged_dead_end_candidate_atoms = 0;
  std::uint64_t source_unbridged_unsafe_candidate_atoms = 0;
  std::uint64_t source_bridge_rejected_candidate_atoms = 0;
  std::map<std::string, std::uint64_t> bridge_reject_reasons;
  std::map<std::string, std::uint64_t> source_bridge_reject_reasons;
  std::uint64_t bridged_port_carry_atoms = 0;
  std::uint64_t bridge_edges = 0;
  std::map<std::pair<AtomId, AtomId>, std::vector<CoordinateAtom>>
      coordinate_port_paths;
};

TileOpLiveBridgeResult bridge_coordinate_live_handoff_to_ports(
    const LiveHandoffV1& handoff,
    const campaign::CampaignConstants& constants,
    const std::vector<campaign::TileCoord>& coords,
    const std::vector<campaign::TileOp>& tileops,
    BandInput& graph_band,
    std::size_t worker_threads);

}  // namespace lb_source
