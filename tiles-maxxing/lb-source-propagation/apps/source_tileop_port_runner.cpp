#include "lb_source/source_propagation.h"
#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
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
  std::size_t max_atoms = 1000000;
  bool seed_inner_flags = false;
  bool require_full_bridge = false;
  std::vector<std::uint64_t> schedule_radii;
  std::optional<std::string> manifest_in;
  std::optional<std::string> prefix_witness_in;
  std::optional<std::string> manifest_out;
  std::optional<std::uint64_t> target_a;
  std::optional<std::uint64_t> target_b;
};

struct Point {
  std::int64_t a = 0;
  std::int64_t b = 0;
  std::uint64_t norm_sq = 0;
};

struct PrefixWitness {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::map<lb_source::AtomId, std::vector<Point>> path_by_target;
};

struct PortManifestBridgeResult {
  std::uint64_t bridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_coordinate_carry_atoms = 0;
  std::uint64_t bridged_port_carry_atoms = 0;
  std::uint64_t bridge_edges = 0;
};

struct TargetBridgeResult {
  bool seen = false;
  std::uint64_t port_atoms = 0;
  std::uint64_t bridge_edges = 0;
};

struct RunnerInventorySummary {
  lb_source::InventorySummary digest;
  std::uint64_t max_norm_sq = 0;
  std::vector<lb_source::AtomId> max_norm_atom_ids;
};

std::uint64_t norm_sq_i64(std::int64_t a, std::int64_t b);
lb_source::AtomId checked_coordinate_atom_id(std::int64_t a, std::int64_t b);
std::uint64_t dist_sq(const Point& lhs, const Point& rhs);

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

bool parse_schedule_radii(std::string_view text,
                          std::vector<std::uint64_t>& out) {
  out.clear();
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t comma = text.find(',', start);
    const std::size_t end =
        comma == std::string_view::npos ? text.size() : comma;
    if (end == start) {
      return false;
    }
    std::uint64_t value = 0;
    if (!parse_uint64(text.substr(start, end - start), value)) {
      return false;
    }
    out.push_back(value);
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return out.size() >= 2;
}

