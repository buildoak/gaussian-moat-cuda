#include "lb_source/tileop_static_reach.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "campaign/constants.h"
#include "lb_source/tileop_port_stream.h"

namespace lb_source {
namespace {

class Dsu {
 public:
  explicit Dsu(std::size_t n) : parent_(n), rank_(n, 0) {
    for (std::size_t i = 0; i < n; ++i) {
      parent_[i] = i;
    }
  }

  std::size_t find(std::size_t x) {
    assert(x < parent_.size());
    if (parent_[x] != x) {
      parent_[x] = find(parent_[x]);
    }
    return parent_[x];
  }

  void unite(std::size_t a, std::size_t b) {
    std::size_t ra = find(a);
    std::size_t rb = find(b);
    if (ra == rb) {
      return;
    }
    if (rank_[ra] < rank_[rb] || (rank_[ra] == rank_[rb] && rb < ra)) {
      std::swap(ra, rb);
    }
    parent_[rb] = ra;
    if (rank_[ra] == rank_[rb]) {
      ++rank_[ra];
    }
  }

 private:
  std::vector<std::size_t> parent_;
  std::vector<std::uint8_t> rank_;
};

using CoordKey = std::pair<std::int32_t, std::int32_t>;

struct PortRef {
  AtomId id = 0;
};

StaticReachProcessResult reject(RejectReason reason, std::string diagnostic,
                                std::uint64_t carry_width = 0) {
  StaticReachProcessResult result;
  result.reject = reason;
  result.diagnostic = std::move(diagnostic);
  result.carry_width = carry_width;
  return result;
}

bool in_final_carry_window(std::uint64_t norm_sq, std::uint64_t outer_radius,
                           std::uint64_t carry_width,
                           bool allow_outer_overshoot) {
  if (!allow_outer_overshoot && norm_sq > outer_radius * outer_radius) {
    return false;
  }
  const std::uint64_t inner_radius =
      outer_radius > carry_width ? outer_radius - carry_width : 0;
  return norm_sq >= inner_radius * inner_radius;
}

void sort_unique_atom_ids(std::vector<AtomId>& ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

bool is_sorted_unique_atom_ids(const std::vector<AtomId>& ids) {
  return std::adjacent_find(ids.begin(), ids.end(),
                            [](AtomId lhs, AtomId rhs) {
                              return lhs >= rhs;
                            }) == ids.end();
}

bool stable_atom_id(AtomId id) {
  return decode_coordinate_atom_id(id).has_value() ||
         decode_port_atom_id(id).has_value();
}

void add_edge(std::set<std::pair<AtomId, AtomId>>& edges, AtomId lhs,
              AtomId rhs) {
  if (lhs == rhs) {
    return;
  }
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  edges.insert({lhs, rhs});
}

const std::vector<PortRef>& face_ports_or_empty(
    const std::map<std::pair<CoordKey, int>, std::vector<PortRef>>& by_face,
    const CoordKey& coord,
    campaign::Face face) {
  static const std::vector<PortRef> kEmpty;
  const auto it = by_face.find({coord, static_cast<int>(face)});
  if (it == by_face.end()) {
    return kEmpty;
  }
  return it->second;
}

}  // namespace

StaticReachSeparator canonicalize_static_reach_separator(
    const StaticReachSeparator& state) {
  StaticReachSeparator out;
  out.carry_atoms = state.carry_atoms;
  std::sort(out.carry_atoms.begin(), out.carry_atoms.end(),
            [](const CarryAtom& a, const CarryAtom& b) {
              if (a.id != b.id) {
                return a.id < b.id;
              }
              return a.norm_sq < b.norm_sq;
            });

  std::vector<std::pair<std::vector<AtomId>, std::uint8_t>> components;
  components.reserve(state.component_partition.size());
  for (std::size_t i = 0; i < state.component_partition.size(); ++i) {
    std::vector<AtomId> component = state.component_partition[i];
    sort_unique_atom_ids(component);
    const std::uint8_t reach =
        i < state.reach_per_component.size() ? state.reach_per_component[i]
                                             : 0;
    components.push_back({std::move(component), reach});
  }

  std::sort(components.begin(), components.end(),
            [](const auto& lhs, const auto& rhs) {
              return std::lexicographical_compare(
                  lhs.first.begin(), lhs.first.end(), rhs.first.begin(),
                  rhs.first.end());
            });

  out.component_partition.reserve(components.size());
  out.reach_per_component.reserve(components.size());
  for (auto& [component, reach] : components) {
    out.component_partition.push_back(std::move(component));
    out.reach_per_component.push_back(reach);
  }
  return out;
}

std::string validate_static_reach_separator(
    const StaticReachSeparator& state) {
  if (state.component_partition.size() != state.reach_per_component.size()) {
    return "reach-bit count does not match component count";
  }

  std::set<AtomId> carry_atoms;
  for (const CarryAtom& atom : state.carry_atoms) {
    if (!stable_atom_id(atom.id)) {
      return "unstable carry atom id";
    }
    const std::optional<CoordinateAtom> coordinate =
        decode_coordinate_atom_id(atom.id);
    if (coordinate.has_value() && coordinate->norm_sq != atom.norm_sq) {
      return "carry atom norm does not match coordinate atom";
    }
    if (!carry_atoms.insert(atom.id).second) {
      return "duplicate carry atom";
    }
  }

  std::set<AtomId> partition_atoms;
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    if ((state.reach_per_component[c] & ~kStaticReachBoth) != 0) {
      return "invalid reach bits";
    }
    const auto& component = state.component_partition[c];
    if (component.empty()) {
      return "empty component";
    }
    if (!is_sorted_unique_atom_ids(component)) {
      return "component atoms are not sorted unique";
    }
    for (const AtomId id : component) {
      if (carry_atoms.find(id) == carry_atoms.end()) {
        return "component references non-carry atom";
      }
      if (!partition_atoms.insert(id).second) {
        return "carry atom appears in multiple components";
      }
    }
  }
  if (partition_atoms != carry_atoms) {
    return "component partition does not cover all carry atoms";
  }
  return "";
}

TileOpStaticReachMicrobandResult build_tileop_static_reach_microband(
    const TileOpStaticReachInput& input) {
  TileOpStaticReachMicrobandResult result;
  result.band.k_sq = input.k_sq;
  result.band.outer_radius = input.outer_radius;

  if (input.k_sq == 0 || input.outer_radius == 0) {
    result.diagnostic = "k_sq and outer_radius must be positive";
    return result;
  }
  if (input.coords.size() != input.tileops.size()) {
    result.diagnostic = "coords and tileops size mismatch";
    return result;
  }

  std::map<CoordKey, std::size_t> index_by_coord;
  std::map<std::pair<CoordKey, int>, std::vector<PortRef>> ports_by_face;
  std::set<std::pair<AtomId, AtomId>> edges;

  for (std::size_t t = 0; t < input.coords.size(); ++t) {
    const campaign::TileCoord& coord = input.coords[t];
    const campaign::TileOp& op = input.tileops[t];
    const CoordKey key{coord.i, coord.j};
    if (!index_by_coord.emplace(key, t).second) {
      result.diagnostic = "duplicate TileOp coordinate";
      return result;
    }

    const TileOpPortDecodedTile decoded =
        decode_tileop_ports(coord, op, true);
    if (!decoded.accepted()) {
      result.diagnostic = decoded.diagnostic;
      return result;
    }
    if (decoded.overflow) {
      ++result.overflow_tiles;
      result.band.force_overflow = true;
      continue;
    }
    if (decoded.empty) {
      ++result.empty_tiles;
      continue;
    }

    std::map<std::uint8_t, std::uint8_t> reach_by_label;
    for (const TileOpDecodedPort& port : decoded.ports) {
      std::uint8_t reach = 0;
      if (port.certified_source) {
        reach |= kStaticReachInner;
      }
      if (port.certified_sink) {
        reach |= kStaticReachOuter;
      }
      reach_by_label[port.local_label] |= reach;
    }

    for (const TileOpDecodedPort& port : decoded.ports) {
      const std::uint8_t reach = reach_by_label[port.local_label];
      if ((reach & kStaticReachInner) != 0) {
        ++result.inner_seed_ports;
      }
      if ((reach & kStaticReachOuter) != 0) {
        ++result.outer_seed_ports;
      }
      result.band.atoms.push_back(StaticReachBandAtom{
          .id = port.id,
          .norm_sq = decoded.support_norm_sq,
          .reach = reach,
          .allow_outer_overshoot_carry = true,
      });
      ++result.port_atoms;
    }
    for (const auto& edge : decoded.internal_edges) {
      const std::size_t before = edges.size();
      add_edge(edges, edge.first, edge.second);
      if (edges.size() != before) {
        ++result.internal_edges;
      }
    }

    for (const TileOpDecodedPort& port : decoded.ports) {
      ports_by_face[{key, port.face}].push_back(PortRef{port.id});
    }
  }

  for (const auto& [coord, t] : index_by_coord) {
    const campaign::TileOp& op = input.tileops[t];
    if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
      continue;
    }

    const CoordKey above{coord.first, coord.second + 1};
    const auto above_it = index_by_coord.find(above);
    if (above_it != index_by_coord.end() &&
        (input.tileops[above_it->second].tile_flags &
         campaign::OVERFLOW_BIT) == 0) {
      const auto& lower_ports =
          face_ports_or_empty(ports_by_face, coord, campaign::Face::O);
      const auto& upper_ports =
          face_ports_or_empty(ports_by_face, above, campaign::Face::I);
      if (lower_ports.size() != upper_ports.size()) {
        result.diagnostic = "I/O TileOp port count mismatch";
        return result;
      }
      for (std::size_t p = 0; p < lower_ports.size(); ++p) {
        const std::size_t before = edges.size();
        add_edge(edges, lower_ports[p].id, upper_ports[p].id);
        if (edges.size() != before) {
          ++result.seam_edges;
        }
      }
    }

    const CoordKey right{coord.first + 1, coord.second};
    const auto right_it = index_by_coord.find(right);
    if (right_it != index_by_coord.end() &&
        (input.tileops[right_it->second].tile_flags &
         campaign::OVERFLOW_BIT) == 0) {
      const auto& left_ports =
          face_ports_or_empty(ports_by_face, coord, campaign::Face::R);
      const auto& right_ports =
          face_ports_or_empty(ports_by_face, right, campaign::Face::L);
      if (left_ports.size() != right_ports.size()) {
        result.diagnostic = "L/R TileOp port count mismatch";
        return result;
      }
      for (std::size_t p = 0; p < left_ports.size(); ++p) {
        const std::size_t before = edges.size();
        add_edge(edges, left_ports[p].id, right_ports[p].id);
        if (edges.size() != before) {
          ++result.seam_edges;
        }
      }
    }
  }

