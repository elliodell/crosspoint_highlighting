#pragma once
#include "../../library_builder/stubs/HalStorage.h"
inline constexpr int O_EXCL = 64;
class HighlightStorage : public HalStorage {
 public:
  bool ensureDirectoryExists(const char* p) {
    if (!exists(p)) {
      fake::add(p, "");
      fake::files[p]->directory = true;
    }
    return true;
  }
};
inline HighlightStorage highlightStorage;
#undef Storage
#define Storage highlightStorage