void usage(const char* prog) {
  std::cout
      << "Usage: " << prog << " [OPTIONS]\n"
      << "\n"
      << "Diagnostic TileOp-port source scheduler for the LB sidecar.\n"
      << "It builds CPU TileOps per radial band, converts TileOp ports to\n"
      << "canonical port atoms, and stitches bands through lb_source::process_band.\n"
      << "This is not a SOURCE_ORIGIN_K26 claim.\n"
      << "\n"
      << "Options:\n"
      << "  --r-start R           starting radius (default 248)\n"
      << "  --r-final R           final radius (default 512)\n"
      << "  --band-width W        radial band width (default 128)\n"
      << "  --schedule-radii CSV  explicit increasing radial boundaries;\n"
      << "                        first must equal --r-start and last --r-final\n"
      << "  --max-atoms N         hard atom cap for sidecar process_band\n"
      << "                        (default 1000000)\n"
      << "  --seed-inner-flags    seed first band from TileOp inner flags\n"
      << "                        (geo-I diagnostic only)\n"
      << "  --require-full-bridge\n"
      << "                        reject manifest handoff if any source coordinate\n"
      << "                        carry atom has no first-band TileOp port bridge\n"
      << "  --manifest-in PATH    read an origin-prefix coordinate carry\n"
      << "                        manifest at --r-start and bridge it to\n"
      << "                        canonical TileOp port atoms\n"
      << "  --prefix-witness-in PATH\n"
      << "                        read diagnostic origin-prefix paths for\n"
      << "                        incoming source carry atoms\n"
      << "  --target-a A --target-b B\n"
      << "                        add a canonical coordinate target atom and\n"
      << "                        bridge it to its TileOp port component when seen\n"
      << "  --manifest-out PATH   write final carry manifest when source survives\n";
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
    } else if (take_value("--schedule-radii", value)) {
      if (!parse_schedule_radii(value, config.schedule_radii)) {
        std::cerr << "invalid --schedule-radii: " << value << "\n";
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
    } else if (arg == "--seed-inner-flags") {
      config.seed_inner_flags = true;
    } else if (arg == "--require-full-bridge") {
      config.require_full_bridge = true;
    } else if (take_value("--manifest-in", value)) {
      if (value.empty()) {
        std::cerr << "--manifest-in must not be empty\n";
        return false;
      }
      config.manifest_in = value;
    } else if (take_value("--prefix-witness-in", value)) {
      if (value.empty()) {
        std::cerr << "--prefix-witness-in must not be empty\n";
        return false;
      }
      config.prefix_witness_in = value;
    } else if (take_value("--target-a", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) ||
          parsed > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
        std::cerr << "invalid --target-a: " << value << "\n";
        return false;
      }
      config.target_a = parsed;
    } else if (take_value("--target-b", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) ||
          parsed > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
        std::cerr << "invalid --target-b: " << value << "\n";
        return false;
      }
      config.target_b = parsed;
    } else if (take_value("--manifest-out", value)) {
      if (value.empty()) {
        std::cerr << "--manifest-out must not be empty\n";
        return false;
      }
      config.manifest_out = value;
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
  if (!config.schedule_radii.empty()) {
    if (config.schedule_radii.front() != config.r_start ||
        config.schedule_radii.back() != config.r_final) {
      std::cerr << "--schedule-radii must start at --r-start and end at "
                   "--r-final\n";
      return false;
    }
    for (std::size_t i = 1; i < config.schedule_radii.size(); ++i) {
      if (config.schedule_radii[i] <= config.schedule_radii[i - 1]) {
        std::cerr << "--schedule-radii must be strictly increasing\n";
        return false;
      }
      if (config.schedule_radii[i] - config.schedule_radii[i - 1] <
          lb_source::ceil_sqrt(campaign::k_sq_value)) {
        std::cerr << "--schedule-radii segment is thinner than "
                     "ceil_sqrt(K_SQ)\n";
        return false;
      }
    }
  }
  if (config.seed_inner_flags && config.manifest_in.has_value()) {
    std::cerr << "--seed-inner-flags cannot be combined with --manifest-in\n";
    return false;
  }
  if (config.prefix_witness_in.has_value() && !config.manifest_in.has_value()) {
    std::cerr << "--prefix-witness-in requires --manifest-in\n";
    return false;
  }
  if (config.target_a.has_value() != config.target_b.has_value()) {
    std::cerr << "--target-a and --target-b must be supplied together\n";
    return false;
  }
  if (config.target_a.has_value() && *config.target_b < *config.target_a) {
    std::cerr << "--target coordinates must be canonical with target-b >= "
                 "target-a\n";
    return false;
  }
  return true;
}

std::vector<std::uint64_t> build_schedule_radii(const Config& config) {
  if (!config.schedule_radii.empty()) {
    return config.schedule_radii;
  }
  std::vector<std::uint64_t> radii;
  for (std::uint64_t r = config.r_start; r < config.r_final;) {
    radii.push_back(r);
    r = std::min(config.r_final, r + config.band_width);
  }
  radii.push_back(config.r_final);
  return radii;
}

std::uint64_t min_schedule_width(const std::vector<std::uint64_t>& radii) {
  std::uint64_t out = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t i = 1; i < radii.size(); ++i) {
    out = std::min(out, radii[i] - radii[i - 1]);
  }
  return out == std::numeric_limits<std::uint64_t>::max() ? 0 : out;
}

std::uint64_t max_schedule_width(const std::vector<std::uint64_t>& radii) {
  std::uint64_t out = 0;
  for (std::size_t i = 1; i < radii.size(); ++i) {
    out = std::max(out, radii[i] - radii[i - 1]);
  }
  return out;
}

lb_source::CarryManifest read_manifest_or_die(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open --manifest-in path: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }
  lb_source::CarryManifestReadResult decoded =
      lb_source::read_carry_manifest(in);
  if (!decoded.accepted()) {
    std::cerr << "invalid --manifest-in: " << decoded.diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  }
  return decoded.manifest;
}

void fail_prefix_witness(const std::string& diagnostic) {
  std::cerr << "invalid --prefix-witness-in: " << diagnostic << "\n";
  std::exit(EXIT_FAILURE);
}

