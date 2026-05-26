#include "lb_source/diagnostic_telemetry.h"
#include "lb_source/source_propagation.h"
#include "lb_source/tileop_live_bridge.h"
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
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "campaign/campaign_constants.h"
#include "campaign/constants.h"
#include "campaign/grid.h"
#include "campaign/sieve.h"
#include "campaign/tileop.h"
#include "../../cpp-campaign-v2/src/sha256.h"

namespace {

constexpr std::string_view kPhase0Schema = "lb_diagnostic_phase0_v1";
constexpr std::string_view kRunnerId = "source_tileop_port_runner_v1";

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
  std::optional<std::string> live_manifest_in;
  std::optional<std::string> prefix_witness_in;
  std::optional<std::string> live_manifest_out;
  std::optional<std::string> last_band_summary_out;
  std::optional<std::string> death_out;
  std::optional<std::string> progress_out;
  std::optional<std::uint64_t> target_a;
  std::optional<std::uint64_t> target_b;
};

struct Point {
  std::int64_t a = 0;
  std::int64_t b = 0;
  std::uint64_t norm_sq = 0;
};

struct PrefixWitnessPathSummary {
  std::uint64_t path_points = 0;
  std::uint64_t seed_norm_sq = 0;
  std::uint64_t target_norm_sq = 0;
};

struct PrefixWitness {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::set<lb_source::AtomId> target_ids;
  std::map<lb_source::AtomId, PrefixWitnessPathSummary> path_by_target;
};

using TileOpLiveBridgeResult = lb_source::TileOpLiveBridgeResult;

struct TargetBridgeResult {
  bool seen = false;
  std::uint64_t port_atoms = 0;
  std::uint64_t bridge_edges = 0;
  std::map<std::pair<lb_source::AtomId, lb_source::AtomId>,
           std::vector<lb_source::CoordinateAtom>>
      coordinate_port_paths;
};

struct CoordinatePortExpansionSummary {
  lb_source::AtomId coordinate_atom_id = 0;
  lb_source::AtomId port_atom_id = 0;
  std::uint64_t path_points = 0;
  std::uint64_t coordinate_norm_sq = 0;
  std::uint64_t port_witness_norm_sq = 0;
  std::vector<lb_source::CoordinateAtom> path;
};

struct CoordinatePortExpansionStatus {
  std::uint64_t required_edges = 0;
  std::uint64_t available_edges = 0;
  std::uint64_t path_points_total = 0;
  std::vector<CoordinatePortExpansionSummary> summaries;
};

struct RunnerInventorySummary {
  lb_source::InventorySummary digest;
  std::uint64_t max_norm_sq = 0;
  std::vector<lb_source::AtomId> max_norm_atom_ids;
};

struct RunningMaxima {
  std::uint64_t resident_tiles = 0;
  std::uint64_t resident_tileops = 0;
  std::uint64_t resident_port_atoms = 0;
  std::uint64_t resident_edges = 0;
  std::uint64_t live_frontier_atoms = 0;
  std::uint64_t live_frontier_components = 0;
};

struct WallTimingTotals {
  std::uint64_t grid_ms = 0;
  std::uint64_t enumerate_ms = 0;
  std::uint64_t tileop_ms = 0;
  std::uint64_t graph_ms = 0;
  std::uint64_t target_bridge_ms = 0;
  std::uint64_t handoff_ms = 0;
  std::uint64_t process_ms = 0;
  std::uint64_t band_total_ms = 0;
};

struct RunnerDsu {
  explicit RunnerDsu(std::size_t n) : parent(n), rank(n, 0) {
    for (std::size_t i = 0; i < n; ++i) {
      parent[i] = i;
    }
  }

  std::size_t find(std::size_t x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  }

  void unite(std::size_t a, std::size_t b) {
    a = find(a);
    b = find(b);
    if (a == b) {
      return;
    }
    if (rank[a] < rank[b]) {
      std::swap(a, b);
    }
    parent[b] = a;
    if (rank[a] == rank[b]) {
      ++rank[a];
    }
  }

  std::vector<std::size_t> parent;
  std::vector<std::uint8_t> rank;
};

