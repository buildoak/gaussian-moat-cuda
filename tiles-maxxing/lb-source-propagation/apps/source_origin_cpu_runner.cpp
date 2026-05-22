#include "lb_source/source_propagation.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
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

lb_source::AtomId atom_id(std::int64_t a, std::int64_t b) {
  return static_cast<lb_source::AtomId>((static_cast<std::uint64_t>(a) << 32) |
                                       static_cast<std::uint64_t>(b));
}

std::pair<std::int64_t, std::int64_t> decode_atom_id(lb_source::AtomId id) {
  const auto raw = static_cast<std::uint64_t>(id);
  return {static_cast<std::int64_t>(raw >> 32),
          static_cast<std::int64_t>(raw & 0xffffffffULL)};
}

std::uint64_t dist_sq(const Point& lhs, const Point& rhs) {
  const __int128 da = static_cast<__int128>(lhs.a) - rhs.a;
  const __int128 db = static_cast<__int128>(lhs.b) - rhs.b;
  return static_cast<std::uint64_t>(da * da + db * db);
}

bool is_gaussian_prime_point(std::int64_t a, std::int64_t b,
                             std::uint64_t norm_sq) {
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
      atom_id(config.endpoint_a, config.endpoint_b);
  const std::uint64_t endpoint_norm =
      norm_sq_i64(config.endpoint_a, config.endpoint_b);

  std::unordered_map<lb_source::AtomId, std::uint64_t> norm_by_id;
  std::optional<lb_source::SeparatorState> incoming;
  lb_source::ProcessResult last;
  std::uint64_t previous_outer = 0;
  std::uint64_t bands_processed = 0;
  std::uint64_t generated_atoms = 0;
  bool endpoint_seen = false;
  bool endpoint_source_reached = false;

  while (previous_outer < config.r_final) {
    const std::uint64_t outer =
        std::min(config.r_final, previous_outer + config.band_width);
    const std::uint64_t low_norm = square_u64(previous_outer);
    const std::uint64_t high_norm = square_u64(outer);
    const std::vector<Point> new_points =
        enumerate_band_points(low_norm, high_norm, outer);

    lb_source::BandInput band;
    band.k_sq = config.k_sq;
    band.outer_radius = outer;
    band.atoms.reserve(new_points.size());
    std::vector<Point> edge_points = new_points;

    if (incoming) {
      for (const lb_source::CarryAtom& atom : incoming->carry_atoms) {
        const auto [a, b] = decode_atom_id(atom.id);
        edge_points.push_back({a, b, atom.norm_sq});
      }
    }

    for (const Point& point : new_points) {
      const lb_source::AtomId id = atom_id(point.a, point.b);
      const bool is_origin_seed = point.norm_sq <= config.k_sq;
      band.atoms.push_back({id, point.norm_sq, is_origin_seed});
      norm_by_id.emplace(id, point.norm_sq);
      if (id == endpoint_id) {
        endpoint_seen = true;
      }
    }

    std::set<std::pair<lb_source::AtomId, lb_source::AtomId>> edges;
    for (std::size_t i = 0; i < edge_points.size(); ++i) {
      for (std::size_t j = i + 1; j < edge_points.size(); ++j) {
        if (dist_sq(edge_points[i], edge_points[j]) > config.k_sq) {
          continue;
        }
        lb_source::AtomId lhs = atom_id(edge_points[i].a, edge_points[i].b);
        lb_source::AtomId rhs = atom_id(edge_points[j].a, edge_points[j].b);
        if (lhs > rhs) {
          std::swap(lhs, rhs);
        }
        edges.insert({lhs, rhs});
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

  const std::vector<lb_source::AtomId> inventory =
      last.accepted() ? source_inventory(last) : std::vector<lb_source::AtomId>{};
  std::uint64_t max_source_norm = 0;
  for (const lb_source::AtomId id : inventory) {
    const auto it = norm_by_id.find(id);
    if (it != norm_by_id.end()) {
      max_source_norm = std::max(max_source_norm, it->second);
    }
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
            << ",\"max_source_norm_sq\":" << max_source_norm
            << ",\"non_claim\":\"small coordinate-fed sidecar runner; not a TileOp/CUDA SOURCE_DEAD_CERT\""
            << "}\n";

  return last.accepted() ? EXIT_SUCCESS : EXIT_FAILURE;
}
