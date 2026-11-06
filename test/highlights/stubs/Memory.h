#pragma once
#include "../../library_builder/stubs/Memory.h"
template <typename F>
struct ScopedCleanup {
  F fn;
  explicit ScopedCleanup(F f) : fn(f) {}
  ~ScopedCleanup() { fn(); }
};
