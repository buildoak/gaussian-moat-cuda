#include "lb_source/source_propagation.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kSq = 26;
constexpr std::uint64_t kConservativeTerminalRadius = 1015645;
constexpr std::uint64_t kPreferredBandWidth = 8192;

using u128 = unsigned __int128;

struct ZoneEvidence {
  bool applicable = true;
  std::vector<std::uint64_t> candidates;
  std::vector<std::uint64_t> gaussian_prime_norms;
};

std::uint64_t square_u64(std::uint64_t value) {
  const u128 square = static_cast<u128>(value) * value;
  if (square > std::numeric_limits<std::uint64_t>::max()) {
    std::cerr << "square overflow for " << value << "\n";
    std::exit(EXIT_FAILURE);
  }
  return static_cast<std::uint64_t>(square);
}

bool is_prime_u64(std::uint64_t n) {
  if (n < 2) return false;
  for (const std::uint64_t p : {2ULL, 3ULL, 5ULL, 7ULL, 11ULL, 13ULL,
                                17ULL, 19ULL, 23ULL, 29ULL, 31ULL, 37ULL}) {
    if (n == p) return true;
    if (n % p == 0) return false;
  }

  std::uint64_t d = n - 1;
  std::uint32_t s = 0;
  while ((d & 1ULL) == 0) {
    d >>= 1U;
    ++s;
  }
  const auto mul_mod = [](std::uint64_t a, std::uint64_t b,
                          std::uint64_t mod) {
    return static_cast<std::uint64_t>((static_cast<u128>(a) * b) % mod);
  };
  const auto pow_mod = [&](std::uint64_t base, std::uint64_t exp,
                           std::uint64_t mod) {
    std::uint64_t result = 1;
    base %= mod;
    while (exp != 0) {
      if ((exp & 1ULL) != 0) result = mul_mod(result, base, mod);
      exp >>= 1U;
      if (exp != 0) base = mul_mod(base, base, mod);
    }
    return result;
  };
  const auto witness_passes = [&](std::uint64_t a) {
    if (a % n == 0) return true;
    std::uint64_t x = pow_mod(a, d, n);
    if (x == 1 || x == n - 1) return true;
    for (std::uint32_t r = 1; r < s; ++r) {
      x = mul_mod(x, x, n);
      if (x == n - 1) return true;
    }
    return false;
  };
  for (const std::uint64_t a : {2ULL, 325ULL, 9375ULL, 28178ULL, 450775ULL,
                                9780504ULL, 1795265022ULL}) {
    if (!witness_passes(a)) return false;
  }
  return true;
}

