"""Exercise the production RTC HAL and reading clock against a future loan floor."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "BoardConfig.h": r'''
#pragma once
namespace BoardConfig {
inline bool rtcSupported = true;
inline bool hasRtc() { return rtcSupported; }
}
''',
    "Arduino.h": r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
inline unsigned long ticks = 100;
inline unsigned long millis() { return ticks; }
inline void delay(unsigned long ms) { ticks += ms; }
inline void configTzTime(const char*, const char*, const char*) {}
''',
    "Rtc.h": r'''
#pragma once
#include <cstdint>
class Rtc {
 public:
  struct DateTime { uint16_t year; uint8_t month, day, hour, minute, second, weekday; };
  inline static bool probeSucceeded = true;
  inline static bool readable = true;
  inline static unsigned reads = 0;
  inline static DateTime value{2026, 1, 2, 12, 0, 0, 5};
  bool begin() { return probeSucceeded; }
  bool now(DateTime& out) { ++reads; if (!readable) return false; out = value; return true; }
  bool set(const DateTime& in) { value = in; return true; }
};
''',
    "Logging.h": '#pragma once\n#define LOG_INF(...) ((void)0)\n#define LOG_ERR(...) ((void)0)\n',
    "WiFi.h": '#pragma once\nconstexpr int WL_CONNECTED = 3;\ninline struct { int status() { return WL_CONNECTED; } } WiFi;\n',
    "esp_sntp.h": '#pragma once\nconstexpr int SNTP_SYNC_STATUS_COMPLETED = 1;\ninline int sntp_get_sync_status() { return 1; }\n',
    "TrustedTime.h": r'''
#pragma once
#include <cstdint>
namespace trustedtime {
inline int64_t floor = 1767358800; // one hour ahead of the RTC
inline unsigned calls = 0;
inline int64_t trustedNow() { ++calls; return floor; }
}
''',
}

CHECK = r'''
#include "activities/reader/ReaderSessionClock.h"
#include "activities/reader/ReaderSession.h"
#include <BoardConfig.h>
#include <cassert>
int main() {
  constexpr int64_t utc = 1767355200; // 2026-01-02 12:00:00 UTC
  halClock.begin();
  halClock.setTimezone("EST5EDT,M3.2.0,M11.1.0");
  assert(readersessionclock::now() == utc);
  assert(trustedtime::calls == 0 && trustedtime::floor == utc + 3600);

  // A warm display cache must not hide a newer RTC reading, even in another TZ.
  struct tm displayed{};
  assert(halClock.localTime(displayed) && displayed.tm_hour == 7);
  Rtc::value.second = 15;
  assert(readersessionclock::now() == utc + 15);
  assert(halClock.localTime(displayed) && displayed.tm_sec == 0);
  halClock.setTimezone("JST-9");
  assert(readersessionclock::now() == utc + 15);

  // The actual accumulator keeps elapsed seconds and progress under a future floor.
  ReaderSession session;
  session.onRenderComplete(1000, readersessionclock::now(), 1000);
  Rtc::value.second = 30;
  session.noteTurn(true, true);
  session.onRenderComplete(16000, readersessionclock::now(), 1100);
  assert(session.startTime() == utc + 15 && session.endTime() == utc + 30);
  assert(session.durationSeconds() == 15 && session.isEmitWorthy());

  // A failed/stopped or unset RTC never substitutes cached time or the loan floor.
  Rtc::readable = false;
  assert(readersessionclock::now() == 0 && trustedtime::calls == 0);
  Rtc::readable = true;
  Rtc::value.year = 2000;
  assert(readersessionclock::now() == 0 && trustedtime::calls == 0);
  Rtc::value = {2026, 1, 2, 12, 0, 0, 5};
  assert(readersessionclock::now() == utc);

  // An RTC-capable board with a failed boot probe must not use the loan floor.
  Rtc::probeSucceeded = false;
  halClock.begin();
  const unsigned before = Rtc::reads;
  assert(halClock.hasRtc() && !halClock.isAvailable());
  assert(readersessionclock::now() == 0);
  assert(trustedtime::calls == 0 && Rtc::reads == before);

  // Boards without an RTC retain their established clock behavior and do no I2C reads.
  BoardConfig::rtcSupported = false;
  assert(readersessionclock::now() == trustedtime::floor);
  assert(trustedtime::calls == 1 && Rtc::reads == before);
}
'''


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="reader-session-clock-") as directory:
        work = Path(directory)
        for name, content in STUBS.items():
            (work / name).write_text(content)
        (work / "check.cpp").write_text(CHECK)
        subprocess.run([
            "c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
            f"-I{work}", f"-I{ROOT / 'lib/hal'}", f"-I{ROOT / 'src'}",
            str(ROOT / "lib/hal/HalClock.cpp"),
            str(ROOT / "src/activities/reader/ReaderSession.cpp"), str(work / "check.cpp"),
            "-o", str(work / "check"),
        ], check=True)
        subprocess.run([str(work / "check")], check=True)
    print("RTC reading-session clock checks passed")
