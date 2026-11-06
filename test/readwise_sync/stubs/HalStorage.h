#pragma once
#include "../../library_builder/stubs/HalStorage.h"
namespace fake {
inline bool storageReady = true;
}
class ReadwiseStorage : public HalStorage {
 public:
  bool ready() const { return fake::storageReady; }
  bool ensureDirectoryExists(const char* path) { return mkdir(path); }
  bool replaceFile(const char* from, const char* to) {
    if (fake::fail(fake::failRename) || !exists(from)) return false;
    remove(to);
    return rename(from, to);
  }
};
inline ReadwiseStorage readwiseStorage;
#undef Storage
#define Storage readwiseStorage