  result.band.edges.assign(edges.begin(), edges.end());
  return result;
}

StaticReachProcessResult process_static_reach_band(
    const StaticReachBandInput& band,
    const std::optional<StaticReachSeparator>& incoming,
    const StaticReachProcessOptions& options) {
  const std::uint64_t carry_width = ceil_sqrt(band.k_sq);

  if (band.force_overflow) {
    return reject(RejectReason::kOverflow, "band marked overflow",
                  carry_width);
  }
  if (band.outer_radius > 0 &&
      band.outer_radius >
          std::numeric_limits<std::uint64_t>::max() / band.outer_radius) {
    return reject(RejectReason::kMalformed, "outer radius square overflows",
                  carry_width);
  }

  std::vector<StaticReachBandAtom> all_atoms = band.atoms;
  std::unordered_map<AtomId, std::size_t> index_by_id;
  index_by_id.reserve(all_atoms.size() + (incoming ? incoming->carry_atoms.size()
                                                   : 0));

  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    if ((all_atoms[i].reach & ~kStaticReachBoth) != 0) {
      return reject(RejectReason::kMalformed, "invalid band atom reach bits",
                    carry_width);
    }
    if (!stable_atom_id(all_atoms[i].id)) {
      return reject(RejectReason::kMalformed, "band atom has unstable id",
                    carry_width);
    }
    const std::optional<CoordinateAtom> coordinate =
        decode_coordinate_atom_id(all_atoms[i].id);
    if (coordinate.has_value() && coordinate->norm_sq != all_atoms[i].norm_sq) {
      return reject(RejectReason::kMalformed,
                    "band atom norm does not match coordinate atom",
                    carry_width);
    }
    if (!index_by_id.emplace(all_atoms[i].id, i).second) {
      return reject(RejectReason::kMalformed, "duplicate band atom id",
                    carry_width);
    }
  }

  if (incoming) {
    const StaticReachSeparator canonical =
        canonicalize_static_reach_separator(*incoming);
    const std::string validation = validate_static_reach_separator(canonical);
    if (!validation.empty()) {
      return reject(RejectReason::kMalformed,
                    "incoming static reach separator " + validation,
                    carry_width);
    }
    for (const CarryAtom& atom : canonical.carry_atoms) {
      const auto existing = index_by_id.find(atom.id);
      if (existing == index_by_id.end()) {
        const std::size_t idx = all_atoms.size();
        all_atoms.push_back(StaticReachBandAtom{
            .id = atom.id,
            .norm_sq = atom.norm_sq,
            .reach = 0,
            .allow_outer_overshoot_carry =
                decode_port_atom_id(atom.id).has_value(),
        });
        index_by_id.emplace(atom.id, idx);
      } else if (all_atoms[existing->second].norm_sq != atom.norm_sq) {
        return reject(RejectReason::kMalformed,
                      "incoming carry atom norm does not match band atom",
                      carry_width);
      }
    }
  }

  if (all_atoms.size() > options.max_atoms) {
    return reject(RejectReason::kOverflow, "atom count exceeds reach cap",
                  carry_width);
  }

  Dsu dsu(all_atoms.size());
  if (incoming) {
    const StaticReachSeparator canonical =
        canonicalize_static_reach_separator(*incoming);
    for (const auto& component : canonical.component_partition) {
      const AtomId first_id = component.front();
      const auto first = index_by_id.find(first_id);
      assert(first != index_by_id.end());
      for (std::size_t i = 1; i < component.size(); ++i) {
        const auto it = index_by_id.find(component[i]);
        assert(it != index_by_id.end());
        dsu.unite(first->second, it->second);
      }
    }
  }

  for (const auto& [a, b] : band.edges) {
    const auto ia = index_by_id.find(a);
    const auto ib = index_by_id.find(b);
    if (ia == index_by_id.end() || ib == index_by_id.end()) {
      return reject(RejectReason::kMalformed, "edge references missing atom",
                    carry_width);
    }
    dsu.unite(ia->second, ib->second);
  }

  std::vector<std::uint8_t> root_reach(all_atoms.size(), 0);
  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    root_reach[dsu.find(i)] |= all_atoms[i].reach;
  }
  if (incoming) {
    const StaticReachSeparator canonical =
        canonicalize_static_reach_separator(*incoming);
    for (std::size_t c = 0; c < canonical.component_partition.size(); ++c) {
      const AtomId id = canonical.component_partition[c].front();
      const auto it = index_by_id.find(id);
      assert(it != index_by_id.end());
      root_reach[dsu.find(it->second)] |= canonical.reach_per_component[c];
    }
  }

  std::map<std::size_t, std::vector<AtomId>> carry_by_root;
  std::unordered_map<AtomId, std::uint64_t> norm_by_id;
  norm_by_id.reserve(all_atoms.size());
  bool spanning = false;
  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    norm_by_id.emplace(all_atoms[i].id, all_atoms[i].norm_sq);
    const std::size_t root = dsu.find(i);
    if ((root_reach[root] & kStaticReachBoth) == kStaticReachBoth) {
      spanning = true;
    }
    if (in_final_carry_window(
            all_atoms[i].norm_sq, band.outer_radius, carry_width,
            all_atoms[i].allow_outer_overshoot_carry)) {
      carry_by_root[root].push_back(all_atoms[i].id);
    }
  }

  StaticReachProcessResult result;
  result.carry_width = carry_width;
  result.spanning = spanning;
  result.outgoing.component_partition.reserve(carry_by_root.size());
  result.outgoing.reach_per_component.reserve(carry_by_root.size());
  for (auto& [root, ids] : carry_by_root) {
    sort_unique_atom_ids(ids);
    if (ids.empty()) {
      continue;
    }
    result.outgoing.component_partition.push_back(ids);
    result.outgoing.reach_per_component.push_back(root_reach[root]);
    for (const AtomId id : ids) {
      result.outgoing.carry_atoms.push_back({id, norm_by_id.at(id)});
    }
  }
  result.outgoing = canonicalize_static_reach_separator(result.outgoing);
  if (result.outgoing.carry_atoms.size() > options.max_carry_atoms ||
      result.outgoing.component_partition.size() > options.max_components) {
    return reject(RejectReason::kOverflow,
                  "separator state exceeds reach caps", carry_width);
  }
  return result;
}

StaticReachProcessResult process_static_reach_bands(
    const std::vector<StaticReachBandInput>& bands,
    const std::optional<StaticReachSeparator>& incoming,
    const StaticReachProcessOptions& options) {
  std::optional<StaticReachSeparator> state = incoming;
  StaticReachProcessResult last;
  bool spanning = false;
  for (const StaticReachBandInput& band : bands) {
    last = process_static_reach_band(band, state, options);
    if (!last.accepted()) {
      return last;
    }
    spanning = spanning || last.spanning;
    last.spanning = spanning;
    if (spanning) {
      return last;
    }
    state = last.outgoing;
  }
  return last;
}

}  // namespace lb_source
