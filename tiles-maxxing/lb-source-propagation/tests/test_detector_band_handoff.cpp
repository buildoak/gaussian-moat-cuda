#include "lb_source/detector_band_handoff.h"
#include "lb_source/stream_checkpoint.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK_TRUE(expr)                                                    \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::cerr << __FILE__ << ":" << __LINE__ << " check failed: " #expr  \
                << "\n";                                                   \
      ++g_failures;                                                         \
      return;                                                               \
    }                                                                       \
  } while (false)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    const auto& actual_value = (a);                                           \
    const auto& expected_value = (b);                                         \
    if (!(actual_value == expected_value)) {                                  \
      std::cerr << __FILE__ << ":" << __LINE__ << " check failed: " #a       \
                << " == " #b << "\n";                                       \
      ++g_failures;                                                          \
      return;                                                                \
    }                                                                        \
  } while (false)

lb_source::AtomId coordinate_id(std::int64_t a, std::int64_t b) {
  const auto id = lb_source::coordinate_atom_id(a, b);
  if (!id.has_value()) {
    std::cerr << "test coordinate atom id overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return *id;
}

lb_source::AtomId port_id(std::int64_t tile_i, std::int64_t tile_j,
                          std::uint64_t face, std::uint64_t ordinal) {
  const auto id = lb_source::port_atom_id(tile_i, tile_j, face, ordinal);
  if (!id.has_value()) {
    std::cerr << "test port atom id overflow\n";
    std::exit(EXIT_FAILURE);
  }
  return *id;
}

lb_source::DetectorBandHandoffV1 handoff_fixture() {
  const lb_source::AtomId a = coordinate_id(3, 4);
  const lb_source::AtomId b = coordinate_id(6, 8);
  const lb_source::AtomId p = port_id(1, 2, 3, 4);

  lb_source::DetectorBandHandoffV1 handoff;
  handoff.k_sq = 36;
  handoff.cut_radius = 1000;
  handoff.carry_width = 6;
  handoff.schedule_index = 7;
  handoff.schedule_digest_algorithm = "sha256:detector_schedule_v1";
  handoff.schedule_digest_hex =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  handoff.geometry_id = "gaussian_octant_tileop_v1";
  handoff.oracle_id = "tileop_static_reach_v1";
  handoff.build_id = "local-test-build";
  handoff.support_envelope_id = "tileop_support_envelope_v1";
  handoff.port_identity_scheme_id = "tile-local-port-v1";
  handoff.boundary_policy_id = "closed-tile-boundary-v1";
  handoff.separator.carry_atoms = {{p, 998001}, {a, 25}, {b, 100}};
  handoff.separator.component_partition = {{b, a}, {p}};
  handoff.separator.reach_per_component = {lb_source::kStaticReachInner,
                                           lb_source::kStaticReachOuter};
  return handoff;
}

lb_source::DetectorBandHandoffExpectedContext expected_context() {
  const lb_source::DetectorBandHandoffV1 handoff = handoff_fixture();
  lb_source::DetectorBandHandoffExpectedContext expected;
  expected.k_sq = handoff.k_sq;
  expected.cut_radius = handoff.cut_radius;
  expected.carry_width = handoff.carry_width;
  expected.schedule_index = handoff.schedule_index;
  expected.schedule_digest_algorithm = handoff.schedule_digest_algorithm;
  expected.schedule_digest_hex = handoff.schedule_digest_hex;
  expected.geometry_id = handoff.geometry_id;
  expected.oracle_id = handoff.oracle_id;
  expected.build_id = handoff.build_id;
  expected.support_envelope_id = handoff.support_envelope_id;
  expected.port_identity_scheme_id = handoff.port_identity_scheme_id;
  expected.boundary_policy_id = handoff.boundary_policy_id;
  return expected;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    bytes[offset + i] =
        static_cast<std::uint8_t>((value >> (8 * i)) & 0xffU);
  }
}

std::size_t count_offset(const std::vector<std::uint8_t>& bytes) {
  std::size_t offset = std::string(lb_source::kDetectorBandHandoffSchema).size();
  offset += 4;
  offset += 4 * 8;
  for (int i = 0; i < 8; ++i) {
    const std::uint32_t size = read_u32(bytes, offset);
    offset += 4 + size;
  }
  return offset;
}

