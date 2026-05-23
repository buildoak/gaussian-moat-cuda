#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
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
constexpr std::int32_t kEndpointTileI = 1468;
constexpr std::int32_t kEndpointTileJ = 3685;
constexpr std::uint64_t kExpectedComponentSize = 14542615005ULL;
constexpr std::string_view kK26RepairedBzScheduleDigest =
    "7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95";

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

bool require_bool(const nlohmann::json& object, const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_boolean()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be boolean, got " + type_name(value));
  }
  return value.get<bool>();
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

struct PortAtom {
  std::int32_t tile_i = 0;
  std::int32_t tile_j = 0;
};

std::optional<PortAtom> decode_port_atom_id(std::int64_t id) {
  if (id >= 0 || id == std::numeric_limits<std::int64_t>::min()) {
    return std::nullopt;
  }
  const std::uint64_t raw = static_cast<std::uint64_t>(-1 - id);
  if ((raw >> 58) != 0) {
    return std::nullopt;
  }
  const std::uint64_t tile_i = raw >> 34;
  const std::uint64_t tile_j = (raw >> 10) & ((1ULL << 24) - 1ULL);
  if (tile_i > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int32_t>::max()) ||
      tile_j > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int32_t>::max())) {
    return std::nullopt;
  }
  return PortAtom{static_cast<std::int32_t>(tile_i),
                  static_cast<std::int32_t>(tile_j)};
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

