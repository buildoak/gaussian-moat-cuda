#include "lb_source/k26_bz_schedule.h"

#include "lb_source/source_propagation.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

#include "sha256.h"

namespace lb_source::k26_bz {
namespace {

std::uint64_t band_count_for(std::uint64_t radius, std::uint64_t width) {
  return (radius + width - 1) / width;
}

std::int64_t boundary_shift(std::uint64_t nominal, std::uint64_t repaired) {
  return static_cast<std::int64_t>(repaired) -
         static_cast<std::int64_t>(nominal);
}

}  // namespace

std::uint64_t repaired_boundary(std::uint64_t nominal) {
  switch (nominal) {
    case 122880:
    case 475136:
    case 622592:
      return nominal - 1;
    default:
      return nominal;
  }
}

std::vector<std::uint64_t> nominal_boundaries() {
  std::vector<std::uint64_t> boundaries;
  for (std::uint64_t r = 0; r < terminal_radius;) {
    boundaries.push_back(r);
    const std::uint64_t remaining = terminal_radius - r;
    r += remaining < preferred_band_width ? remaining : preferred_band_width;
  }
  boundaries.push_back(terminal_radius);
  return boundaries;
}

std::vector<std::uint64_t> canonical_repaired_boundaries() {
  std::vector<std::uint64_t> repaired = nominal_boundaries();
  for (std::uint64_t& boundary : repaired) {
    boundary = repaired_boundary(boundary);
  }
  return repaired;
}

std::string repaired_schedule_digest_payload(
    const std::vector<std::uint64_t>& nominal,
    const std::vector<std::uint64_t>& repaired) {
  if (nominal.size() != repaired.size() || nominal.size() < 2) {
    std::cerr << "invalid K26 BZ schedule digest input\n";
    std::exit(EXIT_FAILURE);
  }

  std::ostringstream payload;
  payload << "LB_SOURCE_K26_REPAIRED_BZ_SCHEDULE_V1\n";
  payload << "k_sq=" << k_sq << "\n";
  payload << "ceil_sqrt_k=" << lb_source::ceil_sqrt(k_sq) << "\n";
  payload << "terminal_radius=" << terminal_radius << "\n";
  payload << "preferred_band_width=" << preferred_band_width << "\n";
  payload << "band_count=" << band_count_for(terminal_radius, preferred_band_width)
          << "\n";
  payload << "repair_strategy=nearest-clean-internal-boundary-negative-delta-first\n";
  payload << "boundary_count=" << repaired.size() << "\n";
  payload << "rows\n";
  for (std::size_t i = 0; i + 1 < repaired.size(); ++i) {
    payload << i << "," << nominal[i] << "," << nominal[i + 1] << ","
            << repaired[i] << "," << repaired[i + 1] << ","
            << (repaired[i + 1] - repaired[i]) << ","
            << boundary_shift(nominal[i], repaired[i]) << ","
            << boundary_shift(nominal[i + 1], repaired[i + 1]) << "\n";
  }
  return payload.str();
}

std::string repaired_schedule_digest_hex(
    const std::vector<std::uint64_t>& nominal,
    const std::vector<std::uint64_t>& repaired) {
  return campaign::detail::sha256_hex(
      repaired_schedule_digest_payload(nominal, repaired));
}

}  // namespace lb_source::k26_bz
