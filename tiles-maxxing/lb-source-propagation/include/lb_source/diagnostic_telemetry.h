#ifndef LB_SOURCE_DIAGNOSTIC_TELEMETRY_H_
#define LB_SOURCE_DIAGNOSTIC_TELEMETRY_H_

#include <chrono>
#include <cstdint>

namespace lb_source {

using DiagnosticClock = std::chrono::steady_clock;

constexpr std::uint64_t kJsonSafeMaxInteger = 9007199254740991ULL;

struct RssSnapshot {
  std::uint64_t current_bytes = 0;
  std::uint64_t peak_bytes = 0;
};

class ElapsedTimer {
 public:
  using time_point = DiagnosticClock::time_point;

  ElapsedTimer() noexcept;
  explicit ElapsedTimer(time_point start) noexcept;

  void reset() noexcept;
  void reset(time_point start) noexcept;

  time_point start_time() const noexcept;
  std::uint64_t elapsed_ms() const noexcept;
  std::uint64_t elapsed_ms(time_point end) const noexcept;

 private:
  time_point start_;
};

std::uint64_t elapsed_ms(DiagnosticClock::time_point begin,
                         DiagnosticClock::time_point end) noexcept;

std::uint64_t current_rss_bytes() noexcept;
std::uint64_t peak_rss_bytes() noexcept;
RssSnapshot rss_snapshot() noexcept;

std::uint64_t json_safe_uint64(std::uint64_t value) noexcept;
double json_safe_finite_double(double value, double fallback = 0.0) noexcept;

}  // namespace lb_source

#endif  // LB_SOURCE_DIAGNOSTIC_TELEMETRY_H_
