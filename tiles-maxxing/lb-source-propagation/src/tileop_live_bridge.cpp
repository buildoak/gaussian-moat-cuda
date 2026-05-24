#include "lb_source/tileop_live_bridge.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "campaign/constants.h"
#include "campaign/sieve.h"
#include "lb_source/tileop_port_graph.h"

namespace lb_source {
namespace {

struct Point {
  std::int64_t a = 0;
  std::int64_t b = 0;
  std::uint64_t norm_sq = 0;
};

struct PointKey {
  std::int64_t a = 0;
  std::int64_t b = 0;

  friend bool operator==(const PointKey&, const PointKey&) = default;
};

struct PointKeyHash {
  std::size_t operator()(const PointKey& key) const noexcept {
    const std::uint64_t a = static_cast<std::uint64_t>(key.a);
    const std::uint64_t b = static_cast<std::uint64_t>(key.b);
    std::uint64_t x = a + 0x9e3779b97f4a7c15ULL;
    x ^= b + 0xbf58476d1ce4e5b9ULL + (x << 6) + (x >> 2);
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return static_cast<std::size_t>(x);
  }
};

std::uint64_t dist_sq(const Point& lhs, const Point& rhs) {
  const __int128 da = static_cast<__int128>(lhs.a) - rhs.a;
  const __int128 db = static_cast<__int128>(lhs.b) - rhs.b;
  return static_cast<std::uint64_t>(da * da + db * db);
}

std::size_t resolve_worker_threads(std::size_t requested,
                                   std::size_t work_items) {
  if (work_items == 0) {
    return 1;
  }
  std::size_t threads = requested;
  if (threads == 0) {
    threads = std::thread::hardware_concurrency();
  }
  if (threads == 0) {
    threads = 1;
  }
  return std::min(threads, work_items);
}

bool all_rejected_candidates_are_dead_end(
    const std::set<std::string>& reasons) {
  return !reasons.empty() && reasons.size() == 1 &&
         reasons.find(
             "visible coordinate component has no encoded face ports") !=
             reasons.end();
}

CoordinateAtom coordinate_atom_from_point(const Point& point) {
  return {.a = point.a, .b = point.b, .norm_sq = point.norm_sq};
}

bool coordinate_path_less(const std::vector<CoordinateAtom>& lhs,
                          const std::vector<CoordinateAtom>& rhs) {
  if (lhs.size() != rhs.size()) {
    return lhs.size() < rhs.size();
  }
  return std::lexicographical_compare(
      lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
      [](const CoordinateAtom& a, const CoordinateAtom& b) {
        if (a.a != b.a) {
          return a.a < b.a;
        }
        if (a.b != b.b) {
          return a.b < b.b;
        }
        return a.norm_sq < b.norm_sq;
      });
}

void record_coordinate_port_path(
    std::map<std::pair<AtomId, AtomId>, std::vector<CoordinateAtom>>& paths,
    AtomId coordinate_atom_id,
    AtomId port_atom_id,
    std::vector<CoordinateAtom> path) {
  if (path.empty()) {
    return;
  }
  const auto key = std::pair{coordinate_atom_id, port_atom_id};
  const auto existing = paths.find(key);
  if (existing == paths.end() || coordinate_path_less(path, existing->second)) {
    paths[key] = std::move(path);
  }
}

struct LocalBridgeAccumulator {
  std::map<AtomId, std::set<AtomId>> ports_by_coord_id;
  std::set<AtomId> candidate_coord_ids;
  std::set<AtomId> bridge_rejected_coord_ids;
  std::map<std::string, std::set<AtomId>> rejected_coord_ids_by_reason;
  std::map<AtomId, std::set<std::string>> rejected_reasons_by_coord_id;
  std::map<std::pair<AtomId, AtomId>, std::vector<CoordinateAtom>>
      coordinate_port_paths;
};

}  // namespace

TileOpLiveBridgeResult bridge_coordinate_live_handoff_to_ports(
    const LiveHandoffV1& handoff,
    const campaign::CampaignConstants& constants,
    const std::vector<campaign::TileCoord>& coords,
    const std::vector<campaign::TileOp>& tileops,
    BandInput& graph_band,
    std::size_t worker_threads) {
  TileOpLiveBridgeResult result;

  std::unordered_map<AtomId, std::uint64_t> port_norm_by_id;
  port_norm_by_id.reserve(graph_band.atoms.size());
  for (const BandAtom& atom : graph_band.atoms) {
    if (!port_norm_by_id.emplace(atom.id, atom.norm_sq).second) {
      std::cerr << "TileOp port graph emitted duplicate atom id\n";
      std::exit(EXIT_FAILURE);
    }
  }

  std::map<AtomId, Point> carry_point_by_id;
  std::unordered_map<PointKey, AtomId, PointKeyHash> carry_id_by_point;
  carry_id_by_point.reserve(handoff.separator.carry_atoms.size());
  std::unordered_set<AtomId> source_carry_ids;
  source_carry_ids.reserve(handoff.separator.carry_atoms.size());
  for (const CarryAtom& atom : handoff.separator.carry_atoms) {
    const std::optional<CoordinateAtom> decoded =
        decode_coordinate_atom_id(atom.id);
    if (!decoded.has_value() || decoded->norm_sq != atom.norm_sq) {
      std::cerr << "live handoff carry atom is not a stable coordinate atom\n";
      std::exit(EXIT_FAILURE);
    }
    const Point point{decoded->a, decoded->b, decoded->norm_sq};
    carry_point_by_id.emplace(atom.id, point);
    if (!carry_id_by_point.emplace(PointKey{point.a, point.b}, atom.id)
             .second) {
      std::cerr << "live handoff emitted duplicate coordinate carry point\n";
      std::exit(EXIT_FAILURE);
    }
  }
  for (std::size_t c = 0; c < handoff.separator.component_partition.size();
       ++c) {
    if (!handoff.separator.source_bit_per_component[c]) {
      continue;
    }
    source_carry_ids.insert(handoff.separator.component_partition[c].begin(),
                            handoff.separator.component_partition[c].end());
  }

  std::map<AtomId, std::set<AtomId>> ports_by_coord_id;
  std::set<AtomId> candidate_coord_ids;
  std::set<AtomId> bridge_rejected_coord_ids;
  std::map<std::string, std::set<AtomId>> rejected_coord_ids_by_reason;
  std::map<AtomId, std::set<std::string>> rejected_reasons_by_coord_id;
  const std::int64_t bridge_radius = static_cast<std::int64_t>(
      ceil_sqrt(static_cast<std::uint64_t>(campaign::k_sq_value)));
  std::vector<PointKey> bridge_offsets;
  bridge_offsets.reserve(static_cast<std::size_t>((2 * bridge_radius + 1) *
                                                  (2 * bridge_radius + 1)));
  for (std::int64_t da = -bridge_radius; da <= bridge_radius; ++da) {
    for (std::int64_t db = -bridge_radius; db <= bridge_radius; ++db) {
      const __int128 offset_dist =
          static_cast<__int128>(da) * da + static_cast<__int128>(db) * db;
      if (offset_dist > static_cast<__int128>(campaign::k_sq_value)) {
        continue;
      }
      bridge_offsets.push_back(PointKey{da, db});
    }
  }

  const auto process_tile_bridge =
      [&](std::size_t t, LocalBridgeAccumulator& local) {
        const std::vector<campaign::Prime> primes =
            campaign::sieve_tile(coords[t], constants);
        std::vector<std::size_t> target_prime_indices;
        std::vector<std::vector<AtomId>> adjacent_ids_by_target;
        for (std::size_t p = 0; p < primes.size(); ++p) {
          const campaign::Prime& prime = primes[p];
          const Point prime_point{prime.a, prime.b, prime.norm_sq};
          std::vector<AtomId> adjacent_carry_ids;
          for (const PointKey& offset : bridge_offsets) {
            const auto carry_it = carry_id_by_point.find(
                PointKey{prime_point.a + offset.a,
                         prime_point.b + offset.b});
            if (carry_it == carry_id_by_point.end()) {
              continue;
            }
            adjacent_carry_ids.push_back(carry_it->second);
          }
          if (adjacent_carry_ids.empty()) {
            continue;
          }
          local.candidate_coord_ids.insert(adjacent_carry_ids.begin(),
                                           adjacent_carry_ids.end());
          target_prime_indices.push_back(p);
          adjacent_ids_by_target.push_back(std::move(adjacent_carry_ids));
        }
        if (target_prime_indices.empty()) {
          return;
        }
        const CoordinatePortBridgeBatchResult batch =
            bridge_coordinate_prime_batch_to_ports({
                .coord = coords[t],
                .constants = constants,
                .tileop = tileops[t],
                .primes = primes,
                .target_indices = target_prime_indices,
            });
        if (!batch.accepted()) {
          std::cerr << "TileOp coordinate bridge rejected: "
                    << batch.diagnostic << "\n";
          std::exit(EXIT_FAILURE);
        }
        if (batch.bridges.size() != adjacent_ids_by_target.size()) {
          std::cerr << "TileOp coordinate bridge batch size mismatch\n";
          std::exit(EXIT_FAILURE);
        }
        for (std::size_t b = 0; b < batch.bridges.size(); ++b) {
          const CoordinatePortBridgeResult& bridge = batch.bridges[b];
          const std::vector<AtomId>& adjacent_carry_ids =
              adjacent_ids_by_target[b];
          if (!bridge.accepted()) {
            local.bridge_rejected_coord_ids.insert(
                adjacent_carry_ids.begin(), adjacent_carry_ids.end());
            std::set<AtomId>& rejected_for_reason =
                local.rejected_coord_ids_by_reason[bridge.diagnostic];
            rejected_for_reason.insert(adjacent_carry_ids.begin(),
                                       adjacent_carry_ids.end());
            for (const AtomId coord_id : adjacent_carry_ids) {
              local.rejected_reasons_by_coord_id[coord_id].insert(
                  bridge.diagnostic);
            }
            continue;
          }
          for (const AtomId coord_id : adjacent_carry_ids) {
            std::set<AtomId>& ports = local.ports_by_coord_id[coord_id];
            ports.insert(bridge.port_atoms.begin(), bridge.port_atoms.end());
          }
          const campaign::Prime& target_prime =
              primes[target_prime_indices[b]];
          for (const CoordinatePortBridgeResult::PortExpansion& expansion :
               bridge.port_expansions) {
            if (expansion.path.empty() ||
                expansion.path.front().a != target_prime.a ||
                expansion.path.front().b != target_prime.b ||
                expansion.path.front().norm_sq != target_prime.norm_sq) {
              std::cerr << "TileOp coordinate bridge emitted mismatched local "
                           "expansion path\n";
              std::exit(EXIT_FAILURE);
            }
            for (const AtomId coord_id : adjacent_carry_ids) {
              const auto carry_it = carry_point_by_id.find(coord_id);
              if (carry_it == carry_point_by_id.end()) {
                std::cerr << "bridge candidate is missing carry point\n";
                std::exit(EXIT_FAILURE);
              }
              std::vector<CoordinateAtom> full_path;
              full_path.reserve(expansion.path.size() + 1);
              const CoordinateAtom carry_atom =
                  coordinate_atom_from_point(carry_it->second);
              if (carry_atom.a != expansion.path.front().a ||
                  carry_atom.b != expansion.path.front().b) {
                if (dist_sq(carry_it->second,
                            Point{expansion.path.front().a,
                                  expansion.path.front().b,
                                  expansion.path.front().norm_sq}) >
                    static_cast<std::uint64_t>(campaign::k_sq_value)) {
                  std::cerr << "carry-to-TileOp expansion first step exceeds "
                               "K_SQ\n";
                  std::exit(EXIT_FAILURE);
                }
                full_path.push_back(carry_atom);
              }
              full_path.insert(full_path.end(), expansion.path.begin(),
                               expansion.path.end());
              record_coordinate_port_path(local.coordinate_port_paths,
                                          coord_id, expansion.port_atom,
                                          std::move(full_path));
            }
          }
        }
      };

  const auto merge_local = [&](const LocalBridgeAccumulator& local) {
    for (const auto& [coord_id, ports] : local.ports_by_coord_id) {
      std::set<AtomId>& merged_ports = ports_by_coord_id[coord_id];
      merged_ports.insert(ports.begin(), ports.end());
    }
    candidate_coord_ids.insert(local.candidate_coord_ids.begin(),
                               local.candidate_coord_ids.end());
    bridge_rejected_coord_ids.insert(local.bridge_rejected_coord_ids.begin(),
                                     local.bridge_rejected_coord_ids.end());
    for (const auto& [reason, ids] : local.rejected_coord_ids_by_reason) {
      std::set<AtomId>& merged_ids = rejected_coord_ids_by_reason[reason];
      merged_ids.insert(ids.begin(), ids.end());
    }
    for (const auto& [coord_id, reasons] : local.rejected_reasons_by_coord_id) {
      std::set<std::string>& merged_reasons =
          rejected_reasons_by_coord_id[coord_id];
      merged_reasons.insert(reasons.begin(), reasons.end());
    }
    for (const auto& [key, path] : local.coordinate_port_paths) {
      record_coordinate_port_path(result.coordinate_port_paths, key.first,
                                  key.second, path);
    }
  };

  worker_threads = resolve_worker_threads(worker_threads, coords.size());
  if (worker_threads == 1) {
    LocalBridgeAccumulator local;
    for (std::size_t t = 0; t < coords.size(); ++t) {
      process_tile_bridge(t, local);
    }
    merge_local(local);
  } else {
    std::atomic<std::size_t> next_tile{0};
    std::vector<LocalBridgeAccumulator> locals(worker_threads);
    std::vector<std::exception_ptr> errors(worker_threads);
    std::vector<std::thread> workers;
    workers.reserve(worker_threads);
    for (std::size_t worker = 0; worker < worker_threads; ++worker) {
      workers.emplace_back([&, worker]() {
        try {
          while (true) {
            const std::size_t tile_index = next_tile.fetch_add(1);
            if (tile_index >= coords.size()) {
              break;
            }
            process_tile_bridge(tile_index, locals[worker]);
          }
        } catch (...) {
          errors[worker] = std::current_exception();
          next_tile.store(coords.size());
        }
      });
    }
    for (std::thread& worker : workers) {
      worker.join();
    }
    for (const std::exception_ptr& error : errors) {
      if (error) {
        std::rethrow_exception(error);
      }
    }
    for (const LocalBridgeAccumulator& local : locals) {
      merge_local(local);
    }
  }

  std::set<AtomId> bridged_ports;
  std::set<std::pair<AtomId, AtomId>> bridge_edges;
  for (const auto& [coord_id, carry_point] : carry_point_by_id) {
    (void)carry_point;
    const auto ports_it = ports_by_coord_id.find(coord_id);
    if (ports_it == ports_by_coord_id.end() || ports_it->second.empty()) {
      ++result.unbridged_coordinate_carry_atoms;
      if (source_carry_ids.find(coord_id) != source_carry_ids.end()) {
        ++result.source_unbridged_coordinate_carry_atoms;
      }
      continue;
    }
    ++result.bridged_coordinate_carry_atoms;
    if (source_carry_ids.find(coord_id) != source_carry_ids.end()) {
      ++result.source_bridged_coordinate_carry_atoms;
    }
    for (const AtomId port_id : ports_it->second) {
      const auto norm_it = port_norm_by_id.find(port_id);
      if (norm_it == port_norm_by_id.end()) {
        std::cerr << "bridged port atom is missing from TileOp port graph\n";
        std::exit(EXIT_FAILURE);
      }
      (void)norm_it;
      bridged_ports.insert(port_id);
      AtomId lhs = coord_id;
      AtomId rhs = port_id;
      if (rhs < lhs) {
        std::swap(lhs, rhs);
      }
      bridge_edges.insert({lhs, rhs});
    }
  }

  result.coordinate_carry_atoms_with_next_band_candidates =
      candidate_coord_ids.size();
  result.bridge_rejected_candidate_atoms = bridge_rejected_coord_ids.size();
  for (const auto& [reason, ids] : rejected_coord_ids_by_reason) {
    result.bridge_reject_reasons[reason] = ids.size();
    std::uint64_t source_count = 0;
    for (const AtomId id : ids) {
      if (source_carry_ids.find(id) != source_carry_ids.end()) {
        ++source_count;
      }
    }
    if (source_count != 0) {
      result.source_bridge_reject_reasons[reason] = source_count;
    }
  }
  for (const AtomId id : candidate_coord_ids) {
    if (source_carry_ids.find(id) != source_carry_ids.end()) {
      ++result.source_coordinate_carry_atoms_with_next_band_candidates;
    }
  }
  for (const AtomId id : bridge_rejected_coord_ids) {
    if (source_carry_ids.find(id) != source_carry_ids.end()) {
      ++result.source_bridge_rejected_candidate_atoms;
    }
  }
  for (const auto& [coord_id, carry_point] : carry_point_by_id) {
    (void)carry_point;
    const auto ports_it = ports_by_coord_id.find(coord_id);
    const bool bridged =
        ports_it != ports_by_coord_id.end() && !ports_it->second.empty();
    if (bridged) {
      continue;
    }
    if (candidate_coord_ids.find(coord_id) != candidate_coord_ids.end()) {
      ++result.unbridged_with_next_band_candidates;
      const auto reasons_it = rejected_reasons_by_coord_id.find(coord_id);
      const bool dead_end_candidate =
          reasons_it != rejected_reasons_by_coord_id.end() &&
          all_rejected_candidates_are_dead_end(reasons_it->second);
      if (dead_end_candidate) {
        ++result.unbridged_dead_end_candidate_atoms;
      } else {
        ++result.unbridged_unsafe_candidate_atoms;
      }
      if (source_carry_ids.find(coord_id) != source_carry_ids.end()) {
        ++result.source_unbridged_with_next_band_candidates;
        if (dead_end_candidate) {
          ++result.source_unbridged_dead_end_candidate_atoms;
        } else {
          ++result.source_unbridged_unsafe_candidate_atoms;
        }
      }
    } else {
      ++result.unbridged_without_next_band_candidates;
      if (source_carry_ids.find(coord_id) != source_carry_ids.end()) {
        ++result.source_unbridged_without_next_band_candidates;
      }
    }
  }

  graph_band.edges.insert(graph_band.edges.end(), bridge_edges.begin(),
                          bridge_edges.end());
  std::sort(graph_band.edges.begin(), graph_band.edges.end());
  graph_band.edges.erase(
      std::unique(graph_band.edges.begin(), graph_band.edges.end()),
      graph_band.edges.end());

  result.bridged_port_carry_atoms = bridged_ports.size();
  result.bridge_edges = bridge_edges.size();
  return result;
}

}  // namespace lb_source
