#include "lb_source/diagnostic_telemetry.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace lb_source {
namespace {

#if defined(__linux__)
std::uint64_t saturating_multiply(std::uint64_t lhs,
                                  std::uint64_t rhs) noexcept {
  if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return lhs * rhs;
}
#endif

#if defined(__APPLE__) || defined(__linux__)
std::uint64_t positive_signed_to_u64(long value) noexcept {
  if (value <= 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(value);
}
#endif

}  // namespace

ElapsedTimer::ElapsedTimer() noexcept : start_(DiagnosticClock::now()) {}

ElapsedTimer::ElapsedTimer(time_point start) noexcept : start_(start) {}

void ElapsedTimer::reset() noexcept { start_ = DiagnosticClock::now(); }

void ElapsedTimer::reset(time_point start) noexcept { start_ = start; }

ElapsedTimer::time_point ElapsedTimer::start_time() const noexcept {
  return start_;
}

std::uint64_t ElapsedTimer::elapsed_ms() const noexcept {
  return lb_source::elapsed_ms(start_, DiagnosticClock::now());
}

std::uint64_t ElapsedTimer::elapsed_ms(time_point end) const noexcept {
  return lb_source::elapsed_ms(start_, end);
}

std::uint64_t elapsed_ms(DiagnosticClock::time_point begin,
                         DiagnosticClock::time_point end) noexcept {
  if (end <= begin) {
    return 0;
  }
  const auto count =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
          .count();
  if (count <= 0) {
    return 0;
  }
  return json_safe_uint64(static_cast<std::uint64_t>(count));
}

std::uint64_t current_rss_bytes() noexcept {
#if defined(__APPLE__)
  mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  const kern_return_t result =
      task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count);
  if (result != KERN_SUCCESS) {
    return 0;
  }
  return static_cast<std::uint64_t>(info.resident_size);
#elif defined(__linux__)
  std::ifstream statm("/proc/self/statm");
  std::uint64_t total_pages = 0;
  std::uint64_t resident_pages = 0;
  statm >> total_pages >> resident_pages;
  if (!statm) {
    return 0;
  }
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return 0;
  }
  return saturating_multiply(resident_pages,
                             static_cast<std::uint64_t>(page_size));
#else
  return 0;
#endif
}

std::uint64_t peak_rss_bytes() noexcept {
#if defined(__APPLE__) || defined(__linux__)
  rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
  const std::uint64_t maxrss = positive_signed_to_u64(usage.ru_maxrss);
  // ru_maxrss is bytes on macOS and KiB on Linux.
#if defined(__APPLE__)
  return maxrss;
#else
  return saturating_multiply(maxrss, 1024);
#endif
#else
  return 0;
#endif
}

RssSnapshot rss_snapshot() noexcept {
  RssSnapshot snapshot;
  snapshot.current_bytes = current_rss_bytes();
  snapshot.peak_bytes = peak_rss_bytes();
  return snapshot;
}

std::uint64_t json_safe_uint64(std::uint64_t value) noexcept {
  return std::min(value, kJsonSafeMaxInteger);
}

double json_safe_finite_double(double value, double fallback) noexcept {
  if (std::isfinite(value)) {
    return value;
  }
  if (std::isfinite(fallback)) {
    return fallback;
  }
  return 0.0;
}

}  // namespace lb_source