void expect_decode_rejects(std::vector<std::uint8_t> bytes,
                           const std::string& diagnostic) {
  const lb_source::DetectorBandHandoffReadResult decoded =
      lb_source::detector_band_handoff_from_bytes(bytes, expected_context());
  CHECK_TRUE(!decoded.accepted());
  CHECK_EQ(decoded.diagnostic, diagnostic);
  CHECK_EQ(decoded.handoff, lb_source::DetectorBandHandoffV1{});
}

void test_canonical_round_trip_and_hash() {
  const lb_source::DetectorBandHandoffV1 original = handoff_fixture();
  const lb_source::DetectorBandHandoffBytesResult encoded =
      lb_source::detector_band_handoff_to_bytes(original);
  CHECK_TRUE(encoded.accepted());
  CHECK_TRUE(!encoded.bytes.empty());

  const lb_source::DetectorBandHandoffReadResult decoded =
      lb_source::detector_band_handoff_from_bytes(encoded.bytes,
                                                  expected_context());
  CHECK_TRUE(decoded.accepted());
  CHECK_EQ(decoded.handoff,
           lb_source::canonicalize_detector_band_handoff(original));

  const std::string hash_a =
      lb_source::detector_band_handoff_sha256_hex(original);
  const std::string hash_b =
      lb_source::detector_band_handoff_sha256_hex(decoded.handoff);
  CHECK_EQ(hash_a.size(), static_cast<std::size_t>(64));
  CHECK_EQ(hash_a, hash_b);

  const lb_source::StreamCheckpointReadResult source_decode =
      lb_source::stream_checkpoint_from_string(std::string(
          reinterpret_cast<const char*>(encoded.bytes.data()),
          encoded.bytes.size()));
  CHECK_TRUE(!source_decode.accepted());
}

void test_reordered_equivalent_separator_hashes_identically() {
  lb_source::DetectorBandHandoffV1 a = handoff_fixture();
  lb_source::DetectorBandHandoffV1 b = handoff_fixture();
  b.separator.carry_atoms = {{coordinate_id(6, 8), 100},
                             {port_id(1, 2, 3, 4), 998001},
                             {coordinate_id(3, 4), 25}};
  b.separator.component_partition = {{port_id(1, 2, 3, 4)},
                                     {coordinate_id(3, 4),
                                      coordinate_id(6, 8)}};
  b.separator.reach_per_component = {lb_source::kStaticReachOuter,
                                     lb_source::kStaticReachInner};

  const lb_source::DetectorBandHandoffBytesResult encoded_a =
      lb_source::detector_band_handoff_to_bytes(a);
  const lb_source::DetectorBandHandoffBytesResult encoded_b =
      lb_source::detector_band_handoff_to_bytes(b);
  CHECK_TRUE(encoded_a.accepted());
  CHECK_TRUE(encoded_b.accepted());
  CHECK_EQ(encoded_a.bytes, encoded_b.bytes);
  CHECK_EQ(lb_source::detector_band_handoff_sha256_hex(a),
           lb_source::detector_band_handoff_sha256_hex(b));
}

void test_rejects_expected_context_mismatch() {
  const lb_source::DetectorBandHandoffBytesResult encoded =
      lb_source::detector_band_handoff_to_bytes(handoff_fixture());
  CHECK_TRUE(encoded.accepted());

  lb_source::DetectorBandHandoffExpectedContext expected = expected_context();
  expected.k_sq = 37;
  const lb_source::DetectorBandHandoffReadResult wrong_k =
      lb_source::detector_band_handoff_from_bytes(encoded.bytes, expected);
  CHECK_TRUE(!wrong_k.accepted());
  CHECK_EQ(wrong_k.diagnostic, std::string("wrong k_sq"));

  expected = expected_context();
  expected.schedule_digest_hex =
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
  const lb_source::DetectorBandHandoffReadResult wrong_digest =
      lb_source::detector_band_handoff_from_bytes(encoded.bytes, expected);
  CHECK_TRUE(!wrong_digest.accepted());
  CHECK_EQ(wrong_digest.diagnostic, std::string("wrong schedule_digest_hex"));

  expected = expected_context();
  expected.geometry_id = "other_geometry";
  const lb_source::DetectorBandHandoffReadResult wrong_geometry =
      lb_source::detector_band_handoff_from_bytes(encoded.bytes, expected);
  CHECK_TRUE(!wrong_geometry.accepted());
  CHECK_EQ(wrong_geometry.diagnostic, std::string("wrong geometry_id"));
}