std::string verify_gap(const nlohmann::json& gap) {
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
  const std::string blocker = require_string(gap, "blocker");
  const bool target_not_reached =
      blocker == "SOURCE_DEAD_CERT_TARGET_NOT_REACHED";
  if (blocker != "SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING" &&
      !target_not_reached) {
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
  if (const auto chunk_ledger_it = gap.find("chunk_ledger_artifact");
      chunk_ledger_it != gap.end()) {
    if (!chunk_ledger_it->is_object()) {
      throw std::runtime_error("chunk_ledger_artifact must be object");
    }
    if (require_string(*chunk_ledger_it, "name") !=
        "k26-continuation-chunks.jsonl") {
      throw std::runtime_error("unexpected chunk ledger artifact name");
    }
    if (!sha256_hex(require_string(*chunk_ledger_it, "sha256"))) {
      throw std::runtime_error("chunk ledger artifact hash is not sha256 hex");
    }
  }

  const nlohmann::json& bz_evidence = require_object(gap, "bz_evidence");
  if (require_string(bz_evidence, "status") !=
      "BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE") {
    throw std::runtime_error("BZ evidence status is not repaired schedule pass");
  }
  if (!require_bool(bz_evidence, "accepted_for_schedule")) {
    throw std::runtime_error("BZ evidence is not accepted for schedule");
  }
  if (require_bool(bz_evidence, "accepted_for_claim")) {
    throw std::runtime_error("BZ evidence must not be accepted for claim");
  }
  if (require_string(bz_evidence, "schedule_digest_algorithm") !=
      "sha256:lb_source_k26_repaired_bz_schedule_v1") {
    throw std::runtime_error("unexpected BZ schedule digest algorithm");
  }
  const std::string bz_digest_hex =
      require_string(bz_evidence, "schedule_digest_hex");
  if (!sha256_hex(bz_digest_hex)) {
    throw std::runtime_error("BZ schedule digest is not sha256 hex");
  }
  if (bz_digest_hex != kK26RepairedBzScheduleDigest) {
    throw std::runtime_error("BZ schedule digest does not match K26 repaired schedule");
  }

  const nlohmann::json& bridge_safety =
      require_object(gap, "bridge_safety");
  if (require_string(bridge_safety, "seam_bridge_policy") !=
      "diagnostic_allow_unbridged") {
    throw std::runtime_error("bridge safety policy is not diagnostic_allow_unbridged");
  }
  const std::uint64_t source_unbridged =
      require_u64(bridge_safety, "source_unbridged_coordinate_carry_atoms");
  const std::uint64_t source_without_candidates =
      require_u64(bridge_safety, "source_unbridged_without_next_band_candidates");
  const std::uint64_t source_with_candidates =
      require_u64(bridge_safety, "source_unbridged_with_next_band_candidates");
  const std::uint64_t source_dead_end =
      require_u64(bridge_safety, "source_unbridged_dead_end_candidate_atoms");
  const std::uint64_t source_unsafe =
      require_u64(bridge_safety, "source_unbridged_unsafe_candidate_atoms");
  (void)require_u64(bridge_safety, "source_bridged_coordinate_carry_atoms");
  (void)require_u64(bridge_safety, "source_bridge_rejected_candidate_atoms");
  if (source_unbridged != source_without_candidates + source_with_candidates) {
    throw std::runtime_error("bridge safety source unbridged counts do not add up");
  }
  if (source_with_candidates != source_dead_end + source_unsafe) {
    throw std::runtime_error("bridge safety source candidate counts do not add up");
  }
  if (source_unsafe != 0) {
    throw std::runtime_error("bridge safety has unsafe source unbridged candidates");
  }

  const std::string target_path_provenance =
      require_string(gap, "target_path_provenance");
  if (target_not_reached) {
    if (target_path_provenance != "component_reachability_only") {
      throw std::runtime_error(
          "target-not-reached gap has wrong target path provenance");
    }
  } else if (target_path_provenance !=
             "mixed_coordinate_port_atom_chain_non_claim") {
    throw std::runtime_error("target path provenance is not the mixed non-claim chain");
  }
  const std::vector<std::int64_t> atom_path =
      require_i64_array(gap, "target_atom_path");
  const std::uint64_t atom_path_length =
      require_u64(gap, "target_atom_path_length");
  if (atom_path.size() != atom_path_length) {
    throw std::runtime_error("target atom path length mismatch");
  }
  if (target_not_reached && !atom_path.empty()) {
    throw std::runtime_error("target-not-reached gap must have empty atom path");
  }
  if (!target_not_reached && atom_path.empty()) {
    throw std::runtime_error("target atom path must be nonempty");
  }
  if (!target_not_reached && atom_path.back() != kEndpointAtomId) {
    throw std::runtime_error("target atom path does not end at K26 endpoint atom");
  }
  std::size_t coordinate_atoms = 0;
  std::size_t port_atoms = 0;
  for (const std::int64_t id : atom_path) {
    if (id >= 0) {
      (void)coordinate_atom_norm_sq(id);
      ++coordinate_atoms;
      continue;
    }
    if (!decode_port_atom_id(id).has_value()) {
      throw std::runtime_error("target atom path contains invalid TileOp port atom id");
    }
    ++port_atoms;
  }
  if (!target_not_reached && coordinate_atoms < 2) {
    throw std::runtime_error("target atom path needs coordinate source and endpoint atoms");
  }
  if (!target_not_reached && port_atoms == 0) {
    throw std::runtime_error("target atom path has no TileOp port atom");
  }
  if (!target_not_reached) {
    const std::optional<PortAtom> endpoint_port =
        decode_port_atom_id(atom_path[atom_path.size() - 2]);
    if (!endpoint_port.has_value() ||
        endpoint_port->tile_i != kEndpointTileI ||
        endpoint_port->tile_j != kEndpointTileJ) {
      throw std::runtime_error("target atom path does not enter endpoint through the K26 endpoint tile");
    }
  }

  const nlohmann::json& path_obligation =
      require_object(gap, "coordinate_path_obligation");
  if (require_string(path_obligation, "required_provenance") !=
      "coordinate_gaussian_prime_path") {
    throw std::runtime_error("coordinate path obligation has wrong required provenance");
  }
  if (require_string(path_obligation, "observed_provenance") !=
      target_path_provenance) {
    throw std::runtime_error("coordinate path obligation has wrong observed provenance");
  }
  if (require_u64(path_obligation, "observed_coordinate_atom_count") !=
      coordinate_atoms) {
    throw std::runtime_error("coordinate path obligation coordinate count mismatch");
  }
  if (require_u64(path_obligation, "observed_port_atom_count") != port_atoms) {
    throw std::runtime_error("coordinate path obligation port count mismatch");
  }
  if (!target_not_reached && port_atoms == 0) {
    throw std::runtime_error("coordinate path obligation needs a port expansion gap");
  }
  if (require_string(path_obligation, "per_port_coordinate_expansion") !=
      "missing") {
    throw std::runtime_error("coordinate path obligation must report missing port expansion");
  }
  if (require_bool(path_obligation, "claim_grade_path_accepted")) {
    throw std::runtime_error("coordinate path obligation must not accept mixed atom chain as claim-grade");
  }

  const nlohmann::json& summary =
      require_object(gap, "terminal_source_inventory_summary");
  const std::uint64_t inventory_count = require_u64(summary, "count");
  if (!target_not_reached && inventory_count != kExpectedComponentSize) {
    throw std::runtime_error("terminal inventory count does not match Tsuchimura");
  }
  if (target_not_reached && inventory_count == 0) {
    throw std::runtime_error("target-not-reached inventory count is zero");
  }
  if (require_string(summary, "digest_algorithm") !=
      "sha256:lb_source_inventory_v1") {
    throw std::runtime_error("unsupported inventory digest algorithm");
  }
  if (!sha256_hex(require_string(summary, "digest_hex"))) {
    throw std::runtime_error("inventory digest is not sha256 hex");
  }
  const std::uint64_t max_norm_sq = require_u64(summary, "max_norm_sq");
  if (!target_not_reached && max_norm_sq != kEndpointNormSq) {
    throw std::runtime_error("max source norm does not match K26 endpoint");
  }
  if (target_not_reached && max_norm_sq >= kEndpointNormSq) {
    throw std::runtime_error(
        "target-not-reached max source norm reaches K26 endpoint norm");
  }
  const std::vector<std::int64_t> ties =
      require_i64_array(summary, "max_norm_atom_ids");
  if (ties.empty()) {
    throw std::runtime_error("max_norm_atom_ids is empty");
  }
  if (!target_not_reached &&
      std::find(ties.begin(), ties.end(), kEndpointAtomId) == ties.end()) {
    throw std::runtime_error("max_norm_atom_ids omits K26 endpoint atom");
  }
  for (const std::int64_t id : ties) {
    if (target_not_reached) {
      if (id >= 0) {
        (void)coordinate_atom_norm_sq(id);
      } else if (!decode_port_atom_id(id).has_value()) {
        throw std::runtime_error("target-not-reached max_norm_atom_ids has invalid port atom");
      }
    } else if (coordinate_atom_norm_sq(id) != kEndpointNormSq) {
      throw std::runtime_error("max_norm_atom_ids norm mismatch");
    }
  }

  const nlohmann::json& inventory_obligation =
      require_object(gap, "terminal_inventory_obligation");
  if (require_string(inventory_obligation, "required_mode") !=
      "claim_grade_terminal_inventory") {
    throw std::runtime_error("terminal inventory obligation has wrong required mode");
  }
  if (require_string(inventory_obligation, "observed_mode") !=
      "summary_digest_only_non_claim") {
    throw std::runtime_error("terminal inventory obligation has wrong observed mode");
  }
  if (require_bool(inventory_obligation, "listed_inventory_present")) {
    throw std::runtime_error("terminal inventory obligation must report missing listed inventory");
  }
  if (require_bool(inventory_obligation, "claim_grade_inventory_accepted")) {
    throw std::runtime_error("terminal inventory obligation must not accept summary-only inventory");
  }
  if (require_u64(inventory_obligation, "observed_count") !=
      inventory_count) {
    throw std::runtime_error("terminal inventory obligation count mismatch");
  }
  if (require_string(inventory_obligation, "observed_digest_algorithm") !=
      "sha256:lb_source_inventory_v1") {
    throw std::runtime_error("terminal inventory obligation digest algorithm mismatch");
  }
  if (require_string(inventory_obligation, "observed_digest_hex") !=
      require_string(summary, "digest_hex")) {
    throw std::runtime_error("terminal inventory obligation digest binding mismatch");
  }
  if (require_u64(inventory_obligation, "observed_max_norm_sq") !=
      max_norm_sq) {
    throw std::runtime_error("terminal inventory obligation max norm mismatch");
  }

  const nlohmann::json& missing =
      require_array(gap, "missing_for_source_dead_cert");
  bool has_coordinate_path_gap = false;
  bool has_target_reachability_gap = false;
  bool has_terminal_inventory_gap = false;
  bool has_bz_schedule_gap = false;
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
    if (text.find("target reachability") != std::string::npos) {
      has_target_reachability_gap = true;
    }
    if (text.find("terminal inventory") != std::string::npos) {
      has_terminal_inventory_gap = true;
    }
    if (text.find("BZ schedule") != std::string::npos) {
      has_bz_schedule_gap = true;
    }
    if (text.find("verifier") != std::string::npos) {
      has_verifier_gap = true;
    }
  }
  if (!has_coordinate_path_gap || !has_terminal_inventory_gap ||
      !has_bz_schedule_gap || !has_verifier_gap) {
    throw std::runtime_error(
        "missing gap list omits coordinate path, terminal inventory, BZ "
        "schedule, or verifier gap");
  }
  if (target_not_reached && !has_target_reachability_gap) {
    throw std::runtime_error(
        "target-not-reached gap omits target reachability blocker");
  }
  return target_not_reached ? "blocked_target_not_reached"
                            : "blocked_coordinate_gaussian_prime_path";
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
    const std::string coordinate_path_obligation = verify_gap(gap);
    std::cout << "{\"status\":\"SOURCE_DEAD_GAP_NON_CLAIM_PASS\","
              << "\"bridge_safety\":\"accepted_non_claim\","
              << "\"coordinate_path_obligation\":\""
              << coordinate_path_obligation << "\","
              << "\"bz_schedule_obligation\":\"blocked_schedule_only_non_claim\","
              << "\"terminal_inventory_obligation\":\"blocked_claim_grade_terminal_inventory\","
              << "\"claim_grade\":false}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "SOURCE_DEAD_GAP_REJECT: " << e.what() << "\n";
    return 1;
  }
}
