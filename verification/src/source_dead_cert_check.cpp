#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

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

bool sane_token(std::string_view value) {
  if (value.empty() || value.size() > 256) return false;
  for (const unsigned char ch : value) {
    if (std::iscntrl(ch) != 0) return false;
  }
  return true;
}

bool pending_like(std::string_view value) {
  return value.empty() || value == "pending" ||
         value.rfind("pending-", 0) == 0 ||
         value.rfind("requires-", 0) == 0;
}

void require_nonpending_metadata(const nlohmann::json& metadata) {
  const std::string source_mode = require_string(metadata, "source_mode");
  if (source_mode != "ORIGIN_SOURCE" && source_mode != "WIRED_SOURCE" &&
      source_mode != "CERTIFIED_SEED") {
    throw std::runtime_error("metadata.source_mode is not accepted");
  }

  for (const char* field : {"source_id", "geometry_id", "commit_id",
                            "build_id", "bz_status", "artifact_hash"}) {
    const std::string value = require_string(metadata, field);
    if (!sane_token(value)) {
      throw std::runtime_error(std::string("metadata.") + field +
                               " is malformed");
    }
    if (pending_like(value)) {
      throw std::runtime_error(std::string("metadata.") + field +
                               " is still pending");
    }
  }
}

class Sha256 {
 public:
  void update(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
      buffer_[buffer_size_++] = data[i];
      bit_count_ += 8;
      if (buffer_size_ == 64) {
        transform(buffer_.data());
        buffer_size_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> final() {
    buffer_[buffer_size_++] = 0x80;
    if (buffer_size_ > 56) {
      while (buffer_size_ < 64) buffer_[buffer_size_++] = 0;
      transform(buffer_.data());
      buffer_size_ = 0;
    }
    while (buffer_size_ < 56) buffer_[buffer_size_++] = 0;
    for (int i = 7; i >= 0; --i) {
      buffer_[buffer_size_++] =
          static_cast<std::uint8_t>((bit_count_ >> (i * 8)) & 0xffU);
    }
    transform(buffer_.data());

    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
      digest[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xffU);
      digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xffU);
      digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xffU);
      digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xffU);
    }
    return digest;
  }

 private:
  static std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
  }

  void transform(const std::uint8_t block[64]) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    std::uint32_t w[64] = {};
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_size_ = 0;
  std::uint64_t bit_count_ = 0;
  std::array<std::uint32_t, 8> state_{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
};

std::string sha256_hex(const std::string& text) {
  Sha256 sha;
  sha.update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  const auto digest = sha.final();
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const std::uint8_t byte : digest) {
    out << std::setw(2) << static_cast<unsigned>(byte);
  }
  return out.str();
}

std::vector<std::int64_t> require_inventory(const nlohmann::json& cert) {
  const nlohmann::json& raw =
      require_array(cert, "terminal_source_inventory");
  std::vector<std::int64_t> ids;
  ids.reserve(raw.size());
  for (const nlohmann::json& item : raw) {
    if (!item.is_number_integer()) {
      throw std::runtime_error("terminal_source_inventory item is not integer");
    }
    ids.push_back(item.get<std::int64_t>());
  }
  std::sort(ids.begin(), ids.end());
  const auto duplicate = std::adjacent_find(ids.begin(), ids.end());
  if (duplicate != ids.end()) {
    throw std::runtime_error("terminal_source_inventory contains duplicates");
  }
  return ids;
}

std::string inventory_digest(const std::vector<std::int64_t>& ids) {
  std::ostringstream payload;
  payload << "LB_SOURCE_INVENTORY_V1\n";
  for (const std::int64_t id : ids) {
    payload << id << "\n";
  }
  return sha256_hex(payload.str());
}

void verify_source_dead_cert(const nlohmann::json& cert) {
  if (!cert.is_object()) {
    throw std::runtime_error("certificate must be object");
  }
  if (require_string(cert, "schema") != "lb_source_dead_cert_draft_v1") {
    throw std::runtime_error("schema is not lb_source_dead_cert_draft_v1");
  }
  if (!sane_token(require_string(cert, "certificate_id"))) {
    throw std::runtime_error("certificate_id is malformed");
  }
  if (!sane_token(require_string(cert, "profile_id"))) {
    throw std::runtime_error("profile_id is malformed");
  }
  require_nonpending_metadata(require_object(cert, "metadata"));
  if (require_u64(cert, "k_sq") == 0) {
    throw std::runtime_error("k_sq must be positive");
  }
  if (require_u64(cert, "terminal_radius") == 0) {
    throw std::runtime_error("terminal_radius must be positive");
  }
  if (!require_bool(cert, "negative_guard_pass")) {
    throw std::runtime_error("negative_guard_pass is false");
  }

  const std::vector<std::int64_t> inventory = require_inventory(cert);
  if (inventory.empty()) {
    throw std::runtime_error("terminal_source_inventory must be nonempty");
  }

  const nlohmann::json& summary =
      require_object(cert, "terminal_source_inventory_summary");
  if (require_u64(summary, "count") != inventory.size()) {
    throw std::runtime_error("inventory summary count mismatch");
  }
  if (require_string(summary, "digest_algorithm") !=
      "sha256:lb_source_inventory_v1") {
    throw std::runtime_error("unsupported inventory digest algorithm");
  }
  const std::string digest_hex = require_string(summary, "digest_hex");
  if (digest_hex.size() != 64 ||
      !std::all_of(digest_hex.begin(), digest_hex.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0 || (ch >= 'a' && ch <= 'f');
      })) {
    throw std::runtime_error("inventory digest is not lowercase sha256 hex");
  }
  if (digest_hex != inventory_digest(inventory)) {
    throw std::runtime_error("inventory digest mismatch");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: source_dead_cert_check <cert.json>\n";
    return 2;
  }

  try {
    std::ifstream in(argv[1]);
    if (!in) {
      throw std::runtime_error("cannot open certificate file");
    }
    nlohmann::json cert = nlohmann::json::parse(in);
    verify_source_dead_cert(cert);
    std::cout << "{\"status\":\"SOURCE_DEAD_CERT_DRAFT_PASS\"}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "SOURCE_DEAD_CERT_DRAFT_REJECT: " << e.what() << "\n";
    return 1;
  }
}
