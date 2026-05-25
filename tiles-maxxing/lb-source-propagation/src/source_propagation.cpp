#include "lb_source/source_propagation.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <istream>
#include <iterator>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "sha256.h"

namespace lb_source {
namespace {

class Dsu {
 public:
  explicit Dsu(std::size_t n) : parent_(n), rank_(n, 0) {
    for (std::size_t i = 0; i < n; ++i) {
      parent_[i] = i;
    }
  }

  std::size_t find(std::size_t x) {
    assert(x < parent_.size());
    if (parent_[x] != x) {
      parent_[x] = find(parent_[x]);
    }
    return parent_[x];
  }

  void unite(std::size_t a, std::size_t b) {
    std::size_t ra = find(a);
    std::size_t rb = find(b);
    if (ra == rb) {
      return;
    }
    if (rank_[ra] < rank_[rb] || (rank_[ra] == rank_[rb] && rb < ra)) {
      std::swap(ra, rb);
    }
    parent_[rb] = ra;
    if (rank_[ra] == rank_[rb]) {
      ++rank_[ra];
    }
  }

 private:
  std::vector<std::size_t> parent_;
  std::vector<std::uint8_t> rank_;
};

ProcessResult reject(RejectReason reason, std::string diagnostic,
                     std::uint64_t carry_width = 0) {
  ProcessResult result;
  result.reject = reason;
  result.diagnostic = std::move(diagnostic);
  result.carry_width = carry_width;
  return result;
}

enum class InventoryMode {
  kCollect,
  kFrontierOnly,
};

LiveProcessResult live_reject(RejectReason reason, std::string diagnostic,
                              std::uint64_t carry_width = 0) {
  LiveProcessResult result;
  result.reject = reason;
  result.diagnostic = std::move(diagnostic);
  result.carry_width = carry_width;
  return result;
}

bool lexicographic_component_less(const std::vector<AtomId>& a,
                                  const std::vector<AtomId>& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

bool in_final_carry_window(std::uint64_t norm_sq, std::uint64_t outer_radius,
                           std::uint64_t carry_width,
                           bool allow_outer_overshoot) {
  if (!allow_outer_overshoot && norm_sq > outer_radius * outer_radius) {
    return false;
  }
  const std::uint64_t inner_radius =
      outer_radius > carry_width ? outer_radius - carry_width : 0;
  return norm_sq >= inner_radius * inner_radius;
}

void sort_unique_atom_ids(std::vector<AtomId>& ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

bool is_sorted_unique_atom_ids(const std::vector<AtomId>& ids) {
  return std::adjacent_find(ids.begin(), ids.end(),
                            [](AtomId lhs, AtomId rhs) {
                              return lhs >= rhs;
                            }) == ids.end();
}

void merge_inventory_payload(std::vector<AtomId>& target,
                             const std::vector<AtomId>& source) {
  if (target.empty()) {
    target = source;
    if (!is_sorted_unique_atom_ids(target)) {
      sort_unique_atom_ids(target);
    }
    return;
  }

  std::vector<AtomId> sorted_source_storage;
  const std::vector<AtomId>* sorted_source = &source;
  if (!is_sorted_unique_atom_ids(source)) {
    sorted_source_storage = source;
    sort_unique_atom_ids(sorted_source_storage);
    sorted_source = &sorted_source_storage;
  }

  std::vector<AtomId> merged;
  merged.reserve(target.size() + sorted_source->size());
  std::merge(target.begin(), target.end(), sorted_source->begin(),
             sorted_source->end(), std::back_inserter(merged));
  merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
  target = std::move(merged);
}

bool inventory_payload_exceeds_cap(
    const std::map<std::size_t, std::vector<AtomId>>& inventory_by_root,
    std::size_t cap) {
  for (const auto& [root, inventory] : inventory_by_root) {
    (void)root;
    if (inventory.size() > cap) {
      return true;
    }
  }
  return false;
}

bool square_ge(std::uint64_t x, std::uint64_t n) {
  if (n == 0) {
    return true;
  }
  if (x == 0) {
    return false;
  }
  const std::uint64_t q = n / x;
  const std::uint64_t r = n % x;
  return x > q || (x == q && r == 0);
}

bool valid_source_mode(std::string_view mode) {
  return mode == "ORIGIN_SOURCE" || mode == "WIRED_SOURCE" ||
         mode == "CERTIFIED_SEED";
}

bool parse_uint64_token(const std::string& token, std::uint64_t& value) {
  if (token.empty() || token[0] == '-') {
    return false;
  }
  const char* begin = token.data();
  const char* end = token.data() + token.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  return ec == std::errc() && ptr == end;
}

bool parse_int64_token(const std::string& token, std::int64_t& value) {
  if (token.empty()) {
    return false;
  }
  const char* begin = token.data();
  const char* end = token.data() + token.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  return ec == std::errc() && ptr == end;
}

bool parse_size_token(const std::string& token, std::size_t& value) {
  std::uint64_t parsed = 0;
  if (!parse_uint64_token(token, parsed) ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

bool valid_manifest_token(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (std::isspace(ch) != 0 || ch < 0x20) {
      return false;
    }
  }
  return true;
}

bool valid_hex_token(std::string_view value) {
  if (!valid_manifest_token(value)) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (std::isxdigit(ch) == 0) {
      return false;
    }
  }
  return true;
}

bool stable_atom_id(AtomId id) {
  return decode_coordinate_atom_id(id).has_value() ||
         decode_port_atom_id(id).has_value();
}

std::string validate_stable_atom_identity(AtomId id, std::uint64_t norm_sq,
                                          std::string_view unstable_diagnostic,
                                          std::string_view norm_diagnostic) {
  const std::optional<CoordinateAtom> coordinate =
      decode_coordinate_atom_id(id);
  if (coordinate.has_value()) {
    if (coordinate->norm_sq != norm_sq) {
      return std::string(norm_diagnostic);
    }
    return "";
  }
  if (!decode_port_atom_id(id).has_value()) {
    return std::string(unstable_diagnostic);
  }
  return "";
}

std::string validate_live_separator_atom_identities(
    const LiveSeparator& state) {
  for (const CarryAtom& atom : state.carry_atoms) {
    const std::string validation = validate_stable_atom_identity(
        atom.id, atom.norm_sq, "unstable carry atom id",
        "carry atom norm does not match coordinate atom");
    if (!validation.empty()) {
      return validation;
    }
  }
  return "";
}

std::string validate_band_atom_identities(const BandInput& band) {
  for (const BandAtom& atom : band.atoms) {
    const std::string validation = validate_stable_atom_identity(
        atom.id, atom.norm_sq, "band atom has unstable id",
        "band atom norm does not match coordinate atom");
    if (!validation.empty()) {
      return validation;
    }
  }
  return "";
}

std::string validate_separator_manifest(const SeparatorState& state) {
  if (state.component_partition.size() !=
      state.source_bit_per_component.size()) {
    return "source-bit count does not match component count";
  }
  if (state.component_inventory.size() != state.component_partition.size()) {
    return "inventory count does not match component count";
  }

  std::set<AtomId> carry_atoms;
  for (const CarryAtom& atom : state.carry_atoms) {
    if (!carry_atoms.insert(atom.id).second) {
      return "duplicate carry atom";
    }
  }

  std::set<AtomId> partition_atoms;
  std::set<AtomId> inventory_atoms;
  for (std::size_t c = 0; c < state.component_partition.size(); ++c) {
    const auto& component = state.component_partition[c];
    const auto& inventory = state.component_inventory[c];
    if (component.empty()) {
      return "empty component";
    }
    if (inventory.empty()) {
      return "empty inventory";
    }
    std::set<AtomId> local_inventory;
    for (const AtomId id : component) {
      if (carry_atoms.find(id) == carry_atoms.end()) {
        return "component references non-carry atom";
      }
      if (!partition_atoms.insert(id).second) {
        return "carry atom appears in multiple components";
      }
      if (std::find(inventory.begin(), inventory.end(), id) ==
          inventory.end()) {
        return "inventory omits carry atom";
      }
    }
    for (const AtomId id : inventory) {
      if (!local_inventory.insert(id).second) {
        return "duplicate inventory atom";
      }
      if (!inventory_atoms.insert(id).second) {
        return "inventory atom appears in multiple components";
      }
    }
  }
  if (partition_atoms != carry_atoms) {
    return "component partition does not cover all carry atoms";
  }
  return "";
}

void append_json_string(std::ostringstream& out, std::string_view value) {
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
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch) << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  out << '"';
}

void append_atom_id_array(std::ostringstream& out,
                          const std::vector<AtomId>& ids) {
  out << '[';
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << ids[i];
  }
  out << ']';
}

void append_inventory_summary_json(std::ostringstream& out,
                                   const InventorySummary& summary) {
  out << "{\"count\":" << summary.count << ",\"digest_algorithm\":";
  append_json_string(out, summary.digest_algorithm);
  out << ",\"digest_hex\":";
  append_json_string(out, summary.digest_hex);
  out << ",\"max_norm_sq\":" << summary.max_norm_sq
      << ",\"max_norm_atom_ids\":";
  append_atom_id_array(out, summary.max_norm_atom_ids);
  out << '}';
}

void append_path_point_json(std::ostringstream& out,
                            const SourcePathPoint& point) {
  out << "{\"a\":" << point.a << ",\"b\":" << point.b
      << ",\"norm_sq\":" << point.norm_sq << '}';
}

void append_path_json(std::ostringstream& out,
                      const std::vector<SourcePathPoint>& path) {
  out << '[';
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    append_path_point_json(out, path[i]);
  }
  out << ']';
}

void append_metadata_json(std::ostringstream& out,
                          const SourceDraftMetadata& metadata) {
  out << "{\"source_mode\":";
  append_json_string(out, metadata.source_mode);
  out << ",\"source_id\":";
  append_json_string(out, metadata.source_id);
  out << ",\"geometry_id\":";
  append_json_string(out, metadata.geometry_id);
  out << ",\"commit_id\":";
  append_json_string(out, metadata.commit_id);
  out << ",\"build_id\":";
  append_json_string(out, metadata.build_id);
  out << ",\"bz_status\":";
  append_json_string(out, metadata.bz_status);
  out << ",\"artifact_hash\":";
  append_json_string(out, metadata.artifact_hash);
  out << '}';
}

void append_manifest_json(std::ostringstream& out,
                          const CarryManifest& manifest) {
  CarryManifest canonical = manifest;
  canonical.separator = canonicalize_separator(canonical.separator);

  out << "{\"schema\":\"lb_source_carry_manifest_v1\",\"k_sq\":"
      << canonical.k_sq << ",\"outer_radius\":" << canonical.outer_radius
      << ",\"carry_width\":" << canonical.carry_width
      << ",\"separator\":{\"carry_atoms\":[";
  for (std::size_t i = 0; i < canonical.separator.carry_atoms.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    const CarryAtom& atom = canonical.separator.carry_atoms[i];
    out << "{\"id\":" << atom.id << ",\"norm_sq\":" << atom.norm_sq << '}';
  }
  out << "],\"components\":[";
  for (std::size_t c = 0; c < canonical.separator.component_partition.size();
       ++c) {
    if (c != 0) {
      out << ',';
    }
    out << "{\"source\":"
        << (canonical.separator.source_bit_per_component[c] ? "true"
                                                            : "false")
        << ",\"carry_atoms\":";
    append_atom_id_array(out, canonical.separator.component_partition[c]);
    out << ",\"inventory\":";
    append_atom_id_array(out, canonical.separator.component_inventory[c]);
    out << '}';
  }
  out << "]}}";
}

LastBandSummaryApplyResult last_band_reject(RejectReason reason,
                                            std::string diagnostic) {
  LastBandSummaryApplyResult result;
  result.reject = reason;
  result.diagnostic = std::move(diagnostic);
  return result;
}

void add_bridge_safety(BridgeSafetyCounters& target,
                       const BridgeSafetyCounters& source) {
  target.coordinate_carry_atoms_checked +=
      source.coordinate_carry_atoms_checked;
  target.coordinate_carry_atoms_bridged +=
      source.coordinate_carry_atoms_bridged;
  target.coordinate_carry_atoms_unbridged +=
      source.coordinate_carry_atoms_unbridged;
  target.coordinate_carry_atoms_without_next_band_candidates +=
      source.coordinate_carry_atoms_without_next_band_candidates;
  target.coordinate_carry_atoms_dead_end_candidates +=
      source.coordinate_carry_atoms_dead_end_candidates;
  target.coordinate_carry_atoms_unsafe_candidates +=
      source.coordinate_carry_atoms_unsafe_candidates;
  target.bridge_rejected_candidate_atoms +=
      source.bridge_rejected_candidate_atoms;
}

void merge_max_atoms(std::uint64_t norm_sq, const std::vector<AtomId>& atoms,
                     std::uint64_t& target_norm_sq,
                     std::vector<AtomId>& target_atoms) {
  if (atoms.empty()) {
    return;
  }
  if (target_atoms.empty() || norm_sq > target_norm_sq) {
    target_norm_sq = norm_sq;
    target_atoms = atoms;
  } else if (norm_sq == target_norm_sq) {
    target_atoms.insert(target_atoms.end(), atoms.begin(), atoms.end());
  }
  sort_unique_atom_ids(target_atoms);
}

std::string validate_last_band_component(
    const LastBandComponentSummaryV1& component) {
  if (component.boundary_atoms.empty()) {
    return "last-band component has no boundary atoms";
  }
  std::vector<AtomId> boundary = component.boundary_atoms;
  if (!is_sorted_unique_atom_ids(boundary)) {
    sort_unique_atom_ids(boundary);
    if (boundary.size() != component.boundary_atoms.size()) {
      return "duplicate atom in last-band component";
    }
  }
  for (const AtomId id : component.boundary_atoms) {
    if (!stable_atom_id(id)) {
      return "last-band component contains unstable atom id";
    }
  }

  if (component.max_coordinate_atom_ids.empty()) {
    if (component.max_coordinate_norm_sq != 0) {
      return "coordinate max norm has no coordinate atom ids";
    }
  } else {
    for (const AtomId id : component.max_coordinate_atom_ids) {
      const std::optional<CoordinateAtom> coordinate =
          decode_coordinate_atom_id(id);
      if (!coordinate.has_value()) {
        return "coordinate max references non-coordinate atom";
      }
      if (coordinate->norm_sq != component.max_coordinate_norm_sq) {
        return "coordinate max norm does not match coordinate atom";
      }
    }
  }

  if (component.max_support_atom_ids.empty()) {
    if (component.max_support_norm_sq != 0) {
      return "support max norm has no support atom ids";
    }
  } else {
    for (const AtomId id : component.max_support_atom_ids) {
      if (!stable_atom_id(id)) {
        return "support max references unstable atom id";
      }
    }
  }
  return "";
}

}  // namespace

