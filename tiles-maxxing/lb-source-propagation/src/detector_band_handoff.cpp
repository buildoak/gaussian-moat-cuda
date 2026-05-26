#include "lb_source/detector_band_handoff.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "sha256.h"

namespace lb_source {
namespace {

constexpr std::uint32_t kWireVersion = 1;
constexpr std::uint64_t kMaxRemainingDivisor = 17;

bool valid_manifest_token(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (ch >= 0x80 || ch < 0x20 || std::isspace(ch) != 0) {
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

bool valid_wire_token(std::string_view value) {
  return valid_manifest_token(value) &&
         value.size() <= std::numeric_limits<std::uint32_t>::max();
}

template <class T>
std::string check_expected(const std::optional<T>& expected, const T& actual,
                           std::string_view diagnostic) {
  if (expected && actual != *expected) {
    return std::string(diagnostic);
  }
  return "";
}

std::uint64_t atom_id_to_wire(AtomId value) {
  if (value >= 0) {
    return static_cast<std::uint64_t>(value);
  }
  if (value == std::numeric_limits<AtomId>::min()) {
    return 1ULL << 63;
  }
  const auto magnitude = static_cast<std::uint64_t>(-value);
  return ~magnitude + 1;
}

AtomId atom_id_from_wire(std::uint64_t raw) {
  if (raw <= static_cast<std::uint64_t>(
                 std::numeric_limits<AtomId>::max())) {
    return static_cast<AtomId>(raw);
  }
  const std::uint64_t magnitude = ~raw + 1;
  if (magnitude == (1ULL << 63)) {
    return std::numeric_limits<AtomId>::min();
  }
  return -static_cast<AtomId>(magnitude);
}

void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
  out.push_back(value);
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xffU));
  }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xffU));
  }
}

void append_i64(std::vector<std::uint8_t>& out, AtomId value) {
  append_u64(out, atom_id_to_wire(value));
}

void append_token(std::vector<std::uint8_t>& out, std::string_view value) {
  append_u32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}

class Reader {
 public:
  Reader(const std::vector<std::uint8_t>& bytes,
         const DetectorBandHandoffLimits& limits)
      : bytes_(bytes), limits_(limits) {}

  bool read_u8(std::uint8_t& value) {
    if (remaining() < 1) {
      return fail("truncated blob");
    }
    value = bytes_[offset_++];
    return true;
  }

  bool read_u32(std::uint32_t& value) {
    if (remaining() < 4) {
      return fail("truncated blob");
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(bytes_[offset_++]) << (8 * i);
    }
    return true;
  }

  bool read_u64(std::uint64_t& value) {
    if (remaining() < 8) {
      return fail("truncated blob");
    }
    value = 0;
    for (int i = 0; i < 8; ++i) {
      value |= static_cast<std::uint64_t>(bytes_[offset_++]) << (8 * i);
    }
    return true;
  }

  bool read_i64(AtomId& value) {
    std::uint64_t raw = 0;
    if (!read_u64(raw)) {
      return false;
    }
    value = atom_id_from_wire(raw);
    return true;
  }

  bool read_token(std::string& value) {
    std::uint32_t size = 0;
    if (!read_u32(size)) {
      return false;
    }
    if (size > limits_.max_token_bytes) {
      return fail("manifest token too large");
    }
    if (remaining() < size) {
      return fail("truncated blob");
    }
    value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
    offset_ += size;
    return true;
  }

  bool consume_magic() {
    const std::string_view magic = kDetectorBandHandoffSchema;
    if (remaining() < magic.size()) {
      return fail("truncated blob");
    }
    const std::string_view actual(
        reinterpret_cast<const char*>(bytes_.data() + offset_),
        magic.size());
    if (actual != magic) {
      return fail("wrong detector handoff magic");
    }
    offset_ += magic.size();
    return true;
  }

  bool finish() {
    if (offset_ != bytes_.size()) {
      return fail("trailing bytes");
    }
    return true;
  }

  const std::string& diagnostic() const { return diagnostic_; }

 private:
  std::uint64_t remaining() const {
    return static_cast<std::uint64_t>(bytes_.size() - offset_);
  }

