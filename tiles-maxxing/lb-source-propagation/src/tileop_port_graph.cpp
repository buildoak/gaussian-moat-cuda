#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "campaign/constants.h"

namespace lb_source {
namespace {

struct PortRef {
  AtomId id = 0;
  std::uint8_t label = 0;
};

using CoordKey = std::pair<std::int32_t, std::int32_t>;

std::uint64_t square_u64(std::uint64_t value) {
  return value * value;
}

std::optional<AtomId> checked_port_atom_id(const campaign::TileCoord& coord,
                                           campaign::Face face,
                                           std::uint8_t ordinal) {
  return port_atom_id(coord.i, coord.j, static_cast<std::uint64_t>(face),
                      ordinal);
}

std::vector<PortRef> ports_for_face(const campaign::TileCoord& coord,
                                    const campaign::TileOp& op,
                                    campaign::Face face,
                                    std::string& diagnostic) {
  std::vector<PortRef> ports;
  const int face_idx = static_cast<int>(face);
  const int offset = campaign::face_offset(op, face);
  ports.reserve(op.n[face_idx]);
  for (std::uint8_t ordinal = 0; ordinal < op.n[face_idx]; ++ordinal) {
    const std::uint8_t label = op.face_groups[offset + ordinal];
    if (label == 0 || label > campaign::MAX_GROUPS_PER_TILE) {
      diagnostic = "active TileOp port has invalid group label";
      return {};
    }
    const std::optional<AtomId> id =
        checked_port_atom_id(coord, face, ordinal);
    if (!id.has_value()) {
      diagnostic = "TileOp port atom id overflow";
      return {};
    }
    ports.push_back({*id, label});
  }
  return ports;
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

}  // namespace

TileOpPortGraphResult make_tileop_port_band(
    const TileOpPortGraphInput& input) {
  TileOpPortGraphResult result;
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
  std::set<std::pair<AtomId, AtomId>> edges;
  std::map<std::pair<CoordKey, int>, std::vector<PortRef>> ports_by_face;
  const std::uint64_t carry_norm = square_u64(input.outer_radius);

  for (std::size_t t = 0; t < input.coords.size(); ++t) {
    const campaign::TileCoord& coord = input.coords[t];
    const campaign::TileOp& op = input.tileops[t];
    if (!index_by_coord.emplace(CoordKey{coord.i, coord.j}, t).second) {
      result.diagnostic = "duplicate TileOp coordinate";
      return result;
    }
    if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
      ++result.overflow_tiles;
      result.band.force_overflow = true;
      continue;
    }
    if ((op.tile_flags & campaign::EMPTY_BIT) != 0) {
      ++result.empty_tiles;
      continue;
    }

    std::map<std::uint8_t, AtomId> first_port_by_label;
    for (int face_idx = 0; face_idx < campaign::NUM_FACES; ++face_idx) {
      const campaign::Face face = static_cast<campaign::Face>(face_idx);
      std::vector<PortRef> ports = ports_for_face(coord, op, face,
                                                  result.diagnostic);
      if (!result.diagnostic.empty()) {
        return result;
      }
      for (const PortRef& port : ports) {
        const bool source =
            input.seed_inner_flags && campaign::bit_test(op.inner_flags,
                                                         port.label);
        result.band.atoms.push_back({port.id, carry_norm, source});
        ++result.port_atoms;

        auto [it, inserted] = first_port_by_label.emplace(port.label, port.id);
        if (!inserted) {
          const std::size_t before = edges.size();
          add_edge(edges, it->second, port.id);
          if (edges.size() != before) {
            ++result.internal_edges;
          }
        }
      }
      ports_by_face.emplace(std::pair{CoordKey{coord.i, coord.j}, face_idx},
                            std::move(ports));
    }
  }

  for (const auto& [coord, t] : index_by_coord) {
    const campaign::TileOp& op = input.tileops[t];
    if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
      continue;
    }
    const CoordKey above{coord.first, coord.second + 1};
    const auto above_it = index_by_coord.find(above);
    if (above_it != index_by_coord.end()) {
      if ((input.tileops[above_it->second].tile_flags &
           campaign::OVERFLOW_BIT) != 0) {
        continue;
      }
      const auto& lower_ports =
          ports_by_face[{coord, static_cast<int>(campaign::Face::O)}];
      const auto& upper_ports =
          ports_by_face[{above, static_cast<int>(campaign::Face::I)}];
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
    if (right_it != index_by_coord.end()) {
      if ((input.tileops[right_it->second].tile_flags &
           campaign::OVERFLOW_BIT) != 0) {
        continue;
      }
      const auto& left_ports =
          ports_by_face[{coord, static_cast<int>(campaign::Face::R)}];
      const auto& right_ports =
          ports_by_face[{right, static_cast<int>(campaign::Face::L)}];
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

}  // namespace lb_source
