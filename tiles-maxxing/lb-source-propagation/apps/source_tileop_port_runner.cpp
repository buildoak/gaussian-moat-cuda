#include "lb_source/source_propagation.h"
#include "lb_source/tileop_port_graph.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
  std::size_t stop_after_bands = 0;
  std::size_t max_atoms = 1000000;
  std::size_t tileop_threads = 0;
  bool seed_inner_flags = false;
  bool require_full_bridge = false;
  std::vector<std::uint64_t> schedule_radii;
  std::optional<std::string> manifest_in;
  std::optional<std::string> prefix_witness_in;
  std::optional<std::string> manifest_out;
  std::optional<std::string> progress_out;
  std::optional<std::uint64_t> target_a;
  std::optional<std::uint64_t> target_b;
};

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

struct PrefixWitness {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::set<lb_source::AtomId> target_ids;
};

struct PortManifestBridgeResult {
  std::uint64_t coordinate_carry_atoms_with_next_band_candidates = 0;
  std::uint64_t bridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_without_next_band_candidates = 0;
  std::uint64_t unbridged_with_next_band_candidates = 0;
  std::uint64_t unbridged_dead_end_candidate_atoms = 0;
  std::uint64_t unbridged_unsafe_candidate_atoms = 0;
  std::uint64_t bridge_rejected_candidate_atoms = 0;
  std::uint64_t source_coordinate_carry_atoms_with_next_band_candidates = 0;
  std::uint64_t source_bridged_coordinate_carry_atoms = 0;
  std::uint64_t source_unbridged_coordinate_carry_atoms = 0;
  std::uint64_t source_unbridged_without_next_band_candidates = 0;
  std::uint64_t source_unbridged_with_next_band_candidates = 0;
  std::uint64_t source_unbridged_dead_end_candidate_atoms = 0;
  std::uint64_t source_unbridged_unsafe_candidate_atoms = 0;
  std::uint64_t source_bridge_rejected_candidate_atoms = 0;
  std::map<std::string, std::uint64_t> bridge_reject_reasons;
  std::map<std::string, std::uint64_t> source_bridge_reject_reasons;
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

enum class ManifestKind {
  kNone,
  kCoordinateCarry,
  kPortCarry,
};

std::uint64_t norm_sq_i64(std::int64_t a, std::int64_t b);
lb_source::AtomId checked_coordinate_atom_id(std::int64_t a, std::int64_t b);
std::uint64_t dist_sq(const Point& lhs, const Point& rhs);
std::size_t resolve_tileop_threads(std::size_t requested,
                                   std::size_t work_items);
bool norm_in_radial_segment(std::uint64_t norm_sq,
                            std::uint64_t r_start,
                            std::uint64_t r_outer);

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
      << "  --stop-after-bands N  stop after N processed bands and allow\n"
      << "                        --manifest-out to checkpoint the separator\n"
      << "  --schedule-radii CSV  explicit increasing radial boundaries;\n"
      << "                        first must equal --r-start and last --r-final\n"
      << "  --max-atoms N         hard atom cap for sidecar process_band\n"
      << "                        also caps accumulated component inventory\n"
      << "                        (default 1000000)\n"
      << "  --tileop-threads N    worker threads for sidecar TileOp build;\n"
      << "                        0 means hardware auto (default 0)\n"
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
      << "  --manifest-out PATH   write final carry manifest when source survives\n"
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
    } else if (take_value("--stop-after-bands", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) || parsed == 0 ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid --stop-after-bands: " << value << "\n";
        return false;
      }
      config.stop_after_bands = static_cast<std::size_t>(parsed);
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
    } else if (take_value("--tileop-threads", value)) {
      std::uint64_t parsed = 0;
      if (!parse_uint64(value, parsed) ||
          parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "invalid --tileop-threads: " << value << "\n";
        return false;
      }
      config.tileop_threads = static_cast<std::size_t>(parsed);
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

ManifestKind classify_manifest_or_die(const lb_source::CarryManifest& manifest) {
  if (manifest.separator.carry_atoms.empty()) {
    std::cerr << "manifest carry atom set is empty\n";
    std::exit(EXIT_FAILURE);
  }

  bool all_coordinate = true;
  bool all_port = true;
  for (const lb_source::CarryAtom& atom : manifest.separator.carry_atoms) {
    const std::optional<lb_source::CoordinateAtom> coordinate =
        lb_source::decode_coordinate_atom_id(atom.id);
    if (!coordinate.has_value() || coordinate->norm_sq != atom.norm_sq) {
      all_coordinate = false;
    }
    if (!lb_source::decode_port_atom_id(atom.id).has_value()) {
      all_port = false;
    }
  }

  if (all_coordinate == all_port) {
    std::cerr << "manifest carry atoms must be all coordinate atoms or all "
                 "TileOp port atoms\n";
    std::exit(EXIT_FAILURE);
  }
  return all_coordinate ? ManifestKind::kCoordinateCarry
                        : ManifestKind::kPortCarry;
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

  const auto fail = [](const std::string& diagnostic) {
    std::cerr << "invalid --manifest-in: " << diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  };
  const auto expect = [&](std::string_view expected) {
    std::string token;
    if (!(in >> token) || token != expected) {
      fail("missing " + std::string(expected));
    }
  };

  lb_source::CarryManifest manifest;
  expect("LB_SOURCE_CARRY_MANIFEST_V1");
  expect("k_sq");
  if (!(in >> manifest.k_sq)) fail("missing or invalid k_sq");
  expect("outer_radius");
  if (!(in >> manifest.outer_radius)) {
    fail("missing or invalid outer_radius");
  }
  expect("carry_width");
  if (!(in >> manifest.carry_width)) {
    fail("missing or invalid carry_width");
  }

  std::size_t carry_count = 0;
  expect("carry_atoms");
  if (!(in >> carry_count)) fail("missing or invalid carry atom count");
  manifest.separator.carry_atoms.reserve(carry_count);
  std::set<lb_source::AtomId> carry_ids;
  for (std::size_t i = 0; i < carry_count; ++i) {
    std::string token;
    lb_source::CarryAtom atom;
    if (!(in >> token) || token != "carry_atom" || !(in >> atom.id) ||
        !(in >> atom.norm_sq)) {
      fail("missing or invalid carry atom");
    }
    if (!carry_ids.insert(atom.id).second) {
      fail("duplicate carry atom");
    }
    manifest.separator.carry_atoms.push_back(atom);
  }

  std::size_t component_count = 0;
  expect("components");
  if (!(in >> component_count)) fail("missing or invalid component count");
  manifest.separator.component_partition.reserve(component_count);
  manifest.separator.source_bit_per_component.reserve(component_count);
  manifest.separator.component_inventory.reserve(component_count);

  std::set<lb_source::AtomId> partition_ids;
  for (std::size_t c = 0; c < component_count; ++c) {
    std::string token;
    std::uint64_t source_bit = 0;
    std::size_t partition_count = 0;
    if (!(in >> token) || token != "component" || !(in >> source_bit) ||
        source_bit > 1 || !(in >> partition_count) ||
        partition_count == 0) {
      fail("missing or invalid component header");
    }
    std::vector<lb_source::AtomId> partition;
    partition.reserve(partition_count);
    for (std::size_t i = 0; i < partition_count; ++i) {
      lb_source::AtomId id = 0;
      if (!(in >> id)) fail("missing or invalid component atom");
      if (carry_ids.find(id) == carry_ids.end()) {
        fail("component references non-carry atom");
      }
      if (!partition_ids.insert(id).second) {
        fail("carry atom appears in multiple components");
      }
      partition.push_back(id);
    }

    std::size_t inventory_count = 0;
    if (!(in >> inventory_count) || inventory_count == 0) {
      fail("missing or invalid inventory count");
    }
    std::vector<lb_source::AtomId> inventory;
    inventory.reserve(inventory_count);
    for (std::size_t i = 0; i < inventory_count; ++i) {
      lb_source::AtomId id = 0;
      if (!(in >> id)) fail("missing or invalid inventory atom");
      inventory.push_back(id);
    }

    manifest.separator.source_bit_per_component.push_back(source_bit != 0);
    manifest.separator.component_partition.push_back(std::move(partition));
    manifest.separator.component_inventory.push_back(std::move(inventory));
  }

  if (partition_ids != carry_ids) {
    fail("component partition does not cover all carry atoms");
  }
  expect("END");
  std::string trailing;
  if (in >> trailing) fail("unexpected trailing manifest tokens");
  return manifest;
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

    Point first_point;
    Point last_point;
    Point previous_point;
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
      if (j == 0) {
        first_point = point;
      } else if (dist_sq(previous_point, point) > witness.k_sq) {
        fail_prefix_witness("path step exceeds k_sq");
      }
      previous_point = point;
      last_point = point;
    }

    if (first_point.norm_sq > witness.k_sq) {
      fail_prefix_witness("path does not start from an origin source seed");
    }
    if (last_point.a != target.a || last_point.b != target.b ||
        last_point.norm_sq != target.norm_sq) {
      fail_prefix_witness("path does not end at its target");
    }
    if (!witness.target_ids.insert(target_id).second) {
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

bool norm_in_radial_segment(std::uint64_t norm_sq,
                            std::uint64_t r_start,
                            std::uint64_t r_outer) {
  return static_cast<unsigned __int128>(norm_sq) >
             static_cast<unsigned __int128>(r_start) * r_start &&
         static_cast<unsigned __int128>(norm_sq) <=
             static_cast<unsigned __int128>(r_outer) * r_outer;
}

std::uint64_t elapsed_ms(std::chrono::steady_clock::time_point begin,
                         std::chrono::steady_clock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
          .count());
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

void append_terminal_inventory_accumulator(
    std::ostream& out, const RunnerInventorySummary& summary) {
  out << "{\"mode\":\"summary_digest_only_non_claim\""
      << ",\"provenance\":\"terminal_component_inventory_accumulator\""
      << ",\"listed_inventory_present\":false"
      << ",\"claim_grade_inventory_accepted\":false"
      << ",\"count\":" << summary.digest.count
      << ",\"digest_algorithm\":\"" << summary.digest.digest_algorithm
      << "\""
      << ",\"digest_hex\":\"" << summary.digest.digest_hex << "\""
      << ",\"max_norm_sq\":" << summary.max_norm_sq
      << ",\"max_norm_atom_ids\":";
  append_atom_id_array(out, summary.max_norm_atom_ids);
  out << '}';
}

void append_json_string(std::ostream& out, std::string_view value) {
  out << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u";
          const char* digits = "0123456789abcdef";
          out << '0' << '0' << digits[(ch >> 4) & 0xf]
              << digits[ch & 0xf];
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  out << '"';
}

void append_count_object(
    std::ostream& out,
    const std::map<std::string, std::uint64_t>& counts) {
  out << '{';
  bool first = true;
  for (const auto& [key, count] : counts) {
    if (!first) {
      out << ',';
    }
    first = false;
    append_json_string(out, key);
    out << ':' << count;
  }
  out << '}';
}

void emit_phase_progress(std::ofstream& progress,
                         std::string_view phase,
                         std::string_view event,
                         std::uint64_t band_index,
                         std::uint64_t r_start,
                         std::uint64_t r_outer,
                         std::uint64_t elapsed_ms_value) {
  if (!progress) {
    return;
  }
  progress << "{\"schema\":\"lb_source_tileop_port_phase_v1\""
           << ",\"phase\":";
  append_json_string(progress, phase);
  progress << ",\"event\":";
  append_json_string(progress, event);
  if (band_index != std::numeric_limits<std::uint64_t>::max()) {
    progress << ",\"band_index\":" << band_index
             << ",\"r_start\":" << r_start
             << ",\"r_outer\":" << r_outer;
  }
  if (elapsed_ms_value != std::numeric_limits<std::uint64_t>::max()) {
    progress << ",\"elapsed_ms\":" << elapsed_ms_value;
  }
  progress << "}\n";
  progress.flush();
}

void emit_tileop_build_begin(std::ofstream& progress,
                             std::uint64_t band_index,
                             std::uint64_t r_start,
                             std::uint64_t r_outer,
                             std::uint64_t tiles,
                             std::uint64_t worker_threads) {
  if (!progress) {
    return;
  }
  progress << "{\"schema\":\"lb_source_tileop_port_phase_v1\""
           << ",\"phase\":\"tileop_build\""
           << ",\"event\":\"begin\""
           << ",\"band_index\":" << band_index
           << ",\"r_start\":" << r_start
           << ",\"r_outer\":" << r_outer
           << ",\"tiles\":" << tiles
           << ",\"tileop_worker_threads\":" << worker_threads
           << "}\n";
  progress.flush();
}

bool all_rejected_candidates_are_dead_end(
    const std::set<std::string>& reasons) {
  return !reasons.empty() &&
         reasons.size() == 1 &&
         reasons.find("visible coordinate component has no encoded face ports") !=
             reasons.end();
}

void insert_adjacency_edge(
    std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>& adjacency,
    lb_source::AtomId lhs, lb_source::AtomId rhs) {
  if (lhs == rhs) {
    return;
  }
  adjacency[lhs].push_back(rhs);
  adjacency[rhs].push_back(lhs);
}

void add_partition_adjacency(
    std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>& adjacency,
    const lb_source::SeparatorState& state) {
  for (const std::vector<lb_source::AtomId>& component :
       state.component_partition) {
    if (component.empty()) {
      continue;
    }
    const lb_source::AtomId anchor = component.front();
    for (std::size_t i = 1; i < component.size(); ++i) {
      insert_adjacency_edge(adjacency, anchor, component[i]);
    }
  }
}

void add_band_adjacency(
    std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>& adjacency,
    const lb_source::BandInput& band) {
  for (const auto& edge : band.edges) {
    insert_adjacency_edge(adjacency, edge.first, edge.second);
  }
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

std::vector<lb_source::AtomId> atom_path_to_target(
    std::vector<lb_source::AtomId> source_ids, lb_source::AtomId target_id,
    const std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>&
        adjacency) {
  std::sort(source_ids.begin(), source_ids.end());
  source_ids.erase(std::unique(source_ids.begin(), source_ids.end()),
                   source_ids.end());

  std::queue<lb_source::AtomId> pending;
  std::map<lb_source::AtomId, lb_source::AtomId> parent_by_id;
  for (const lb_source::AtomId source_id : source_ids) {
    if (!parent_by_id.emplace(source_id, source_id).second) {
      continue;
    }
    pending.push(source_id);
  }

  while (!pending.empty()) {
    const lb_source::AtomId current = pending.front();
    pending.pop();
    if (current == target_id) {
      break;
    }
    const auto neighbors = adjacency.find(current);
    if (neighbors == adjacency.end()) {
      continue;
    }
    for (const lb_source::AtomId neighbor : neighbors->second) {
      if (!parent_by_id.emplace(neighbor, current).second) {
        continue;
      }
      pending.push(neighbor);
    }
  }

  if (parent_by_id.find(target_id) == parent_by_id.end()) {
    return {};
  }

  std::vector<lb_source::AtomId> reversed;
  lb_source::AtomId current = target_id;
  while (true) {
    reversed.push_back(current);
    const lb_source::AtomId parent = parent_by_id.at(current);
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return {reversed.rbegin(), reversed.rend()};
}

std::vector<campaign::TileOp> build_tileops(
    const std::vector<campaign::TileCoord>& coords,
    const campaign::CampaignConstants& constants,
    const campaign::Grid& grid,
    std::uint64_t& overflow_tiles,
    std::size_t worker_threads) {
  std::vector<campaign::TileOp> tileops;
  tileops.resize(coords.size());
  worker_threads = std::max<std::size_t>(1, worker_threads);
  if (coords.empty()) {
    return tileops;
  }
  worker_threads = std::min(worker_threads, coords.size());
  if (worker_threads == 1) {
    for (std::size_t i = 0; i < coords.size(); ++i) {
      campaign::TileOp op = campaign::process_tile(coords[i], constants, grid);
      if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
        ++overflow_tiles;
      }
      tileops[i] = op;
    }
    return tileops;
  }

  std::atomic<std::size_t> next_index{0};
  std::vector<std::uint64_t> overflow_by_worker(worker_threads, 0);
  std::vector<std::exception_ptr> errors(worker_threads);
  std::vector<std::thread> workers;
  workers.reserve(worker_threads);
  for (std::size_t worker = 0; worker < worker_threads; ++worker) {
    workers.emplace_back([&, worker]() {
      try {
        while (true) {
          const std::size_t index = next_index.fetch_add(1);
          if (index >= coords.size()) {
            break;
          }
          campaign::TileOp op =
              campaign::process_tile(coords[index], constants, grid);
          if ((op.tile_flags & campaign::OVERFLOW_BIT) != 0) {
            ++overflow_by_worker[worker];
          }
          tileops[index] = op;
        }
      } catch (...) {
        errors[worker] = std::current_exception();
        next_index.store(coords.size());
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
  for (const std::uint64_t count : overflow_by_worker) {
    overflow_tiles += count;
  }
  return tileops;
}

std::size_t resolve_tileop_threads(std::size_t requested,
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

PortManifestBridgeResult bridge_coordinate_manifest_to_ports(
    const lb_source::CarryManifest& manifest,
    const campaign::CampaignConstants& constants,
    const std::vector<campaign::TileCoord>& coords,
    const std::vector<campaign::TileOp>& tileops,
    lb_source::BandInput& graph_band,
    std::size_t worker_threads) {
  PortManifestBridgeResult result;

  std::unordered_map<lb_source::AtomId, std::uint64_t> port_norm_by_id;
  port_norm_by_id.reserve(graph_band.atoms.size());
  for (const lb_source::BandAtom& atom : graph_band.atoms) {
    if (!port_norm_by_id.emplace(atom.id, atom.norm_sq).second) {
      std::cerr << "TileOp port graph emitted duplicate atom id\n";
      std::exit(EXIT_FAILURE);
    }
  }

  std::map<lb_source::AtomId, Point> carry_point_by_id;
  std::unordered_map<PointKey, lb_source::AtomId, PointKeyHash>
      carry_id_by_point;
  carry_id_by_point.reserve(manifest.separator.carry_atoms.size());
  std::unordered_set<lb_source::AtomId> source_carry_ids;
  source_carry_ids.reserve(manifest.separator.carry_atoms.size());
  for (const lb_source::CarryAtom& atom : manifest.separator.carry_atoms) {
    const std::optional<lb_source::CoordinateAtom> decoded =
        lb_source::decode_coordinate_atom_id(atom.id);
    if (!decoded.has_value() || decoded->norm_sq != atom.norm_sq) {
      std::cerr << "manifest carry atom is not a stable coordinate atom\n";
      std::exit(EXIT_FAILURE);
    }
    const Point point{decoded->a, decoded->b, decoded->norm_sq};
    carry_point_by_id.emplace(atom.id, point);
    if (!carry_id_by_point
             .emplace(PointKey{point.a, point.b}, atom.id)
             .second) {
      std::cerr << "manifest emitted duplicate coordinate carry point\n";
      std::exit(EXIT_FAILURE);
    }
  }
  for (std::size_t c = 0; c < manifest.separator.component_partition.size();
       ++c) {
    if (!manifest.separator.source_bit_per_component[c]) {
      continue;
    }
    source_carry_ids.insert(manifest.separator.component_partition[c].begin(),
                            manifest.separator.component_partition[c].end());
  }

  std::map<lb_source::AtomId, std::set<lb_source::AtomId>> ports_by_coord_id;
  std::set<lb_source::AtomId> candidate_coord_ids;
  std::set<lb_source::AtomId> bridge_rejected_coord_ids;
  std::map<std::string, std::set<lb_source::AtomId>>
      rejected_coord_ids_by_reason;
  std::map<lb_source::AtomId, std::set<std::string>>
      rejected_reasons_by_coord_id;
  const std::int64_t bridge_radius = static_cast<std::int64_t>(
      lb_source::ceil_sqrt(static_cast<std::uint64_t>(campaign::k_sq_value)));
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

  struct LocalBridgeAccumulator {
    std::map<lb_source::AtomId, std::set<lb_source::AtomId>> ports_by_coord_id;
    std::set<lb_source::AtomId> candidate_coord_ids;
    std::set<lb_source::AtomId> bridge_rejected_coord_ids;
    std::map<std::string, std::set<lb_source::AtomId>>
        rejected_coord_ids_by_reason;
    std::map<lb_source::AtomId, std::set<std::string>>
        rejected_reasons_by_coord_id;
  };

  const auto process_tile_bridge =
      [&](std::size_t t, LocalBridgeAccumulator& local) {
    const std::vector<campaign::Prime> primes =
        campaign::sieve_tile(coords[t], constants);
    std::vector<std::size_t> target_prime_indices;
    std::vector<std::vector<lb_source::AtomId>> adjacent_ids_by_target;
    for (std::size_t p = 0; p < primes.size(); ++p) {
      const campaign::Prime& prime = primes[p];
      const Point prime_point{prime.a, prime.b, prime.norm_sq};
      std::vector<lb_source::AtomId> adjacent_carry_ids;
      for (const PointKey& offset : bridge_offsets) {
        const auto carry_it = carry_id_by_point.find(
            PointKey{prime_point.a + offset.a, prime_point.b + offset.b});
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
    const lb_source::CoordinatePortBridgeBatchResult batch =
        lb_source::bridge_coordinate_prime_batch_to_ports({
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
      const lb_source::CoordinatePortBridgeResult& bridge = batch.bridges[b];
      const std::vector<lb_source::AtomId>& adjacent_carry_ids =
          adjacent_ids_by_target[b];
      if (!bridge.accepted()) {
        local.bridge_rejected_coord_ids.insert(adjacent_carry_ids.begin(),
                                               adjacent_carry_ids.end());
        std::set<lb_source::AtomId>& rejected_for_reason =
            local.rejected_coord_ids_by_reason[bridge.diagnostic];
        rejected_for_reason.insert(adjacent_carry_ids.begin(),
                                   adjacent_carry_ids.end());
        for (const lb_source::AtomId coord_id : adjacent_carry_ids) {
          local.rejected_reasons_by_coord_id[coord_id].insert(
              bridge.diagnostic);
        }
        continue;
      }
      for (const lb_source::AtomId coord_id : adjacent_carry_ids) {
        std::set<lb_source::AtomId>& ports =
            local.ports_by_coord_id[coord_id];
        ports.insert(bridge.port_atoms.begin(), bridge.port_atoms.end());
      }
    }
  };

  const auto merge_local = [&](const LocalBridgeAccumulator& local) {
    for (const auto& [coord_id, ports] : local.ports_by_coord_id) {
      std::set<lb_source::AtomId>& merged_ports = ports_by_coord_id[coord_id];
      merged_ports.insert(ports.begin(), ports.end());
    }
    candidate_coord_ids.insert(local.candidate_coord_ids.begin(),
                               local.candidate_coord_ids.end());
    bridge_rejected_coord_ids.insert(local.bridge_rejected_coord_ids.begin(),
                                     local.bridge_rejected_coord_ids.end());
    for (const auto& [reason, ids] : local.rejected_coord_ids_by_reason) {
      std::set<lb_source::AtomId>& merged_ids =
          rejected_coord_ids_by_reason[reason];
      merged_ids.insert(ids.begin(), ids.end());
    }
    for (const auto& [coord_id, reasons] : local.rejected_reasons_by_coord_id) {
      std::set<std::string>& merged_reasons =
          rejected_reasons_by_coord_id[coord_id];
      merged_reasons.insert(reasons.begin(), reasons.end());
    }
  };

  worker_threads = resolve_tileop_threads(worker_threads, coords.size());
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

  std::set<lb_source::AtomId> bridged_ports;
  std::set<std::pair<lb_source::AtomId, lb_source::AtomId>> bridge_edges;
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

  result.coordinate_carry_atoms_with_next_band_candidates =
      candidate_coord_ids.size();
  result.bridge_rejected_candidate_atoms = bridge_rejected_coord_ids.size();
  for (const auto& [reason, ids] : rejected_coord_ids_by_reason) {
    result.bridge_reject_reasons[reason] = ids.size();
    std::uint64_t source_count = 0;
    for (const lb_source::AtomId id : ids) {
      if (source_carry_ids.find(id) != source_carry_ids.end()) {
        ++source_count;
      }
    }
    if (source_count != 0) {
      result.source_bridge_reject_reasons[reason] = source_count;
    }
  }
  for (const lb_source::AtomId id : candidate_coord_ids) {
    if (source_carry_ids.find(id) != source_carry_ids.end()) {
      ++result.source_coordinate_carry_atoms_with_next_band_candidates;
    }
  }
  for (const lb_source::AtomId id : bridge_rejected_coord_ids) {
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

  std::ofstream progress;
  if (config.progress_out.has_value()) {
    progress.open(*config.progress_out);
    if (!progress) {
      std::cerr << "cannot open --progress-out path: " << *config.progress_out
                << "\n";
      return EXIT_FAILURE;
    }
  }

  std::optional<lb_source::CarryManifest> coordinate_manifest;
  ManifestKind manifest_kind = ManifestKind::kNone;
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
  std::uint64_t coordinate_carry_atoms_with_next_band_candidates = 0;
  std::uint64_t bridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_coordinate_carry_atoms = 0;
  std::uint64_t unbridged_without_next_band_candidates = 0;
  std::uint64_t unbridged_with_next_band_candidates = 0;
  std::uint64_t unbridged_dead_end_candidate_atoms = 0;
  std::uint64_t unbridged_unsafe_candidate_atoms = 0;
  std::uint64_t bridge_rejected_candidate_atoms = 0;
  std::uint64_t source_coordinate_carry_atoms_with_next_band_candidates = 0;
  std::uint64_t source_bridged_coordinate_carry_atoms = 0;
  std::uint64_t source_unbridged_coordinate_carry_atoms = 0;
  std::uint64_t source_unbridged_without_next_band_candidates = 0;
  std::uint64_t source_unbridged_with_next_band_candidates = 0;
  std::uint64_t source_unbridged_dead_end_candidate_atoms = 0;
  std::uint64_t source_unbridged_unsafe_candidate_atoms = 0;
  std::uint64_t source_bridge_rejected_candidate_atoms = 0;
  std::map<std::string, std::uint64_t> bridge_reject_reasons;
  std::map<std::string, std::uint64_t> source_bridge_reject_reasons;
  std::uint64_t bridged_port_carry_atoms = 0;
  std::uint64_t bridge_edges = 0;
  bool target_seen = false;
  std::uint64_t target_port_atoms = 0;
  std::uint64_t target_bridge_edges = 0;
  std::vector<lb_source::AtomId> provenance_source_ids;
  std::map<lb_source::AtomId, std::vector<lb_source::AtomId>>
      provenance_adjacency;
  if (config.manifest_in.has_value()) {
    const auto manifest_read_begin = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "manifest_read", "begin",
                        std::numeric_limits<std::uint64_t>::max(), 0, 0,
                        std::numeric_limits<std::uint64_t>::max());
    coordinate_manifest = read_manifest_or_die(*config.manifest_in);
    manifest_kind = classify_manifest_or_die(*coordinate_manifest);
    emit_phase_progress(progress, "manifest_read", "end",
                        std::numeric_limits<std::uint64_t>::max(), 0, 0,
                        elapsed_ms(manifest_read_begin,
                                   std::chrono::steady_clock::now()));
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
    source_mode = manifest_kind == ManifestKind::kCoordinateCarry
                      ? "ORIGIN_PREFIX_PORT_MANIFEST"
                      : "PORT_CARRY_MANIFEST";
    for (std::size_t c = 0;
         c < coordinate_manifest->separator.component_partition.size(); ++c) {
      if (!coordinate_manifest->separator.source_bit_per_component[c]) {
        continue;
      }
      manifest_source_carry_atoms +=
          coordinate_manifest->separator.component_partition[c].size();
      provenance_source_ids.insert(
          provenance_source_ids.end(),
          coordinate_manifest->separator.component_partition[c].begin(),
          coordinate_manifest->separator.component_partition[c].end());
    }
    if (config.prefix_witness_in.has_value()) {
      if (manifest_kind != ManifestKind::kCoordinateCarry) {
        std::cerr << "--prefix-witness-in requires a coordinate carry "
                     "manifest\n";
        return EXIT_FAILURE;
      }
      const auto prefix_witness_read_begin = std::chrono::steady_clock::now();
      emit_phase_progress(progress, "prefix_witness_read", "begin",
                          std::numeric_limits<std::uint64_t>::max(), 0, 0,
                          std::numeric_limits<std::uint64_t>::max());
      prefix_witness = read_prefix_witness_or_die(*config.prefix_witness_in);
      emit_phase_progress(progress, "prefix_witness_read", "end",
                          std::numeric_limits<std::uint64_t>::max(), 0, 0,
                          elapsed_ms(prefix_witness_read_begin,
                                     std::chrono::steady_clock::now()));
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
          if (prefix_witness->target_ids.find(id) ==
              prefix_witness->target_ids.end()) {
            std::cerr << "prefix witness is missing a source carry atom\n";
            return EXIT_FAILURE;
          }
          ++prefix_witness_targets;
        }
      }
      if (prefix_witness_targets != prefix_witness->target_ids.size()) {
        std::cerr << "prefix witness contains non-source carry targets\n";
        return EXIT_FAILURE;
      }
    }
    if (config.require_full_bridge &&
        manifest_kind != ManifestKind::kCoordinateCarry) {
      std::cerr << "--require-full-bridge requires a coordinate carry "
                   "manifest\n";
      return EXIT_FAILURE;
    }
  }

  std::optional<lb_source::SeparatorState> incoming;
  lb_source::ProcessResult last;
  const std::vector<std::uint64_t> schedule_radii = build_schedule_radii(config);
  std::uint64_t bands_processed = 0;
  std::uint64_t campaign_tiles_processed = 0;
  std::uint64_t tileop_overflows = 0;
  std::uint64_t max_tileop_worker_threads = 0;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::map<lb_source::AtomId, std::uint64_t> norm_by_id;
  std::uint64_t processed_outer = config.r_start;
  if (coordinate_manifest.has_value() &&
      manifest_kind == ManifestKind::kPortCarry) {
    incoming = coordinate_manifest->separator;
    add_partition_adjacency(provenance_adjacency,
                            coordinate_manifest->separator);
    for (const lb_source::CarryAtom& atom :
         coordinate_manifest->separator.carry_atoms) {
      norm_by_id.emplace(atom.id, atom.norm_sq);
    }
  }

  const std::size_t total_segments =
      schedule_radii.empty() ? 0 : schedule_radii.size() - 1;
  if (config.stop_after_bands != 0 &&
      config.stop_after_bands > total_segments) {
    std::cerr << "--stop-after-bands exceeds scheduled band count\n";
    return EXIT_FAILURE;
  }

  for (std::size_t segment = 0; segment + 1 < schedule_radii.size();
       ++segment) {
    const auto band_begin = std::chrono::steady_clock::now();
    const std::uint64_t previous_outer = schedule_radii[segment];
    const std::uint64_t outer = schedule_radii[segment + 1];
    emit_phase_progress(progress, "band", "begin", segment, previous_outer,
                        outer, std::numeric_limits<std::uint64_t>::max());
    campaign::CampaignConstants constants;
    campaign::Grid grid;
    const auto grid_begin = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "grid_build", "begin", segment,
                        previous_outer, outer,
                        std::numeric_limits<std::uint64_t>::max());
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
    const auto grid_done = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "grid_build", "end", segment,
                        previous_outer, outer,
                        elapsed_ms(grid_begin, grid_done));

    emit_phase_progress(progress, "active_tile_enumerate", "begin", segment,
                        previous_outer, outer,
                        std::numeric_limits<std::uint64_t>::max());
    const std::vector<campaign::TileCoord> coords =
        grid.enumerate_active_tiles();
    campaign_tiles_processed += coords.size();
    const auto enumerate_done = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "active_tile_enumerate", "end", segment,
                        previous_outer, outer,
                        elapsed_ms(grid_done, enumerate_done));
    const std::size_t tileop_threads =
        resolve_tileop_threads(config.tileop_threads, coords.size());
    max_tileop_worker_threads =
        std::max<std::uint64_t>(max_tileop_worker_threads, tileop_threads);
    emit_tileop_build_begin(progress, segment, previous_outer, outer,
                            coords.size(), tileop_threads);
    std::vector<campaign::TileOp> tileops =
        build_tileops(coords, constants, grid, tileop_overflows,
                      tileop_threads);
    const auto tileop_done = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "tileop_build", "end", segment,
                        previous_outer, outer,
                        elapsed_ms(enumerate_done, tileop_done));

    emit_phase_progress(progress, "port_graph", "begin", segment,
                        previous_outer, outer,
                        std::numeric_limits<std::uint64_t>::max());
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
    const auto graph_done = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "port_graph", "end", segment,
                        previous_outer, outer,
                        elapsed_ms(tileop_done, graph_done));
    for (const lb_source::BandAtom& atom : graph.band.atoms) {
      norm_by_id.emplace(atom.id, atom.norm_sq);
    }
    lb_source::BandInput band = graph.band;
    bool segment_target_seen = false;
    std::uint64_t segment_target_port_atoms = 0;
    std::uint64_t segment_target_bridge_edges = 0;
    emit_phase_progress(progress, "target_bridge", "begin", segment,
                        previous_outer, outer,
                        std::numeric_limits<std::uint64_t>::max());
    if (target.has_value() && !target_seen &&
        norm_in_radial_segment(target->norm_sq, previous_outer, outer)) {
      const TargetBridgeResult target_bridge =
          bridge_target_coordinate_to_ports(*target, constants, coords, tileops,
                                            band);
      if (target_bridge.seen) {
        segment_target_seen = true;
        segment_target_port_atoms = target_bridge.port_atoms;
        segment_target_bridge_edges = target_bridge.bridge_edges;
        target_seen = true;
        target_port_atoms = target_bridge.port_atoms;
        target_bridge_edges = target_bridge.bridge_edges;
        norm_by_id.emplace(*target_id, target->norm_sq);
      }
    }
    const auto target_bridge_done = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "target_bridge", "end", segment,
                        previous_outer, outer,
                        elapsed_ms(graph_done, target_bridge_done));
    auto bridge_done = target_bridge_done;
    PortManifestBridgeResult segment_bridge;
    if (coordinate_manifest.has_value() &&
        manifest_kind == ManifestKind::kCoordinateCarry &&
        bands_processed == 0) {
      lb_source::BandInput bridged_band = band;
      for (const lb_source::CarryAtom& atom :
           coordinate_manifest->separator.carry_atoms) {
        norm_by_id.emplace(atom.id, atom.norm_sq);
      }
      add_partition_adjacency(provenance_adjacency,
                              coordinate_manifest->separator);
      emit_phase_progress(progress, "manifest_bridge", "begin", segment,
                          previous_outer, outer,
                          std::numeric_limits<std::uint64_t>::max());
      const PortManifestBridgeResult bridge =
          bridge_coordinate_manifest_to_ports(*coordinate_manifest, constants,
                                              coords, tileops, bridged_band,
                                              tileop_threads);
      bridge_done = std::chrono::steady_clock::now();
      emit_phase_progress(progress, "manifest_bridge", "end", segment,
                          previous_outer, outer,
                          elapsed_ms(target_bridge_done, bridge_done));
      segment_bridge = bridge;
      if (config.require_full_bridge &&
          bridge.unbridged_coordinate_carry_atoms != 0) {
        std::cerr
            << "strict seam bridge requires zero unbridged coordinate carry "
               "atoms, got "
            << bridge.unbridged_coordinate_carry_atoms << "\n";
        return EXIT_FAILURE;
      }
      incoming = coordinate_manifest->separator;
      emit_phase_progress(progress, "source_process", "begin", segment,
                          previous_outer, outer,
                          std::numeric_limits<std::uint64_t>::max());
      last = lb_source::process_band(
          bridged_band, incoming,
          {.max_atoms = config.max_atoms,
           .max_carry_atoms = config.max_atoms,
           .max_components = config.max_atoms,
           .max_inventory_atoms = config.max_atoms});
      const auto source_process_done = std::chrono::steady_clock::now();
      emit_phase_progress(progress, "source_process", "end", segment,
                          previous_outer, outer,
                          elapsed_ms(bridge_done, source_process_done));
      add_band_adjacency(provenance_adjacency, bridged_band);
      coordinate_carry_atoms_with_next_band_candidates =
          bridge.coordinate_carry_atoms_with_next_band_candidates;
      bridged_coordinate_carry_atoms =
          bridge.bridged_coordinate_carry_atoms;
      unbridged_coordinate_carry_atoms =
          bridge.unbridged_coordinate_carry_atoms;
      unbridged_without_next_band_candidates =
          bridge.unbridged_without_next_band_candidates;
      unbridged_with_next_band_candidates =
          bridge.unbridged_with_next_band_candidates;
      unbridged_dead_end_candidate_atoms =
          bridge.unbridged_dead_end_candidate_atoms;
      unbridged_unsafe_candidate_atoms =
          bridge.unbridged_unsafe_candidate_atoms;
      bridge_rejected_candidate_atoms =
          bridge.bridge_rejected_candidate_atoms;
      source_coordinate_carry_atoms_with_next_band_candidates =
          bridge.source_coordinate_carry_atoms_with_next_band_candidates;
      source_bridged_coordinate_carry_atoms =
          bridge.source_bridged_coordinate_carry_atoms;
      source_unbridged_coordinate_carry_atoms =
          bridge.source_unbridged_coordinate_carry_atoms;
      source_unbridged_without_next_band_candidates =
          bridge.source_unbridged_without_next_band_candidates;
      source_unbridged_with_next_band_candidates =
          bridge.source_unbridged_with_next_band_candidates;
      source_unbridged_dead_end_candidate_atoms =
          bridge.source_unbridged_dead_end_candidate_atoms;
      source_unbridged_unsafe_candidate_atoms =
          bridge.source_unbridged_unsafe_candidate_atoms;
      source_bridge_rejected_candidate_atoms =
          bridge.source_bridge_rejected_candidate_atoms;
      bridge_reject_reasons = bridge.bridge_reject_reasons;
      source_bridge_reject_reasons = bridge.source_bridge_reject_reasons;
      bridged_port_carry_atoms = bridge.bridged_port_carry_atoms;
      bridge_edges = bridge.bridge_edges;
    } else {
      emit_phase_progress(progress, "source_process", "begin", segment,
                          previous_outer, outer,
                          std::numeric_limits<std::uint64_t>::max());
      last = lb_source::process_band(
          band, incoming,
          {.max_atoms = config.max_atoms,
           .max_carry_atoms = config.max_atoms,
           .max_components = config.max_atoms,
           .max_inventory_atoms = config.max_atoms});
      const auto source_process_done = std::chrono::steady_clock::now();
      emit_phase_progress(progress, "source_process", "end", segment,
                          previous_outer, outer,
                          elapsed_ms(bridge_done, source_process_done));
      add_band_adjacency(provenance_adjacency, band);
      if (config.seed_inner_flags && bands_processed == 0) {
        for (const lb_source::BandAtom& atom : band.atoms) {
          if (atom.certified_source) {
            provenance_source_ids.push_back(atom.id);
          }
        }
      }
    }
    const auto process_done = std::chrono::steady_clock::now();
    port_atoms += graph.port_atoms;
    internal_edges += graph.internal_edges;
    seam_edges += graph.seam_edges;

    ++bands_processed;
    processed_outer = outer;
    if (progress) {
      const bool segment_accepted = last.accepted();
      const bool segment_source_carry =
          segment_accepted && !last.terminal_source_dead &&
          has_source_carry(last.outgoing);
      progress << "{\"schema\":\"lb_source_tileop_port_progress_v1\""
               << ",\"band_index\":" << (bands_processed - 1)
               << ",\"r_start\":" << previous_outer
               << ",\"r_outer\":" << outer
               << ",\"tiles\":" << coords.size()
               << ",\"tileop_worker_threads\":" << tileop_threads
               << ",\"port_atoms\":" << graph.port_atoms
               << ",\"internal_edges\":" << graph.internal_edges
               << ",\"seam_edges\":" << graph.seam_edges
               << ",\"tileop_overflows_total\":" << tileop_overflows
               << ",\"accepted\":" << (segment_accepted ? "true" : "false")
               << ",\"reject\":\""
               << lb_source::reject_reason_name(last.reject) << "\""
               << ",\"reject_diagnostic\":";
      append_json_string(progress, last.diagnostic);
      progress
               << ",\"terminal_source_dead\":"
               << (segment_accepted && last.terminal_source_dead ? "true"
                                                                 : "false")
               << ",\"has_source_carry\":"
               << (segment_source_carry ? "true" : "false")
               << ",\"source_carry_atoms\":"
               << (segment_source_carry ? source_carry_atoms(last.outgoing)
                                        : 0)
               << ",\"outgoing_carry_atoms\":"
               << (segment_accepted ? last.outgoing.carry_atoms.size() : 0)
               << ",\"outgoing_components\":"
               << (segment_accepted
                       ? last.outgoing.component_partition.size()
                       : 0)
               << ",\"coordinate_carry_atoms_with_next_band_candidates\":"
               << segment_bridge
                      .coordinate_carry_atoms_with_next_band_candidates
               << ",\"bridged_coordinate_carry_atoms\":"
               << segment_bridge.bridged_coordinate_carry_atoms
               << ",\"unbridged_coordinate_carry_atoms\":"
               << segment_bridge.unbridged_coordinate_carry_atoms
               << ",\"unbridged_without_next_band_candidates\":"
               << segment_bridge.unbridged_without_next_band_candidates
               << ",\"unbridged_with_next_band_candidates\":"
               << segment_bridge.unbridged_with_next_band_candidates
               << ",\"unbridged_dead_end_candidate_atoms\":"
               << segment_bridge.unbridged_dead_end_candidate_atoms
               << ",\"unbridged_unsafe_candidate_atoms\":"
               << segment_bridge.unbridged_unsafe_candidate_atoms
               << ",\"bridge_rejected_candidate_atoms\":"
               << segment_bridge.bridge_rejected_candidate_atoms
               << ",\"source_coordinate_carry_atoms_with_next_band_candidates\":"
               << segment_bridge
                      .source_coordinate_carry_atoms_with_next_band_candidates
               << ",\"source_bridged_coordinate_carry_atoms\":"
               << segment_bridge.source_bridged_coordinate_carry_atoms
               << ",\"source_unbridged_coordinate_carry_atoms\":"
               << segment_bridge.source_unbridged_coordinate_carry_atoms
               << ",\"source_unbridged_without_next_band_candidates\":"
               << segment_bridge
                      .source_unbridged_without_next_band_candidates
               << ",\"source_unbridged_with_next_band_candidates\":"
               << segment_bridge.source_unbridged_with_next_band_candidates
               << ",\"source_unbridged_dead_end_candidate_atoms\":"
               << segment_bridge.source_unbridged_dead_end_candidate_atoms
               << ",\"source_unbridged_unsafe_candidate_atoms\":"
               << segment_bridge.source_unbridged_unsafe_candidate_atoms
               << ",\"source_bridge_rejected_candidate_atoms\":"
               << segment_bridge.source_bridge_rejected_candidate_atoms
               << ",\"bridge_reject_reasons\":";
      append_count_object(progress, segment_bridge.bridge_reject_reasons);
      progress << ",\"source_bridge_reject_reasons\":";
      append_count_object(progress,
                          segment_bridge.source_bridge_reject_reasons);
      progress << ",\"bridged_port_carry_atoms\":"
               << segment_bridge.bridged_port_carry_atoms
               << ",\"bridge_edges\":" << segment_bridge.bridge_edges
               << ",\"target_seen\":"
               << (segment_target_seen ? "true" : "false")
               << ",\"target_port_atoms\":" << segment_target_port_atoms
               << ",\"target_bridge_edges\":"
               << segment_target_bridge_edges
               << ",\"grid_ms\":" << elapsed_ms(band_begin, grid_done)
               << ",\"enumerate_ms\":"
               << elapsed_ms(grid_done, enumerate_done)
               << ",\"tileop_ms\":"
               << elapsed_ms(enumerate_done, tileop_done)
               << ",\"graph_ms\":" << elapsed_ms(tileop_done, graph_done)
               << ",\"target_bridge_ms\":"
               << elapsed_ms(graph_done, target_bridge_done)
               << ",\"process_ms\":"
               << elapsed_ms(target_bridge_done, process_done)
               << ",\"total_ms\":"
               << elapsed_ms(band_begin, process_done) << "}\n";
      progress.flush();
    }
    emit_phase_progress(progress, "band", "end", segment, previous_outer,
                        outer, elapsed_ms(band_begin, process_done));
    if (!last.accepted()) {
      break;
    }
    if (last.terminal_source_dead) {
      break;
    }
    incoming = last.outgoing;
    if (config.stop_after_bands != 0 &&
        bands_processed >= config.stop_after_bands) {
      break;
    }
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
                      processed_outer, last));
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
  canonicalize_adjacency(provenance_adjacency);
  const std::vector<lb_source::AtomId> target_atom_path =
      target_source_reached
          ? atom_path_to_target(provenance_source_ids, *target_id,
                                provenance_adjacency)
          : std::vector<lb_source::AtomId>{};
  if (target_source_reached && target_atom_path.empty()) {
    std::cerr << "target source reachability lacks atom-chain provenance\n";
    return EXIT_FAILURE;
  }

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
            << ",\"r_final\":" << processed_outer
            << ",\"requested_r_final\":" << config.r_final
            << ",\"band_width\":" << config.band_width
            << ",\"stop_after_bands\":" << config.stop_after_bands
            << ",\"schedule_mode\":\""
            << (config.schedule_radii.empty() ? "fixed_width"
                                               : "explicit_radii")
            << "\",\"schedule_boundary_count\":" << schedule_radii.size()
            << ",\"schedule_min_width\":"
            << min_schedule_width(schedule_radii)
            << ",\"schedule_max_width\":"
            << max_schedule_width(schedule_radii)
            << ",\"bands_processed\":" << bands_processed
            << ",\"tileop_worker_threads\":" << max_tileop_worker_threads
            << ",\"campaign_tiles_processed\":" << campaign_tiles_processed
            << ",\"tileop_overflows\":" << tileop_overflows
            << ",\"port_atoms\":" << port_atoms
            << ",\"internal_edges\":" << internal_edges
            << ",\"seam_edges\":" << seam_edges
            << ",\"manifest_source_carry_atoms\":"
            << manifest_source_carry_atoms
            << ",\"prefix_witness_targets\":" << prefix_witness_targets
            << ",\"coordinate_carry_atoms_with_next_band_candidates\":"
            << coordinate_carry_atoms_with_next_band_candidates
            << ",\"bridged_coordinate_carry_atoms\":"
            << bridged_coordinate_carry_atoms
            << ",\"unbridged_coordinate_carry_atoms\":"
            << unbridged_coordinate_carry_atoms
            << ",\"unbridged_without_next_band_candidates\":"
            << unbridged_without_next_band_candidates
            << ",\"unbridged_with_next_band_candidates\":"
            << unbridged_with_next_band_candidates
            << ",\"unbridged_dead_end_candidate_atoms\":"
            << unbridged_dead_end_candidate_atoms
            << ",\"unbridged_unsafe_candidate_atoms\":"
            << unbridged_unsafe_candidate_atoms
            << ",\"bridge_rejected_candidate_atoms\":"
            << bridge_rejected_candidate_atoms
            << ",\"source_coordinate_carry_atoms_with_next_band_candidates\":"
            << source_coordinate_carry_atoms_with_next_band_candidates
            << ",\"source_bridged_coordinate_carry_atoms\":"
            << source_bridged_coordinate_carry_atoms
            << ",\"source_unbridged_coordinate_carry_atoms\":"
            << source_unbridged_coordinate_carry_atoms
            << ",\"source_unbridged_without_next_band_candidates\":"
            << source_unbridged_without_next_band_candidates
            << ",\"source_unbridged_with_next_band_candidates\":"
            << source_unbridged_with_next_band_candidates
            << ",\"source_unbridged_dead_end_candidate_atoms\":"
            << source_unbridged_dead_end_candidate_atoms
            << ",\"source_unbridged_unsafe_candidate_atoms\":"
            << source_unbridged_unsafe_candidate_atoms
            << ",\"source_bridge_rejected_candidate_atoms\":"
            << source_bridge_rejected_candidate_atoms
            << ",\"bridge_reject_reasons\":";
  append_count_object(std::cout, bridge_reject_reasons);
  std::cout << ",\"source_bridge_reject_reasons\":";
  append_count_object(std::cout, source_bridge_reject_reasons);
  std::cout << ",\"bridged_port_carry_atoms\":"
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
              << ",\"path_provenance\":\""
              << (target_atom_path.empty()
                      ? "component_reachability_only"
                      : "mixed_coordinate_port_atom_chain_non_claim")
              << "\",\"atom_path_length\":" << target_atom_path.size()
              << ",\"atom_path\":";
    append_atom_id_array(std::cout, target_atom_path);
  }
  std::cout << "}"
            << ",\"accepted\":" << (accepted ? "true" : "false")
            << ",\"reject\":\"" << lb_source::reject_reason_name(last.reject)
            << "\""
            << ",\"reject_diagnostic\":";
  append_json_string(std::cout, last.diagnostic);
  std::cout
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
            << (manifest_written ? "true" : "false");
  if (accepted && last.terminal_source_dead) {
    std::cout << ",\"terminal_source_inventory_accumulator\":";
    append_terminal_inventory_accumulator(std::cout, inventory_summary);
  }
  std::cout
            << ",\"non_claim\":\"TileOp-port scheduler diagnostic; not SOURCE_ORIGIN_K26 or SOURCE_DEAD_CERT\""
            << "}\n";

  return accepted && tileop_overflows == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
