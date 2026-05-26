#include "lb_source/diagnostic_telemetry.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

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

void test_elapsed_time_points_are_nonnegative() {
  const lb_source::DiagnosticClock::time_point begin =
      lb_source::DiagnosticClock::now();
  const lb_source::DiagnosticClock::time_point later =
      begin + std::chrono::milliseconds(7);

  CHECK_EQ(lb_source::elapsed_ms(begin, begin), std::uint64_t{0});
  CHECK_TRUE(lb_source::elapsed_ms(begin, later) >= 7);
  CHECK_EQ(lb_source::elapsed_ms(later, begin), std::uint64_t{0});
}

void test_timer_elapsed_time_is_monotonic() {
  lb_source::ElapsedTimer timer;
  const std::uint64_t first = timer.elapsed_ms();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const std::uint64_t second = timer.elapsed_ms();
  const std::uint64_t third = timer.elapsed_ms();

  CHECK_TRUE(second >= first);
  CHECK_TRUE(third >= second);

  timer.reset(timer.start_time() + std::chrono::milliseconds(5));
  CHECK_EQ(timer.elapsed_ms(timer.start_time()), std::uint64_t{0});
}

void test_rss_helpers_do_not_require_platform_availability() {
  const std::uint64_t current = lb_source::current_rss_bytes();
  const std::uint64_t peak = lb_source::peak_rss_bytes();
  const lb_source::RssSnapshot snapshot = lb_source::rss_snapshot();

  CHECK_TRUE(current == 0 || current >= 1024);
  CHECK_TRUE(peak == 0 || peak >= 1024);
  CHECK_TRUE(snapshot.current_bytes == 0 || snapshot.current_bytes >= 1024);
  CHECK_TRUE(snapshot.peak_bytes == 0 || snapshot.peak_bytes >= 1024);
}

void test_json_safe_numeric_helpers() {
  CHECK_EQ(lb_source::json_safe_uint64(42), std::uint64_t{42});
  CHECK_EQ(lb_source::json_safe_uint64(
               std::numeric_limits<std::uint64_t>::max()),
           lb_source::kJsonSafeMaxInteger);
  CHECK_EQ(lb_source::json_safe_finite_double(1.25), 1.25);
  CHECK_EQ(lb_source::json_safe_finite_double(
               std::numeric_limits<double>::infinity(), 2.5),
           2.5);
  CHECK_EQ(lb_source::json_safe_finite_double(
               std::numeric_limits<double>::quiet_NaN(),
               std::numeric_limits<double>::infinity()),
           0.0);
}

}  // namespace

int main() {
  test_elapsed_time_points_are_nonnegative();
  test_timer_elapsed_time_is_monotonic();
  test_rss_helpers_do_not_require_platform_availability();
  test_json_safe_numeric_helpers();

  if (g_failures != 0) {
    std::cerr << g_failures << " diagnostic telemetry test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
