#pragma once
#include <cstddef>
namespace fake {
inline size_t heapBytes = 2 * 1024 * 1024;
}
struct HalMemory {
  struct Heap {
    size_t freeBytes;
    size_t largestBlockBytes;
  };
  static Heap getDefaultHeap() { return {fake::heapBytes, fake::heapBytes}; }
  static Heap getInternalHeap() { return {fake::heapBytes, fake::heapBytes}; }
};
