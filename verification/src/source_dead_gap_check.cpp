#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr std::uint64_t kSq = 26;
constexpr std::uint64_t kTerminalRadius = 1015645;
constexpr std::uint64_t kEndpointA = 376039;
constexpr std::uint64_t kEndpointB = 943460;
constexpr std::uint64_t kEndpointNormSq = 1031522101121ULL;
constexpr std::int64_t kEndpointAtomId = 1615075207964004LL;
constexpr std::uint64_t kExpectedComponentSize = 14542615005ULL;

std::string type_name(const nlohmann::json& value) {
  return std::string(value.type_name());
}

const nlohmann::json& require_field(const nlohmann::json& object,
                                    const char* field) {
  const auto it = object.find(field);
  if (it == object.end()) {
    throw std::runtime_error(std::string("missing required field: ") + field);
  }
  return *it;
}

std::string require_string(const nlohmann::json& object, const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_string()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be string, got " + type_name(value));
  }
  return value.get<std::string>();
}

std::uint64_t require_u64(const nlohmann::json& object, const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_number_unsigned()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be unsigned integer, got " +
                             type_name(value));
  }
  return value.get<std::uint64_t>();
}

std::int64_t require_i64(const nlohmann::json& object, const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_number_integer()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be integer, got " + type_name(value));
  }
  return value.get<std::int64_t>();
}

const nlohmann::json& require_object(const nlohmann::json& object,
                                     const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_object()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be object, got " + type_name(value));
  }
  return value;
}

const nlohmann::json& require_array(const nlohmann::json& object,
                                    const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_array()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be array, got " + type_name(value));
  }
  return value;
}

bool sane_text(std::string_view value) {
  if (value.empty() || value.size() > 512) return false;
  for (const unsigned char ch : value) {
    if (std::iscntrl(ch) != 0) return false;
  }
  return true;
}

bool sha256_hex(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char ch) {
           return std::isdigit(ch) != 0 || (ch >= 'a' && ch <= 'f');
         });
}

std::uint64_t coordinate_atom_norm_sq(std::int64_t id) {
  if (id < 0) {
    throw std::runtime_error("coordinate atom id is negative");
  }
  const std::uint64_t raw = static_cast<std::uint64_t>(id);
  const std::uint64_t a = raw >> 32;
  const std::uint64_t b = raw & 0xffffffffULL;
  const unsigned __int128 norm =
      static_cast<unsigned __int128>(a) * a +
      static_cast<unsigned __int128>(b) * b;
  if (norm > std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("coordinate atom norm overflows u64");
  }
  return static_cast<std::uint64_t>(norm);
}

void require_point(const nlohmann::json& object, const char* field,
                   std::uint64_t expected_a, std::uint64_t expected_b) {
  const nlohmann::json& point = require_object(object, field);
  if (require_u64(point, "a") != expected_a ||
      require_u64(point, "b") != expected_b ||
      require_u64(point, "norm_sq") != kEndpointNormSq) {
    throw std::runtime_error(std::string(field) +
                             " does not match expected K26 endpoint");
  }
}

std::vector<std::int64_t> require_i64_array(const nlohmann::json& object,
                                            const char* field) {
  const nlohmann::json& raw = require_array(object, field);
  std::vector<std::int64_t> values;
  values.reserve(raw.size());
  for (const nlohmann::json& item : raw) {
    if (!item.is_number_integer()) {
      throw std::runtime_error(std::string(field) +
                               " item must be integer");
    }
    values.push_back(item.get<std::int64_t>());
  }
  return values;
}