PrefixWitness read_prefix_witness_or_die(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open --prefix-witness-in path: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }

  PrefixWitness witness;
  std::string token;
  if (!(in >> token) || token != "LB_SOURCE_PREFIX_WITNESS_V1") {
    fail_prefix_witness("missing prefix witness header");
  }
  if (!(in >> token) || token != "k_sq" || !(in >> witness.k_sq)) {
    fail_prefix_witness("missing k_sq");
  }
  if (!(in >> token) || token != "outer_radius" ||
      !(in >> witness.outer_radius)) {
    fail_prefix_witness("missing outer_radius");
  }
  std::uint64_t witness_count = 0;
  if (!(in >> token) || token != "witness_count" ||
      !(in >> witness_count)) {
    fail_prefix_witness("missing witness_count");
  }

  for (std::uint64_t i = 0; i < witness_count; ++i) {
    lb_source::AtomId raw_id = 0;
    Point target;
    std::uint64_t path_count = 0;
    if (!(in >> token) || token != "witness" || !(in >> raw_id) ||
        !(in >> target.a) || !(in >> target.b) ||
        !(in >> target.norm_sq) || !(in >> path_count)) {
      fail_prefix_witness("malformed witness row");
    }
    if (target.a < 0 || target.b < 0 || target.b < target.a ||
        norm_sq_i64(target.a, target.b) != target.norm_sq) {
      fail_prefix_witness("invalid witness target coordinate");
    }
    const lb_source::AtomId target_id =
        checked_coordinate_atom_id(target.a, target.b);
    if (target_id != raw_id) {
      fail_prefix_witness("target atom id does not match coordinate");
    }
    if (path_count == 0) {
      fail_prefix_witness("empty source path");
    }

    std::vector<Point> path_points;
    path_points.reserve(static_cast<std::size_t>(path_count));
    for (std::uint64_t j = 0; j < path_count; ++j) {
      Point point;
      if (!(in >> token) || token != "point" || !(in >> point.a) ||
          !(in >> point.b) || !(in >> point.norm_sq)) {
        fail_prefix_witness("malformed path point");
      }
      if (point.a < 0 || point.b < 0 || point.b < point.a ||
          norm_sq_i64(point.a, point.b) != point.norm_sq) {
        fail_prefix_witness("invalid path coordinate");
      }
      (void)checked_coordinate_atom_id(point.a, point.b);
      path_points.push_back(point);
    }

    if (path_points.front().norm_sq > witness.k_sq) {
      fail_prefix_witness("path does not start from an origin source seed");
    }
    if (path_points.back().a != target.a || path_points.back().b != target.b ||
        path_points.back().norm_sq != target.norm_sq) {
      fail_prefix_witness("path does not end at its target");
    }
    for (std::size_t j = 1; j < path_points.size(); ++j) {
      if (dist_sq(path_points[j - 1], path_points[j]) > witness.k_sq) {
        fail_prefix_witness("path step exceeds k_sq");
      }
    }
    if (!witness.path_by_target.emplace(target_id, std::move(path_points))
             .second) {
      fail_prefix_witness("duplicate witness target");
    }
  }

  if (!(in >> token) || token != "END") {
    fail_prefix_witness("missing END marker");
  }
  if (in >> token) {
    fail_prefix_witness("unexpected trailing token");
  }
  return witness;
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

lb_source::AtomId checked_coordinate_atom_id(std::int64_t a, std::int64_t b) {
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

bool has_source_carry(const lb_source::SeparatorState& state) {
  return std::find(state.source_bit_per_component.begin(),
                   state.source_bit_per_component.end(),
                   true) != state.source_bit_per_component.end();
}

std::uint64_t source_carry_atoms(const lb_source::SeparatorState& state) {
  std::uint64_t count = 0;
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    if (state.source_bit_per_component[c]) {
      count += state.component_partition[c].size();
    }
  }
  return count;
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

RunnerInventorySummary summarize_runner_inventory(
    const std::vector<lb_source::AtomId>& inventory,
    const std::map<lb_source::AtomId, std::uint64_t>& norm_by_id) {
  RunnerInventorySummary summary;
  summary.digest = lb_source::summarize_inventory(inventory);

  std::vector<lb_source::AtomId> canonical = inventory;
  std::sort(canonical.begin(), canonical.end());
  canonical.erase(std::unique(canonical.begin(), canonical.end()),
                  canonical.end());
  for (const lb_source::AtomId id : canonical) {
    std::optional<std::uint64_t> norm;
    if (const std::optional<lb_source::CoordinateAtom> atom =
            lb_source::decode_coordinate_atom_id(id)) {
      norm = atom->norm_sq;
    } else if (const auto it = norm_by_id.find(id); it != norm_by_id.end()) {
      norm = it->second;
    }
    if (!norm.has_value()) {
      continue;
    }
    if (summary.max_norm_atom_ids.empty() || *norm > summary.max_norm_sq) {
      summary.max_norm_sq = *norm;
      summary.max_norm_atom_ids = {id};
    } else if (*norm == summary.max_norm_sq) {
      summary.max_norm_atom_ids.push_back(id);
    }
  }
  return summary;
}

void append_atom_id_array(std::ostream& out,
                          const std::vector<lb_source::AtomId>& ids) {
  out << '[';
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << ids[i];
  }
  out << ']';
}

