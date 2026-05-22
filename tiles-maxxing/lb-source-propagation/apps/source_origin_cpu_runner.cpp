#include "lb_source/source_propagation.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "campaign/sieve.h"

namespace {

struct Config {
  std::uint64_t k_sq = 26;
  std::uint64_t r_final = 64;
  std::uint64_t band_width = 16;
  std::int64_t endpoint_a = 0;
  std::int64_t endpoint_b = 3;
  std::size_t max_atoms = 65535;
  std::optional<std::string> cert_out;
  std::optional<std::string> manifest_out;
  std::optional<std::string> prefix_witness_out;
  std::optional<std::string> progress_out;
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
      << "Small-radius diagnostic source/origin runner for the LB sidecar.\n"
      << "It scans canonical-octant Gaussian-prime coordinates directly,\n"
      << "seeds Omega by norm_sq <= K, and processes radial bands through\n"
      << "lb_source::process_band. It is not a production K26 certificate.\n"
      << "\n"
      << "Options:\n"
      << "  --k-sq N              squared step bound (default 26)\n"
      << "  --r-final R           final radius for the diagnostic run (default 64)\n"
      << "  --band-width W        radial band width (default 16)\n"
      << "  --endpoint-a A        endpoint real coordinate (default 0)\n"
      << "  --endpoint-b B        endpoint imaginary coordinate (default 3)\n"
      << "  --max-atoms N         hard atom cap for sidecar process_band\n"
      << "                        (default 65535)\n"
      << "  --cert-out PATH       write lb_source_dead_cert_draft_v1 when the\n"
      << "                        endpoint is reached and source dies\n"
      << "  --manifest-out PATH   write carry manifest when source survives into\n"
      << "                        the final carry window\n"
      << "  --prefix-witness-out PATH\n"
      << "                        write diagnostic origin-prefix paths to live\n"
      << "                        source carry atoms\n"
      << "  --progress-out PATH   write one JSONL progress row per processed band\n";
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
    if (take_value("--k-sq", value)) {
      if (!parse_uint64(value, config.k_sq)) {
        std::cerr << "invalid --k-sq: " << value << "\n";
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
    } else if (take_value("--cert-out", value)) {
      if (value.empty()) {
        std::cerr << "--cert-out must not be empty\n";
        return false;
      }
      config.cert_out = value;
    } else if (take_value("--manifest-out", value)) {
      if (value.empty()) {
        std::cerr << "--manifest-out must not be empty\n";
        return false;
      }
      config.manifest_out = value;
    } else if (take_value("--prefix-witness-out", value)) {
      if (value.empty()) {
        std::cerr << "--prefix-witness-out must not be empty\n";
        return false;
      }
      config.prefix_witness_out = value;
    } else if (take_value("--progress-out", value)) {
      if (value.empty()) {
        std::cerr << "--progress-out must not be empty\n";
        return false;
      }
      config.progress_out = value;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return false;
    }
  }

