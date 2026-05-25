#include "lb_source/tileop_port_stream.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "campaign/constants.h"

namespace lb_source {
namespace {

using CoordKey = std::pair<std::int32_t, std::int32_t>;

struct PortRef {
  AtomId id = 0;
  std::uint8_t local_label = 0;
};

std::optional<std::uint64_t> tile_support_norm_sq(
    const campaign::TileCoord& coord) {
  const std::int64_t a_hi =
      coord.a_lo + static_cast<std::int64_t>(campaign::S);
  const std::int64_t b_hi =
      coord.b_lo + static_cast<std::int64_t>(campaign::S);
  if (a_hi < 0 || b_hi < 0) {
    return std::nullopt;
  }
  const unsigned __int128 norm =
      static_cast<unsigned __int128>(a_hi) *
          static_cast<unsigned __int128>(a_hi) +
      static_cast<unsigned __int128>(b_hi) *
          static_cast<unsigned __int128>(b_hi);
  if (norm > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(norm);
}

std::optional<AtomId> checked_port_atom_id(const campaign::TileCoord& coord,
                                           campaign::Face face,
                                           std::uint8_t ordinal) {
  return port_atom_id(coord.i, coord.j, static_cast<std::uint64_t>(face),
                      ordinal);
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

std::vector<PortRef> ports_for_face(
    const campaign::TileCoord& coord,
    const campaign::TileOp& op,
    campaign::Face face,
    std::vector<TileOpDecodedPort>* decoded_ports,
    bool seed_inner_flags,
    std::string& diagnostic) {
  std::vector<PortRef> ports;
  const int face_idx = static_cast<int>(face);
  const int offset = campaign::face_offset(op, face);
  if (offset + op.n[face_idx] >
      static_cast<int>(sizeof(op.face_groups) / sizeof(op.face_groups[0]))) {
    diagnostic = "TileOp active port payload exceeds face_groups";
    return {};
  }
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
    const bool source =
        seed_inner_flags && campaign::bit_test(op.inner_flags, label);
    ports.push_back({*id, label});
    if (decoded_ports != nullptr) {
      decoded_ports->push_back(TileOpDecodedPort{
          .id = *id,
          .face = static_cast<std::uint8_t>(face),
          .ordinal = ordinal,
          .local_label = label,
          .certified_source = source,
      });
    }
  }
  return ports;
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

TileOpPortDecodedTile decode_tileop_ports(
    const campaign::TileCoord& coord,
    const campaign::TileOp& tileop,
    bool seed_inner_flags) {
  TileOpPortDecodedTile result;
  result.coord = coord;
  const std::optional<std::uint64_t> norm = tile_support_norm_sq(coord);
  if (!norm.has_value()) {
    result.diagnostic = "TileOp support norm overflow";
    return result;
  }
  result.support_norm_sq = *norm;

  if ((tileop.tile_flags & campaign::OVERFLOW_BIT) != 0) {
    result.overflow = true;
    return result;
  }
  if ((tileop.tile_flags & campaign::EMPTY_BIT) != 0) {
    result.empty = true;
    return result;
  }

  std::map<std::uint8_t, AtomId> first_port_by_label;
  std::set<std::pair<AtomId, AtomId>> internal_edges;
  for (int face_idx = 0; face_idx < campaign::NUM_FACES; ++face_idx) {
    const campaign::Face face = static_cast<campaign::Face>(face_idx);
    std::vector<PortRef> ports =
        ports_for_face(coord, tileop, face, &result.ports, seed_inner_flags,
                       result.diagnostic);
    if (!result.diagnostic.empty()) {
      return result;
    }
    for (const PortRef& port : ports) {
      auto [it, inserted] =
          first_port_by_label.emplace(port.local_label, port.id);
      if (!inserted) {
        add_edge(internal_edges, it->second, port.id);
      }
    }
  }
  result.internal_edges.assign(internal_edges.begin(), internal_edges.end());
  return result;
}

TileOpPortStreamResult build_tileop_port_microband(
    const TileOpPortStreamInput& input) {
  TileOpPortStreamResult result;
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
        decode_tileop_ports(coord, op, input.seed_inner_flags);
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

    for (const TileOpDecodedPort& port : decoded.ports) {
      result.band.atoms.push_back(BandAtom{
          .id = port.id,
          .norm_sq = decoded.support_norm_sq,
          .certified_source = port.certified_source,
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

    for (int face_idx = 0; face_idx < campaign::NUM_FACES; ++face_idx) {
      const campaign::Face face = static_cast<campaign::Face>(face_idx);
      std::vector<PortRef> ports =
          ports_for_face(coord, op, face, nullptr, input.seed_inner_flags,
                         result.diagnostic);
      if (!result.diagnostic.empty()) {
        return result;
      }
      ports_by_face.emplace(std::pair{key, face_idx}, std::move(ports));
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
    }

    const CoordKey right{coord.first + 1, coord.second};
    const auto right_it = index_by_coord.find(right);
    if (right_it != index_by_coord.end()) {
      if ((input.tileops[right_it->second].tile_flags &
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
  }

  result.band.edges.assign(edges.begin(), edges.end());
  return result;
}

}  // namespace lb_source