  bool fail(std::string diagnostic) {
    diagnostic_ = std::move(diagnostic);
    return false;
  }

  const std::vector<std::uint8_t>& bytes_;
  const DetectorBandHandoffLimits& limits_;
  std::size_t offset_ = 0;
  std::string diagnostic_;
};

bool remaining_records_possible(std::uint64_t carry_count,
                                std::uint64_t component_count,
                                std::uint64_t component_atom_count,
                                std::uint64_t remaining) {
  const unsigned __int128 minimum =
      static_cast<unsigned __int128>(carry_count) * 16 +
      static_cast<unsigned __int128>(component_count) * 17 +
      static_cast<unsigned __int128>(component_atom_count) * 8;
  return minimum <= remaining &&
         minimum <= std::numeric_limits<std::uint64_t>::max();
}

std::string check_limits(std::uint64_t carry_count,
                         std::uint64_t component_count,
                         std::uint64_t component_atom_count,
                         const DetectorBandHandoffLimits& limits,
                         std::uint64_t remaining) {
  if (carry_count > limits.max_carry_atoms ||
      component_count > limits.max_components ||
      component_atom_count > limits.max_component_atom_entries) {
    return "handoff counts exceed limits";
  }
  if (!remaining_records_possible(carry_count, component_count,
                                  component_atom_count, remaining)) {
    return "handoff counts overflow remaining bytes";
  }
  if (remaining / kMaxRemainingDivisor < component_count &&
      component_count != 0) {
    return "component count overflows record allocation";
  }
  return "";
}

std::vector<AtomId> component_atom_table(
    const StaticReachSeparator& separator) {
  std::vector<AtomId> table;
  for (const auto& component : separator.component_partition) {
    table.insert(table.end(), component.begin(), component.end());
  }
  return table;
}

}  // namespace

DetectorBandHandoffV1 canonicalize_detector_band_handoff(
    const DetectorBandHandoffV1& handoff) {
  DetectorBandHandoffV1 out = handoff;
  out.separator = canonicalize_static_reach_separator(handoff.separator);
  return out;
}

std::string validate_detector_band_handoff(
    const DetectorBandHandoffV1& handoff,
    const DetectorBandHandoffExpectedContext& expected) {
  if (handoff.k_sq == 0) {
    return "missing or invalid k_sq";
  }
  if (handoff.cut_radius == 0) {
    return "missing or invalid cut_radius";
  }
  if (handoff.carry_width != ceil_sqrt(handoff.k_sq)) {
    return "carry_width does not match k_sq";
  }
  if (!valid_wire_token(handoff.schedule_digest_algorithm)) {
    return "missing or invalid schedule_digest_algorithm";
  }
  if (!valid_hex_token(handoff.schedule_digest_hex) ||
      handoff.schedule_digest_hex.size() >
          std::numeric_limits<std::uint32_t>::max()) {
    return "missing or invalid schedule_digest_hex";
  }
  if (!valid_wire_token(handoff.geometry_id)) {
    return "missing or invalid geometry_id";
  }
  if (!valid_wire_token(handoff.oracle_id)) {
    return "missing or invalid oracle_id";
  }
  if (!valid_wire_token(handoff.build_id)) {
    return "missing or invalid build_id";
  }
  if (!valid_wire_token(handoff.support_envelope_id)) {
    return "missing or invalid support_envelope_id";
  }
  if (!valid_wire_token(handoff.port_identity_scheme_id)) {
    return "missing or invalid port_identity_scheme_id";
  }
  if (!valid_wire_token(handoff.boundary_policy_id)) {
    return "missing or invalid boundary_policy_id";
  }
  if (const std::string diagnostic =
          validate_static_reach_separator(handoff.separator);
      !diagnostic.empty()) {
    return "detector separator " + diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.k_sq, handoff.k_sq, "wrong k_sq");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic = check_expected(
          expected.cut_radius, handoff.cut_radius, "wrong cut_radius");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic = check_expected(
          expected.carry_width, handoff.carry_width, "wrong carry_width");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.schedule_index, handoff.schedule_index,
                         "wrong schedule_index");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.schedule_digest_algorithm,
                         handoff.schedule_digest_algorithm,
                         "wrong schedule_digest_algorithm");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.schedule_digest_hex,
                         handoff.schedule_digest_hex,
                         "wrong schedule_digest_hex");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic = check_expected(
          expected.geometry_id, handoff.geometry_id, "wrong geometry_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.oracle_id, handoff.oracle_id,
                         "wrong oracle_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.build_id, handoff.build_id,
                         "wrong build_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.support_envelope_id,
                         handoff.support_envelope_id,
                         "wrong support_envelope_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.port_identity_scheme_id,
                         handoff.port_identity_scheme_id,
                         "wrong port_identity_scheme_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  if (const std::string diagnostic =
          check_expected(expected.boundary_policy_id,
                         handoff.boundary_policy_id,
                         "wrong boundary_policy_id");
      !diagnostic.empty()) {
    return diagnostic;
  }
  return "";
}