std::uint64_t ceil_sqrt(std::uint64_t n) {
  if (n == 0) {
    return 0;
  }
  std::uint64_t lo = 0;
  std::uint64_t hi = 1;
  while (!square_ge(hi, n)) {
    if (hi > std::numeric_limits<std::uint64_t>::max() / 2) {
      hi = std::numeric_limits<std::uint64_t>::max();
      break;
    }
    hi *= 2;
  }

  while (lo + 1 < hi) {
    const std::uint64_t mid = lo + (hi - lo) / 2;
    if (square_ge(mid, n)) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  return hi;
}

std::optional<AtomId> coordinate_atom_id(std::int64_t a, std::int64_t b) {
  if (a < 0 || b < 0 ||
      a > std::numeric_limits<std::int32_t>::max() ||
      b > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  const std::uint64_t raw =
      (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
  if (raw > static_cast<std::uint64_t>(
                std::numeric_limits<AtomId>::max())) {
    return std::nullopt;
  }
  return static_cast<AtomId>(raw);
}

std::optional<CoordinateAtom> decode_coordinate_atom_id(AtomId id) {
  if (id < 0) {
    return std::nullopt;
  }
  const std::uint64_t raw = static_cast<std::uint64_t>(id);
  const std::int64_t a = static_cast<std::int64_t>(raw >> 32);
  const std::int64_t b =
      static_cast<std::int64_t>(raw & 0xffffffffULL);
  const unsigned __int128 norm =
      static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(a) +
      static_cast<unsigned __int128>(b) * static_cast<unsigned __int128>(b);
  if (norm > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  return CoordinateAtom{a, b, static_cast<std::uint64_t>(norm)};
}

std::optional<AtomId> port_atom_id(std::int64_t tile_i,
                                   std::int64_t tile_j,
                                   std::uint64_t face,
                                   std::uint64_t ordinal) {
  constexpr std::uint64_t kMaxTileCoord = (1ULL << 24) - 1ULL;
  if (tile_i < 0 || tile_j < 0 || face > 3 || ordinal > 255 ||
      static_cast<std::uint64_t>(tile_i) > kMaxTileCoord ||
      static_cast<std::uint64_t>(tile_j) > kMaxTileCoord) {
    return std::nullopt;
  }

  const std::uint64_t raw =
      (static_cast<std::uint64_t>(tile_i) << 34) |
      (static_cast<std::uint64_t>(tile_j) << 10) |
      (face << 8) | ordinal;
  if (raw > static_cast<std::uint64_t>(
                std::numeric_limits<AtomId>::max())) {
    return std::nullopt;
  }
  return static_cast<AtomId>(-1 - static_cast<AtomId>(raw));
}

std::optional<PortAtom> decode_port_atom_id(AtomId id) {
  if (id >= 0) {
    return std::nullopt;
  }
  if (id == std::numeric_limits<AtomId>::min()) {
    return std::nullopt;
  }
  const std::uint64_t raw =
      static_cast<std::uint64_t>(-1 - id);
  if ((raw >> 58) != 0) {
    return std::nullopt;
  }
  const std::uint64_t tile_i = raw >> 34;
  const std::uint64_t tile_j = (raw >> 10) & ((1ULL << 24) - 1ULL);
  const std::uint64_t face = (raw >> 8) & 0x3ULL;
  const std::uint64_t ordinal = raw & 0xffULL;
  if (tile_i > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int32_t>::max()) ||
      tile_j > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int32_t>::max())) {
    return std::nullopt;
  }
  return PortAtom{static_cast<std::int32_t>(tile_i),
                  static_cast<std::int32_t>(tile_j),
                  static_cast<std::uint8_t>(face),
                  static_cast<std::uint8_t>(ordinal)};
}

SeparatorState canonicalize_separator(const SeparatorState& state) {
  SeparatorState out;
  out.carry_atoms = state.carry_atoms;
  std::sort(out.carry_atoms.begin(), out.carry_atoms.end(),
            [](const CarryAtom& a, const CarryAtom& b) {
              if (a.id != b.id) {
                return a.id < b.id;
              }
              return a.norm_sq < b.norm_sq;
            });

  std::vector<std::pair<std::vector<AtomId>, bool>> components;
  components.reserve(state.component_partition.size());
  for (std::size_t i = 0; i < state.component_partition.size(); ++i) {
    std::vector<AtomId> component = state.component_partition[i];
    std::sort(component.begin(), component.end());
    component.erase(std::unique(component.begin(), component.end()),
                    component.end());
    std::vector<AtomId> inventory =
        i < state.component_inventory.size() ? state.component_inventory[i]
                                             : component;
    std::sort(inventory.begin(), inventory.end());
    inventory.erase(std::unique(inventory.begin(), inventory.end()),
                    inventory.end());
    const bool source =
        i < state.source_bit_per_component.size()
            ? state.source_bit_per_component[i]
            : false;
    for (const AtomId id : component) {
      if (!std::binary_search(inventory.begin(), inventory.end(), id)) {
        inventory.push_back(id);
      }
    }
    std::sort(inventory.begin(), inventory.end());
    components.push_back({std::move(component), source});
    out.component_inventory.push_back(std::move(inventory));
  }
  std::vector<std::size_t> order(components.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return lexicographic_component_less(components[a].first,
                                        components[b].first);
  });

  std::vector<std::vector<AtomId>> inventories = std::move(out.component_inventory);
  out.component_inventory.clear();
  out.component_partition.reserve(order.size());
  out.source_bit_per_component.reserve(order.size());
  out.component_inventory.reserve(order.size());
  for (const std::size_t index : order) {
    out.component_partition.push_back(std::move(components[index].first));
    out.source_bit_per_component.push_back(components[index].second);
    out.component_inventory.push_back(std::move(inventories[index]));
  }
  return out;
}

LiveSeparator canonicalize_live_separator(const LiveSeparator& state) {
  LiveSeparator out;
  out.carry_atoms = state.carry_atoms;
  std::sort(out.carry_atoms.begin(), out.carry_atoms.end(),
            [](const CarryAtom& a, const CarryAtom& b) {
              if (a.id != b.id) {
                return a.id < b.id;
              }
              return a.norm_sq < b.norm_sq;
            });

  std::vector<std::pair<std::vector<AtomId>, bool>> components;
  components.reserve(state.component_partition.size());
  for (std::size_t i = 0; i < state.component_partition.size(); ++i) {
    std::vector<AtomId> component = state.component_partition[i];
    std::sort(component.begin(), component.end());
    component.erase(std::unique(component.begin(), component.end()),
                    component.end());
    const bool source =
        i < state.source_bit_per_component.size()
            ? state.source_bit_per_component[i]
            : false;
    components.push_back({std::move(component), source});
  }

  std::sort(components.begin(), components.end(),
            [](const auto& a, const auto& b) {
              return lexicographic_component_less(a.first, b.first);
            });

  out.component_partition.reserve(components.size());
  out.source_bit_per_component.reserve(components.size());
  for (auto& [component, source] : components) {
    out.component_partition.push_back(std::move(component));
    out.source_bit_per_component.push_back(source);
  }
  return out;
}

std::string validate_live_separator(const LiveSeparator& state) {
  if (state.component_partition.size() !=
      state.source_bit_per_component.size()) {
    return "source-bit count does not match component count";
  }

  std::set<AtomId> carry_atoms;
  for (const CarryAtom& atom : state.carry_atoms) {
    if (!carry_atoms.insert(atom.id).second) {
      return "duplicate carry atom";
    }
  }

  std::set<AtomId> partition_atoms;
  for (const auto& component : state.component_partition) {
    if (component.empty()) {
      return "empty component";
    }
    for (const AtomId id : component) {
      if (carry_atoms.find(id) == carry_atoms.end()) {
        return "component references non-carry atom";
      }
      if (!partition_atoms.insert(id).second) {
        return "carry atom appears in multiple components";
      }
    }
  }
  if (partition_atoms != carry_atoms) {
    return "component partition does not cover all carry atoms";
  }
  return "";
}

LiveHandoffV1 canonicalize_live_handoff(const LiveHandoffV1& handoff) {
  LiveHandoffV1 canonical = handoff;
  canonical.separator = canonicalize_live_separator(canonical.separator);
  return canonical;
}

std::string validate_live_handoff(
    const LiveHandoffV1& handoff,
    const LiveHandoffExpectedContext& expected) {
  if (handoff.carry_width != ceil_sqrt(handoff.k_sq)) {
    return "carry width does not match k_sq";
  }
  if (!valid_manifest_token(handoff.source_mode)) {
    return "missing or invalid source_mode";
  }
  if (!valid_manifest_token(handoff.source_id)) {
    return "missing or invalid source_id";
  }
  if (!valid_manifest_token(handoff.geometry_id)) {
    return "missing or invalid geometry_id";
  }
  if (!valid_manifest_token(handoff.build_id)) {
    return "missing or invalid build_id";
  }
  if (!valid_manifest_token(handoff.schedule_digest_algorithm)) {
    return "missing or invalid schedule_digest_algorithm";
  }
  if (!valid_hex_token(handoff.schedule_digest_hex)) {
    return "missing or invalid schedule_digest_hex";
  }
  if (!valid_manifest_token(handoff.overflow_summary)) {
    return "missing or invalid overflow_summary";
  }

  const std::string separator_validation =
      validate_live_separator(handoff.separator);
  if (!separator_validation.empty()) {
    return separator_validation;
  }

  const std::string atom_identity_validation =
      validate_live_separator_atom_identities(handoff.separator);
  if (!atom_identity_validation.empty()) {
    return atom_identity_validation;
  }

  if (expected.k_sq && handoff.k_sq != *expected.k_sq) {
    return "wrong k_sq";
  }
  if (expected.cut_radius && handoff.cut_radius != *expected.cut_radius) {
    return "stale cut radius";
  }
  if (expected.carry_width && handoff.carry_width != *expected.carry_width) {
    return "wrong carry width";
  }
  if (expected.source_mode && handoff.source_mode != *expected.source_mode) {
    return "wrong source_mode";
  }
  if (expected.source_id && handoff.source_id != *expected.source_id) {
    return "wrong source_id";
  }
  if (expected.geometry_id && handoff.geometry_id != *expected.geometry_id) {
    return "wrong geometry_id";
  }
  if (expected.build_id && handoff.build_id != *expected.build_id) {
    return "wrong build_id";
  }
  if (expected.schedule_digest_algorithm &&
      handoff.schedule_digest_algorithm !=
          *expected.schedule_digest_algorithm) {
    return "wrong schedule_digest_algorithm";
  }
  if (expected.schedule_digest_hex &&
      handoff.schedule_digest_hex != *expected.schedule_digest_hex) {
    return "wrong schedule_digest_hex";
  }
  if (expected.overflow_summary &&
      handoff.overflow_summary != *expected.overflow_summary) {
    return "wrong overflow_summary";
  }
  return "";
}

LiveSeparator live_separator_from_separator(const SeparatorState& state) {
  LiveSeparator live;
  live.carry_atoms = state.carry_atoms;
  live.component_partition = state.component_partition;
  live.source_bit_per_component = state.source_bit_per_component;
  return canonicalize_live_separator(live);
}

SeparatorState separator_from_live_separator(const LiveSeparator& state) {
  const LiveSeparator canonical = canonicalize_live_separator(state);
  SeparatorState separator;
  separator.carry_atoms = canonical.carry_atoms;
  separator.component_partition = canonical.component_partition;
  separator.source_bit_per_component = canonical.source_bit_per_component;
  return separator;
}

SourceSeedApplyResult apply_source_seeds(BandInput& band,
                                         const std::vector<SourceSeed>& seeds) {
  SourceSeedApplyResult result;
  if (seeds.empty()) {
    result.diagnostic = "source seed set must not be empty";
    return result;
  }

  std::unordered_map<AtomId, std::size_t> atom_index;
  atom_index.reserve(band.atoms.size());
  for (std::size_t i = 0; i < band.atoms.size(); ++i) {
    if (!atom_index.emplace(band.atoms[i].id, i).second) {
      result.diagnostic = "duplicate band atom id";
      return result;
    }
  }

  std::set<AtomId> applied_atoms;
  for (const SourceSeed& seed : seeds) {
    if (!valid_source_mode(seed.source_mode)) {
      result.diagnostic = "invalid source seed mode: " + seed.source_mode;
      return result;
    }
    if (seed.source_id.empty()) {
      result.diagnostic = "source seed id must not be empty";
      return result;
    }
    const auto it = atom_index.find(seed.atom_id);
    if (it == atom_index.end()) {
      result.diagnostic = "source seed references missing atom";
      return result;
    }
    band.atoms[it->second].certified_source = true;
    applied_atoms.insert(seed.atom_id);
  }
  result.applied = applied_atoms.size();
  return result;
}

CarryManifest make_carry_manifest(std::uint64_t k_sq,
                                  std::uint64_t outer_radius,
                                  const ProcessResult& result) {
  CarryManifest manifest;
  manifest.k_sq = k_sq;
  manifest.outer_radius = outer_radius;
  manifest.carry_width = result.carry_width;
  manifest.separator = canonicalize_separator(result.outgoing);
  return manifest;
}

std::ostream& write_carry_manifest(std::ostream& out,
                                   const CarryManifest& manifest) {
  CarryManifest canonical = manifest;
  canonical.separator = canonicalize_separator(canonical.separator);

  out << "LB_SOURCE_CARRY_MANIFEST_V1\n";
  out << "k_sq " << canonical.k_sq << "\n";
  out << "outer_radius " << canonical.outer_radius << "\n";
  out << "carry_width " << canonical.carry_width << "\n";
  out << "carry_atoms " << canonical.separator.carry_atoms.size() << "\n";
  for (const CarryAtom& atom : canonical.separator.carry_atoms) {
    out << "carry_atom " << atom.id << ' ' << atom.norm_sq << "\n";
  }
  out << "components " << canonical.separator.component_partition.size()
      << "\n";
  for (std::size_t c = 0; c < canonical.separator.component_partition.size();
       ++c) {
    out << "component "
        << (canonical.separator.source_bit_per_component[c] ? 1 : 0) << ' '
        << canonical.separator.component_partition[c].size();
    for (const AtomId id : canonical.separator.component_partition[c]) {
      out << ' ' << id;
    }
    out << ' ' << canonical.separator.component_inventory[c].size();
    for (const AtomId id : canonical.separator.component_inventory[c]) {
      out << ' ' << id;
    }
    out << "\n";
  }
  out << "END\n";
  return out;
}

CarryManifestReadResult read_carry_manifest(std::istream& in) {
  CarryManifestReadResult result;
  std::string token;
  const auto fail = [&](std::string diagnostic) {
    result = {};
    result.diagnostic = std::move(diagnostic);
    return result;
  };
  const auto expect = [&](std::string_view expected) -> bool {
    return (in >> token) && token == expected;
  };
  const auto read_uint64 = [&](std::uint64_t& value) -> bool {
    return (in >> token) && parse_uint64_token(token, value);
  };
  const auto read_int64 = [&](std::int64_t& value) -> bool {
    return (in >> token) && parse_int64_token(token, value);
  };
  const auto read_size = [&](std::size_t& value) -> bool {
    return (in >> token) && parse_size_token(token, value);
  };

  if (!expect("LB_SOURCE_CARRY_MANIFEST_V1")) {
    return fail("missing carry manifest header");
  }
  if (!expect("k_sq") || !read_uint64(result.manifest.k_sq)) {
    return fail("missing or invalid k_sq");
  }
  if (!expect("outer_radius") ||
      !read_uint64(result.manifest.outer_radius)) {
    return fail("missing or invalid outer_radius");
  }
  if (!expect("carry_width") || !read_uint64(result.manifest.carry_width)) {
    return fail("missing or invalid carry_width");
  }

  std::size_t carry_count = 0;
  if (!expect("carry_atoms") || !read_size(carry_count)) {
    return fail("missing or invalid carry atom count");
  }
  result.manifest.separator.carry_atoms.reserve(carry_count);
  for (std::size_t i = 0; i < carry_count; ++i) {
    CarryAtom atom;
    if (!expect("carry_atom") || !read_int64(atom.id) ||
        !read_uint64(atom.norm_sq)) {
      return fail("missing or invalid carry atom");
    }
    result.manifest.separator.carry_atoms.push_back(atom);
  }

  std::size_t component_count = 0;
  if (!expect("components") || !read_size(component_count)) {
    return fail("missing or invalid component count");
  }
  result.manifest.separator.component_partition.reserve(component_count);
  result.manifest.separator.source_bit_per_component.reserve(component_count);
  result.manifest.separator.component_inventory.reserve(component_count);
  for (std::size_t c = 0; c < component_count; ++c) {
    std::uint64_t source_bit = 0;
    std::size_t partition_count = 0;
    if (!expect("component") || !read_uint64(source_bit) ||
        source_bit > 1 || !read_size(partition_count)) {
      return fail("missing or invalid component header");
    }
    std::vector<AtomId> partition;
    partition.reserve(partition_count);
    for (std::size_t i = 0; i < partition_count; ++i) {
      AtomId id = 0;
      if (!read_int64(id)) {
        return fail("missing or invalid component atom");
      }
      partition.push_back(id);
    }

    std::size_t inventory_count = 0;
    if (!read_size(inventory_count)) {
      return fail("missing or invalid inventory count");
    }
    std::vector<AtomId> inventory;
    inventory.reserve(inventory_count);
    for (std::size_t i = 0; i < inventory_count; ++i) {
      AtomId id = 0;
      if (!read_int64(id)) {
        return fail("missing or invalid inventory atom");
      }
      inventory.push_back(id);
    }

    result.manifest.separator.source_bit_per_component.push_back(source_bit !=
                                                                 0);
    result.manifest.separator.component_partition.push_back(
        std::move(partition));
    result.manifest.separator.component_inventory.push_back(
        std::move(inventory));
  }

  if (!expect("END")) {
    return fail("missing manifest END marker");
  }
  if (in >> token) {
    return fail("unexpected trailing manifest tokens");
  }

  const std::string validation =
      validate_separator_manifest(result.manifest.separator);
  if (!validation.empty()) {
    return fail(validation);
  }
  result.manifest.separator =
      canonicalize_separator(result.manifest.separator);
  return result;
}

std::string carry_manifest_to_string(const CarryManifest& manifest) {
  std::ostringstream out;
  write_carry_manifest(out, manifest);
  return out.str();
}

CarryManifestReadResult carry_manifest_from_string(std::string_view text) {
  std::istringstream in{std::string(text)};
  return read_carry_manifest(in);
}

std::ostream& write_live_handoff(std::ostream& out,
                                 const LiveHandoffV1& handoff) {
  const LiveHandoffV1 canonical = canonicalize_live_handoff(handoff);

  out << "LB_SOURCE_LIVE_HANDOFF_V1\n";
  out << "k_sq " << canonical.k_sq << "\n";
  out << "cut_radius " << canonical.cut_radius << "\n";
  out << "carry_width " << canonical.carry_width << "\n";
  out << "source_mode " << canonical.source_mode << "\n";
  out << "source_id " << canonical.source_id << "\n";
  out << "geometry_id " << canonical.geometry_id << "\n";
  out << "build_id " << canonical.build_id << "\n";
  out << "schedule_digest_algorithm "
      << canonical.schedule_digest_algorithm << "\n";
  out << "schedule_digest_hex " << canonical.schedule_digest_hex << "\n";
  out << "overflow_summary " << canonical.overflow_summary << "\n";
  out << "carry_atoms " << canonical.separator.carry_atoms.size() << "\n";
  for (const CarryAtom& atom : canonical.separator.carry_atoms) {
    out << "carry_atom " << atom.id << ' ' << atom.norm_sq << "\n";
  }
  out << "components " << canonical.separator.component_partition.size()
      << "\n";
  for (std::size_t c = 0; c < canonical.separator.component_partition.size();
       ++c) {
    out << "component "
        << (canonical.separator.source_bit_per_component[c] ? 1 : 0) << ' '
        << canonical.separator.component_partition[c].size();
    for (const AtomId id : canonical.separator.component_partition[c]) {
      out << ' ' << id;
    }
    out << "\n";
  }
  out << "END\n";
  return out;
}

LiveHandoffReadResult read_live_handoff(
    std::istream& in, const LiveHandoffExpectedContext& expected) {
  LiveHandoffReadResult result;
  std::string token;
  const auto fail = [&](std::string diagnostic) {
    result = {};
    result.diagnostic = std::move(diagnostic);
    return result;
  };
  const auto expect = [&](std::string_view expected_token) -> bool {
    return (in >> token) && token == expected_token;
  };
  const auto read_uint64 = [&](std::uint64_t& value) -> bool {
    return (in >> token) && parse_uint64_token(token, value);
  };
  const auto read_int64 = [&](std::int64_t& value) -> bool {
    return (in >> token) && parse_int64_token(token, value);
  };
  const auto read_size = [&](std::size_t& value) -> bool {
    return (in >> token) && parse_size_token(token, value);
  };
  const auto read_string = [&](std::string& value) -> bool {
    return static_cast<bool>(in >> value);
  };

  if (!expect("LB_SOURCE_LIVE_HANDOFF_V1")) {
    return fail("missing live handoff header");
  }
  if (!expect("k_sq") || !read_uint64(result.handoff.k_sq)) {
    return fail("missing or invalid k_sq");
  }
  if (!expect("cut_radius") || !read_uint64(result.handoff.cut_radius)) {
    return fail("missing or invalid cut_radius");
  }
  if (!expect("carry_width") || !read_uint64(result.handoff.carry_width)) {
    return fail("missing or invalid carry_width");
  }
  if (!expect("source_mode") || !read_string(result.handoff.source_mode)) {
    return fail("missing or invalid source_mode");
  }
  if (!expect("source_id") || !read_string(result.handoff.source_id)) {
    return fail("missing or invalid source_id");
  }
  if (!expect("geometry_id") || !read_string(result.handoff.geometry_id)) {
    return fail("missing or invalid geometry_id");
  }
  if (!expect("build_id") || !read_string(result.handoff.build_id)) {
    return fail("missing or invalid build_id");
  }
  if (!expect("schedule_digest_algorithm") ||
      !read_string(result.handoff.schedule_digest_algorithm)) {
    return fail("missing or invalid schedule_digest_algorithm");
  }
  if (!expect("schedule_digest_hex") ||
      !read_string(result.handoff.schedule_digest_hex)) {
    return fail("missing or invalid schedule_digest_hex");
  }
  if (!expect("overflow_summary") ||
      !read_string(result.handoff.overflow_summary)) {
    return fail("missing or invalid overflow_summary");
  }

  std::size_t carry_count = 0;
  if (!expect("carry_atoms") || !read_size(carry_count)) {
    return fail("missing or invalid carry atom count");
  }
  result.handoff.separator.carry_atoms.reserve(carry_count);
  for (std::size_t i = 0; i < carry_count; ++i) {
    CarryAtom atom;
    if (!expect("carry_atom") || !read_int64(atom.id) ||
        !read_uint64(atom.norm_sq)) {
      return fail("missing or invalid carry atom");
    }
    result.handoff.separator.carry_atoms.push_back(atom);
  }

  std::size_t component_count = 0;
  if (!expect("components") || !read_size(component_count)) {
    return fail("missing or invalid component count");
  }
  result.handoff.separator.component_partition.reserve(component_count);
  result.handoff.separator.source_bit_per_component.reserve(component_count);
  for (std::size_t c = 0; c < component_count; ++c) {
    std::uint64_t source_bit = 0;
    std::size_t partition_count = 0;
    if (!expect("component") || !read_uint64(source_bit) ||
        source_bit > 1 || !read_size(partition_count)) {
      return fail("missing or invalid component header");
    }
    std::vector<AtomId> partition;
    partition.reserve(partition_count);
    for (std::size_t i = 0; i < partition_count; ++i) {
      AtomId id = 0;
      if (!read_int64(id)) {
        return fail("missing or invalid component atom");
      }
      partition.push_back(id);
    }
    result.handoff.separator.source_bit_per_component.push_back(source_bit !=
                                                                0);
    result.handoff.separator.component_partition.push_back(
        std::move(partition));
  }

  if (!expect("END")) {
    return fail("missing live handoff END marker");
  }
  if (in >> token) {
    return fail("unexpected trailing live handoff tokens");
  }

  const std::string validation = validate_live_handoff(result.handoff,
                                                       expected);
  if (!validation.empty()) {
    return fail(validation);
  }
  result.handoff = canonicalize_live_handoff(result.handoff);
  return result;
}

LiveHandoffReadResult read_live_handoff(std::istream& in) {
  return read_live_handoff(in, LiveHandoffExpectedContext{});
}

std::string live_handoff_to_string(const LiveHandoffV1& handoff) {
  std::ostringstream out;
  write_live_handoff(out, handoff);
  return out.str();
}

LiveHandoffReadResult live_handoff_from_string(
    std::string_view text, const LiveHandoffExpectedContext& expected) {
  std::istringstream in{std::string(text)};
  return read_live_handoff(in, expected);
}

LiveHandoffReadResult live_handoff_from_string(std::string_view text) {
  return live_handoff_from_string(text, LiveHandoffExpectedContext{});
}

LastBandSummaryApplyResult apply_last_band_summary(
    const LiveHandoffV1& incoming,
    const LastBandReachabilitySummaryV1& summary) {
  if (!summary.transfer_summary_present) {
    return last_band_reject(RejectReason::kMalformed,
                            "missing last-band transfer summary");
  }
  if (summary.components.empty()) {
    return last_band_reject(RejectReason::kMalformed,
                            "empty last-band transfer summary");
  }
  if (summary.k_sq != incoming.k_sq) {
    return last_band_reject(RejectReason::kMalformed,
                            "summary k_sq does not match incoming handoff");
  }
  if (summary.r_start != incoming.cut_radius) {
    return last_band_reject(
        RejectReason::kMalformed,
        "summary r_start does not match incoming cut radius");
  }
  if (summary.r_outer <= summary.r_start) {
    return last_band_reject(RejectReason::kMalformed,
                            "summary r_outer must be after r_start");
  }
  if (summary.carry_width != incoming.carry_width ||
      summary.carry_width != ceil_sqrt(summary.k_sq)) {
    return last_band_reject(RejectReason::kMalformed,
                            "summary carry width does not match k_sq");
  }
  if (!summary.source_mode.empty() &&
      summary.source_mode != incoming.source_mode) {
    return last_band_reject(RejectReason::kMalformed,
                            "summary source_mode does not match incoming");
  }
  if (!summary.source_id.empty() && summary.source_id != incoming.source_id) {
    return last_band_reject(RejectReason::kMalformed,
                            "summary source_id does not match incoming");
  }

  const std::string incoming_validation =
      validate_live_separator(incoming.separator);
  if (!incoming_validation.empty()) {
    return last_band_reject(RejectReason::kMalformed,
                            "incoming live separator " +
                                incoming_validation);
  }

  std::vector<AtomId> ids;
  ids.reserve(incoming.separator.carry_atoms.size() +
              summary.components.size() * 2);
  std::unordered_map<AtomId, std::size_t> index_by_id;
  const auto ensure_index = [&](AtomId id) -> std::size_t {
    const auto found = index_by_id.find(id);
    if (found != index_by_id.end()) {
      return found->second;
    }
    const std::size_t index = ids.size();
    ids.push_back(id);
    index_by_id.emplace(id, index);
    return index;
  };

  for (const CarryAtom& atom : incoming.separator.carry_atoms) {
    if (!stable_atom_id(atom.id)) {
      return last_band_reject(RejectReason::kMalformed,
                              "incoming carry atom has unstable id");
    }
    ensure_index(atom.id);
  }

  std::set<AtomId> summary_atoms;
  for (const LastBandComponentSummaryV1& component : summary.components) {
    const std::string component_validation =
        validate_last_band_component(component);
    if (!component_validation.empty()) {
      return last_band_reject(RejectReason::kMalformed,
                              component_validation);
    }
    for (const AtomId id : component.boundary_atoms) {
      if (!summary_atoms.insert(id).second) {
        return last_band_reject(
            RejectReason::kMalformed,
            "atom appears in multiple last-band components");
      }
      ensure_index(id);
    }
  }
  for (const CarryAtom& atom : incoming.separator.carry_atoms) {
    if (summary_atoms.find(atom.id) == summary_atoms.end()) {
      return last_band_reject(RejectReason::kMalformed,
                              "last-band summary omits incoming carry atom");
    }
  }

  Dsu dsu(ids.size());
  for (const auto& component : incoming.separator.component_partition) {
    const AtomId first_id = component.front();
    const auto first = index_by_id.find(first_id);
    assert(first != index_by_id.end());
    for (std::size_t i = 1; i < component.size(); ++i) {
      const auto it = index_by_id.find(component[i]);
      assert(it != index_by_id.end());
      dsu.unite(first->second, it->second);
    }
  }
  for (const LastBandComponentSummaryV1& component : summary.components) {
    const auto first = index_by_id.find(component.boundary_atoms.front());
    assert(first != index_by_id.end());
    for (std::size_t i = 1; i < component.boundary_atoms.size(); ++i) {
      const auto it = index_by_id.find(component.boundary_atoms[i]);
      assert(it != index_by_id.end());
      dsu.unite(first->second, it->second);
    }
  }

  std::vector<bool> root_is_source(ids.size(), false);
  LastBandSummaryApplyResult result;
  for (std::size_t c = 0; c < incoming.separator.component_partition.size();
       ++c) {
    if (!incoming.separator.source_bit_per_component[c]) {
      continue;
    }
    const AtomId id = incoming.separator.component_partition[c].front();
    const auto it = index_by_id.find(id);
    assert(it != index_by_id.end());
    root_is_source[dsu.find(it->second)] = true;
    result.has_incoming_source = true;
  }

  for (const LastBandComponentSummaryV1& component : summary.components) {
    const auto it = index_by_id.find(component.boundary_atoms.front());
    assert(it != index_by_id.end());
    const std::size_t root = dsu.find(it->second);
    if (!root_is_source[root]) {
      continue;
    }
    if (component.touches_outer_coordinate_carry ||
        component.touches_port_overhang) {
      result.has_source_continuation = true;
    }
    merge_max_atoms(component.max_coordinate_norm_sq,
                    component.max_coordinate_atom_ids,
                    result.max_source_coordinate_norm_sq,
                    result.max_source_coordinate_atom_ids);
    merge_max_atoms(component.max_support_norm_sq,
                    component.max_support_atom_ids,
                    result.max_source_support_norm_sq,
                    result.max_source_support_atom_ids);
    add_bridge_safety(result.source_bridge_safety, component.bridge_safety);
  }

  result.terminal_source_dead =
      result.has_incoming_source && !result.has_source_continuation;
  return result;
}

InventorySummary summarize_inventory(const std::vector<AtomId>& atom_ids) {
  std::vector<AtomId> canonical = atom_ids;
  std::sort(canonical.begin(), canonical.end());
  canonical.erase(std::unique(canonical.begin(), canonical.end()),
                  canonical.end());

  std::ostringstream payload;
  payload << "LB_SOURCE_INVENTORY_V1\n";
  for (const AtomId id : canonical) {
    payload << id << "\n";
  }

  InventorySummary summary;
  summary.count = static_cast<std::uint64_t>(canonical.size());
  summary.digest_algorithm = "sha256:lb_source_inventory_v1";
  summary.digest_hex = campaign::detail::sha256_hex(payload.str());
  for (const AtomId id : canonical) {
    const std::optional<CoordinateAtom> atom = decode_coordinate_atom_id(id);
    if (!atom.has_value()) {
      continue;
    }
    if (summary.max_norm_atom_ids.empty() ||
        atom->norm_sq > summary.max_norm_sq) {
      summary.max_norm_sq = atom->norm_sq;
      summary.max_norm_atom_ids = {id};
    } else if (atom->norm_sq == summary.max_norm_sq) {
      summary.max_norm_atom_ids.push_back(id);
    }
  }
  return summary;
}

std::string source_profile_draft_json(const SourceProfileDraft& profile) {
  std::ostringstream out;
  const InventorySummary inventory_summary =
      summarize_inventory(profile.terminal_source_inventory);
  out << "{\"schema\":\"lb_source_profile_draft_v1\",\"profile_id\":";
  append_json_string(out, profile.profile_id);
  out << ",\"metadata\":";
  append_metadata_json(out, profile.metadata);
  out << ",\"k_sq\":" << profile.carry_manifest.k_sq
      << ",\"outer_radius\":" << profile.carry_manifest.outer_radius
      << ",\"carry_width\":" << profile.carry_manifest.carry_width
      << ",\"reject\":";
  append_json_string(out, reject_reason_name(profile.reject));
  out << ",\"diagnostic\":";
  append_json_string(out, profile.diagnostic);
  out << ",\"terminal_source_dead\":"
      << (profile.terminal_source_dead ? "true" : "false")
      << ",\"terminal_source_inventory_summary\":";
  append_inventory_summary_json(out, inventory_summary);
  out << ",\"terminal_source_inventory\":";
  append_atom_id_array(out, profile.terminal_source_inventory);
  out << ",\"carry_manifest\":";
  append_manifest_json(out, profile.carry_manifest);
  out << '}';
  return out.str();
}

std::string source_certificate_draft_json(
    const SourceCertificateDraft& certificate) {
  std::ostringstream out;
  const InventorySummary inventory_summary =
      summarize_inventory(certificate.terminal_source_inventory);
  out << "{\"schema\":\"lb_source_dead_cert_draft_v1\",\"certificate_id\":";
  append_json_string(out, certificate.certificate_id);
  out << ",\"profile_id\":";
  append_json_string(out, certificate.profile_id);
  out << ",\"metadata\":";
  append_metadata_json(out, certificate.metadata);
  out << ",\"k_sq\":" << certificate.k_sq
      << ",\"terminal_radius\":" << certificate.terminal_radius
      << ",\"negative_guard_pass\":"
      << (certificate.negative_guard_pass ? "true" : "false")
      << ",\"endpoint\":";
  append_path_point_json(out, certificate.endpoint);
  out << ",\"endpoint_atom_id\":" << certificate.endpoint_atom_id;
  out << ",\"source_path_provenance\":\"coordinate_gaussian_prime_path\"";
  out << ",\"source_path\":";
  append_path_json(out, certificate.source_path);
  out
      << ",\"terminal_source_inventory_summary\":";
  append_inventory_summary_json(out, inventory_summary);
  out << ",\"terminal_source_inventory\":";
  append_atom_id_array(out, certificate.terminal_source_inventory);
  out << '}';
  return out.str();
}

namespace {

ProcessResult process_band_impl(const BandInput& band,
                                const std::optional<SeparatorState>& incoming,
                                const ProcessOptions& options,
                                InventoryMode inventory_mode) {
  const std::uint64_t carry_width = ceil_sqrt(band.k_sq);
  const bool collect_inventory = inventory_mode == InventoryMode::kCollect;

  if (band.force_overflow) {
    return reject(RejectReason::kOverflow, "band marked overflow", carry_width);
  }
  if (band.outer_radius > 0 &&
      band.outer_radius >
          std::numeric_limits<std::uint64_t>::max() / band.outer_radius) {
    return reject(RejectReason::kMalformed, "outer radius square overflows",
                  carry_width);
  }

  std::vector<BandAtom> all_atoms = band.atoms;
  std::unordered_map<AtomId, std::size_t> index_by_id;
  index_by_id.reserve(all_atoms.size() + (incoming ? incoming->carry_atoms.size()
                                                   : 0));

  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    if (!index_by_id.emplace(all_atoms[i].id, i).second) {
      return reject(RejectReason::kMalformed, "duplicate band atom id",
                    carry_width);
    }
  }

  if (incoming) {
    std::set<AtomId> incoming_atoms;
    for (const CarryAtom& atom : incoming->carry_atoms) {
      if (!incoming_atoms.insert(atom.id).second) {
        return reject(RejectReason::kMalformed, "duplicate incoming carry atom",
                      carry_width);
      }
      const auto existing = index_by_id.find(atom.id);
      if (existing == index_by_id.end()) {
        const std::size_t idx = all_atoms.size();
        all_atoms.push_back(
            {atom.id, atom.norm_sq, false,
             decode_port_atom_id(atom.id).has_value()});
        index_by_id.emplace(atom.id, idx);
      } else if (all_atoms[existing->second].norm_sq != atom.norm_sq) {
        return reject(RejectReason::kMalformed,
                      "incoming carry atom norm does not match band atom",
                      carry_width);
      }
    }
    if (incoming->component_partition.size() !=
        incoming->source_bit_per_component.size()) {
      return reject(RejectReason::kMalformed,
                    "incoming source-bit count does not match partition",
                    carry_width);
    }
    if (!incoming->component_inventory.empty() &&
        incoming->component_inventory.size() !=
            incoming->component_partition.size()) {
      return reject(RejectReason::kMalformed,
                    "incoming inventory count does not match partition",
                    carry_width);
    }
    std::set<AtomId> partition_atoms;
    std::set<AtomId> inventory_atoms;
    for (const auto& component : incoming->component_partition) {
      if (component.empty()) {
        return reject(RejectReason::kMalformed,
                      "incoming separator contains empty component",
                      carry_width);
      }
      for (const AtomId id : component) {
        if (incoming_atoms.find(id) == incoming_atoms.end()) {
          return reject(RejectReason::kMalformed,
                        "incoming partition references non-carry atom",
                        carry_width);
        }
        if (!partition_atoms.insert(id).second) {
          return reject(RejectReason::kMalformed,
                        "incoming atom appears in multiple components",
                        carry_width);
        }
      }
    }
    for (std::size_t c = 0; c < incoming->component_inventory.size(); ++c) {
      const auto& inventory = incoming->component_inventory[c];
      if (inventory.empty()) {
        return reject(RejectReason::kMalformed,
                      "incoming separator contains empty inventory",
                      carry_width);
      }
      for (const AtomId id : incoming->component_partition[c]) {
        if (std::find(inventory.begin(), inventory.end(), id) ==
            inventory.end()) {
          return reject(RejectReason::kMalformed,
                        "incoming inventory omits carry atom", carry_width);
        }
      }
      for (const AtomId id : inventory) {
        if (!inventory_atoms.insert(id).second) {
          return reject(RejectReason::kMalformed,
                        "incoming atom appears in multiple inventories",
                        carry_width);
        }
      }
    }
    if (partition_atoms != incoming_atoms) {
      return reject(RejectReason::kMalformed,
                    "incoming partition does not cover all carry atoms",
                    carry_width);
    }
  }

  if (all_atoms.size() > options.max_atoms) {
    return reject(RejectReason::kOverflow, "atom count exceeds source cap",
                  carry_width);
  }

  Dsu dsu(all_atoms.size());
  if (incoming) {
    for (const auto& component : incoming->component_partition) {
      const AtomId first_id = component.front();
      const auto first = index_by_id.find(first_id);
      assert(first != index_by_id.end());
      for (std::size_t i = 1; i < component.size(); ++i) {
        const auto it = index_by_id.find(component[i]);
        assert(it != index_by_id.end());
        dsu.unite(first->second, it->second);
      }
    }
  }

  for (const auto& [a, b] : band.edges) {
    const auto ia = index_by_id.find(a);
    const auto ib = index_by_id.find(b);
    if (ia == index_by_id.end() || ib == index_by_id.end()) {
      return reject(RejectReason::kMalformed, "edge references missing atom",
                    carry_width);
    }
    dsu.unite(ia->second, ib->second);
  }

  std::vector<bool> root_is_source(all_atoms.size(), false);
  bool any_source = false;
  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    if (all_atoms[i].certified_source) {
      root_is_source[dsu.find(i)] = true;
      any_source = true;
    }
  }
  if (incoming) {
    for (std::size_t c = 0; c < incoming->component_partition.size(); ++c) {
      if (!incoming->source_bit_per_component[c]) {
        continue;
      }
      const AtomId id = incoming->component_partition[c].front();
      const auto it = index_by_id.find(id);
      assert(it != index_by_id.end());
      root_is_source[dsu.find(it->second)] = true;
      any_source = true;
    }
  }

  std::map<std::size_t, std::vector<AtomId>> carry_by_root;
  std::map<std::size_t, std::vector<AtomId>> inventory_by_root;
  std::unordered_map<AtomId, std::uint64_t> norm_by_id;
  norm_by_id.reserve(all_atoms.size());

  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    norm_by_id.emplace(all_atoms[i].id, all_atoms[i].norm_sq);
    const std::size_t root = dsu.find(i);
    if (collect_inventory && i < band.atoms.size()) {
      inventory_by_root[root].push_back(all_atoms[i].id);
    }
    if (in_final_carry_window(
            all_atoms[i].norm_sq, band.outer_radius, carry_width,
            all_atoms[i].allow_outer_overshoot_carry)) {
      carry_by_root[root].push_back(all_atoms[i].id);
    }
  }
  if (collect_inventory) {
    for (auto& [root, inventory] : inventory_by_root) {
      (void)root;
      sort_unique_atom_ids(inventory);
    }
  }
  if (collect_inventory && incoming) {
    for (std::size_t c = 0; c < incoming->component_partition.size(); ++c) {
      const AtomId id = incoming->component_partition[c].front();
      const auto it = index_by_id.find(id);
      assert(it != index_by_id.end());
      std::vector<AtomId>& payload = inventory_by_root[dsu.find(it->second)];
      const std::vector<AtomId>& inventory =
          incoming->component_inventory.empty()
              ? incoming->component_partition[c]
              : incoming->component_inventory[c];
      merge_inventory_payload(payload, inventory);
    }
  }

  ProcessResult result;
  result.carry_width = carry_width;
  result.outgoing.component_partition.reserve(carry_by_root.size());
  result.outgoing.source_bit_per_component.reserve(carry_by_root.size());

  for (auto& [root, ids] : carry_by_root) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.empty()) {
      continue;
    }
    result.outgoing.component_partition.push_back(ids);
    result.outgoing.source_bit_per_component.push_back(root_is_source[root]);
    if (collect_inventory) {
      result.outgoing.component_inventory.push_back(inventory_by_root[root]);
    }
    for (const AtomId id : ids) {
      result.outgoing.carry_atoms.push_back({id, norm_by_id.at(id)});
    }
  }

  if (collect_inventory) {
    result.outgoing = canonicalize_separator(result.outgoing);
  } else {
    result.outgoing =
        separator_from_live_separator(live_separator_from_separator(
            result.outgoing));
  }
  if (result.outgoing.carry_atoms.size() > options.max_carry_atoms ||
      result.outgoing.component_partition.size() > options.max_components) {
    return reject(RejectReason::kOverflow,
                  "separator state exceeds source caps", carry_width);
  }
  if (collect_inventory &&
      inventory_payload_exceeds_cap(inventory_by_root,
                                    options.max_inventory_atoms)) {
    return reject(RejectReason::kOverflow,
                  "component inventory exceeds source cap", carry_width);
  }

  const bool has_source_carry =
      std::find(result.outgoing.source_bit_per_component.begin(),
                result.outgoing.source_bit_per_component.end(),
                true) != result.outgoing.source_bit_per_component.end();
  result.terminal_source_dead = any_source && !has_source_carry;
  if (collect_inventory && result.terminal_source_dead) {
    std::vector<AtomId> source_inventory;
    for (const auto& [root, inventory] : inventory_by_root) {
      if (root_is_source[root]) {
        if (inventory.size() > options.max_inventory_atoms ||
            source_inventory.size() >
                options.max_inventory_atoms - inventory.size()) {
          return reject(RejectReason::kOverflow,
                        "terminal source inventory exceeds source cap",
                        carry_width);
        }
        source_inventory.insert(source_inventory.end(), inventory.begin(),
                                inventory.end());
      }
    }
    std::sort(source_inventory.begin(), source_inventory.end());
    source_inventory.erase(
        std::unique(source_inventory.begin(), source_inventory.end()),
        source_inventory.end());
    result.terminal_source_inventory = std::move(source_inventory);
  }
  return result;
}

}  // namespace

