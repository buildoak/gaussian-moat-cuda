#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lb_source/tileop_static_reach.h"

namespace lb_source {

inline constexpr const char kDetectorBandHandoffSchema[] =
    "DETECTOR_BAND_HANDOFF_V1";

struct DetectorBandHandoffV1 {
  std::uint64_t k_sq = 0;
  std::uint64_t cut_radius = 0;
  std::uint64_t carry_width = 0;
  std::uint64_t schedule_index = 0;
  std::string schedule_digest_algorithm;
  std::string schedule_digest_hex;
  std::string geometry_id;
  std::string oracle_id;
  std::string build_id;
  std::string support_envelope_id;
  std::string port_identity_scheme_id;
  std::string boundary_policy_id;
  StaticReachSeparator separator;

  friend bool operator==(const DetectorBandHandoffV1&,
                         const DetectorBandHandoffV1&) = default;
};

struct DetectorBandHandoffExpectedContext {
  std::optional<std::uint64_t> k_sq;
  std::optional<std::uint64_t> cut_radius;
  std::optional<std::uint64_t> carry_width;
  std::optional<std::uint64_t> schedule_index;
  std::optional<std::string> schedule_digest_algorithm;
  std::optional<std::string> schedule_digest_hex;
  std::optional<std::string> geometry_id;
  std::optional<std::string> oracle_id;
  std::optional<std::string> build_id;
  std::optional<std::string> support_envelope_id;
  std::optional<std::string> port_identity_scheme_id;
  std::optional<std::string> boundary_policy_id;
};

struct DetectorBandHandoffLimits {
  std::uint64_t max_carry_atoms = 1000000;
  std::uint64_t max_components = 1000000;
  std::uint64_t max_component_atom_entries = 1000000;
  std::uint64_t max_token_bytes = 4096;
};

struct DetectorBandHandoffBytesResult {
  std::vector<std::uint8_t> bytes;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

struct DetectorBandHandoffReadResult {
  DetectorBandHandoffV1 handoff;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

DetectorBandHandoffV1 canonicalize_detector_band_handoff(
    const DetectorBandHandoffV1& handoff);

std::string validate_detector_band_handoff(
    const DetectorBandHandoffV1& handoff,
    const DetectorBandHandoffExpectedContext& expected = {});

DetectorBandHandoffBytesResult detector_band_handoff_to_bytes(
    const DetectorBandHandoffV1& handoff);

DetectorBandHandoffReadResult detector_band_handoff_from_bytes(
    const std::vector<std::uint8_t>& bytes,
    const DetectorBandHandoffExpectedContext& expected = {},
    const DetectorBandHandoffLimits& limits = {});

std::string detector_band_handoff_sha256_hex(
    const DetectorBandHandoffV1& handoff);

}  // namespace lb_source