DetectorBandHandoffBytesResult detector_band_handoff_to_bytes(
    const DetectorBandHandoffV1& handoff) {
  DetectorBandHandoffBytesResult result;
  const DetectorBandHandoffV1 canonical =
      canonicalize_detector_band_handoff(handoff);
  if (const std::string diagnostic =
          validate_detector_band_handoff(canonical);
      !diagnostic.empty()) {
    result.diagnostic = diagnostic;
    return result;
  }

  const std::vector<AtomId> atom_table =
      component_atom_table(canonical.separator);
  result.bytes.insert(result.bytes.end(), kDetectorBandHandoffSchema,
                      kDetectorBandHandoffSchema +
                          std::string_view(kDetectorBandHandoffSchema).size());
  append_u32(result.bytes, kWireVersion);
  append_u64(result.bytes, canonical.k_sq);
  append_u64(result.bytes, canonical.cut_radius);
  append_u64(result.bytes, canonical.carry_width);
  append_u64(result.bytes, canonical.schedule_index);
  append_token(result.bytes, canonical.schedule_digest_algorithm);
  append_token(result.bytes, canonical.schedule_digest_hex);
  append_token(result.bytes, canonical.geometry_id);
  append_token(result.bytes, canonical.oracle_id);
  append_token(result.bytes, canonical.build_id);
  append_token(result.bytes, canonical.support_envelope_id);
  append_token(result.bytes, canonical.port_identity_scheme_id);
  append_token(result.bytes, canonical.boundary_policy_id);
  append_u64(result.bytes, canonical.separator.carry_atoms.size());
  append_u64(result.bytes, canonical.separator.component_partition.size());
  append_u64(result.bytes, atom_table.size());

  for (const CarryAtom& atom : canonical.separator.carry_atoms) {
    append_i64(result.bytes, atom.id);
    append_u64(result.bytes, atom.norm_sq);
  }
  std::uint64_t first = 0;
  for (std::size_t i = 0; i < canonical.separator.component_partition.size();
       ++i) {
    const auto& component = canonical.separator.component_partition[i];
    append_u64(result.bytes, first);
    append_u64(result.bytes, component.size());
    append_u8(result.bytes, canonical.separator.reach_per_component[i]);
    first += component.size();
  }
  for (const AtomId atom_id : atom_table) {
    append_i64(result.bytes, atom_id);
  }
  return result;
}