ProcessResult process_band(const BandInput& band,
                           const std::optional<SeparatorState>& incoming,
                           const ProcessOptions& options) {
  return process_band_impl(band, incoming, options, InventoryMode::kCollect);
}

LiveProcessResult process_band_live(
    const BandInput& band,
    const std::optional<LiveSeparator>& incoming,
    const ProcessOptions& options) {
  const std::uint64_t carry_width = ceil_sqrt(band.k_sq);
  std::optional<SeparatorState> legacy_incoming;

  const std::string band_atom_validation =
      validate_band_atom_identities(band);
  if (!band_atom_validation.empty()) {
    return live_reject(RejectReason::kMalformed, band_atom_validation,
                       carry_width);
  }

  if (incoming) {
    for (const BandAtom& atom : band.atoms) {
      if (atom.certified_source) {
        return live_reject(
            RejectReason::kMalformed,
            "fresh certified source is not allowed with incoming live handoff",
            carry_width);
      }
    }

    const std::string validation = validate_live_separator(*incoming);
    if (!validation.empty()) {
      return live_reject(RejectReason::kMalformed,
                         "incoming live separator " + validation,
                         carry_width);
    }
    const std::string atom_identity_validation =
        validate_live_separator_atom_identities(*incoming);
    if (!atom_identity_validation.empty()) {
      return live_reject(RejectReason::kMalformed,
                         "incoming live separator " +
                             atom_identity_validation,
                         carry_width);
    }
    const LiveSeparator canonical = canonicalize_live_separator(*incoming);
    legacy_incoming = separator_from_live_separator(canonical);
  }

  const ProcessResult legacy =
      process_band_impl(band, legacy_incoming, options,
                        InventoryMode::kFrontierOnly);

  LiveProcessResult result;
  result.reject = legacy.reject;
  result.diagnostic = legacy.diagnostic;
  result.carry_width = legacy.carry_width;
  result.terminal_source_dead = legacy.terminal_source_dead;
  if (!legacy.accepted()) {
    return result;
  }
  result.outgoing = live_separator_from_separator(legacy.outgoing);
  return result;
}

ProcessResult process_bands(const std::vector<BandInput>& bands,
                            const std::optional<SeparatorState>& incoming,
                            const ProcessOptions& options) {
  std::optional<SeparatorState> state = incoming;
  ProcessResult last;
  for (const BandInput& band : bands) {
    last = process_band(band, state, options);
    if (!last.accepted()) {
      return last;
    }
    if (last.terminal_source_dead) {
      return last;
    }
    state = last.outgoing;
  }
  return last;
}

std::string reject_reason_name(RejectReason reason) {
  switch (reason) {
    case RejectReason::kNone:
      return "none";
    case RejectReason::kOverflow:
      return "overflow";
    case RejectReason::kMalformed:
      return "malformed";
  }
  return "unknown";
}

}  // namespace lb_source
