#pragma once

#include <HalClock.h>
#include <TrustedTime.h>

#include <cstdint>

namespace readersessionclock {

inline int64_t now() {
  // The loan-expiry floor may be ahead of a corrected RTC. Reading events
  // need actual UTC; keep the existing fallback only on boards without RTC.
  if (!halClock.hasRtc()) return trustedtime::trustedNow();
  time_t epoch;
  if (!halClock.utcTime(epoch)) return 0;
  const int64_t value = static_cast<int64_t>(epoch);
  return value >= 1735689600LL && value < 4102444800LL ? value : 0;
}

}  // namespace readersessionclock