  if (config.k_sq == 0 || config.r_final == 0 || config.band_width == 0) {
    std::cerr << "--k-sq, --r-final, and --band-width must be positive\n";
    return false;
  }
  if (config.band_width < lb_source::ceil_sqrt(config.k_sq)) {
    std::cerr << "--band-width must be at least ceil_sqrt(k_sq)\n";
    return false;
  }
  if (config.r_final >
      std::numeric_limits<std::uint64_t>::max() / config.r_final) {
    std::cerr << "--r-final square overflows u64\n";
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

std::vector<std::pair<lb_source::AtomId, lb_source::AtomId>>
build_local_edges(const std::vector<Point>& edge_points,
                  std::uint64_t k_sq) {
  std::unordered_set<lb_source::AtomId> atoms;
  atoms.reserve(edge_points.size() * 2);
  for (const Point& point : edge_points) {
    atoms.insert(checked_atom_id(point.a, point.b));
  }

  const std::int64_t radius =
      static_cast<std::int64_t>(lb_source::ceil_sqrt(k_sq));
  std::vector<std::pair<lb_source::AtomId, lb_source::AtomId>> edges;
  for (const Point& point : edge_points) {
    const lb_source::AtomId lhs = checked_atom_id(point.a, point.b);
    for (std::int64_t da = -radius; da <= radius; ++da) {
      const std::int64_t candidate_a = point.a + da;
      if (candidate_a < 0) {
        continue;
      }
      for (std::int64_t db = -radius; db <= radius; ++db) {
        const std::int64_t candidate_b = point.b + db;
        if (candidate_b < candidate_a) {
          continue;
        }
        const std::uint64_t distance_sq =
            static_cast<std::uint64_t>(da * da + db * db);
        if (distance_sq == 0 || distance_sq > k_sq) {
          continue;
        }
        const lb_source::AtomId rhs =
            checked_atom_id(candidate_a, candidate_b);
        if (lhs >= rhs || atoms.find(rhs) == atoms.end()) {
          continue;
        }
        edges.push_back({lhs, rhs});
      }
    }
  }
  std::sort(edges.begin(), edges.end());
  return edges;
}

std::uint64_t elapsed_ms(std::chrono::steady_clock::time_point begin,
                         std::chrono::steady_clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
          .count());
}

bool is_gaussian_prime_point(std::int64_t a, std::int64_t b,
                             std::uint64_t norm_sq) {
  (void)b;
  if (a == 0) {
    return campaign::is_gaussian_prime_norm(norm_sq);
  }
  return campaign::is_gaussian_prime_norm(norm_sq);
}

std::vector<Point> enumerate_band_points(std::uint64_t low_norm_exclusive,
                                         std::uint64_t high_norm_inclusive,
                                         std::uint64_t r_outer) {
  std::vector<Point> points;
  for (std::int64_t a = 0; a <= static_cast<std::int64_t>(r_outer); ++a) {
    for (std::int64_t b = a; b <= static_cast<std::int64_t>(r_outer); ++b) {
      const std::uint64_t norm_sq = norm_sq_i64(a, b);
      if (norm_sq <= low_norm_exclusive ||
          norm_sq > high_norm_inclusive) {
        continue;
      }
      if (is_gaussian_prime_point(a, b, norm_sq)) {
        points.push_back({a, b, norm_sq});
      }
    }
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

std::map<lb_source::AtomId, lb_source::AtomId> source_parent_tree(
    const std::vector<lb_source::AtomId>& source_seed_ids,
    const std::map<lb_source::AtomId, Point>& point_by_id,
    const std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>&
        adjacency) {
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

  return parent_by_id;
}

std::vector<lb_source::AtomId> source_path_from_parent_tree(
    lb_source::AtomId target_id,
    const std::map<lb_source::AtomId, lb_source::AtomId>& parent_by_id) {
  if (parent_by_id.find(target_id) == parent_by_id.end()) {
    return {};
  }

  std::vector<lb_source::AtomId> reversed_path;
  lb_source::AtomId current = target_id;
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

std::vector<lb_source::SourcePathPoint> make_source_path_points(
    const std::vector<lb_source::AtomId>& source_path,
    const std::map<lb_source::AtomId, Point>& point_by_id) {
  std::vector<lb_source::SourcePathPoint> points;
  points.reserve(source_path.size());
  for (const lb_source::AtomId id : source_path) {
    const Point& point = point_by_id.at(id);
    points.push_back({point.a, point.b, point.norm_sq});
  }
  return points;
}

bool has_source_carry(const lb_source::SeparatorState& state) {
  return std::find(state.source_bit_per_component.begin(),
                   state.source_bit_per_component.end(),
                   true) != state.source_bit_per_component.end();
}

std::vector<lb_source::AtomId> source_carry_ids(
    const lb_source::SeparatorState& state) {
  const lb_source::SeparatorState canonical =
      lb_source::canonicalize_separator(state);
  std::vector<lb_source::AtomId> ids;
  for (std::size_t c = 0; c < canonical.component_partition.size(); ++c) {
    if (!canonical.source_bit_per_component[c]) {
      continue;
    }
    ids.insert(ids.end(), canonical.component_partition[c].begin(),
               canonical.component_partition[c].end());
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

std::size_t write_prefix_witness_or_die(
    const std::string& path, std::uint64_t k_sq, std::uint64_t outer_radius,
    const lb_source::SeparatorState& state,
    const std::vector<lb_source::AtomId>& source_seed_ids,
    const std::map<lb_source::AtomId, Point>& point_by_id,
    const std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>&
        adjacency) {
  const std::vector<lb_source::AtomId> targets = source_carry_ids(state);
  if (targets.empty()) {
    std::cerr << "--prefix-witness-out requires at least one source carry atom\n";
    std::exit(EXIT_FAILURE);
  }

  std::ofstream witness(path);
  if (!witness) {
    std::cerr << "cannot open --prefix-witness-out path: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }

  witness << "LB_SOURCE_PREFIX_WITNESS_V1\n"
          << "k_sq " << k_sq << "\n"
          << "outer_radius " << outer_radius << "\n"
          << "witness_count " << targets.size() << "\n";

  const std::map<lb_source::AtomId, lb_source::AtomId> parent_by_id =
      source_parent_tree(source_seed_ids, point_by_id, adjacency);
  for (const lb_source::AtomId target : targets) {
    const auto point_it = point_by_id.find(target);
    if (point_it == point_by_id.end()) {
      std::cerr << "source carry target is missing from point map\n";
      std::exit(EXIT_FAILURE);
    }
    const std::vector<lb_source::AtomId> path_ids =
        source_path_from_parent_tree(target, parent_by_id);
    if (path_ids.empty()) {
      std::cerr << "source carry target lacks an origin-prefix witness path\n";
      std::exit(EXIT_FAILURE);
    }

    const Point& target_point = point_it->second;
    witness << "witness " << target << " " << target_point.a << " "
            << target_point.b << " " << target_point.norm_sq << " "
            << path_ids.size() << "\n";
    for (const lb_source::AtomId id : path_ids) {
      const Point& point = point_by_id.at(id);
      witness << "point " << point.a << " " << point.b << " "
              << point.norm_sq << "\n";
    }
  }

  witness << "END\n";
  return targets.size();
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) {
    return EXIT_FAILURE;
  }

  const lb_source::AtomId endpoint_id =
      checked_atom_id(config.endpoint_a, config.endpoint_b);
  const std::uint64_t endpoint_norm =
      norm_sq_i64(config.endpoint_a, config.endpoint_b);

  std::unordered_map<lb_source::AtomId, std::uint64_t> norm_by_id;
  std::map<lb_source::AtomId, Point> point_by_id;
  std::map<lb_source::AtomId, std::vector<lb_source::AtomId>> adjacency;
  std::vector<lb_source::AtomId> source_seed_ids;
  std::optional<lb_source::SeparatorState> incoming;
  lb_source::ProcessResult last;
  std::uint64_t previous_outer = 0;
  std::uint64_t bands_processed = 0;
  std::uint64_t generated_atoms = 0;
  bool endpoint_seen = false;
  bool endpoint_source_reached = false;
  std::ofstream progress;
  if (config.progress_out.has_value()) {
    progress.open(*config.progress_out);
    if (!progress) {
      std::cerr << "cannot open --progress-out path: " << *config.progress_out
                << "\n";
      return EXIT_FAILURE;
    }
  }

  while (previous_outer < config.r_final) {
    const auto band_begin = std::chrono::steady_clock::now();
    const std::uint64_t outer =
        std::min(config.r_final, previous_outer + config.band_width);
    const std::uint64_t low_norm = square_u64(previous_outer);
    const std::uint64_t high_norm = square_u64(outer);
    const std::vector<Point> new_points =
        enumerate_band_points(low_norm, high_norm, outer);
    const auto enumerate_done = std::chrono::steady_clock::now();

    lb_source::BandInput band;
    band.k_sq = config.k_sq;
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
      const bool is_origin_seed = point.norm_sq <= config.k_sq;
      band.atoms.push_back({id, point.norm_sq, is_origin_seed});
      norm_by_id.emplace(id, point.norm_sq);
      point_by_id.emplace(id, point);
      if (is_origin_seed) {
        source_seed_ids.push_back(id);
      }
      if (id == endpoint_id) {
        endpoint_seen = true;
      }
    }

    band.edges = build_local_edges(edge_points, config.k_sq);
    for (const auto& [lhs, rhs] : band.edges) {
      insert_adjacency_edge(adjacency, lhs, rhs);
    }
    const auto edges_done = std::chrono::steady_clock::now();

    last = lb_source::process_band(
        band, incoming,
        {.max_atoms = config.max_atoms,
         .max_carry_atoms = config.max_atoms,
         .max_components = config.max_atoms});
    const auto process_done = std::chrono::steady_clock::now();
    ++bands_processed;
    generated_atoms += new_points.size();
    if (!last.accepted()) {
      if (progress) {
        progress << "{\"schema\":\"lb_source_origin_progress_v1\""
                 << ",\"band_index\":" << (bands_processed - 1)
                 << ",\"r_start\":" << previous_outer
                 << ",\"r_outer\":" << outer
                 << ",\"new_atoms\":" << new_points.size()
                 << ",\"edge_points\":" << edge_points.size()
                 << ",\"edges\":" << band.edges.size()
                 << ",\"accepted\":false"
                 << ",\"reject\":\""
                 << lb_source::reject_reason_name(last.reject) << "\""
                 << ",\"terminal_source_dead\":false"
                 << ",\"has_source_carry\":false"
                 << ",\"source_inventory_count\":0"
                 << ",\"enumerate_ms\":"
                 << elapsed_ms(band_begin, enumerate_done)
                 << ",\"edges_ms\":"
                 << elapsed_ms(enumerate_done, edges_done)
                 << ",\"process_ms\":"
                 << elapsed_ms(edges_done, process_done)
                 << ",\"total_ms\":"
                 << elapsed_ms(band_begin, process_done) << "}\n";
        progress.flush();
      }
      break;
    }

    const std::vector<lb_source::AtomId> inventory = source_inventory(last);
    endpoint_source_reached =
        endpoint_source_reached ||
        std::find(inventory.begin(), inventory.end(), endpoint_id) !=
            inventory.end();
    const bool live_source_carry = has_source_carry(last.outgoing);
    const auto inventory_done = std::chrono::steady_clock::now();
    if (progress) {
      progress << "{\"schema\":\"lb_source_origin_progress_v1\""
               << ",\"band_index\":" << (bands_processed - 1)
               << ",\"r_start\":" << previous_outer
               << ",\"r_outer\":" << outer
               << ",\"new_atoms\":" << new_points.size()
               << ",\"edge_points\":" << edge_points.size()
               << ",\"edges\":" << band.edges.size()
               << ",\"accepted\":true"
               << ",\"reject\":\"none\""
               << ",\"terminal_source_dead\":"
               << (last.terminal_source_dead ? "true" : "false")
               << ",\"has_source_carry\":"
               << (live_source_carry ? "true" : "false")
               << ",\"source_inventory_count\":" << inventory.size()
               << ",\"enumerate_ms\":"
               << elapsed_ms(band_begin, enumerate_done)
               << ",\"edges_ms\":"
               << elapsed_ms(enumerate_done, edges_done)
               << ",\"process_ms\":"
               << elapsed_ms(edges_done, process_done)
               << ",\"inventory_ms\":"
               << elapsed_ms(process_done, inventory_done)
               << ",\"total_ms\":"
               << elapsed_ms(band_begin, inventory_done) << "}\n";
      progress.flush();
    }
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
  std::uint64_t max_source_norm = 0;
  for (const lb_source::AtomId id : inventory) {
    const auto it = norm_by_id.find(id);
    if (it != norm_by_id.end()) {
      max_source_norm = std::max(max_source_norm, it->second);
    }
  }
  const std::vector<lb_source::AtomId> source_path =
      endpoint_source_reached
          ? source_path_to_endpoint(source_seed_ids, endpoint_id, point_by_id,
                                    adjacency)
          : std::vector<lb_source::AtomId>{};
  bool manifest_written = false;
  bool prefix_witness_written = false;
  std::size_t prefix_witness_targets = 0;
  if (config.manifest_out.has_value()) {
    if (!last.accepted() || last.terminal_source_dead ||
        !has_source_carry(last.outgoing)) {
      std::cerr << "--manifest-out requires accepted live source carry\n";
      return EXIT_FAILURE;
    }
    std::ofstream manifest(*config.manifest_out);
    if (!manifest) {
      std::cerr << "cannot open --manifest-out path: "
                << *config.manifest_out << "\n";
      return EXIT_FAILURE;
    }
    lb_source::write_carry_manifest(
        manifest, lb_source::make_carry_manifest(config.k_sq, config.r_final,
                                                 last));
    manifest_written = true;
  }
  if (config.prefix_witness_out.has_value()) {
    if (!last.accepted() || last.terminal_source_dead ||
        !has_source_carry(last.outgoing)) {
      std::cerr << "--prefix-witness-out requires accepted live source carry\n";
      return EXIT_FAILURE;
    }
    prefix_witness_targets = write_prefix_witness_or_die(
        *config.prefix_witness_out, config.k_sq, config.r_final, last.outgoing,
        source_seed_ids, point_by_id, adjacency);
    prefix_witness_written = true;
  }
  bool cert_written = false;
  if (config.cert_out.has_value()) {
    if (!last.accepted() || !last.terminal_source_dead ||
        !endpoint_source_reached || source_path.empty() || inventory.empty()) {
      std::cerr << "--cert-out requires accepted terminal source death with "
                   "a reached endpoint, source path, and terminal inventory\n";
      return EXIT_FAILURE;
    }

    const lb_source::SourceDraftMetadata metadata{
        .source_mode = "ORIGIN_SOURCE",
        .source_id = "omega",
        .geometry_id = "canonical-octant-diagnostic",
        .commit_id = "local-diagnostic",
        .build_id = "sidecar-cmake",
        .bz_status = "BZ_CLEAN_DIAGNOSTIC",
        .artifact_hash = "coordinate-fed-diagnostic"};
    const lb_source::SourceCertificateDraft certificate{
        .certificate_id = "source-origin-diagnostic-dead-cert",
        .profile_id = "source-origin-diagnostic-profile",
        .metadata = metadata,
        .k_sq = config.k_sq,
        .terminal_radius = config.r_final,
        .negative_guard_pass = true,
        .endpoint = {config.endpoint_a, config.endpoint_b, endpoint_norm},
        .endpoint_atom_id = endpoint_id,
        .source_path = make_source_path_points(source_path, point_by_id),
        .terminal_source_inventory = inventory};

    std::ofstream cert(*config.cert_out);
    if (!cert) {
      std::cerr << "cannot open --cert-out path: " << *config.cert_out << "\n";
      return EXIT_FAILURE;
    }
    cert << lb_source::source_certificate_draft_json(certificate) << "\n";
    cert_written = true;
  }

  std::cout << "{"
            << "\"schema\":\"lb_source_origin_cpu_runner_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_DIAGNOSTIC\","
            << "\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\","
            << "\"domain\":\"canonical-octant-diagnostic\","
            << "\"k_sq\":" << config.k_sq
            << ",\"r_final\":" << config.r_final
            << ",\"band_width\":" << config.band_width
            << ",\"carry_width\":" << lb_source::ceil_sqrt(config.k_sq)
            << ",\"bands_processed\":" << bands_processed
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
            << ",\"max_source_norm_sq\":" << max_source_norm
            << ",\"source_path_length\":" << source_path.size()
            << ",\"source_path\":";
  append_source_path_json(std::cout, source_path, point_by_id);
  std::cout << ",\"cert_written\":" << (cert_written ? "true" : "false")
            << ",\"manifest_written\":"
            << (manifest_written ? "true" : "false")
            << ",\"prefix_witness_written\":"
            << (prefix_witness_written ? "true" : "false")
            << ",\"prefix_witness_targets\":" << prefix_witness_targets
            << ",\"non_claim\":\"small coordinate-fed sidecar runner; not a TileOp/CUDA SOURCE_DEAD_CERT\""
            << "}\n";

  return last.accepted() ? EXIT_SUCCESS : EXIT_FAILURE;
}