std::uint64_t floor_sqrt_u64(std::uint64_t n) {
  std::uint64_t lo = 0;
  std::uint64_t hi = 1ULL << 32;
  while (lo + 1 < hi) {
    const std::uint64_t mid = lo + (hi - lo) / 2;
    if (static_cast<u128>(mid) * mid <= n) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

bool gaussian_prime_norm(std::uint64_t n) {
  if (n == 2) return true;
  if (is_prime_u64(n)) return (n & 3ULL) == 1ULL;
  const std::uint64_t root = floor_sqrt_u64(n);
  return root * root == n && (root & 3ULL) == 3ULL && is_prime_u64(root);
}

std::uint64_t first_true(std::uint64_t lo, std::uint64_t hi,
                         const auto& pred) {
  while (lo < hi) {
    const std::uint64_t mid = lo + (hi - lo) / 2;
    if (pred(mid)) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}

std::uint64_t last_true(std::uint64_t lo, std::uint64_t hi,
                        const auto& pred) {
  while (lo < hi) {
    const std::uint64_t mid = lo + (hi - lo + 1) / 2;
    if (pred(mid)) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

ZoneEvidence inner_bz(std::uint64_t r) {
  ZoneEvidence evidence;
  if (r == 0) {
    evidence.applicable = false;
    return evidence;
  }

  const std::uint64_t ceil_k = lb_source::ceil_sqrt(kSq);
  const std::uint64_t hi = square_u64(r + ceil_k) + 4;
  const u128 r_sq = static_cast<u128>(r) * r;
  const u128 lower_base = r_sq - 1 + kSq;
  const u128 upper_base = r_sq + kSq;
  const u128 lower_radicand4 = 4 * static_cast<u128>(kSq) * (r_sq - 1);
  const u128 upper_radicand4 = 4 * r_sq * kSq;

  const auto above_lower = [&](std::uint64_t n) {
    const u128 value = n;
    if (value <= lower_base) return false;
    const u128 delta = value - lower_base;
    return delta * delta > lower_radicand4;
  };
  const auto below_upper = [&](std::uint64_t n) {
    const u128 value = n;
    if (value <= upper_base) return true;
    const u128 delta = value - upper_base;
    return delta * delta <= upper_radicand4;
  };

  const std::uint64_t first = first_true(0, hi, above_lower);
  const std::uint64_t last = last_true(0, hi, below_upper);
  if (first <= last) {
    for (std::uint64_t n = first; n <= last; ++n) {
      evidence.candidates.push_back(n);
    }
  }
  for (const std::uint64_t n : evidence.candidates) {
    if (gaussian_prime_norm(n)) evidence.gaussian_prime_norms.push_back(n);
  }
  return evidence;
}

ZoneEvidence outer_bz(std::uint64_t r) {
  ZoneEvidence evidence;
  if (r <= lb_source::ceil_sqrt(kSq)) {
    evidence.applicable = false;
    return evidence;
  }

  const std::uint64_t hi = square_u64(r + 1) + kSq + 4;
  const u128 r_sq = static_cast<u128>(r) * r;
  const u128 lower_base = r_sq + kSq;
  const u128 upper_base = r_sq + 1 + kSq;
  const u128 lower_radicand4 = 4 * r_sq * kSq;
  const u128 upper_radicand4 = 4 * static_cast<u128>(kSq) * (r_sq + 1);

  const auto above_lower = [&](std::uint64_t n) {
    const u128 value = n;
    if (value >= lower_base) return true;
    const u128 delta = lower_base - value;
    return delta * delta <= lower_radicand4;
  };
  const auto below_upper = [&](std::uint64_t n) {
    const u128 value = n;
    if (value >= upper_base) return false;
    const u128 delta = upper_base - value;
    return delta * delta > upper_radicand4;
  };

  const std::uint64_t first = first_true(0, hi, above_lower);
  const std::uint64_t last = last_true(0, hi, below_upper);
  if (first <= last) {
    for (std::uint64_t n = first; n <= last; ++n) {
      evidence.candidates.push_back(n);
    }
  }
  for (const std::uint64_t n : evidence.candidates) {
    if (gaussian_prime_norm(n)) evidence.gaussian_prime_norms.push_back(n);
  }
  return evidence;
}

std::uint64_t band_count_for(std::uint64_t terminal_radius,
                             std::uint64_t band_width) {
  return (terminal_radius + band_width - 1) / band_width;
}

void emit_u64_array(const std::vector<std::uint64_t>& values) {
  std::cout << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) std::cout << ",";
    std::cout << values[i];
  }
  std::cout << "]";
}

void emit_zone(const char* name, const ZoneEvidence& evidence) {
  std::cout << "\"" << name << "\":{"
            << "\"applicable\":" << (evidence.applicable ? "true" : "false")
            << ",\"candidate_count\":" << evidence.candidates.size()
            << ",\"candidates\":";
  emit_u64_array(evidence.candidates);
  std::cout << ",\"gaussian_prime_norm_count\":"
            << evidence.gaussian_prime_norms.size()
            << ",\"gaussian_prime_norms\":";
  emit_u64_array(evidence.gaussian_prime_norms);
  std::cout << "}";
}

}  // namespace

int main() {
  if (lb_source::ceil_sqrt(kSq) != 6) {
    std::cerr << "ceil_sqrt(26) mismatch\n";
    return EXIT_FAILURE;
  }
  if (band_count_for(kConservativeTerminalRadius, kPreferredBandWidth) !=
      124) {
    std::cerr << "unexpected K26 band count\n";
    return EXIT_FAILURE;
  }

  std::uint64_t bad_norm_count = 0;
  std::uint64_t rows_with_bad_norms = 0;
  std::uint64_t rows_checked = 0;

  std::cout << "{"
            << "\"schema\":\"lb_source_k26_bz_schedule_check_v1\","
            << "\"claim_label\":\"SOURCE_ORIGIN_K26\","
            << "\"proof_status\":\"BZ_SCHEDULE_REQUIRES_ROW_SHIFTS_DIAGNOSTIC\","
            << "\"accepted_for_claim\":false,"
            << "\"non_claim\":\"exact K26 bad-zone schedule diagnostic only; nominal rows require BZ repair before any source/origin run\","
            << "\"k_sq\":" << kSq
            << ",\"ceil_sqrt_k\":" << lb_source::ceil_sqrt(kSq)
            << ",\"terminal_radius\":" << kConservativeTerminalRadius
            << ",\"preferred_band_width\":" << kPreferredBandWidth
            << ",\"band_count\":"
            << band_count_for(kConservativeTerminalRadius, kPreferredBandWidth)
            << ",\"rows\":[";

  std::uint64_t r_start = 0;
  std::uint64_t row_index = 0;
  while (r_start < kConservativeTerminalRadius) {
    const std::uint64_t remaining = kConservativeTerminalRadius - r_start;
    const std::uint64_t width =
        remaining < kPreferredBandWidth ? remaining : kPreferredBandWidth;
    const std::uint64_t r_outer = r_start + width;
    const ZoneEvidence inner = inner_bz(r_start);
    const ZoneEvidence outer = outer_bz(r_outer);
    const std::uint64_t row_bad_norms =
        inner.gaussian_prime_norms.size() + outer.gaussian_prime_norms.size();
    bad_norm_count += row_bad_norms;
    rows_with_bad_norms += row_bad_norms == 0 ? 0 : 1;
    ++rows_checked;

    if (row_index != 0) std::cout << ",";
    std::cout << "{\"index\":" << row_index
              << ",\"r_start\":" << r_start
              << ",\"r_outer\":" << r_outer
              << ",\"width\":" << width << ",";
    emit_zone("bz_i", inner);
    std::cout << ",";
    emit_zone("bz_o", outer);
    std::cout << ",\"bz_clean\":" << (row_bad_norms == 0 ? "true" : "false")
              << "}";

    r_start = r_outer;
    ++row_index;
  }

  std::cout << "],\"summary\":{\"rows_checked\":" << rows_checked
            << ",\"rows_with_bad_norms\":" << rows_with_bad_norms
            << ",\"bad_norm_count\":" << bad_norm_count
            << ",\"bz_clean\":" << (bad_norm_count == 0 ? "true" : "false")
            << "}}\n";

  return EXIT_SUCCESS;
}
