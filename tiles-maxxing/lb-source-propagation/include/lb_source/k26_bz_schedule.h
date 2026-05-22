#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lb_source::k26_bz {

inline constexpr std::uint64_t k_sq = 26;
inline constexpr std::uint64_t terminal_radius = 1015645;
inline constexpr std::uint64_t preferred_band_width = 8192;
inline constexpr const char* schedule_digest_algorithm =
    "sha256:lb_source_k26_repaired_bz_schedule_v1";

std::uint64_t repaired_boundary(std::uint64_t nominal);
std::vector<std::uint64_t> nominal_boundaries();
std::vector<std::uint64_t> canonical_repaired_boundaries();

std::string repaired_schedule_digest_payload(
    const std::vector<std::uint64_t>& nominal,
    const std::vector<std::uint64_t>& repaired);
std::string repaired_schedule_digest_hex(
    const std::vector<std::uint64_t>& nominal,
    const std::vector<std::uint64_t>& repaired);

}  // namespace lb_source::k26_bz
