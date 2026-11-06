#pragma once

#include <cstddef>
#include <memory>

namespace readwiseplan {
constexpr size_t MAX_SOURCE_BYTES = 96 * 1024;
constexpr size_t MAX_PAYLOAD_BYTES = 6 * MAX_SOURCE_BYTES + 4096;

struct Upload {
  std::unique_ptr<char[]> payload;
  size_t size = 0;
  unsigned parts = 0;
  char id[33] = {};
  char acknowledgement[65] = {};
};

bool validId(const char* id);
bool accountDirectory(const char* token, char (&directory)[128]);
bool hasPending(const char* directory);
bool alreadySent(const char* directory, const char* id);
// Planning retains original captures. Only acknowledged, unchanged captures
// can suppress matching source paragraphs in a later upload.
bool prepare(const char* directory, const char* id);
bool resume(const char* directory, Upload& upload);
bool acknowledge(const char* directory, const Upload& upload);
}  // namespace readwiseplan