std::vector<campaign::TileOp> build_tileops(
    const std::vector<campaign::TileCoord>& coords,
    const campaign::CampaignConstants& constants,
    const campaign::Grid& grid,
    std::uint64_t& overflow_tiles) {
  std::vector<campaign::TileOp> tileops;
  tileops.reserve(coords.size());
  for (const campaign::TileCoord& coord : coords) {
    campaign::TileOp op = campaign::process_tile(coord, constants, grid);
    if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
      ++overflow_tiles;
    }
    tileops.push_back(op);
  }
  return tileops;
}

PortManifestBridgeResult bridge_coordinate_manifest_to_ports(
    const lb_source::CarryManifest& manifest,
    const campaign::CampaignConstants& constants,
    const std::vector<campaign::TileCoord>& coords,
    const std::vector<campaign::TileOp>& tileops,
    lb_source::BandInput& graph_band) {
  PortManifestBridgeResult result;

  std::map<lb_source::AtomId, std::uint64_t> port_norm_by_id;
  for (const lb_source::BandAtom& atom : graph_band.atoms) {
    if (!port_norm_by_id.emplace(atom.id, atom.norm_sq).second) {
      std::cerr << "TileOp port graph emitted duplicate atom id\n";
      std::exit(EXIT_FAILURE);
    }
  }

  std::map<lb_source::AtomId, Point> carry_point_by_id;
  for (const lb_source::CarryAtom& atom : manifest.separator.carry_atoms) {
    const std::optional<lb_source::CoordinateAtom> decoded =
        lb_source::decode_coordinate_atom_id(atom.id);
    if (!decoded.has_value() || decoded->norm_sq != atom.norm_sq) {
      std::cerr << "manifest carry atom is not a stable coordinate atom\n";
      std::exit(EXIT_FAILURE);
    }
    carry_point_by_id.emplace(
        atom.id, Point{decoded->a, decoded->b, decoded->norm_sq});
  }

  std::map<lb_source::AtomId, std::set<lb_source::AtomId>> ports_by_coord_id;
  for (std::size_t t = 0; t < coords.size(); ++t) {
    const std::vector<campaign::Prime> primes =
        campaign::sieve_tile(coords[t], constants);
    for (const campaign::Prime& prime : primes) {
      const Point prime_point{prime.a, prime.b, prime.norm_sq};
      std::vector<lb_source::AtomId> adjacent_carry_ids;
      for (const auto& [coord_id, carry_point] : carry_point_by_id) {
        if (dist_sq(carry_point, prime_point) <=
            static_cast<std::uint64_t>(campaign::k_sq_value)) {
          adjacent_carry_ids.push_back(coord_id);
        }
      }
      if (adjacent_carry_ids.empty()) {
        continue;
      }
      const lb_source::CoordinatePortBridgeResult bridge =
          lb_source::bridge_coordinate_prime_to_ports({
              .coord = coords[t],
              .constants = constants,
              .tileop = tileops[t],
              .target = prime,
              .primes = primes,
          });
      if (!bridge.accepted()) {
        continue;
      }
      for (const lb_source::AtomId coord_id : adjacent_carry_ids) {
        std::set<lb_source::AtomId>& ports = ports_by_coord_id[coord_id];
        ports.insert(bridge.port_atoms.begin(), bridge.port_atoms.end());
      }
    }
  }

  std::set<lb_source::AtomId> bridged_ports;
  std::set<std::pair<lb_source::AtomId, lb_source::AtomId>> bridge_edges;
  for (const auto& [coord_id, carry_point] : carry_point_by_id) {
    (void)carry_point;
    const auto ports_it = ports_by_coord_id.find(coord_id);
    if (ports_it == ports_by_coord_id.end() || ports_it->second.empty()) {
      ++result.unbridged_coordinate_carry_atoms;
      continue;
    }
    ++result.bridged_coordinate_carry_atoms;
    for (const lb_source::AtomId port_id : ports_it->second) {
      const auto norm_it = port_norm_by_id.find(port_id);
      if (norm_it == port_norm_by_id.end()) {
        std::cerr << "bridged port atom is missing from TileOp port graph\n";
        std::exit(EXIT_FAILURE);
      }
      (void)norm_it;
      bridged_ports.insert(port_id);
      lb_source::AtomId lhs = coord_id;
      lb_source::AtomId rhs = port_id;
      if (rhs < lhs) {
        std::swap(lhs, rhs);
      }
      bridge_edges.insert({lhs, rhs});
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

TargetBridgeResult bridge_target_coordinate_to_ports(
    const Point& target,
    const campaign::CampaignConstants& constants,
    const std::vector<campaign::TileCoord>& coords,
    const std::vector<campaign::TileOp>& tileops,
    lb_source::BandInput& graph_band) {
  TargetBridgeResult result;
  const lb_source::AtomId target_id =
      checked_coordinate_atom_id(target.a, target.b);

  std::set<lb_source::AtomId> graph_atoms;
  for (const lb_source::BandAtom& atom : graph_band.atoms) {
    graph_atoms.insert(atom.id);
  }

  std::set<lb_source::AtomId> target_ports;
  for (std::size_t t = 0; t < coords.size(); ++t) {
    const std::vector<campaign::Prime> primes =
        campaign::sieve_tile(coords[t], constants);
    for (const campaign::Prime& prime : primes) {
      if (prime.a != target.a || prime.b != target.b ||
          prime.norm_sq != target.norm_sq) {
        continue;
      }
      result.seen = true;
      const lb_source::CoordinatePortBridgeResult bridge =
          lb_source::bridge_coordinate_prime_to_ports({
              .coord = coords[t],
              .constants = constants,
              .tileop = tileops[t],
              .target = prime,
              .primes = primes,
          });
      if (!bridge.accepted()) {
        std::cerr << "target TileOp-port bridge rejected: "
                  << bridge.diagnostic << "\n";
        std::exit(EXIT_FAILURE);
      }
      target_ports.insert(bridge.port_atoms.begin(), bridge.port_atoms.end());
    }
  }

  if (!result.seen) {
    return result;
  }
  if (graph_atoms.find(target_id) != graph_atoms.end()) {
    std::cerr << "target coordinate atom collides with TileOp port graph\n";
    std::exit(EXIT_FAILURE);
  }
  graph_band.atoms.push_back(
      {.id = target_id, .norm_sq = target.norm_sq, .certified_source = false});
  graph_atoms.insert(target_id);

  for (const lb_source::AtomId port_id : target_ports) {
    if (graph_atoms.find(port_id) == graph_atoms.end()) {
      std::cerr << "target bridge port is missing from TileOp port graph\n";
      std::exit(EXIT_FAILURE);
    }
    lb_source::AtomId lhs = target_id;
    lb_source::AtomId rhs = port_id;
    if (rhs < lhs) {
      std::swap(lhs, rhs);
    }
    graph_band.edges.push_back({lhs, rhs});
  }
  std::sort(graph_band.edges.begin(), graph_band.edges.end());
  graph_band.edges.erase(
      std::unique(graph_band.edges.begin(), graph_band.edges.end()),
      graph_band.edges.end());

  result.port_atoms = target_ports.size();
  result.bridge_edges = target_ports.size();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) {
    return EXIT_FAILURE;
  }

  std::optional<lb_source::CarryManifest> coordinate_manifest;
  std::optional<PrefixWitness> prefix_witness;
  std::optional<Point> target;
  std::optional<lb_source::AtomId> target_id;
  if (config.target_a.has_value()) {
    const std::int64_t a = static_cast<std::int64_t>(*config.target_a);
    const std::int64_t b = static_cast<std::int64_t>(*config.target_b);
    target = Point{a, b, norm_sq_i64(a, b)};
    target_id = checked_coordinate_atom_id(a, b);
  }
  std::string source_mode = config.seed_inner_flags ? "GEO_I_PORT_DIAGNOSTIC"
                                                    : "NONE";
  std::uint64_t manifest_source_carry_atoms = 0;
  std::uint64_t prefix_witness_targets = 0;
  std::uint64_t bridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_coordinate_carry_atoms = 0;
  std::uint64_t bridged_port_carry_atoms = 0;
  std::uint64_t bridge_edges = 0;
  bool target_seen = false;
  std::uint64_t target_port_atoms = 0;
  std::uint64_t target_bridge_edges = 0;
  if (config.manifest_in.has_value()) {
    coordinate_manifest = read_manifest_or_die(*config.manifest_in);
    if (coordinate_manifest->k_sq !=
        static_cast<std::uint64_t>(campaign::k_sq_value)) {
      std::cerr << "manifest k_sq does not match compiled K_SQ\n";
      return EXIT_FAILURE;
    }
    if (coordinate_manifest->outer_radius != config.r_start) {
      std::cerr << "manifest outer_radius must equal --r-start\n";
      return EXIT_FAILURE;
    }
    if (coordinate_manifest->carry_width !=
        lb_source::ceil_sqrt(coordinate_manifest->k_sq)) {
      std::cerr << "manifest carry_width mismatch\n";
      return EXIT_FAILURE;
    }
    source_mode = "ORIGIN_PREFIX_PORT_MANIFEST";
    for (std::size_t c = 0;
         c < coordinate_manifest->separator.component_partition.size(); ++c) {
      if (!coordinate_manifest->separator.source_bit_per_component[c]) {
        continue;
      }
      manifest_source_carry_atoms +=
          coordinate_manifest->separator.component_partition[c].size();
    }
    if (config.prefix_witness_in.has_value()) {
      prefix_witness = read_prefix_witness_or_die(*config.prefix_witness_in);
      if (prefix_witness->k_sq !=
          static_cast<std::uint64_t>(campaign::k_sq_value)) {
        std::cerr << "prefix witness k_sq does not match compiled K_SQ\n";
        return EXIT_FAILURE;
      }
      if (prefix_witness->outer_radius != config.r_start) {
        std::cerr << "prefix witness outer_radius must equal --r-start\n";
        return EXIT_FAILURE;
      }
      source_mode = "ORIGIN_PREFIX_PORT_WITNESS";
      for (std::size_t c = 0;
           c < coordinate_manifest->separator.component_partition.size(); ++c) {
        if (!coordinate_manifest->separator.source_bit_per_component[c]) {
          continue;
        }
        for (const lb_source::AtomId id :
             coordinate_manifest->separator.component_partition[c]) {
          if (prefix_witness->path_by_target.find(id) ==
              prefix_witness->path_by_target.end()) {
            std::cerr << "prefix witness is missing a source carry atom\n";
            return EXIT_FAILURE;
          }
          ++prefix_witness_targets;
        }
      }
      if (prefix_witness_targets != prefix_witness->path_by_target.size()) {
        std::cerr << "prefix witness contains non-source carry targets\n";
        return EXIT_FAILURE;
      }
    }
  }

  std::optional<lb_source::SeparatorState> incoming;
  lb_source::ProcessResult last;
  const std::vector<std::uint64_t> schedule_radii = build_schedule_radii(config);
  std::uint64_t bands_processed = 0;
  std::uint64_t campaign_tiles_processed = 0;
  std::uint64_t tileop_overflows = 0;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::map<lb_source::AtomId, std::uint64_t> norm_by_id;

  for (std::size_t segment = 0; segment + 1 < schedule_radii.size();
       ++segment) {
    const std::uint64_t previous_outer = schedule_radii[segment];
    const std::uint64_t outer = schedule_radii[segment + 1];
    campaign::CampaignConstants constants;
    campaign::Grid grid;
    try {
      constants = campaign::CampaignConstants::from_radii(
          previous_outer, outer, campaign::k_sq_value);
      grid = campaign::Grid::build(previous_outer, outer,
                                   campaign::k_sq_value);
    } catch (const std::exception& ex) {
      std::cerr << "campaign band construction failed: " << ex.what() << "\n";
      return EXIT_FAILURE;
    }
    const std::string invariant_error = grid.verify_invariants();
    if (!invariant_error.empty()) {
      std::cerr << "grid invariant failed: " << invariant_error << "\n";
      return EXIT_FAILURE;
    }

    const std::vector<campaign::TileCoord> coords =
        grid.enumerate_active_tiles();
    campaign_tiles_processed += coords.size();
    std::vector<campaign::TileOp> tileops =
        build_tileops(coords, constants, grid, tileop_overflows);

    const lb_source::TileOpPortGraphResult graph =
        lb_source::make_tileop_port_band({
            .k_sq = static_cast<std::uint64_t>(campaign::k_sq_value),
            .outer_radius = outer,
            .coords = coords,
            .tileops = tileops,
            .seed_inner_flags = config.seed_inner_flags &&
                                bands_processed == 0,
        });
    if (!graph.accepted()) {
      std::cerr << "TileOp port graph rejected: " << graph.diagnostic << "\n";
      return EXIT_FAILURE;
    }
    for (const lb_source::BandAtom& atom : graph.band.atoms) {
      norm_by_id.emplace(atom.id, atom.norm_sq);
    }
    lb_source::BandInput band = graph.band;
    if (target.has_value() && !target_seen) {
      const TargetBridgeResult target_bridge =
          bridge_target_coordinate_to_ports(*target, constants, coords, tileops,
                                            band);
      if (target_bridge.seen) {
        target_seen = true;
        target_port_atoms = target_bridge.port_atoms;
        target_bridge_edges = target_bridge.bridge_edges;
        norm_by_id.emplace(*target_id, target->norm_sq);
      }
    }
    if (coordinate_manifest.has_value() && bands_processed == 0) {
      lb_source::BandInput bridged_band = band;
      for (const lb_source::CarryAtom& atom :
           coordinate_manifest->separator.carry_atoms) {
        norm_by_id.emplace(atom.id, atom.norm_sq);
      }
      const PortManifestBridgeResult bridge =
          bridge_coordinate_manifest_to_ports(*coordinate_manifest, constants,
                                              coords, tileops, bridged_band);
      if (config.require_full_bridge &&
          bridge.unbridged_coordinate_carry_atoms != 0) {
        std::cerr
            << "strict seam bridge requires zero unbridged coordinate carry "
               "atoms, got "
            << bridge.unbridged_coordinate_carry_atoms << "\n";
        return EXIT_FAILURE;
      }
      incoming = coordinate_manifest->separator;
      last = lb_source::process_band(
          bridged_band, incoming,
          {.max_atoms = config.max_atoms,
           .max_carry_atoms = config.max_atoms,
           .max_components = config.max_atoms});
      bridged_coordinate_carry_atoms =
          bridge.bridged_coordinate_carry_atoms;
      unbridged_coordinate_carry_atoms =
          bridge.unbridged_coordinate_carry_atoms;
      bridged_port_carry_atoms = bridge.bridged_port_carry_atoms;
      bridge_edges = bridge.bridge_edges;
    } else {
      last = lb_source::process_band(
          band, incoming,
          {.max_atoms = config.max_atoms,
           .max_carry_atoms = config.max_atoms,
           .max_components = config.max_atoms});
    }
    port_atoms += graph.port_atoms;
    internal_edges += graph.internal_edges;
    seam_edges += graph.seam_edges;

    ++bands_processed;
    if (!last.accepted()) {
      break;
    }
    if (last.terminal_source_dead) {
      break;
    }
    incoming = last.outgoing;
  }

  bool manifest_written = false;
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
        manifest, lb_source::make_carry_manifest(
                      static_cast<std::uint64_t>(campaign::k_sq_value),
                      config.r_final, last));
    manifest_written = true;
  }

  const bool accepted = last.accepted();
  const bool source_carry =
      accepted && !last.terminal_source_dead && has_source_carry(last.outgoing);
  const std::vector<lb_source::AtomId> inventory =
      accepted ? source_inventory(last) : std::vector<lb_source::AtomId>{};
  const RunnerInventorySummary inventory_summary =
      summarize_runner_inventory(inventory, norm_by_id);
  const bool target_source_reached =
      target_id.has_value() &&
      std::binary_search(inventory.begin(), inventory.end(), *target_id);

  std::cout << "{"
            << "\"schema\":\"lb_source_tileop_port_runner_v1\","
            << "\"claim_label\":\"SOURCE_TILEOP_PORT_DIAGNOSTIC\","
            << "\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\","
            << "\"source_mode\":\"" << source_mode << "\","
            << "\"seam_bridge_policy\":\""
            << (config.require_full_bridge ? "require_full_bridge"
                                           : "diagnostic_allow_unbridged")
            << "\","
            << "\"k_sq\":" << campaign::k_sq_value
            << ",\"r_start\":" << config.r_start
            << ",\"r_final\":" << config.r_final
            << ",\"band_width\":" << config.band_width
            << ",\"schedule_mode\":\""
            << (config.schedule_radii.empty() ? "fixed_width"
                                               : "explicit_radii")
            << "\",\"schedule_boundary_count\":" << schedule_radii.size()
            << ",\"schedule_min_width\":"
            << min_schedule_width(schedule_radii)
            << ",\"schedule_max_width\":"
            << max_schedule_width(schedule_radii)
            << ",\"bands_processed\":" << bands_processed
            << ",\"campaign_tiles_processed\":" << campaign_tiles_processed
            << ",\"tileop_overflows\":" << tileop_overflows
            << ",\"port_atoms\":" << port_atoms
            << ",\"internal_edges\":" << internal_edges
            << ",\"seam_edges\":" << seam_edges
            << ",\"manifest_source_carry_atoms\":"
            << manifest_source_carry_atoms
            << ",\"prefix_witness_targets\":" << prefix_witness_targets
            << ",\"bridged_coordinate_carry_atoms\":"
            << bridged_coordinate_carry_atoms
            << ",\"unbridged_coordinate_carry_atoms\":"
            << unbridged_coordinate_carry_atoms
            << ",\"bridged_port_carry_atoms\":"
            << bridged_port_carry_atoms
            << ",\"bridge_edges\":" << bridge_edges
            << ",\"target\":{\"enabled\":"
            << (target.has_value() ? "true" : "false");
  if (target.has_value()) {
    std::cout << ",\"a\":" << target->a << ",\"b\":" << target->b
              << ",\"norm_sq\":" << target->norm_sq
              << ",\"seen\":" << (target_seen ? "true" : "false")
              << ",\"port_atoms\":" << target_port_atoms
              << ",\"bridge_edges\":" << target_bridge_edges
              << ",\"source_reached\":"
              << (target_source_reached ? "true" : "false")
              << ",\"path_provenance\":\"component_reachability_only\"";
  }
  std::cout << "}"
            << ",\"accepted\":" << (accepted ? "true" : "false")
            << ",\"reject\":\"" << lb_source::reject_reason_name(last.reject)
            << "\""
            << ",\"terminal_source_dead\":"
            << (accepted && last.terminal_source_dead ? "true" : "false")
            << ",\"has_source_carry\":"
            << (source_carry ? "true" : "false")
            << ",\"source_carry_atoms\":"
            << (source_carry ? source_carry_atoms(last.outgoing) : 0)
            << ",\"source_inventory_count\":"
            << inventory_summary.digest.count
            << ",\"source_inventory_digest_algorithm\":\""
            << inventory_summary.digest.digest_algorithm << "\""
            << ",\"source_inventory_digest_hex\":\""
            << inventory_summary.digest.digest_hex << "\""
            << ",\"max_source_norm_sq\":"
            << inventory_summary.max_norm_sq
            << ",\"max_source_norm_atom_ids\":";
  append_atom_id_array(std::cout, inventory_summary.max_norm_atom_ids);
  std::cout
            << ",\"manifest_written\":"
            << (manifest_written ? "true" : "false")
            << ",\"non_claim\":\"TileOp-port scheduler diagnostic; not SOURCE_ORIGIN_K26 or SOURCE_DEAD_CERT\""
            << "}\n";

  return accepted && tileop_overflows == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