DetectorBandHandoffReadResult detector_band_handoff_from_bytes(
    const std::vector<std::uint8_t>& bytes,
    const DetectorBandHandoffExpectedContext& expected,
    const DetectorBandHandoffLimits& limits) {
  DetectorBandHandoffReadResult result;
  Reader reader(bytes, limits);
  std::uint32_t version = 0;
  if (!reader.consume_magic() || !reader.read_u32(version)) {
    result.diagnostic = reader.diagnostic();
    return result;
  }
  if (version != kWireVersion) {
    result.diagnostic = "wrong detector handoff version";
    return result;
  }

  DetectorBandHandoffV1 handoff;
  if (!reader.read_u64(handoff.k_sq) ||
      !reader.read_u64(handoff.cut_radius) ||
      !reader.read_u64(handoff.carry_width) ||
      !reader.read_u64(handoff.schedule_index) ||
      !reader.read_token(handoff.schedule_digest_algorithm) ||
      !reader.read_token(handoff.schedule_digest_hex) ||
      !reader.read_token(handoff.geometry_id) ||
      !reader.read_token(handoff.oracle_id) ||
      !reader.read_token(handoff.build_id) ||
      !reader.read_token(handoff.support_envelope_id) ||
      !reader.read_token(handoff.port_identity_scheme_id) ||
      !reader.read_token(handoff.boundary_policy_id)) {
    result.diagnostic = reader.diagnostic();
    return result;
  }

  std::uint64_t carry_count = 0;
  std::uint64_t component_count = 0;
  std::uint64_t component_atom_count = 0;
  if (!reader.read_u64(carry_count) || !reader.read_u64(component_count) ||
      !reader.read_u64(component_atom_count)) {
    result.diagnostic = reader.diagnostic();
    return result;
  }
  if (const std::string diagnostic =
          check_limits(carry_count, component_count, component_atom_count,
                       limits, bytes.size());
      !diagnostic.empty()) {
    result.diagnostic = diagnostic;
    return result;
  }

  handoff.separator.carry_atoms.reserve(static_cast<std::size_t>(carry_count));
  for (std::uint64_t i = 0; i < carry_count; ++i) {
    CarryAtom atom;
    if (!reader.read_i64(atom.id) || !reader.read_u64(atom.norm_sq)) {
      result.diagnostic = reader.diagnostic();
      return result;
    }
    handoff.separator.carry_atoms.push_back(atom);
  }

  struct ComponentWire {
    std::uint64_t first = 0;
    std::uint64_t count = 0;
    std::uint8_t reach = 0;
  };
  std::vector<ComponentWire> components;
  components.reserve(static_cast<std::size_t>(component_count));
  for (std::uint64_t i = 0; i < component_count; ++i) {
    ComponentWire component;
    if (!reader.read_u64(component.first) ||
        !reader.read_u64(component.count) || !reader.read_u8(component.reach)) {
      result.diagnostic = reader.diagnostic();
      return result;
    }
    if (component.count == 0 ||
        component.first > component_atom_count ||
        component.count > component_atom_count - component.first) {
      result.diagnostic = "invalid component atom range";
      return result;
    }
    components.push_back(component);
  }

  std::vector<AtomId> atom_table;
  atom_table.reserve(static_cast<std::size_t>(component_atom_count));
  for (std::uint64_t i = 0; i < component_atom_count; ++i) {
    AtomId id = 0;
    if (!reader.read_i64(id)) {
      result.diagnostic = reader.diagnostic();
      return result;
    }
    atom_table.push_back(id);
  }
  if (!reader.finish()) {
    result.diagnostic = reader.diagnostic();
    return result;
  }

  handoff.separator.component_partition.reserve(
      static_cast<std::size_t>(component_count));
  handoff.separator.reach_per_component.reserve(
      static_cast<std::size_t>(component_count));
  for (const ComponentWire& component : components) {
    const auto first = static_cast<std::size_t>(component.first);
    const auto count = static_cast<std::size_t>(component.count);
    handoff.separator.component_partition.emplace_back(
        atom_table.begin() + first, atom_table.begin() + first + count);
    handoff.separator.reach_per_component.push_back(component.reach);
  }

  if (const std::string diagnostic =
          validate_detector_band_handoff(handoff, expected);
      !diagnostic.empty()) {
    result.diagnostic = diagnostic;
    return result;
  }
  result.handoff = handoff;
  return result;
}

std::string detector_band_handoff_sha256_hex(
    const DetectorBandHandoffV1& handoff) {
  const DetectorBandHandoffBytesResult encoded =
      detector_band_handoff_to_bytes(handoff);
  if (!encoded.accepted()) {
    return "";
  }
  return campaign::detail::sha256_hex(encoded.bytes.data(),
                                      encoded.bytes.size());
}

}  // namespace lb_source