enum class HandoffKind {
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
      << "canonical port atoms, and stitches bands through "
         "lb_source::process_band_live.\n"
      << "This is not a SOURCE_ORIGIN_K26 claim.\n"
      << "\n"
      << "Options:\n"
      << "  --r-start R           starting radius (default 248)\n"
      << "  --r-final R           final radius (default 512)\n"
      << "  --band-width W        radial band width (default 128)\n"
      << "  --stop-after-bands N  stop after N processed bands and allow\n"
      << "                        --live-manifest-out to checkpoint the separator\n"
      << "  --schedule-radii CSV  explicit increasing radial boundaries;\n"
      << "                        first must equal --r-start and last --r-final\n"
      << "  --max-atoms N         hard atom cap for sidecar process_band_live\n"
      << "                        and live carry/frontier state\n"
      << "                        (default 1000000)\n"
      << "  --tileop-threads N    worker threads for sidecar TileOp build;\n"
      << "                        0 means hardware auto (default 0)\n"
      << "  --seed-inner-flags    seed first band from TileOp inner flags\n"
      << "                        (geo-I diagnostic only)\n"
      << "  --require-full-bridge\n"
      << "                        reject live handoff if any source coordinate\n"
      << "                        carry atom has no first-band TileOp port bridge\n"
      << "  --live-manifest-in PATH\n"
      << "                        read an LB_SOURCE_LIVE_HANDOFF_V1 checkpoint\n"
      << "                        at --r-start and resume without fresh seeding\n"
      << "  --prefix-witness-in PATH\n"
      << "                        read diagnostic origin-prefix paths for\n"
      << "                        incoming source carry atoms\n"
      << "  --target-a A --target-b B\n"
      << "                        add a canonical coordinate target atom and\n"
      << "                        bridge it to its TileOp port component when seen\n"
      << "  --live-manifest-out PATH\n"
      << "                        write final LB_SOURCE_LIVE_HANDOFF_V1 checkpoint\n"
      << "                        when source survives\n"
      << "  --last-band-summary-out PATH\n"
      << "                        write the active last-band summary diagnostic\n"
      << "  --death-out PATH      write previous-live + active-summary death\n"
      << "                        diagnostic when terminal_source_dead is true\n"
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
    } else if (take_value("--live-manifest-in", value)) {
      if (value.empty()) {
        std::cerr << "--live-manifest-in must not be empty\n";
        return false;
      }
      config.live_manifest_in = value;
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
    } else if (take_value("--live-manifest-out", value)) {
      if (value.empty()) {
        std::cerr << "--live-manifest-out must not be empty\n";
        return false;
      }
      config.live_manifest_out = value;
    } else if (take_value("--last-band-summary-out", value)) {
      if (value.empty()) {
        std::cerr << "--last-band-summary-out must not be empty\n";
        return false;
      }
      config.last_band_summary_out = value;
    } else if (take_value("--death-out", value)) {
      if (value.empty()) {
        std::cerr << "--death-out must not be empty\n";
        return false;
      }
      config.death_out = value;
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
  if (config.seed_inner_flags && config.live_manifest_in.has_value()) {
    std::cerr << "--seed-inner-flags cannot be combined with "
                 "--live-manifest-in\n";
    return false;
  }
  if (config.prefix_witness_in.has_value() &&
      !config.live_manifest_in.has_value()) {
    std::cerr << "--prefix-witness-in requires --live-manifest-in\n";
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

HandoffKind classify_handoff_or_die(const lb_source::LiveHandoffV1& handoff) {
  if (handoff.separator.carry_atoms.empty()) {
    std::cerr << "live handoff carry atom set is empty\n";
    std::exit(EXIT_FAILURE);
  }

  bool all_coordinate = true;
  bool all_port = true;
  for (const lb_source::CarryAtom& atom : handoff.separator.carry_atoms) {
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
    std::cerr << "live handoff carry atoms must be all coordinate atoms or all "
                 "TileOp port atoms\n";
    std::exit(EXIT_FAILURE);
  }
  return all_coordinate ? HandoffKind::kCoordinateCarry
                        : HandoffKind::kPortCarry;
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

lb_source::LiveHandoffV1 read_live_handoff_or_die(
    const std::string& path,
    const lb_source::LiveHandoffExpectedContext& expected) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open --live-manifest-in path: " << path << "\n";
    std::exit(EXIT_FAILURE);
  }

  const lb_source::LiveHandoffReadResult result =
      lb_source::read_live_handoff(in, expected);
  if (!result.accepted()) {
    std::cerr << "invalid --live-manifest-in: " << result.diagnostic << "\n";
    std::exit(EXIT_FAILURE);
  }
  return result.handoff;
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
    if (!witness.path_by_target
             .emplace(target_id,
                      PrefixWitnessPathSummary{
                          .path_points = path_count,
                          .seed_norm_sq = first_point.norm_sq,
                          .target_norm_sq = last_point.norm_sq,
                      })
             .second) {
      fail_prefix_witness("duplicate witness path target");
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
  return lb_source::elapsed_ms(begin, end);
}

bool has_source_carry(const lb_source::LiveSeparator& state) {
  return std::find(state.source_bit_per_component.begin(),
                   state.source_bit_per_component.end(),
                   true) != state.source_bit_per_component.end();
}

std::uint64_t source_carry_atoms(const lb_source::LiveSeparator& state) {
  std::uint64_t count = 0;
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    if (state.source_bit_per_component[c]) {
      count += state.component_partition[c].size();
    }
  }
  return count;
}

std::vector<lb_source::AtomId> source_frontier_ids(
    const lb_source::LiveSeparator& state) {
  std::vector<lb_source::AtomId> ids;
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    if (!state.source_bit_per_component[c]) {
      continue;
    }
    ids.insert(ids.end(), state.component_partition[c].begin(),
               state.component_partition[c].end());
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

std::string sha256_hex_string(const std::string& text) {
  return campaign::detail::sha256_hex(text);
}

std::string diagnostic_schedule_digest_hex() {
  return sha256_hex_string("lb_source_tileop_port_runner_live_schedule_v1");
}

lb_source::LiveHandoffV1 make_live_handoff(
    std::uint64_t cut_radius,
    std::string_view source_mode,
    const lb_source::LiveSeparator& separator,
    const std::optional<lb_source::LiveHandoffV1>& previous = std::nullopt) {
  lb_source::LiveHandoffV1 handoff;
  handoff.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
  handoff.cut_radius = cut_radius;
  handoff.carry_width = lb_source::ceil_sqrt(handoff.k_sq);
  handoff.source_mode = std::string(source_mode);
  handoff.source_id = previous ? previous->source_id
                               : "tileop_port_materialized_source_v1";
  handoff.geometry_id = previous ? previous->geometry_id
                                 : "gaussian_octant_tileop_port_v1";
  handoff.build_id = previous ? previous->build_id : "local_campaign_build";
  handoff.schedule_digest_algorithm =
      previous ? previous->schedule_digest_algorithm
               : "sha256:tileop_port_diagnostic_schedule_v1";
  handoff.schedule_digest_hex =
      previous ? previous->schedule_digest_hex
               : diagnostic_schedule_digest_hex();
  handoff.overflow_summary = previous ? previous->overflow_summary : "none";
  handoff.separator = separator;
  return lb_source::canonicalize_live_handoff(handoff);
}

lb_source::BridgeSafetyCounters bridge_safety_from_segment(
    const TileOpLiveBridgeResult& bridge) {
  lb_source::BridgeSafetyCounters counters;
  counters.coordinate_carry_atoms_checked =
      bridge.bridged_coordinate_carry_atoms +
      bridge.unbridged_coordinate_carry_atoms;
  counters.coordinate_carry_atoms_bridged =
      bridge.bridged_coordinate_carry_atoms;
  counters.coordinate_carry_atoms_unbridged =
      bridge.unbridged_coordinate_carry_atoms;
  counters.coordinate_carry_atoms_without_next_band_candidates =
      bridge.unbridged_without_next_band_candidates;
  counters.coordinate_carry_atoms_dead_end_candidates =
      bridge.unbridged_dead_end_candidate_atoms;
  counters.coordinate_carry_atoms_unsafe_candidates =
      bridge.unbridged_unsafe_candidate_atoms;
  counters.bridge_rejected_candidate_atoms =
      bridge.bridge_rejected_candidate_atoms;
  return counters;
}

void sort_unique_atom_ids(std::vector<lb_source::AtomId>& ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

void merge_component_max(
    lb_source::LastBandComponentSummaryV1& component,
    lb_source::AtomId id,
    std::uint64_t norm_sq) {
  if (component.max_support_atom_ids.empty() ||
      norm_sq > component.max_support_norm_sq) {
    component.max_support_norm_sq = norm_sq;
    component.max_support_atom_ids = {id};
  } else if (norm_sq == component.max_support_norm_sq) {
    component.max_support_atom_ids.push_back(id);
  }

  const std::optional<lb_source::CoordinateAtom> coordinate =
      lb_source::decode_coordinate_atom_id(id);
  if (!coordinate.has_value() || coordinate->norm_sq != norm_sq) {
    return;
  }
  if (component.max_coordinate_atom_ids.empty() ||
      norm_sq > component.max_coordinate_norm_sq) {
    component.max_coordinate_norm_sq = norm_sq;
    component.max_coordinate_atom_ids = {id};
  } else if (norm_sq == component.max_coordinate_norm_sq) {
    component.max_coordinate_atom_ids.push_back(id);
  }
}

std::optional<lb_source::LastBandReachabilitySummaryV1>
make_active_band_summary(
    const lb_source::LiveHandoffV1& previous_handoff,
    const lb_source::BandInput& band,
    const lb_source::LiveSeparator& outgoing,
    std::uint64_t r_start,
    std::uint64_t r_outer,
    const TileOpLiveBridgeResult& segment_bridge) {
  std::vector<lb_source::BandAtom> all_atoms = band.atoms;
  std::unordered_map<lb_source::AtomId, std::size_t> index_by_id;
  index_by_id.reserve(all_atoms.size() +
                      previous_handoff.separator.carry_atoms.size());
  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    index_by_id.emplace(all_atoms[i].id, i);
  }
  for (const lb_source::CarryAtom& atom :
       previous_handoff.separator.carry_atoms) {
    if (index_by_id.find(atom.id) != index_by_id.end()) {
      continue;
    }
    const std::size_t idx = all_atoms.size();
    all_atoms.push_back({.id = atom.id,
                         .norm_sq = atom.norm_sq,
                         .certified_source = false,
                         .allow_outer_overshoot_carry =
                             lb_source::decode_port_atom_id(atom.id)
                                 .has_value()});
    index_by_id.emplace(atom.id, idx);
  }
  RunnerDsu dsu(all_atoms.size());
  for (const auto& component :
       previous_handoff.separator.component_partition) {
    if (component.empty()) {
      continue;
    }
    const auto first = index_by_id.find(component.front());
    if (first == index_by_id.end()) {
      return std::nullopt;
    }
    for (std::size_t i = 1; i < component.size(); ++i) {
      const auto it = index_by_id.find(component[i]);
      if (it == index_by_id.end()) {
        return std::nullopt;
      }
      dsu.unite(first->second, it->second);
    }
  }
  for (const auto& edge : band.edges) {
    const auto lhs = index_by_id.find(edge.first);
    const auto rhs = index_by_id.find(edge.second);
    if (lhs == index_by_id.end() || rhs == index_by_id.end()) {
      return std::nullopt;
    }
    dsu.unite(lhs->second, rhs->second);
  }

  std::map<std::size_t, lb_source::LastBandComponentSummaryV1>
      component_by_root;
  for (const lb_source::CarryAtom& atom :
       previous_handoff.separator.carry_atoms) {
    const auto it = index_by_id.find(atom.id);
    if (it == index_by_id.end()) {
      return std::nullopt;
    }
    component_by_root[dsu.find(it->second)].boundary_atoms.push_back(atom.id);
  }
  for (const lb_source::CarryAtom& atom : outgoing.carry_atoms) {
    const auto it = index_by_id.find(atom.id);
    if (it == index_by_id.end()) {
      continue;
    }
    lb_source::LastBandComponentSummaryV1& component =
        component_by_root[dsu.find(it->second)];
    component.boundary_atoms.push_back(atom.id);
    if (lb_source::decode_coordinate_atom_id(atom.id).has_value()) {
      component.touches_outer_coordinate_carry = true;
    } else if (lb_source::decode_port_atom_id(atom.id).has_value()) {
      component.touches_port_overhang = true;
    }
  }
  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    const std::size_t root = dsu.find(i);
    const auto component_it = component_by_root.find(root);
    if (component_it == component_by_root.end()) {
      continue;
    }
    merge_component_max(component_it->second, all_atoms[i].id,
                        all_atoms[i].norm_sq);
  }

  const lb_source::BridgeSafetyCounters bridge_safety =
      bridge_safety_from_segment(segment_bridge);
  lb_source::LastBandReachabilitySummaryV1 summary;
  summary.k_sq = previous_handoff.k_sq;
  summary.r_start = r_start;
  summary.r_outer = r_outer;
  summary.carry_width = previous_handoff.carry_width;
  summary.source_mode = previous_handoff.source_mode;
  summary.source_id = previous_handoff.source_id;
  summary.geometry_id = previous_handoff.geometry_id;
  summary.build_id = previous_handoff.build_id;
  summary.schedule_digest_algorithm =
      previous_handoff.schedule_digest_algorithm;
  summary.schedule_digest_hex = previous_handoff.schedule_digest_hex;
  summary.overflow_summary = previous_handoff.overflow_summary;
  summary.bridge_policy = "materialized_tileop_port_diagnostic";
  summary.transfer_summary_present = true;
  for (auto& [root, component] : component_by_root) {
    (void)root;
    sort_unique_atom_ids(component.boundary_atoms);
    sort_unique_atom_ids(component.max_coordinate_atom_ids);
    sort_unique_atom_ids(component.max_support_atom_ids);
    // The materialized runner currently has aggregate bridge counters only.
    // Attach them conservatively to every local component for non-claim output.
    component.bridge_safety = bridge_safety;
    summary.components.push_back(std::move(component));
  }
  return summary;
}

void append_bridge_safety_json(
    std::ostream& out,
    const lb_source::BridgeSafetyCounters& counters) {
  out << "{\"coordinate_carry_atoms_checked\":"
      << counters.coordinate_carry_atoms_checked
      << ",\"coordinate_carry_atoms_bridged\":"
      << counters.coordinate_carry_atoms_bridged
      << ",\"coordinate_carry_atoms_unbridged\":"
      << counters.coordinate_carry_atoms_unbridged
      << ",\"coordinate_carry_atoms_without_next_band_candidates\":"
      << counters.coordinate_carry_atoms_without_next_band_candidates
      << ",\"coordinate_carry_atoms_dead_end_candidates\":"
      << counters.coordinate_carry_atoms_dead_end_candidates
      << ",\"coordinate_carry_atoms_unsafe_candidates\":"
      << counters.coordinate_carry_atoms_unsafe_candidates
      << ",\"bridge_rejected_candidate_atoms\":"
      << counters.bridge_rejected_candidate_atoms << '}';
}

void append_last_band_summary_json(
    std::ostream& out,
    const lb_source::LastBandReachabilitySummaryV1& summary) {
  out << "{\"schema\":\"lb_source_last_band_reachability_summary_v1\""
      << ",\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\""
      << ",\"k_sq\":" << summary.k_sq
      << ",\"r_start\":" << summary.r_start
      << ",\"r_outer\":" << summary.r_outer
      << ",\"carry_width\":" << summary.carry_width
      << ",\"source_mode\":";
  append_json_string(out, summary.source_mode);
  out << ",\"source_id\":";
  append_json_string(out, summary.source_id);
  out << ",\"geometry_id\":";
  append_json_string(out, summary.geometry_id);
  out << ",\"build_id\":";
  append_json_string(out, summary.build_id);
  out << ",\"schedule_digest_algorithm\":";
  append_json_string(out, summary.schedule_digest_algorithm);
  out << ",\"schedule_digest_hex\":";
  append_json_string(out, summary.schedule_digest_hex);
  out << ",\"overflow_summary\":";
  append_json_string(out, summary.overflow_summary);
  out << ",\"bridge_policy\":";
  append_json_string(out, summary.bridge_policy);
  out << ",\"transfer_summary_present\":"
      << (summary.transfer_summary_present ? "true" : "false")
      << ",\"components\":[";
  for (std::size_t i = 0; i < summary.components.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    const lb_source::LastBandComponentSummaryV1& component =
        summary.components[i];
    out << "{\"boundary_atoms\":";
    append_atom_id_array(out, component.boundary_atoms);
    out << ",\"touches_outer_coordinate_carry\":"
        << (component.touches_outer_coordinate_carry ? "true" : "false")
        << ",\"touches_port_overhang\":"
        << (component.touches_port_overhang ? "true" : "false")
        << ",\"max_coordinate_norm_sq\":"
        << component.max_coordinate_norm_sq
        << ",\"max_coordinate_atom_ids\":";
    append_atom_id_array(out, component.max_coordinate_atom_ids);
    out << ",\"max_support_norm_sq\":" << component.max_support_norm_sq
        << ",\"max_support_atom_ids\":";
    append_atom_id_array(out, component.max_support_atom_ids);
    out << ",\"bridge_safety\":";
    append_bridge_safety_json(out, component.bridge_safety);
    out << '}';
  }
  out << "],\"non_claim\":\"materialized runner summary only; not a "
         "SOURCE_DEAD_CERT\"}";
}

std::string last_band_summary_to_json(
    const lb_source::LastBandReachabilitySummaryV1& summary) {
  std::ostringstream out;
  append_last_band_summary_json(out, summary);
  out << '\n';
  return out.str();
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
    const lb_source::LiveSeparator& state) {
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

bool coordinate_path_less(const std::vector<lb_source::CoordinateAtom>& lhs,
                          const std::vector<lb_source::CoordinateAtom>& rhs) {
  if (lhs.size() != rhs.size()) {
    return lhs.size() < rhs.size();
  }
  return std::lexicographical_compare(
      lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
      [](const lb_source::CoordinateAtom& a,
         const lb_source::CoordinateAtom& b) {
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
    std::map<std::pair<lb_source::AtomId, lb_source::AtomId>,
             std::vector<lb_source::CoordinateAtom>>& paths,
    lb_source::AtomId coordinate_atom_id,
    lb_source::AtomId port_atom_id,
    std::vector<lb_source::CoordinateAtom> path) {
  if (path.empty()) {
    return;
  }
  const auto key = std::pair{coordinate_atom_id, port_atom_id};
  const auto existing = paths.find(key);
  if (existing == paths.end() || coordinate_path_less(path, existing->second)) {
    paths[key] = std::move(path);
  }
}

void append_coordinate_port_expansion_status(
    std::ostream& out, const CoordinatePortExpansionStatus& status) {
  out << "{\"required_edges\":" << status.required_edges
      << ",\"available_edges\":" << status.available_edges
      << ",\"path_points_total\":" << status.path_points_total
      << ",\"expansions\":[";
  for (std::size_t i = 0; i < status.summaries.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    const CoordinatePortExpansionSummary& summary = status.summaries[i];
    out << "{\"coordinate_atom_id\":" << summary.coordinate_atom_id
        << ",\"port_atom_id\":" << summary.port_atom_id
        << ",\"path_points\":" << summary.path_points
        << ",\"coordinate_norm_sq\":" << summary.coordinate_norm_sq
        << ",\"port_witness_norm_sq\":" << summary.port_witness_norm_sq
        << ",\"path\":[";
    for (std::size_t j = 0; j < summary.path.size(); ++j) {
      if (j != 0) {
        out << ',';
      }
      const lb_source::CoordinateAtom& point = summary.path[j];
      out << "{\"a\":" << point.a << ",\"b\":" << point.b
          << ",\"norm_sq\":" << point.norm_sq << '}';
    }
    out << "]}";
  }
  out << "]}";
}

CoordinatePortExpansionStatus summarize_coordinate_port_expansions(
    const std::vector<lb_source::AtomId>& atom_path,
    const std::map<std::pair<lb_source::AtomId, lb_source::AtomId>,
                   std::vector<lb_source::CoordinateAtom>>& paths) {
  CoordinatePortExpansionStatus status;
  for (std::size_t i = 1; i < atom_path.size(); ++i) {
    lb_source::AtomId coordinate_atom_id = 0;
    lb_source::AtomId port_atom_id = 0;
    if (atom_path[i - 1] >= 0 && atom_path[i] < 0) {
      coordinate_atom_id = atom_path[i - 1];
      port_atom_id = atom_path[i];
    } else if (atom_path[i - 1] < 0 && atom_path[i] >= 0) {
      coordinate_atom_id = atom_path[i];
      port_atom_id = atom_path[i - 1];
    } else {
      continue;
    }
    ++status.required_edges;
    const auto path_it = paths.find({coordinate_atom_id, port_atom_id});
    if (path_it == paths.end() || path_it->second.empty()) {
      continue;
    }
    const std::vector<lb_source::CoordinateAtom>& path = path_it->second;
    ++status.available_edges;
    status.path_points_total += path.size();
    status.summaries.push_back(CoordinatePortExpansionSummary{
        .coordinate_atom_id = coordinate_atom_id,
        .port_atom_id = port_atom_id,
        .path_points = path.size(),
        .coordinate_norm_sq = path.front().norm_sq,
        .port_witness_norm_sq = path.back().norm_sq,
        .path = path,
    });
  }
  return status;
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
      for (const lb_source::CoordinatePortBridgeResult::PortExpansion&
               expansion : bridge.port_expansions) {
        if (expansion.path.empty() || expansion.path.front().a != target.a ||
            expansion.path.front().b != target.b ||
            expansion.path.front().norm_sq != target.norm_sq) {
          std::cerr << "target TileOp-port bridge emitted mismatched local "
                       "expansion path\n";
          std::exit(EXIT_FAILURE);
        }
        record_coordinate_port_path(result.coordinate_port_paths, target_id,
                                    expansion.port_atom, expansion.path);
      }
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
  const auto total_begin = lb_source::DiagnosticClock::now();
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

  HandoffKind handoff_kind = HandoffKind::kNone;
  RunningMaxima maxima;
  WallTimingTotals timings;
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
  std::uint64_t live_manifest_source_carry_atoms = 0;
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
  std::map<std::pair<lb_source::AtomId, lb_source::AtomId>,
           std::vector<lb_source::CoordinateAtom>>
      coordinate_port_expansion_paths;
  std::optional<lb_source::LiveHandoffV1> first_source_artifact;
  std::optional<lb_source::LiveHandoffV1> previous_live_handoff;
  std::optional<lb_source::LiveHandoffV1> current_live_handoff;
  std::optional<lb_source::LastBandReachabilitySummaryV1>
      active_band_summary;
  std::optional<lb_source::LastBandReachabilitySummaryV1>
      terminal_summary_if_dead;
  std::optional<lb_source::LiveHandoffV1> terminal_previous_live_handoff;
  std::optional<lb_source::LiveSeparator> live_incoming;
  lb_source::LiveProcessResult live_last;
  if (config.live_manifest_in.has_value()) {
    const auto live_manifest_read_begin = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "live_manifest_read", "begin",
                        std::numeric_limits<std::uint64_t>::max(), 0, 0,
                        std::numeric_limits<std::uint64_t>::max());
    lb_source::LiveHandoffExpectedContext expected;
    expected.k_sq = static_cast<std::uint64_t>(campaign::k_sq_value);
    expected.cut_radius = config.r_start;
    expected.carry_width =
        lb_source::ceil_sqrt(static_cast<std::uint64_t>(
            campaign::k_sq_value));
    previous_live_handoff =
        read_live_handoff_or_die(*config.live_manifest_in, expected);
    emit_phase_progress(progress, "live_manifest_read", "end",
                        std::numeric_limits<std::uint64_t>::max(), 0, 0,
                        elapsed_ms(live_manifest_read_begin,
                                   std::chrono::steady_clock::now()));
    timings.handoff_ms += elapsed_ms(live_manifest_read_begin,
                                     std::chrono::steady_clock::now());
    first_source_artifact = previous_live_handoff;
    live_incoming = previous_live_handoff->separator;
    source_mode = previous_live_handoff->source_mode;
    handoff_kind = classify_handoff_or_die(*previous_live_handoff);
    if (config.prefix_witness_in.has_value()) {
      if (handoff_kind != HandoffKind::kCoordinateCarry) {
        std::cerr << "--prefix-witness-in requires a coordinate carry "
                     "live handoff\n";
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
      timings.handoff_ms += elapsed_ms(prefix_witness_read_begin,
                                       std::chrono::steady_clock::now());
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
           c < previous_live_handoff->separator.component_partition.size();
           ++c) {
        if (!previous_live_handoff->separator.source_bit_per_component[c]) {
          continue;
        }
        for (const lb_source::AtomId id :
             previous_live_handoff->separator.component_partition[c]) {
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
        handoff_kind != HandoffKind::kCoordinateCarry) {
      std::cerr << "--require-full-bridge requires a coordinate carry "
                   "live handoff\n";
      return EXIT_FAILURE;
    }
  }

  const std::vector<std::uint64_t> schedule_radii = build_schedule_radii(config);
  campaign::CampaignConstants tileop_constants;
  try {
    tileop_constants = campaign::CampaignConstants::from_radii(
        config.r_start, config.r_final, campaign::k_sq_value);
  } catch (const std::exception& ex) {
    std::cerr << "campaign run construction failed: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }
  std::uint64_t bands_processed = 0;
  std::uint64_t campaign_tiles_processed = 0;
  std::uint64_t tileop_overflows = 0;
  std::uint64_t max_tileop_worker_threads = 0;
  std::uint64_t port_atoms = 0;
  std::uint64_t internal_edges = 0;
  std::uint64_t seam_edges = 0;
  std::map<lb_source::AtomId, std::uint64_t> norm_by_id;
  std::uint64_t processed_outer = config.r_start;
  if (live_incoming.has_value()) {
    add_partition_adjacency(provenance_adjacency, *live_incoming);
    for (const lb_source::CarryAtom& atom : live_incoming->carry_atoms) {
      norm_by_id.emplace(atom.id, atom.norm_sq);
    }
    for (std::size_t c = 0; c < live_incoming->component_partition.size();
         ++c) {
      if (!live_incoming->source_bit_per_component[c]) {
        continue;
      }
      live_manifest_source_carry_atoms +=
          live_incoming->component_partition[c].size();
      provenance_source_ids.insert(
          provenance_source_ids.end(),
          live_incoming->component_partition[c].begin(),
          live_incoming->component_partition[c].end());
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
    campaign::Grid grid;
    const auto grid_begin = std::chrono::steady_clock::now();
    emit_phase_progress(progress, "grid_build", "begin", segment,
                        previous_outer, outer,
                        std::numeric_limits<std::uint64_t>::max());
    try {
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
    const std::uint64_t grid_ms = elapsed_ms(grid_begin, grid_done);
    timings.grid_ms += grid_ms;
    emit_phase_progress(progress, "grid_build", "end", segment,
                        previous_outer, outer, grid_ms);

    emit_phase_progress(progress, "active_tile_enumerate", "begin", segment,
                        previous_outer, outer,
                        std::numeric_limits<std::uint64_t>::max());
    const std::vector<campaign::TileCoord> coords =
        grid.enumerate_active_tiles();
    campaign_tiles_processed += coords.size();
    const auto enumerate_done = std::chrono::steady_clock::now();
    const std::uint64_t enumerate_ms = elapsed_ms(grid_done, enumerate_done);
    timings.enumerate_ms += enumerate_ms;
    emit_phase_progress(progress, "active_tile_enumerate", "end", segment,
                        previous_outer, outer, enumerate_ms);
    const std::size_t tileop_threads =
        resolve_tileop_threads(config.tileop_threads, coords.size());
    max_tileop_worker_threads =
        std::max<std::uint64_t>(max_tileop_worker_threads, tileop_threads);
    emit_tileop_build_begin(progress, segment, previous_outer, outer,
                            coords.size(), tileop_threads);
    std::vector<campaign::TileOp> tileops =
        build_tileops(coords, tileop_constants, grid, tileop_overflows,
                      tileop_threads);
    const auto tileop_done = std::chrono::steady_clock::now();
    const std::uint64_t tileop_ms = elapsed_ms(enumerate_done, tileop_done);
    timings.tileop_ms += tileop_ms;
    emit_phase_progress(progress, "tileop_build", "end", segment,
                        previous_outer, outer, tileop_ms);

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
    const std::uint64_t graph_ms = elapsed_ms(tileop_done, graph_done);
    timings.graph_ms += graph_ms;
    emit_phase_progress(progress, "port_graph", "end", segment,
                        previous_outer, outer, graph_ms);
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
          bridge_target_coordinate_to_ports(*target, tileop_constants, coords,
                                            tileops, band);
      if (target_bridge.seen) {
        segment_target_seen = true;
        segment_target_port_atoms = target_bridge.port_atoms;
        segment_target_bridge_edges = target_bridge.bridge_edges;
        target_seen = true;
        target_port_atoms = target_bridge.port_atoms;
        target_bridge_edges = target_bridge.bridge_edges;
        norm_by_id.emplace(*target_id, target->norm_sq);
        for (const auto& [key, path] : target_bridge.coordinate_port_paths) {
          record_coordinate_port_path(coordinate_port_expansion_paths,
                                      key.first, key.second, path);
        }
      }
    }
    const auto target_bridge_done = std::chrono::steady_clock::now();
    const std::uint64_t target_bridge_ms =
        elapsed_ms(graph_done, target_bridge_done);
    timings.target_bridge_ms += target_bridge_ms;
    emit_phase_progress(progress, "target_bridge", "end", segment,
                        previous_outer, outer, target_bridge_ms);
    auto bridge_done = target_bridge_done;
    TileOpLiveBridgeResult segment_bridge;
    lb_source::BandInput processed_band_for_summary = band;
    if (previous_live_handoff.has_value() &&
        handoff_kind == HandoffKind::kCoordinateCarry &&
        bands_processed == 0) {
      lb_source::BandInput bridged_band = band;
      for (const lb_source::CarryAtom& atom :
           previous_live_handoff->separator.carry_atoms) {
        norm_by_id.emplace(atom.id, atom.norm_sq);
      }
      add_partition_adjacency(provenance_adjacency,
                              previous_live_handoff->separator);
      emit_phase_progress(progress, "live_handoff_bridge", "begin", segment,
                          previous_outer, outer,
                          std::numeric_limits<std::uint64_t>::max());
      const TileOpLiveBridgeResult bridge =
          lb_source::bridge_coordinate_live_handoff_to_ports(
              *previous_live_handoff, tileop_constants, coords, tileops,
              bridged_band, tileop_threads);
      bridge_done = std::chrono::steady_clock::now();
      const std::uint64_t live_handoff_bridge_ms =
          elapsed_ms(target_bridge_done, bridge_done);
      timings.handoff_ms += live_handoff_bridge_ms;
      emit_phase_progress(progress, "live_handoff_bridge", "end", segment,
                          previous_outer, outer, live_handoff_bridge_ms);
      segment_bridge = bridge;
      if (config.require_full_bridge &&
          bridge.unbridged_coordinate_carry_atoms != 0) {
        std::cerr
            << "strict seam bridge requires zero unbridged coordinate carry "
               "atoms, got "
            << bridge.unbridged_coordinate_carry_atoms << "\n";
        return EXIT_FAILURE;
      }
      processed_band_for_summary = bridged_band;
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
      for (const auto& [key, path] : bridge.coordinate_port_paths) {
        record_coordinate_port_path(coordinate_port_expansion_paths, key.first,
                                    key.second, path);
      }
    } else {
      add_band_adjacency(provenance_adjacency, band);
      if (config.seed_inner_flags && bands_processed == 0) {
        for (const lb_source::BandAtom& atom : band.atoms) {
          if (atom.certified_source) {
            provenance_source_ids.push_back(atom.id);
          }
        }
      }
    }
    if (previous_live_handoff.has_value()) {
      add_partition_adjacency(provenance_adjacency,
                              previous_live_handoff->separator);
    }
    const std::optional<lb_source::LiveSeparator> live_input = live_incoming;
    emit_phase_progress(progress, "source_process", "begin", segment,
                        previous_outer, outer,
                        std::numeric_limits<std::uint64_t>::max());
    live_last = lb_source::process_band_live(
        processed_band_for_summary, live_input,
        {.max_atoms = config.max_atoms,
         .max_carry_atoms = config.max_atoms,
         .max_components = config.max_atoms,
         .max_inventory_atoms = config.max_atoms});
    const auto source_process_done = std::chrono::steady_clock::now();
    const std::uint64_t source_process_ms =
        elapsed_ms(bridge_done, source_process_done);
    timings.process_ms += source_process_ms;
    emit_phase_progress(progress, "source_process", "end", segment,
                        previous_outer, outer, source_process_ms);
    active_band_summary.reset();
    if (live_last.accepted() && previous_live_handoff.has_value()) {
      active_band_summary = make_active_band_summary(
          *previous_live_handoff, processed_band_for_summary,
          live_last.outgoing, previous_outer, outer, segment_bridge);
      if (!active_band_summary.has_value()) {
        std::cerr << "failed to construct active last-band summary\n";
        return EXIT_FAILURE;
      }
    }
    if (live_last.accepted()) {
      current_live_handoff = make_live_handoff(
          outer, source_mode, live_last.outgoing, previous_live_handoff);
      if (!first_source_artifact.has_value()) {
        first_source_artifact = current_live_handoff;
      }
      if (live_last.terminal_source_dead) {
        terminal_previous_live_handoff = previous_live_handoff;
        terminal_summary_if_dead = active_band_summary;
      }
    }
    const auto process_done = std::chrono::steady_clock::now();
    const std::uint64_t band_total_ms = elapsed_ms(band_begin, process_done);
    timings.band_total_ms += band_total_ms;
    port_atoms += graph.port_atoms;
    internal_edges += graph.internal_edges;
    seam_edges += graph.seam_edges;
    maxima.resident_tiles =
        std::max<std::uint64_t>(maxima.resident_tiles, coords.size());
    maxima.resident_tileops =
        std::max<std::uint64_t>(maxima.resident_tileops, tileops.size());
    maxima.resident_port_atoms =
        std::max<std::uint64_t>(maxima.resident_port_atoms, graph.port_atoms);
    maxima.resident_edges =
        std::max<std::uint64_t>(maxima.resident_edges,
                                processed_band_for_summary.edges.size());
    if (live_last.accepted()) {
      maxima.live_frontier_atoms =
          std::max<std::uint64_t>(maxima.live_frontier_atoms,
                                  live_last.outgoing.carry_atoms.size());
      maxima.live_frontier_components =
          std::max<std::uint64_t>(
              maxima.live_frontier_components,
              live_last.outgoing.component_partition.size());
    }

    ++bands_processed;
    processed_outer = outer;
    if (progress) {
      const bool segment_accepted = live_last.accepted();
      const bool segment_source_carry =
          segment_accepted && !live_last.terminal_source_dead &&
          has_source_carry(live_last.outgoing);
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
               << lb_source::reject_reason_name(live_last.reject) << "\""
               << ",\"reject_diagnostic\":";
      append_json_string(progress, live_last.diagnostic);
      progress
               << ",\"terminal_source_dead\":"
               << (segment_accepted && live_last.terminal_source_dead
                       ? "true"
                       : "false")
               << ",\"has_source_carry\":"
               << (segment_source_carry ? "true" : "false")
               << ",\"source_carry_atoms\":"
               << (segment_source_carry ? source_carry_atoms(live_last.outgoing)
                                        : 0)
               << ",\"outgoing_carry_atoms\":"
               << (segment_accepted ? live_last.outgoing.carry_atoms.size()
                                    : 0)
               << ",\"outgoing_components\":"
               << (segment_accepted
                       ? live_last.outgoing.component_partition.size()
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
               << enumerate_ms
               << ",\"tileop_ms\":" << tileop_ms
               << ",\"graph_ms\":" << graph_ms
               << ",\"target_bridge_ms\":" << target_bridge_ms
               << ",\"handoff_ms\":"
               << elapsed_ms(target_bridge_done, bridge_done)
               << ",\"process_ms\":" << source_process_ms
               << ",\"total_ms\":" << band_total_ms << "}\n";
      progress.flush();
    }
    emit_phase_progress(progress, "band", "end", segment, previous_outer,
                        outer, band_total_ms);
    if (!live_last.accepted()) {
      break;
    }
    if (live_last.terminal_source_dead) {
      break;
    }
    if (current_live_handoff.has_value()) {
      previous_live_handoff = current_live_handoff;
      live_incoming = current_live_handoff->separator;
    }
    if (config.stop_after_bands != 0 &&
        bands_processed >= config.stop_after_bands) {
      break;
    }
  }

  bool live_manifest_written = false;
  if (config.live_manifest_out.has_value()) {
    if (!live_last.accepted() || live_last.terminal_source_dead ||
        !current_live_handoff.has_value() ||
        !has_source_carry(current_live_handoff->separator)) {
      std::cerr << "--live-manifest-out requires accepted live source carry\n";
      return EXIT_FAILURE;
    }
    std::ofstream manifest(*config.live_manifest_out);
    if (!manifest) {
      std::cerr << "cannot open --live-manifest-out path: "
                << *config.live_manifest_out << "\n";
      return EXIT_FAILURE;
    }
    const auto live_manifest_write_begin = lb_source::DiagnosticClock::now();
    lb_source::write_live_handoff(manifest, *current_live_handoff);
    timings.handoff_ms +=
        lb_source::elapsed_ms(live_manifest_write_begin,
                              lb_source::DiagnosticClock::now());
    live_manifest_written = true;
  }

  bool last_band_summary_written = false;
  if (config.last_band_summary_out.has_value()) {
    if (!active_band_summary.has_value()) {
      std::cerr << "--last-band-summary-out requires an active band summary\n";
      return EXIT_FAILURE;
    }
    std::ofstream summary(*config.last_band_summary_out);
    if (!summary) {
      std::cerr << "cannot open --last-band-summary-out path: "
                << *config.last_band_summary_out << "\n";
      return EXIT_FAILURE;
    }
    const auto summary_write_begin = lb_source::DiagnosticClock::now();
    summary << last_band_summary_to_json(*active_band_summary);
    timings.handoff_ms +=
        lb_source::elapsed_ms(summary_write_begin,
                              lb_source::DiagnosticClock::now());
    last_band_summary_written = true;
  }

  bool death_written = false;
  if (config.death_out.has_value()) {
    if (!live_last.accepted() || !live_last.terminal_source_dead) {
      std::cerr << "--death-out requires terminal_source_dead=true; current "
                   "terminal_source_dead is false\n";
      return EXIT_FAILURE;
    }
    if (!terminal_previous_live_handoff.has_value()) {
      std::cerr << "--death-out requires previous live handoff; refusing "
                   "progress-row-only death evidence\n";
      return EXIT_FAILURE;
    }
    if (!terminal_summary_if_dead.has_value()) {
      std::cerr << "--death-out requires active band summary; refusing "
                   "progress-row-only death evidence\n";
      return EXIT_FAILURE;
    }
    const lb_source::LastBandSummaryApplyResult replay =
        lb_source::apply_last_band_summary(*terminal_previous_live_handoff,
                                           *terminal_summary_if_dead);
    if (!replay.accepted() || !replay.terminal_source_dead) {
      std::cerr << "--death-out replay of previous live handoff plus active "
                   "summary did not confirm terminal source death: "
                << replay.diagnostic << "\n";
      return EXIT_FAILURE;
    }
    const std::string previous_text =
        lb_source::live_handoff_to_string(*terminal_previous_live_handoff);
    const std::string summary_text =
        last_band_summary_to_json(*terminal_summary_if_dead);
    const std::string previous_hash = sha256_hex_string(previous_text);
    const std::string summary_hash = sha256_hex_string(summary_text);
    std::ofstream death(*config.death_out);
    if (!death) {
      std::cerr << "cannot open --death-out path: " << *config.death_out
                << "\n";
      return EXIT_FAILURE;
    }
    const auto death_write_begin = lb_source::DiagnosticClock::now();
    death << "{\"schema\":\"lb_source_tileop_port_death_diagnostic_v1\""
          << ",\"proof_status\":\"DIAGNOSTIC_NON_CLAIM\""
          << ",\"terminal_source_dead\":true"
          << ",\"previous_live_handoff_hash_algorithm\":\"sha256\""
          << ",\"previous_live_handoff_sha256\":\"" << previous_hash
          << "\""
          << ",\"active_band_summary_hash_algorithm\":\"sha256\""
          << ",\"active_band_summary_sha256\":\"" << summary_hash << "\""
          << ",\"coordinate_metrics\":{\"max_source_coordinate_norm_sq\":"
          << replay.max_source_coordinate_norm_sq
          << ",\"max_source_coordinate_atom_ids\":";
    append_atom_id_array(death, replay.max_source_coordinate_atom_ids);
    death << "},\"port_support_metrics\":{\"max_source_support_norm_sq\":"
          << replay.max_source_support_norm_sq
          << ",\"max_source_support_atom_ids\":";
    append_atom_id_array(death, replay.max_source_support_atom_ids);
    death << "},\"bridge_counters\":";
    append_bridge_safety_json(death, replay.source_bridge_safety);
    death << ",\"non_claim\":\"diagnostic death artifact only; previous "
             "handoff and active summary are required; not a claim-grade "
             "source-dead certificate\"}\n";
    timings.handoff_ms +=
        lb_source::elapsed_ms(death_write_begin,
                              lb_source::DiagnosticClock::now());
    death_written = true;
  }

  const bool accepted = live_last.accepted();
  const bool source_carry =
      accepted && !live_last.terminal_source_dead &&
      has_source_carry(live_last.outgoing);
  const std::vector<lb_source::AtomId> source_frontier =
      source_carry ? source_frontier_ids(live_last.outgoing)
                   : std::vector<lb_source::AtomId>{};
  const RunnerInventorySummary source_frontier_summary =
      summarize_runner_inventory(source_frontier, norm_by_id);
  canonicalize_adjacency(provenance_adjacency);
  const std::vector<lb_source::AtomId> target_atom_path =
      target_id.has_value()
          ? atom_path_to_target(provenance_source_ids, *target_id,
                                provenance_adjacency)
          : std::vector<lb_source::AtomId>{};
  const bool target_source_reached =
      target_id.has_value() && !target_atom_path.empty();
  if (target_source_reached && target_atom_path.empty()) {
    std::cerr << "target source reachability lacks atom-chain provenance\n";
    return EXIT_FAILURE;
  }
  std::optional<lb_source::AtomId> target_prefix_atom_id;
  std::optional<PrefixWitnessPathSummary> target_prefix_path;
  if (prefix_witness.has_value() && !target_atom_path.empty()) {
    for (const lb_source::AtomId atom_id : target_atom_path) {
      if (atom_id < 0) {
        continue;
      }
      target_prefix_atom_id = atom_id;
      break;
    }
    if (target_prefix_atom_id.has_value()) {
      const auto path_it =
          prefix_witness->path_by_target.find(*target_prefix_atom_id);
      if (path_it != prefix_witness->path_by_target.end()) {
        target_prefix_path = path_it->second;
      }
    }
  }
  if (target_source_reached && prefix_witness.has_value() &&
      !target_prefix_path.has_value()) {
    std::cerr << "target atom-chain lacks prefix witness path provenance\n";
    return EXIT_FAILURE;
  }
  const CoordinatePortExpansionStatus target_expansion_status =
      summarize_coordinate_port_expansions(target_atom_path,
                                           coordinate_port_expansion_paths);
  const lb_source::RssSnapshot rss = lb_source::rss_snapshot();
  const std::uint64_t total_ms =
      lb_source::elapsed_ms(total_begin, lb_source::DiagnosticClock::now());

  std::cout << "{"
            << "\"schema\":\"lb_source_tileop_port_runner_v1\","
            << "\"phase0_schema\":\"" << kPhase0Schema << "\","
            << "\"runner_id\":\"" << kRunnerId << "\","
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
            << ",\"live_manifest_source_carry_atoms\":"
            << live_manifest_source_carry_atoms
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
    std::cout << ",\"prefix_witness_path\":{\"available\":"
              << (target_prefix_path.has_value() ? "true" : "false")
              << ",\"target_atom_id\":";
    if (target_prefix_atom_id.has_value()) {
      std::cout << *target_prefix_atom_id;
    } else {
      std::cout << "null";
    }
    std::cout << ",\"path_points\":"
              << (target_prefix_path.has_value()
                      ? target_prefix_path->path_points
                      : 0)
              << ",\"seed_norm_sq\":"
              << (target_prefix_path.has_value()
                      ? target_prefix_path->seed_norm_sq
                      : 0)
              << ",\"target_norm_sq\":"
              << (target_prefix_path.has_value()
                      ? target_prefix_path->target_norm_sq
                      : 0)
              << '}';
    std::cout << ",\"coordinate_port_expansions\":";
    append_coordinate_port_expansion_status(std::cout,
                                            target_expansion_status);
  }
  std::cout << "}"
            << ",\"accepted\":" << (accepted ? "true" : "false")
            << ",\"reject\":\""
            << lb_source::reject_reason_name(live_last.reject)
            << "\""
            << ",\"reject_diagnostic\":";
  append_json_string(std::cout, live_last.diagnostic);
  std::cout
            << ",\"terminal_source_dead\":"
            << (accepted && live_last.terminal_source_dead ? "true" : "false")
            << ",\"has_source_carry\":"
            << (source_carry ? "true" : "false")
            << ",\"source_carry_atoms\":"
            << (source_carry ? source_carry_atoms(live_last.outgoing) : 0)
            << ",\"source_frontier_mode\":\"live_frontier_only_non_claim\""
            << ",\"source_frontier_count\":"
            << source_frontier_summary.digest.count
            << ",\"source_frontier_digest_algorithm\":\""
            << source_frontier_summary.digest.digest_algorithm << "\""
            << ",\"source_frontier_digest_hex\":\""
            << source_frontier_summary.digest.digest_hex << "\""
            << ",\"max_source_frontier_norm_sq\":"
            << source_frontier_summary.max_norm_sq
            << ",\"max_source_frontier_norm_atom_ids\":";
  append_atom_id_array(std::cout, source_frontier_summary.max_norm_atom_ids);
  if (config.live_manifest_out.has_value()) {
    std::cout << ",\"live_manifest_written\":"
              << (live_manifest_written ? "true" : "false");
  }
  if (config.last_band_summary_out.has_value()) {
    std::cout << ",\"last_band_summary_written\":"
              << (last_band_summary_written ? "true" : "false");
  }
  if (config.death_out.has_value()) {
    std::cout << ",\"death_written\":"
              << (death_written ? "true" : "false");
  }
  std::cout
            << ",\"non_claim\":\"TileOp-port scheduler diagnostic; not SOURCE_ORIGIN_K26 or SOURCE_DEAD_CERT\""
            << ",\"rss_bytes\":"
            << lb_source::json_safe_uint64(rss.current_bytes)
            << ",\"peak_rss_bytes\":"
            << lb_source::json_safe_uint64(rss.peak_bytes)
            << ",\"max_resident_tiles\":" << maxima.resident_tiles
            << ",\"max_resident_tileops\":" << maxima.resident_tileops
            << ",\"max_resident_port_atoms\":"
            << maxima.resident_port_atoms
            << ",\"max_resident_edges\":" << maxima.resident_edges
            << ",\"max_live_frontier_atoms\":"
            << maxima.live_frontier_atoms
            << ",\"max_resident_components\":"
            << maxima.live_frontier_components
            << ",\"grid_ms\":" << timings.grid_ms
            << ",\"enumerate_ms\":" << timings.enumerate_ms
            << ",\"tileop_ms\":" << timings.tileop_ms
            << ",\"graph_ms\":" << timings.graph_ms
            << ",\"target_bridge_ms\":" << timings.target_bridge_ms
            << ",\"process_ms\":" << timings.process_ms
            << ",\"handoff_ms\":" << timings.handoff_ms
            << ",\"band_total_ms\":" << timings.band_total_ms
            << ",\"total_ms\":" << total_ms
            << "}\n";

  return accepted && tileop_overflows == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
