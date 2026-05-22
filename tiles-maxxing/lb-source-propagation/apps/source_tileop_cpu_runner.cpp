#include "lb_source/source_propagation.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/sieve.h"
#include "campaign/tileop.h"

namespace {

struct Config {
  std::uint64_t r_start = 248;
  std::uint64_t r_final = 512;
  std::uint64_t band_width = 128;
  std::int64_t seed_a = 0;
  std::int64_t seed_b = 251;
  std::int64_t endpoint_a = 0;
  std::int64_t endpoint_b = 251;
  std::size_t max_atoms = 65535;
};

struct Point {
  std::int64_t a = 0;
  std::int64_t b = 0;
  std::uint64_t norm_sq = 0;
};

bool parse_uint64(std::string_view text, std::uint64_t& out) {
  try {
    std::size_t pos = 0;
    const std::uint64_t value = std::stoull(std::string(text), &pos);
    if (pos != text.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_int64(std::string_view text, std::int64_t& out) {
  try {
    std::size_t pos = 0;
    const std::int64_t value = std::stoll(std::string(text), &pos);
    if (pos != text.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

void usage(const char* prog) {
  std::cout
      << "Usage: " << prog << " [OPTIONS]\n"
      << "\n"
      << "Diagnostic CPU TileOp-fed source runner for the LB sidecar.\n"
      << "Each radial band is produced through campaign::Grid,\n"
      << "campaign::process_tile, and campaign::sieve_tile before being\n"
      << "stitched by lb_source::process_band. It is not a production K26\n"
      << "certificate or a CUDA campaign runner.\n"
      << "\n"
      << "Options:\n"
      << "  --r-start R           starting radius (default 248)\n"
      << "  --r-final R           final radius (default 512)\n"
      << "  --band-width W        radial band width (default 128)\n"
      << "  --seed-a A            certified seed real coordinate (default 0)\n"
      << "  --seed-b B            certified seed imaginary coordinate (default 251)\n"
      << "  --endpoint-a A        endpoint real coordinate (default 0)\n"
      << "  --endpoint-b B        endpoint imaginary coordinate (default 251)\n"
      << "  --max-atoms N         hard atom cap for sidecar process_band\n"
      << "                        (default 65535)\n";
}

bool parse_args(int argc, char** argv, Config& config) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      std::exit(EXIT_SUCCESS);
    }

    auto take_value = [&](const std::string& flag,
                          std::string& value) -> bool {
      if (arg.rfind(flag + "=", 0) == 0) {
        value = arg.substr(flag.size() + 1);
        return true;
      }
      if (arg == flag) {
        if (i + 1 >= argc) {
          std::cerr << "missing value for " << flag << "\n";
          std::exit(EXIT_FAILURE);
        }
        value = argv[++i];
        return true;
      }
      return false;
    };

    std::string value;
    if (take_value("--r-start", value)) {
      if (!parse_uint64(value, config.r_start)) {
        std::cerr << "invalid --r-start: " << value << "\n";
        return false;
      }
    } else if (take_value("--r-final", value)) {
      if (!parse_uint64(value, config.r_final)) {
        std::cerr << "invalid --r-final: " << value << "\n";
        return false;
      }
    } else if (take_value("--band-width", value)) {
      if (!parse_uint64(value, config.band_width)) {
        std::cerr << "invalid --band-width: " << value << "\n";
        return false;
      }
    } else if (take_value("--seed-a", value)) {
      if (!parse_int64(value, config.seed_a)) {
        std::cerr << "invalid --seed-a: " << value << "\n";
        return false;
      }
    } else if (take_value("--seed-b", value)) {
      if (!parse_int64(value, config.seed_b)) {
        std::cerr << "invalid --seed-b: " << value << "\n";
        return false;
      }
    } else if (take_value("--endpoint-a", value)) {
      if (!parse_int64(value, config.endpoint_a)) {
        std::cerr << "invalid --endpoint-a: " << value << "\n";
        return false;
      }
    } else if (take_value("--endpoint-b", value)) {
      if (!parse_int64(value, config.endpoint_b)) {
        std::cerr << "invalid --endpoint-b: " << value << "\n";
        return false;
      }
    } else if (take_value("--max-atoms", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid --max-atoms: " << value << "\n";
        return false;
      }
      config.max_atoms = static_cast<std::size_t>(parsed);
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return false;
    }
  }

  if (config.r_start == 0 || config.r_final <= config.r_start ||
      config.band_width == 0) {
    std::cerr << "--r-start must be positive, --r-final must be greater, "
                 "and --band-width must be positive\n";
    return false;
  }
  if (config.band_width < lb_source::ceil_sqrt(campaign::k_sq_value)) {
    std::cerr << "--band-width must be at least ceil_sqrt(K_SQ)\n";
    return false;
  }
  if (config.seed_a < 0 || config.seed_b < 0 ||
      config.seed_b < config.seed_a) {
    std::cerr << "seed must be in canonical octant: 0 <= a <= b\n";
    return false;
  }
  if (config.endpoint_a < 0 || config.endpoint_b < 0 ||
      config.endpoint_b < config.endpoint_a) {
    std::cerr << "endpoint must be in canonical octant: 0 <= a <= b\n";
    return false;
  }
  return true;
}

std::uint64_t square_u64(std::uint64_t value) {
  const unsigned __int128 square =
      static_cast<unsigned __int128>(value) * value;
  if (square > std::numeric_limits<std::uint64_t>::max()) {
    std::cerr << "square overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return static_cast<std::uint64_t>(square);
}

std::uint64_t norm_sq_i64(std::int64_t a, std::int64_t b) {
  const unsigned __int128 norm =
      static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(a) +
      static_cast<unsigned __int128>(b) * static_cast<unsigned __int128>(b);
  if (norm > std::numeric_limits<std::uint64_t>::max()) {
    std::cerr << "norm overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return static_cast<std::uint64_t>(norm);
}

lb_source::AtomId checked_atom_id(std::int64_t a, std::int64_t b) {
  const std::optional<lb_source::AtomId> id =
      lb_source::coordinate_atom_id(a, b);
  if (!id.has_value()) {
    std::cerr << "coordinate atom id overflow or non-canonical coordinate\n";
    std::exit(EXIT_FAILURE);
  }
  return *id;
}

std::uint64_t dist_sq(const Point& lhs, const Point& rhs) {
  const __int128 da = static_cast<__int128>(lhs.a) - rhs.a;
  const __int128 db = static_cast<__int128>(lhs.b) - rhs.b;
  return static_cast<std::uint64_t>(da * da + db * db);
}

std::vector<Point> enumerate_tileop_band_points(
    std::uint64_t inner_radius,
    std::uint64_t outer_radius,
    std::uint64_t& tiles_processed,
    std::uint64_t& tileop_overflows) {
  const campaign::CampaignConstants constants =
      campaign::CampaignConstants::from_radii(inner_radius, outer_radius,
                                              campaign::k_sq_value);
  const campaign::Grid grid =
      campaign::Grid::build(inner_radius, outer_radius, campaign::k_sq_value);
  const std::string invariant_error = grid.verify_invariants();
  if (!invariant_error.empty()) {
    std::cerr << "grid invariant failed: " << invariant_error << "\n";
    std::exit(EXIT_FAILURE);
  }

  std::map<lb_source::AtomId, Point> unique_points;
  const std::uint64_t low_norm = square_u64(inner_radius);
  const std::uint64_t high_norm = square_u64(outer_radius);
  for (const campaign::TileCoord& coord : grid.enumerate_active_tiles()) {
    ++tiles_processed;
    const campaign::TileOp op = campaign::process_tile(coord, constants, grid);
    if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
      ++tileop_overflows;
      continue;
    }

    for (const campaign::Prime& prime : campaign::sieve_tile(coord, constants)) {
      if (prime.norm_sq <= low_norm || prime.norm_sq > high_norm) {
        continue;
      }
      const lb_source::AtomId id = checked_atom_id(prime.a, prime.b);
      unique_points.emplace(id, Point{prime.a, prime.b, prime.norm_sq});
    }
  }

  std::vector<Point> points;
  points.reserve(unique_points.size());
  for (const auto& [id, point] : unique_points) {
    (void)id;
    points.push_back(point);
  }
  std::sort(points.begin(), points.end(), [](const Point& lhs,
                                             const Point& rhs) {
    if (lhs.norm_sq != rhs.norm_sq) {
      return lhs.norm_sq < rhs.norm_sq;
    }
    if (lhs.a != rhs.a) {
      return lhs.a < rhs.a;
    }
    return lhs.b < rhs.b;
  });
  return points;
}

std::vector<lb_source::AtomId> source_inventory(
    const lb_source::ProcessResult& result) {
  if (result.terminal_source_dead) {
    return result.terminal_source_inventory;
  }
  std::vector<lb_source::AtomId> ids;
  for (std::size_t c = 0; c < result.outgoing.component_partition.size(); ++c) {
    if (!result.outgoing.source_bit_per_component[c]) {
      continue;
    }
    ids.insert(ids.end(), result.outgoing.component_inventory[c].begin(),
               result.outgoing.component_inventory[c].end());
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

void insert_adjacency_edge(
    std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>& adjacency,
    lb_source::AtomId lhs, lb_source::AtomId rhs) {
  adjacency[lhs].push_back(rhs);
  adjacency[rhs].push_back(lhs);
}

void canonicalize_adjacency(
    std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>& adjacency) {
  for (auto& [id, neighbors] : adjacency) {
    (void)id;
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                    neighbors.end());
  }
}

std::vector<lb_source::AtomId> source_path_to_endpoint(
    const std::vector<lb_source::AtomId>& source_seed_ids,
    lb_source::AtomId endpoint_id,
    const std::map<lb_source::AtomId, Point>& point_by_id,
    const std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>&
        adjacency) {
  if (point_by_id.find(endpoint_id) == point_by_id.end()) {
    return {};
  }

  std::vector<lb_source::AtomId> seeds = source_seed_ids;
  std::sort(seeds.begin(), seeds.end());
  seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());

  std::queue<lb_source::AtomId> pending;
  std::map<lb_source::AtomId, lb_source::AtomId> parent_by_id;
  for (const lb_source::AtomId seed_id : seeds) {
    if (point_by_id.find(seed_id) == point_by_id.end()) {
      continue;
    }
    if (!parent_by_id.emplace(seed_id, seed_id).second) {
      continue;
    }
    pending.push(seed_id);
  }

  while (!pending.empty()) {
    const lb_source::AtomId current = pending.front();
    pending.pop();
    if (current == endpoint_id) {
      break;
    }

    const auto neighbors = adjacency.find(current);
    if (neighbors == adjacency.end()) {
      continue;
    }
    for (const lb_source::AtomId neighbor : neighbors->second) {
      if (point_by_id.find(neighbor) == point_by_id.end()) {
        continue;
      }
      if (!parent_by_id.emplace(neighbor, current).second) {
        continue;
      }
      pending.push(neighbor);
    }
  }

  if (parent_by_id.find(endpoint_id) == parent_by_id.end()) {
    return {};
  }

  std::vector<lb_source::AtomId> reversed_path;
  lb_source::AtomId current = endpoint_id;
  while (true) {
    reversed_path.push_back(current);
    const lb_source::AtomId parent = parent_by_id.at(current);
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return {reversed_path.rbegin(), reversed_path.rend()};
}

void append_source_path_json(
    std::ostream& out,
    const std::vector<lb_source::AtomId>& source_path,
    const std::map<lb_source::AtomId, Point>& point_by_id) {
  out << '[';
  for (std::size_t i = 0; i < source_path.size(); ++i) {
    const Point& point = point_by_id.at(source_path[i]);
    if (i != 0) {
      out << ',';
    }
    out << "{\"a\":" << point.a << ",\"b\":" << point.b
        << ",\"norm_sq\":" << point.norm_sq << '}';
  }
  out << ']';
}

bool has_source_carry(const lb_source::SeparatorState& state) {
  return std::find(state.source_bit_per_component.begin(),
                   state.source_bit_per_component.end(),
                   true) != state.source_bit_per_component.end();
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) {
    return EXIT_FAILURE;
  }

  const lb_source::AtomId endpoint_id =
      checked_atom_id(config.endpoint_a, config.endpoint_b);
  const lb_source::AtomId seed_id = checked_atom_id(config.seed_a,
                                                   config.seed_b);
  const std::uint64_t endpoint_norm =
      norm_sq_i64(config.endpoint_a, config.endpoint_b);
  const std::uint64_t seed_norm = norm_sq_i64(config.seed_a, config.seed_b);

  std::map<lb_source::AtomId, Point> point_by_id;
  std::map<lb_source::AtomId, std::vector<lb_source::AtomId>> adjacency;
  std::vector<lb_source::AtomId> source_seed_ids;
  std::optional<lb_source::SeparatorState> incoming;
  lb_source::ProcessResult last;
  std::uint64_t previous_outer = config.r_start;
  std::uint64_t bands_processed = 0;
  std::uint64_t generated_atoms = 0;
  std::uint64_t campaign_tiles_processed = 0;
  std::uint64_t tileop_overflows = 0;
  bool endpoint_seen = false;
  bool endpoint_source_reached = false;

  while (previous_outer < config.r_final) {
    const std::uint64_t outer =
        std::min(config.r_final, previous_outer + config.band_width);
    const std::vector<Point> new_points = enumerate_tileop_band_points(
        previous_outer, outer, campaign_tiles_processed, tileop_overflows);

    lb_source::BandInput band;
    band.k_sq = campaign::k_sq_value;
    band.outer_radius = outer;
    band.atoms.reserve(new_points.size());
    std::vector<Point> edge_points = new_points;

    if (incoming) {
      for (const lb_source::CarryAtom& atom : incoming->carry_atoms) {
        const std::optional<lb_source::CoordinateAtom> decoded =
            lb_source::decode_coordinate_atom_id(atom.id);
        if (!decoded.has_value() || decoded->norm_sq != atom.norm_sq) {
          std::cerr << "incoming carry atom is not a stable coordinate atom\n";
          return EXIT_FAILURE;
        }
        edge_points.push_back({decoded->a, decoded->b, atom.norm_sq});
      }
    }

    for (const Point& point : new_points) {
      const lb_source::AtomId id = checked_atom_id(point.a, point.b);
      const bool is_certified_seed = id == seed_id;
      band.atoms.push_back({id, point.norm_sq, is_certified_seed});
      point_by_id.emplace(id, point);
      if (is_certified_seed) {
        source_seed_ids.push_back(id);
      }
      if (id == endpoint_id) {
        endpoint_seen = true;
      }
    }

    std::set<std::pair<lb_source::AtomId, lb_source::AtomId>> edges;
    for (std::size_t i = 0; i < edge_points.size(); ++i) {
      for (std::size_t j = i + 1; j < edge_points.size(); ++j) {
        if (dist_sq(edge_points[i], edge_points[j]) >
            static_cast<std::uint64_t>(campaign::k_sq_value)) {
          continue;
        }
        lb_source::AtomId lhs =
            checked_atom_id(edge_points[i].a, edge_points[i].b);
        lb_source::AtomId rhs =
            checked_atom_id(edge_points[j].a, edge_points[j].b);
        if (lhs > rhs) {
          std::swap(lhs, rhs);
        }
        edges.insert({lhs, rhs});
        insert_adjacency_edge(adjacency, lhs, rhs);
      }
    }
    band.edges.assign(edges.begin(), edges.end());

    last = lb_source::process_band(
        band, incoming,
        {.max_atoms = config.max_atoms,
         .max_carry_atoms = config.max_atoms,
         .max_components = config.max_atoms});
    ++bands_processed;
    generated_atoms += new_points.size();
    if (!last.accepted()) {
      break;
    }

    const std::vector<lb_source::AtomId> inventory = source_inventory(last);
    endpoint_source_reached =
        endpoint_source_reached ||
        std::find(inventory.begin(), inventory.end(), endpoint_id) !=
            inventory.end();
    if (last.terminal_source_dead) {
      break;
    }
    incoming = last.outgoing;
    previous_outer = outer;
  }
  canonicalize_adjacency(adjacency);

  const std::vector<lb_source::AtomId> inventory =
      last.accepted() ? source_inventory(last) : std::vector<lb_source::AtomId>{};
  const lb_source::InventorySummary inventory_summary =
      lb_source::summarize_inventory(inventory);
  const std::vector<lb_source::AtomId> source_path =
      endpoint_source_reached
          ? source_path_to_endpoint(source_seed_ids, endpoint_id, point_by_id,
                                    adjacency)
          : std::vector<lb_source::AtomId>{};

  std::cout << "{"
            << "\"schema\":\"lb_source_tileop_cpu_runner_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_TILEOP_CPU_DIAGNOSTIC\","
            << "\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\","
            << "\"domain\":\"cpu-tileop-fed-canonical-octant\","
            << "\"k_sq\":" << campaign::k_sq_value
            << ",\"source_mode\":\"CERTIFIED_SEED\""
            << ",\"seed\":{\"a\":" << config.seed_a
            << ",\"b\":" << config.seed_b
            << ",\"norm_sq\":" << seed_norm << "}"
            << ",\"r_start\":" << config.r_start
            << ",\"r_final\":" << config.r_final
            << ",\"band_width\":" << config.band_width
            << ",\"carry_width\":"
            << lb_source::ceil_sqrt(campaign::k_sq_value)
            << ",\"bands_processed\":" << bands_processed
            << ",\"campaign_tiles_processed\":" << campaign_tiles_processed
            << ",\"tileop_overflows\":" << tileop_overflows
            << ",\"generated_atoms\":" << generated_atoms
            << ",\"endpoint\":{\"a\":" << config.endpoint_a
            << ",\"b\":" << config.endpoint_b
            << ",\"norm_sq\":" << endpoint_norm
            << ",\"seen\":" << (endpoint_seen ? "true" : "false")
            << ",\"source_reached\":"
            << (endpoint_source_reached ? "true" : "false") << "}"
            << ",\"accepted\":" << (last.accepted() ? "true" : "false")
            << ",\"reject\":\"" << lb_source::reject_reason_name(last.reject)
            << "\""
            << ",\"terminal_source_dead\":"
            << (last.terminal_source_dead ? "true" : "false")
            << ",\"has_source_carry\":"
            << (last.accepted() && has_source_carry(last.outgoing) ? "true"
                                                                   : "false")
            << ",\"source_inventory_count\":" << inventory.size()
            << ",\"source_inventory_digest_algorithm\":\""
            << inventory_summary.digest_algorithm << "\""
            << ",\"source_inventory_digest_hex\":\""
            << inventory_summary.digest_hex << "\""
            << ",\"max_source_norm_sq\":" << inventory_summary.max_norm_sq
            << ",\"source_path_length\":" << source_path.size()
            << ",\"source_path\":";
  append_source_path_json(std::cout, source_path, point_by_id);
  std::cout << ",\"non_claim\":\"CPU TileOp-fed sidecar diagnostic; not a CUDA campaign or SOURCE_DEAD_CERT\""
            << "}\n";

  return last.accepted() && tileop_overflows == 0 ? EXIT_SUCCESS
                                                  : EXIT_FAILURE;
}
