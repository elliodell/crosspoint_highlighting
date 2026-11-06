#pragma once
#include <cstdint>
namespace fake {
inline uint32_t now = 0;
}
inline uint32_t millis() { return fake::now; }

inline void delay(uint32_t milliseconds) { fake::now += milliseconds; }
