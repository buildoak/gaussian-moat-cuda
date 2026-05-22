#include "lb_source/source_propagation.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <iomanip>
#include <istream>
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

bool lexicographic_component_less(const std::vector<AtomId>& a,
                                  const std::vector<AtomId>& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

bool in_final_carry_window(std::uint64_t norm_sq, std::uint64_t outer_radius,
                           std::uint64_t carry_width) {
  if (norm_sq > outer_radius * outer_radius) {
    return false;
  }
  const std::uint64_t inner_radius =
      outer_radius > carry_width ? outer_radius - carry_width : 0;
  return norm_sq >= inner_radius * inner_radius;
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
  std::vector<std::string> tokens;
  std::string token;
  while (in >> token) {
    tokens.push_back(token);
  }

  std::size_t cursor = 0;
  const auto fail = [&](std::string diagnostic) {
    result = {};
    result.diagnostic = std::move(diagnostic);
    return result;
  };
  const auto next = [&]() -> const std::string* {
    if (cursor >= tokens.size()) {
      return nullptr;
    }
    return &tokens[cursor++];
  };
  const auto expect = [&](std::string_view expected) -> bool {
    const std::string* actual = next();
    return actual != nullptr && *actual == expected;
  };
  const auto read_uint64 = [&](std::uint64_t& value) -> bool {
    const std::string* actual = next();
    return actual != nullptr && parse_uint64_token(*actual, value);
  };
  const auto read_int64 = [&](std::int64_t& value) -> bool {
    const std::string* actual = next();
    return actual != nullptr && parse_int64_token(*actual, value);
  };
  const auto read_size = [&](std::size_t& value) -> bool {
    const std::string* actual = next();
    return actual != nullptr && parse_size_token(*actual, value);
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
  if (cursor != tokens.size()) {
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

ProcessResult process_band(const BandInput& band,
                           const std::optional<SeparatorState>& incoming,
                           const ProcessOptions& options) {
  const std::uint64_t carry_width = ceil_sqrt(band.k_sq);

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
        all_atoms.push_back({atom.id, atom.norm_sq, false});
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
    inventory_by_root[root].push_back(all_atoms[i].id);
    if (in_final_carry_window(all_atoms[i].norm_sq, band.outer_radius,
                              carry_width)) {
      carry_by_root[root].push_back(all_atoms[i].id);
    }
  }
  if (incoming) {
    for (std::size_t c = 0; c < incoming->component_partition.size(); ++c) {
      const AtomId id = incoming->component_partition[c].front();
      const auto it = index_by_id.find(id);
      assert(it != index_by_id.end());
      std::vector<AtomId>& payload = inventory_by_root[dsu.find(it->second)];
      const std::vector<AtomId>& inventory =
          incoming->component_inventory.empty()
              ? incoming->component_partition[c]
              : incoming->component_inventory[c];
      payload.insert(payload.end(), inventory.begin(), inventory.end());
    }
  }
  for (auto& [root, inventory] : inventory_by_root) {
    std::sort(inventory.begin(), inventory.end());
    inventory.erase(std::unique(inventory.begin(), inventory.end()),
                    inventory.end());
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
    result.outgoing.component_inventory.push_back(inventory_by_root[root]);
    for (const AtomId id : ids) {
      result.outgoing.carry_atoms.push_back({id, norm_by_id.at(id)});
    }
  }

  result.outgoing = canonicalize_separator(result.outgoing);
  if (result.outgoing.carry_atoms.size() > options.max_carry_atoms ||
      result.outgoing.component_partition.size() > options.max_components) {
    return reject(RejectReason::kOverflow,
                  "separator state exceeds source caps", carry_width);
  }

  const bool has_source_carry =
      std::find(result.outgoing.source_bit_per_component.begin(),
                result.outgoing.source_bit_per_component.end(),
                true) != result.outgoing.source_bit_per_component.end();
  result.terminal_source_dead = any_source && !has_source_carry;
  if (result.terminal_source_dead) {
    std::vector<AtomId> source_inventory;
    for (const auto& [root, inventory] : inventory_by_root) {
      if (root_is_source[root]) {
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