void verify_gap(const nlohmann::json& gap) {
  if (!gap.is_object()) {
    throw std::runtime_error("gap artifact must be object");
  }
  if (require_string(gap, "schema") != "lb_source_k26_source_dead_gap_v1") {
    throw std::runtime_error("schema is not lb_source_k26_source_dead_gap_v1");
  }
  if (require_string(gap, "claim_label") != "SOURCE_ORIGIN_K26") {
    throw std::runtime_error("claim_label is not SOURCE_ORIGIN_K26");
  }
  if (require_string(gap, "proof_status") != "DIAGNOSTIC_NON_CLAIM") {
    throw std::runtime_error("gap artifact must remain DIAGNOSTIC_NON_CLAIM");
  }
  if (require_string(gap, "blocker") !=
      "SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING") {
    throw std::runtime_error("unexpected source-dead blocker");
  }
  if (!sane_text(require_string(gap, "non_claim"))) {
    throw std::runtime_error("non_claim explanation is malformed");
  }
  if (require_u64(gap, "k_sq") != kSq ||
      require_u64(gap, "terminal_radius") != kTerminalRadius) {
    throw std::runtime_error("unexpected K26 geometry constants");
  }

  const nlohmann::json& target = require_object(gap, "target");
  require_point(target, "tsuchimura_endpoint", kEndpointB, kEndpointA);
  require_point(target, "canonical_octant_endpoint", kEndpointA, kEndpointB);

  const nlohmann::json& continuation =
      require_object(gap, "continuation_artifact");
  if (require_string(continuation, "name") != "k26-continuation-result.json") {
    throw std::runtime_error("unexpected continuation artifact name");
  }
  if (!sha256_hex(require_string(continuation, "sha256"))) {
    throw std::runtime_error("continuation artifact hash is not sha256 hex");
  }

  if (require_string(gap, "target_path_provenance") !=
      "mixed_coordinate_port_atom_chain_non_claim") {
    throw std::runtime_error("target path provenance is not the mixed non-claim chain");
  }
  const std::vector<std::int64_t> atom_path =
      require_i64_array(gap, "target_atom_path");
  const std::uint64_t atom_path_length =
      require_u64(gap, "target_atom_path_length");
  if (atom_path.empty() || atom_path.size() != atom_path_length) {
    throw std::runtime_error("target atom path length mismatch");
  }
  if (atom_path.back() != kEndpointAtomId) {
    throw std::runtime_error("target atom path does not end at K26 endpoint atom");
  }
  if (std::none_of(atom_path.begin(), atom_path.end(),
                   [](std::int64_t id) { return id < 0; })) {
    throw std::runtime_error("target atom path has no TileOp port atom");
  }

  const nlohmann::json& summary =
      require_object(gap, "terminal_source_inventory_summary");
  if (require_u64(summary, "count") != kExpectedComponentSize) {
    throw std::runtime_error("terminal inventory count does not match Tsuchimura");
  }
  if (require_string(summary, "digest_algorithm") !=
      "sha256:lb_source_inventory_v1") {
    throw std::runtime_error("unsupported inventory digest algorithm");
  }
  if (!sha256_hex(require_string(summary, "digest_hex"))) {
    throw std::runtime_error("inventory digest is not sha256 hex");
  }
  if (require_u64(summary, "max_norm_sq") != kEndpointNormSq) {
    throw std::runtime_error("max source norm does not match K26 endpoint");
  }
  const std::vector<std::int64_t> ties =
      require_i64_array(summary, "max_norm_atom_ids");
  if (ties.empty()) {
    throw std::runtime_error("max_norm_atom_ids is empty");
  }
  if (std::find(ties.begin(), ties.end(), kEndpointAtomId) == ties.end()) {
    throw std::runtime_error("max_norm_atom_ids omits K26 endpoint atom");
  }
  for (const std::int64_t id : ties) {
    if (coordinate_atom_norm_sq(id) != kEndpointNormSq) {
      throw std::runtime_error("max_norm_atom_ids norm mismatch");
    }
  }

  const nlohmann::json& missing =
      require_array(gap, "missing_for_source_dead_cert");
  bool has_coordinate_path_gap = false;
  bool has_verifier_gap = false;
  for (const nlohmann::json& item : missing) {
    if (!item.is_string() || !sane_text(item.get<std::string>())) {
      throw std::runtime_error("missing_for_source_dead_cert item is malformed");
    }
    const std::string text = item.get<std::string>();
    if (text.find("coordinate Gaussian-prime source_path") !=
        std::string::npos) {
      has_coordinate_path_gap = true;
    }
    if (text.find("verifier") != std::string::npos) {
      has_verifier_gap = true;
    }
  }
  if (!has_coordinate_path_gap || !has_verifier_gap) {
    throw std::runtime_error("missing gap list omits coordinate path or verifier gap");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: source_dead_gap_check <gap.json>\n";
    return 2;
  }

  try {
    std::ifstream in(argv[1]);
    if (!in) {
      throw std::runtime_error("cannot open gap artifact");
    }
    const nlohmann::json gap = nlohmann::json::parse(in);
    verify_gap(gap);
    std::cout << "{\"status\":\"SOURCE_DEAD_GAP_NON_CLAIM_PASS\"}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "SOURCE_DEAD_GAP_REJECT: " << e.what() << "\n";
    return 1;
  }
}
