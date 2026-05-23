#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <tuple>
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

struct PortWitness {
  AtomId id = 0;
  std::uint8_t face = 0;
  std::uint8_t ordinal = 0;
  std::uint8_t label = 0;
  std::int32_t representative_index = -1;
  std::int64_t h = 0;
  std::int64_t p_perp = 0;
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

bool within_k_sq(const campaign::Prime& lhs,
                 const campaign::Prime& rhs) noexcept {
  const __int128 da =
      static_cast<__int128>(lhs.a) - static_cast<__int128>(rhs.a);
  const __int128 db =
      static_cast<__int128>(lhs.b) - static_cast<__int128>(rhs.b);
  return da * da + db * db <= static_cast<__int128>(campaign::k_sq_value);
}

std::int64_t rel_col(const campaign::Prime& prime,
                     const campaign::TileCoord& coord) noexcept {
  return prime.a - coord.a_lo;
}

std::int64_t rel_row(const campaign::Prime& prime,
                     const campaign::TileCoord& coord) noexcept {
  return prime.b - coord.b_lo;
}

std::int64_t face_h(const campaign::Prime& prime,
                    const campaign::TileCoord& coord,
                    campaign::Face face) noexcept {
  switch (face) {
    case campaign::Face::I:
    case campaign::Face::O:
      return rel_col(prime, coord);
    case campaign::Face::L:
    case campaign::Face::R:
      return rel_row(prime, coord);
  }
  return 0;
}

std::int64_t face_perp(const campaign::Prime& prime,
                       const campaign::TileCoord& coord,
                       campaign::Face face) noexcept {
  switch (face) {
    case campaign::Face::I:
      return rel_row(prime, coord);
    case campaign::Face::O:
      return rel_row(prime, coord) - campaign::S;
    case campaign::Face::L:
      return rel_col(prime, coord);
    case campaign::Face::R:
      return rel_col(prime, coord) - campaign::S;
  }
  return 0;
}

bool on_face_strip(const campaign::Prime& prime,
                   const campaign::TileCoord& coord,
                   campaign::Face face) noexcept {
  const std::int64_t p_perp = face_perp(prime, coord, face);
  return -static_cast<std::int64_t>(campaign::C) <= p_perp &&
         p_perp <= static_cast<std::int64_t>(campaign::C);
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

std::vector<std::vector<std::int32_t>> build_prime_adjacency(
    const std::vector<campaign::Prime>& primes) {
  std::vector<std::vector<std::int32_t>> adjacency(primes.size());
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(primes.size()); ++i) {
    for (std::int32_t j = i + 1;
         j < static_cast<std::int32_t>(primes.size()); ++j) {
      if (!within_k_sq(primes[static_cast<std::size_t>(i)],
                       primes[static_cast<std::size_t>(j)])) {
        continue;
      }
      adjacency[static_cast<std::size_t>(i)].push_back(j);
      adjacency[static_cast<std::size_t>(j)].push_back(i);
    }
  }
  return adjacency;
}

std::vector<std::int32_t> prime_index_path(
    const std::vector<std::vector<std::int32_t>>& adjacency,
    std::int32_t begin,
    std::int32_t end) {
  if (begin < 0 || end < 0 ||
      begin >= static_cast<std::int32_t>(adjacency.size()) ||
      end >= static_cast<std::int32_t>(adjacency.size())) {
    return {};
  }
  std::vector<std::int32_t> parent(adjacency.size(), -1);
  std::queue<std::int32_t> pending;
  parent[static_cast<std::size_t>(begin)] = begin;
  pending.push(begin);
  while (!pending.empty()) {
    const std::int32_t current = pending.front();
    pending.pop();
    if (current == end) {
      break;
    }
    for (const std::int32_t next : adjacency[static_cast<std::size_t>(current)]) {
      if (parent[static_cast<std::size_t>(next)] != -1) {
        continue;
      }
      parent[static_cast<std::size_t>(next)] = current;
      pending.push(next);
    }
  }
  if (parent[static_cast<std::size_t>(end)] == -1) {
    return {};
  }
  std::vector<std::int32_t> path;
  for (std::int32_t at = end;; at = parent[static_cast<std::size_t>(at)]) {
    path.push_back(at);
    if (at == begin) {
      break;
    }
  }
  std::reverse(path.begin(), path.end());
  return path;
}

std::vector<CoordinateAtom> coordinate_path_for_prime_indices(
    const std::vector<campaign::Prime>& primes,
    const std::vector<std::int32_t>& indices) {
  std::vector<CoordinateAtom> path;
  path.reserve(indices.size());
  for (const std::int32_t index : indices) {
    const campaign::Prime& prime = primes[static_cast<std::size_t>(index)];
    path.push_back(
        {.a = prime.a, .b = prime.b, .norm_sq = prime.norm_sq});
  }
  return path;
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

std::vector<PortWitness> build_port_witnesses(
    const campaign::TileCoord& coord,
    const campaign::TileOp& op,
    const std::vector<campaign::Prime>& sorted_primes,
    campaign::DSU* local_dsu,
    const campaign::internal::DenseRemap& remap,
    std::string& diagnostic) {
  std::vector<PortWitness> witnesses;
  const std::array<campaign::Face, campaign::NUM_FACES> faces = {
      campaign::Face::I, campaign::Face::O, campaign::Face::L,
      campaign::Face::R};
  for (const campaign::Face face : faces) {
    std::vector<std::int32_t> face_indices;
    for (std::int32_t i = 0;
         i < static_cast<std::int32_t>(sorted_primes.size()); ++i) {
      if (on_face_strip(sorted_primes[static_cast<std::size_t>(i)], coord,
                        face)) {
        face_indices.push_back(i);
      }
    }

    campaign::DSU face_dsu(static_cast<std::int32_t>(face_indices.size()));
    for (std::int32_t i = 0;
         i < static_cast<std::int32_t>(face_indices.size()); ++i) {
      for (std::int32_t j = i + 1;
           j < static_cast<std::int32_t>(face_indices.size()); ++j) {
        const campaign::Prime& lhs =
            sorted_primes[static_cast<std::size_t>(
                face_indices[static_cast<std::size_t>(i)])];
        const campaign::Prime& rhs =
            sorted_primes[static_cast<std::size_t>(
                face_indices[static_cast<std::size_t>(j)])];
        if (within_k_sq(lhs, rhs)) {
          face_dsu.unite(i, j);
        }
      }
    }

    struct LocalPortWitness {
      std::int64_t h = 0;
      std::int64_t p_perp = 0;
      std::uint8_t label = 0;
      std::int32_t representative_index = -1;
    };
    std::vector<LocalPortWitness> local_ports;
    const std::vector<std::int32_t> roots = face_dsu.roots();
    local_ports.reserve(roots.size());
    for (const std::int32_t root : roots) {
      bool have_rep = false;
      LocalPortWitness best;
      for (std::int32_t k = 0;
           k < static_cast<std::int32_t>(face_indices.size()); ++k) {
        if (face_dsu.find(k) != root) {
          continue;
        }
        const std::int32_t prime_idx =
            face_indices[static_cast<std::size_t>(k)];
        const campaign::Prime& prime =
            sorted_primes[static_cast<std::size_t>(prime_idx)];
        const std::int64_t h = face_h(prime, coord, face);
        const std::int64_t p_perp = face_perp(prime, coord, face);
        if (!have_rep || h < best.h ||
            (h == best.h && p_perp < best.p_perp)) {
          have_rep = true;
          const std::int32_t raw_root = local_dsu->find(prime_idx);
          best = LocalPortWitness{
              .h = h,
              .p_perp = p_perp,
              .label = remap.wire_label_by_raw_root[static_cast<std::size_t>(
                  raw_root)],
              .representative_index = prime_idx,
          };
        }
      }
      if (!have_rep) {
        diagnostic = "empty TileOp face-port component";
        return {};
      }
      local_ports.push_back(best);
    }
    std::sort(local_ports.begin(), local_ports.end(),
              [](const LocalPortWitness& lhs,
                 const LocalPortWitness& rhs) {
                if (lhs.h != rhs.h) {
                  return lhs.h < rhs.h;
                }
                if (lhs.p_perp != rhs.p_perp) {
                  return lhs.p_perp < rhs.p_perp;
                }
                return lhs.label < rhs.label;
              });
    if (local_ports.size() != op.n[static_cast<int>(face)]) {
      diagnostic = "reconstructed TileOp port count mismatch";
      return {};
    }
    const int offset = campaign::face_offset(op, face);
    for (std::uint8_t ordinal = 0; ordinal < local_ports.size(); ++ordinal) {
      const LocalPortWitness& local = local_ports[ordinal];
      if (local.label != op.face_groups[offset + ordinal]) {
        diagnostic = "reconstructed TileOp port label mismatch";
        return {};
      }
      const std::optional<AtomId> id =
          checked_port_atom_id(coord, face, ordinal);
      if (!id.has_value()) {
        diagnostic = "TileOp port atom id overflow";
        return {};
      }
      witnesses.push_back(PortWitness{
          .id = *id,
          .face = static_cast<std::uint8_t>(face),
          .ordinal = ordinal,
          .label = local.label,
          .representative_index = local.representative_index,
          .h = local.h,
          .p_perp = local.p_perp,
      });
    }
  }
  return witnesses;
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
        result.band.atoms.push_back(
            {.id = port.id,
             .norm_sq = atom_norm,
             .certified_source = source,
             .allow_outer_overshoot_carry = true});
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
  std::optional<std::size_t> target_index;
  for (std::size_t i = 0; i < primes.size(); ++i) {
    const campaign::Prime& prime = primes[i];
    if (prime.a != input.target.a || prime.b != input.target.b) {
      continue;
    }
    if (input.target.norm_sq != 0 && prime.norm_sq != input.target.norm_sq) {
      result.diagnostic = "target prime norm mismatch";
      return result;
    }
    target_index = i;
    break;
  }
  if (!target_index.has_value()) {
    result.diagnostic = "target prime not found in tile sieve";
    return result;
  }

  CoordinatePortBridgeBatchResult batch =
      bridge_coordinate_prime_batch_to_ports({
          .coord = input.coord,
          .constants = input.constants,
          .tileop = input.tileop,
          .primes = std::move(primes),
          .target_indices = {*target_index},
      });
  if (!batch.accepted()) {
    result.diagnostic = batch.diagnostic;
    return result;
  }
  return batch.bridges.front();
}

CoordinatePortBridgeBatchResult bridge_coordinate_prime_batch_to_ports(
    const CoordinatePortBridgeBatchInput& input) {
  CoordinatePortBridgeBatchResult batch;
  std::vector<campaign::Prime> primes = input.primes;
  if (primes.empty()) {
    primes = campaign::sieve_tile(input.coord, input.constants);
  }
  if (primes.empty()) {
    batch.diagnostic = "target prime not found in empty tile sieve";
    return batch;
  }

  std::vector<PrimeWithFlags> zipped =
      sorted_primes_with_flags(primes, input.constants);

  std::vector<campaign::Prime> sorted_primes;
  std::vector<campaign::internal::PrimeGeoFlags> sorted_flags;
  std::map<std::tuple<std::int64_t, std::int64_t, std::uint64_t>, std::int32_t>
      sorted_index_by_prime;
  sorted_primes.reserve(zipped.size());
  sorted_flags.reserve(zipped.size());
  for (std::size_t i = 0; i < zipped.size(); ++i) {
    const campaign::Prime& prime = zipped[i].prime;
    sorted_index_by_prime.emplace(
        std::tuple<std::int64_t, std::int64_t, std::uint64_t>{
            prime.a, prime.b, prime.norm_sq},
        static_cast<std::int32_t>(i));
    sorted_primes.push_back(prime);
    sorted_flags.push_back(zipped[i].flags);
  }

  const campaign::TileOp expected =
      campaign::internal::build_tileop_for_primes(sorted_primes, sorted_flags,
                                                  input.coord,
                                                  input.constants);
  if (std::memcmp(&expected, &input.tileop, sizeof(campaign::TileOp)) != 0) {
    batch.diagnostic = "TileOp bytes do not match coord/constants/primes";
    return batch;
  }

  std::vector<std::size_t> target_indices = input.target_indices;
  if (target_indices.empty()) {
    batch.diagnostic = "batch target indices must not be empty";
    return batch;
  }
  batch.bridges.resize(target_indices.size());

  if ((input.tileop.tile_flags & campaign::OVERFLOW_BIT) != 0) {
    for (CoordinatePortBridgeResult& bridge : batch.bridges) {
      bridge.diagnostic = "cannot bridge coordinate prime through overflow TileOp";
    }
    return batch;
  }
  if ((input.tileop.tile_flags & campaign::EMPTY_BIT) != 0) {
    for (CoordinatePortBridgeResult& bridge : batch.bridges) {
      bridge.diagnostic = "cannot bridge coordinate prime through empty TileOp";
    }
    return batch;
  }

  campaign::DSU local_dsu = campaign::internal::build_local_dsu(sorted_primes);
  const campaign::internal::DenseRemap remap =
      campaign::internal::dense_remap_visible_roots(
          &local_dsu, sorted_primes, sorted_flags, input.coord);
  if (remap.overflow) {
    batch.diagnostic = "TileOp visible component remap overflow";
    return batch;
  }

  std::map<std::uint8_t, std::vector<AtomId>> ports_by_label;
  std::map<AtomId, PortWitness> witness_by_port;
  const std::vector<PortWitness> port_witnesses = build_port_witnesses(
      input.coord, input.tileop, sorted_primes, &local_dsu, remap,
      batch.diagnostic);
  if (!batch.diagnostic.empty()) {
    return batch;
  }
  for (const PortWitness& witness : port_witnesses) {
    if (!witness_by_port.emplace(witness.id, witness).second) {
      batch.diagnostic = "reconstructed duplicate TileOp port witness";
      return batch;
    }
  }
  for (int face_idx = 0; face_idx < campaign::NUM_FACES; ++face_idx) {
    const campaign::Face face = static_cast<campaign::Face>(face_idx);
    const int offset = campaign::face_offset(input.tileop, face);
    for (std::uint8_t ordinal = 0; ordinal < input.tileop.n[face_idx];
         ++ordinal) {
      const std::uint8_t label = input.tileop.face_groups[offset + ordinal];
      const std::optional<AtomId> id =
          checked_port_atom_id(input.coord, face, ordinal);
      if (!id.has_value()) {
        batch.diagnostic = "TileOp port atom id overflow";
        return batch;
      }
      ports_by_label[label].push_back(*id);
    }
  }
  const std::vector<std::vector<std::int32_t>> prime_adjacency =
      build_prime_adjacency(sorted_primes);

  for (std::size_t i = 0; i < target_indices.size(); ++i) {
    CoordinatePortBridgeResult& bridge = batch.bridges[i];
    const std::size_t target_index = target_indices[i];
    if (target_index >= primes.size()) {
      bridge.diagnostic = "target prime index out of range";
      continue;
    }
    const campaign::Prime& target = primes[target_index];
    const auto sorted_it = sorted_index_by_prime.find(
        {target.a, target.b, target.norm_sq});
    if (sorted_it == sorted_index_by_prime.end()) {
      bridge.diagnostic = "target prime not found in tile sieve";
      continue;
    }
    const std::int32_t raw_root = local_dsu.find(sorted_it->second);
    bridge.tileop_label =
        remap.wire_label_by_raw_root[static_cast<std::size_t>(raw_root)];
    if (bridge.tileop_label == 0) {
      bridge.diagnostic = "coordinate component is not TileOp-visible";
      continue;
    }
    const auto ports_it = ports_by_label.find(bridge.tileop_label);
    if (ports_it == ports_by_label.end() || ports_it->second.empty()) {
      bridge.diagnostic =
          "visible coordinate component has no encoded face ports";
      continue;
    }
    bridge.port_atoms = ports_it->second;
    bridge.port_expansions.reserve(bridge.port_atoms.size());
    for (const AtomId port_atom : bridge.port_atoms) {
      const auto witness_it = witness_by_port.find(port_atom);
      if (witness_it == witness_by_port.end()) {
        bridge.diagnostic = "missing reconstructed TileOp port witness";
        bridge.port_atoms.clear();
        bridge.port_expansions.clear();
        break;
      }
      const PortWitness& witness = witness_it->second;
      const std::vector<std::int32_t> index_path =
          prime_index_path(prime_adjacency, sorted_it->second,
                           witness.representative_index);
      if (index_path.empty()) {
        bridge.diagnostic =
            "visible coordinate component lacks local prime path to port";
        bridge.port_atoms.clear();
        bridge.port_expansions.clear();
        break;
      }
      bridge.port_expansions.push_back(
          CoordinatePortBridgeResult::PortExpansion{
              .port_atom = port_atom,
              .face = witness.face,
              .ordinal = witness.ordinal,
              .tileop_label = bridge.tileop_label,
              .path =
                  coordinate_path_for_prime_indices(sorted_primes, index_path),
          });
    }
  }
  return batch;
}

}  // namespace lb_source
