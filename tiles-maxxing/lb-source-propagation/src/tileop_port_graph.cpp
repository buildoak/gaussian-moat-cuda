#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "campaign/constants.h"
#include "campaign/geo_tests.h"
#include "tileop_internal.h"

namespace lb_source {
namespace {

struct PortRef {
  AtomId id = 0;
  std::uint8_t label = 0;
};

using CoordKey = std::pair<std::int32_t, std::int32_t>;

struct PrimeWithFlags {
  campaign::Prime prime;
  campaign::internal::PrimeGeoFlags flags;
};

bool prime_less(const campaign::Prime& lhs,
                const campaign::Prime& rhs) noexcept {
  if (lhs.a != rhs.a) {
    return lhs.a < rhs.a;
  }
  if (lhs.b != rhs.b) {
    return lhs.b < rhs.b;
  }
  if (lhs.norm_sq != rhs.norm_sq) {
    return lhs.norm_sq < rhs.norm_sq;
  }
  return lhs.packed_pos < rhs.packed_pos;
}

std::uint64_t tile_support_norm_sq(const campaign::TileCoord& coord) {
  const std::int64_t a_hi =
      coord.a_lo + static_cast<std::int64_t>(campaign::S);
  const std::int64_t b_hi =
      coord.b_lo + static_cast<std::int64_t>(campaign::S);
  if (a_hi < 0 || b_hi < 0) {
    std::cerr << "negative TileOp support coordinate\n";
    std::exit(EXIT_FAILURE);
  }
  const unsigned __int128 norm =
      static_cast<unsigned __int128>(a_hi) *
          static_cast<unsigned __int128>(a_hi) +
      static_cast<unsigned __int128>(b_hi) *
          static_cast<unsigned __int128>(b_hi);
  if (norm > std::numeric_limits<std::uint64_t>::max()) {
    std::cerr << "TileOp support norm overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return static_cast<std::uint64_t>(norm);
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

std::vector<PrimeWithFlags> sorted_primes_with_flags(
    std::vector<campaign::Prime> primes,
    const campaign::CampaignConstants& constants) {
  std::vector<PrimeWithFlags> zipped;
  zipped.reserve(primes.size());
  for (const campaign::Prime& prime : primes) {
    const auto norm_sq = static_cast<std::int64_t>(prime.norm_sq);
    zipped.push_back(PrimeWithFlags{
        prime,
        campaign::internal::PrimeGeoFlags{
            campaign::is_inner_prime(norm_sq, constants),
            campaign::is_outer_prime(norm_sq, constants),
        },
    });
  }
  std::sort(zipped.begin(), zipped.end(),
            [](const PrimeWithFlags& lhs, const PrimeWithFlags& rhs) {
              return prime_less(lhs.prime, rhs.prime);
            });
  return zipped;
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

  for (std::size_t t = 0; t < input.coords.size(); ++t) {
    const campaign::TileCoord& coord = input.coords[t];
    const campaign::TileOp& op = input.tileops[t];
    const std::uint64_t atom_norm = tile_support_norm_sq(coord);
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
        result.band.atoms.push_back({port.id, atom_norm, source});
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

CoordinatePortBridgeResult bridge_coordinate_prime_to_ports(
    const CoordinatePortBridgeInput& input) {
  CoordinatePortBridgeResult result;

  std::vector<campaign::Prime> primes = input.primes;
  if (primes.empty()) {
    primes = campaign::sieve_tile(input.coord, input.constants);
  }
  if (primes.empty()) {
    result.diagnostic = "target prime not found in empty tile sieve";
    return result;
  }

  std::vector<PrimeWithFlags> zipped =
      sorted_primes_with_flags(std::move(primes), input.constants);

  std::vector<campaign::Prime> sorted_primes;
  std::vector<campaign::internal::PrimeGeoFlags> sorted_flags;
  sorted_primes.reserve(zipped.size());
  sorted_flags.reserve(zipped.size());
  std::optional<std::int32_t> target_index;
  for (std::size_t i = 0; i < zipped.size(); ++i) {
    const campaign::Prime& prime = zipped[i].prime;
    if (prime.a == input.target.a && prime.b == input.target.b) {
      if (input.target.norm_sq != 0 && prime.norm_sq != input.target.norm_sq) {
        result.diagnostic = "target prime norm mismatch";
        return result;
      }
      target_index = static_cast<std::int32_t>(i);
    }
    sorted_primes.push_back(prime);
    sorted_flags.push_back(zipped[i].flags);
  }

  const campaign::TileOp expected =
      campaign::internal::build_tileop_for_primes(sorted_primes, sorted_flags,
                                                  input.coord,
                                                  input.constants);
  if (std::memcmp(&expected, &input.tileop, sizeof(campaign::TileOp)) != 0) {
    result.diagnostic = "TileOp bytes do not match coord/constants/primes";
    return result;
  }

  if ((input.tileop.tile_flags & campaign::OVERFLOW_BIT) != 0) {
    result.diagnostic = "cannot bridge coordinate prime through overflow TileOp";
    return result;
  }
  if ((input.tileop.tile_flags & campaign::EMPTY_BIT) != 0) {
    result.diagnostic = "cannot bridge coordinate prime through empty TileOp";
    return result;
  }

  if (!target_index.has_value()) {
    result.diagnostic = "target prime not found in tile sieve";
    return result;
  }

  campaign::DSU local_dsu = campaign::internal::build_local_dsu(sorted_primes);
  const campaign::internal::DenseRemap remap =
      campaign::internal::dense_remap_visible_roots(
          &local_dsu, sorted_primes, sorted_flags, input.coord);
  if (remap.overflow) {
    result.diagnostic = "TileOp visible component remap overflow";
    return result;
  }

  const std::int32_t raw_root = local_dsu.find(*target_index);
  result.tileop_label =
      remap.wire_label_by_raw_root[static_cast<std::size_t>(raw_root)];
  if (result.tileop_label == 0) {
    result.diagnostic = "coordinate component is not TileOp-visible";
    return result;
  }

  for (int face_idx = 0; face_idx < campaign::NUM_FACES; ++face_idx) {
    const campaign::Face face = static_cast<campaign::Face>(face_idx);
    const int offset = campaign::face_offset(input.tileop, face);
    for (std::uint8_t ordinal = 0; ordinal < input.tileop.n[face_idx];
         ++ordinal) {
      const std::uint8_t label = input.tileop.face_groups[offset + ordinal];
      if (label != result.tileop_label) {
        continue;
      }
      const std::optional<AtomId> id =
          checked_port_atom_id(input.coord, face, ordinal);
      if (!id.has_value()) {
        result.diagnostic = "TileOp port atom id overflow";
        result.port_atoms.clear();
        return result;
      }
      result.port_atoms.push_back(*id);
    }
  }

  if (result.port_atoms.empty()) {
    result.diagnostic = "visible coordinate component has no encoded face ports";
    return result;
  }
  return result;
}

}  // namespace lb_source