void test_rejects_structural_invariants() {
  lb_source::DetectorBandHandoffV1 handoff = handoff_fixture();
  handoff.separator.carry_atoms.push_back(handoff.separator.carry_atoms[0]);
  CHECK_EQ(lb_source::validate_detector_band_handoff(handoff),
           std::string("detector separator duplicate carry atom"));

  handoff = handoff_fixture();
  handoff.separator.component_partition = {
      {coordinate_id(3, 4), coordinate_id(6, 8)},
      {port_id(1, 2, 3, 4), coordinate_id(3, 4)}};
  CHECK_EQ(lb_source::validate_detector_band_handoff(handoff),
           std::string("detector separator carry atom appears in multiple "
                       "components"));

  handoff = handoff_fixture();
  handoff.separator.reach_per_component[0] = 0x4;
  CHECK_EQ(lb_source::validate_detector_band_handoff(handoff),
           std::string("detector separator invalid reach bits"));

  handoff = handoff_fixture();
  handoff.separator.carry_atoms[0].id =
      std::numeric_limits<lb_source::AtomId>::min();
  CHECK_EQ(lb_source::validate_detector_band_handoff(handoff),
           std::string("detector separator unstable carry atom id"));

  handoff = handoff_fixture();
  handoff.carry_width = 7;
  CHECK_EQ(lb_source::validate_detector_band_handoff(handoff),
           std::string("carry_width does not match k_sq"));
}

void test_rejects_malformed_binary() {
  const lb_source::DetectorBandHandoffBytesResult encoded =
      lb_source::detector_band_handoff_to_bytes(handoff_fixture());
  CHECK_TRUE(encoded.accepted());

  std::vector<std::uint8_t> truncated = encoded.bytes;
  truncated.pop_back();
  expect_decode_rejects(truncated, "truncated blob");

  std::vector<std::uint8_t> trailing = encoded.bytes;
  trailing.push_back(0);
  expect_decode_rejects(trailing, "trailing bytes");

  std::vector<std::uint8_t> wrong_magic = encoded.bytes;
  wrong_magic[0] = 'X';
  expect_decode_rejects(wrong_magic, "wrong detector handoff magic");

  std::vector<std::uint8_t> invalid_reach = encoded.bytes;
  const std::size_t counts = count_offset(invalid_reach);
  const std::uint64_t carry_count = invalid_reach[counts];
  const std::size_t first_component = counts + 24 + carry_count * 16;
  invalid_reach[first_component + 16] = 0x4;
  expect_decode_rejects(invalid_reach,
                        "detector separator invalid reach bits");

  std::vector<std::uint8_t> huge_count = encoded.bytes;
  write_u64(huge_count, count_offset(huge_count), 1000001);
  expect_decode_rejects(huge_count, "handoff counts exceed limits");
}

void run(const char* name, void (*fn)()) {
  const int before = g_failures;
  fn();
  if (g_failures == before) {
    std::cout << "PASS " << name << "\n";
  }
}

}  // namespace

int main() {
  run("canonical_round_trip_and_hash", test_canonical_round_trip_and_hash);
  run("reordered_equivalent_separator_hashes_identically",
      test_reordered_equivalent_separator_hashes_identically);
  run("rejects_expected_context_mismatch",
      test_rejects_expected_context_mismatch);
  run("rejects_structural_invariants", test_rejects_structural_invariants);
  run("rejects_malformed_binary", test_rejects_malformed_binary);

  if (g_failures != 0) {
    std::cerr << g_failures << " test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
