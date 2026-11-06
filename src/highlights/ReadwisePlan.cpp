#include "ReadwisePlan.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr char ROOT[] = "/.crosspoint/readwise/highlights-v2";
constexpr char PARAGRAPH_BREAK[] = "\xe2\x80\xa9";
constexpr size_t MAX_HEADER_BYTES = 4096;
constexpr size_t MAX_PARAGRAPHS = 1024;
constexpr unsigned MAX_PARTS = 128;
constexpr size_t MAX_GROUPS = 32;
constexpr size_t MAX_HIGHLIGHT_BYTES = 8191;
constexpr size_t MAX_TITLE_BYTES = 511;
constexpr size_t MAX_AUTHOR_BYTES = 1024;
constexpr uint32_t PLAN_BUDGET_MS = 30000;

bool canAllocate(size_t bytes) {
  const auto heap = HalMemory::getDefaultHeap();
  return heap.largestBlockBytes >= bytes && heap.freeBytes >= bytes + 64 * 1024;
}

bool digest(const char* data, size_t size, char (&hex)[65], const char* second = nullptr) {
  auto context = makeUniqueNoThrow<mbedtls_sha256_context>();
  if (!context) return false;
  mbedtls_sha256_init(context.get());
  bool ok = mbedtls_sha256_starts(context.get(), 0) == 0 &&
            mbedtls_sha256_update(context.get(), reinterpret_cast<const unsigned char*>(data), size) == 0;
  if (second) {
    const unsigned char separator = 0;
    ok = ok && mbedtls_sha256_update(context.get(), &separator, 1) == 0 &&
         mbedtls_sha256_update(context.get(), reinterpret_cast<const unsigned char*>(second), strlen(second)) == 0;
  }
  unsigned char bytes[32];
  ok = ok && mbedtls_sha256_finish(context.get(), bytes) == 0;
  mbedtls_sha256_free(context.get());
  if (!ok) return false;
  constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    hex[2 * i] = digits[bytes[i] >> 4];
    hex[2 * i + 1] = digits[bytes[i] & 15];
  }
  hex[64] = '\0';
  return true;
}

bool validHex(const char* value, size_t length) {
  if (!value || strlen(value) != length) return false;
  for (const char* p = value; *p; ++p) {
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
  }
  return true;
}

bool validText(JsonVariantConst value, size_t limit, bool allowEmpty = false) {
  const JsonString text = value.as<JsonString>();
  return !text.isNull() && text.size() <= limit && (allowEmpty || text.size() > 0) &&
         strlen(text.c_str()) == text.size();
}

void pathFor(char (&path)[176], const char* directory, const char* name) {
  snprintf(path, sizeof(path), "%s/%s", directory, name);
}

bool pathExists(const char* directory, const char* name) {
  char path[176];
  pathFor(path, directory, name);
  return Storage.exists(path);
}

HalFile openPath(const char* directory, const char* name) {
  char path[176];
  pathFor(path, directory, name);
  return Storage.open(path);
}

bool openWrite(const char* directory, const char* name, HalFile& file) {
  char path[176];
  pathFor(path, directory, name);
  return Storage.openFileForWrite("READWISE", path, file);
}

bool removePath(const char* directory, const char* name) {
  char path[176];
  pathFor(path, directory, name);
  return Storage.remove(path);
}

// Checksummed header line, then exact request bytes. The immutable request and
// its separate acknowledgement never share an SD cluster chain through rename.
bool readHeader(HalFile& file, const char* directory, JsonDocument& header, size_t& payloadOffset,
                char* headerHash = nullptr) {
  if (!file || file.isDirectory() || file.size() < 68 ||
      file.size() > MAX_HEADER_BYTES + 66 + readwiseplan::MAX_PAYLOAD_BYTES || !canAllocate(MAX_HEADER_BYTES + 67))
    return false;
  auto buffer = makeUniqueNoThrow<char[]>(MAX_HEADER_BYTES + 67);
  if (!buffer) return false;
  const size_t size = std::min(file.size(), MAX_HEADER_BYTES + 66);
  if (file.read(buffer.get(), size) != int(size) || buffer[64] != '\n') return false;
  buffer[64] = '\0';
  if (!validHex(buffer.get(), 64)) return false;
  size_t end = 65;
  while (end < size && buffer[end] != '\n') ++end;
  if (end == size || end - 65 > MAX_HEADER_BYTES) return false;
  char actual[65];
  if (!digest(buffer.get() + 65, end - 65, actual) || strcmp(actual, buffer.get()) != 0) return false;
  // Const input copies the small header strings; buffer is released on return.
  if (deserializeJson(header, static_cast<const char*>(buffer.get() + 65), end - 65,
                      DeserializationOption::NestingLimit(3)) ||
      header.overflowed())
    return false;
  if (header["version"] != 1 || !validHex(header["id"], 32) || !validHex(header["account"], 64) ||
      strcmp(header["account"], strrchr(directory, '/') + 1) != 0 || !validHex(header["book"], 64) ||
      !validHex(header["source"], 64) || !validHex(header["payload_hash"], 64) ||
      !header["payload_size"].is<unsigned>() || !header["parts"].is<unsigned>() ||
      header["parts"].as<unsigned>() > MAX_PARTS)
    return false;
  const auto groups = header["groups"].as<JsonArrayConst>();
  if (groups.isNull() || groups.size() == 0 || groups.size() > MAX_GROUPS) return false;
  for (const auto group : groups) {
    if (!validHex(group, 32)) return false;
  }
  if (headerHash) memcpy(headerHash, actual, 65);
  const size_t payloadSize = header["payload_size"];
  payloadOffset = end + 1;
  return payloadSize <= readwiseplan::MAX_PAYLOAD_BYTES && file.size() == payloadOffset + payloadSize &&
         ((header["parts"].as<unsigned>() == 0) == (payloadSize == 0));
}

bool readReceipt(const char* directory, const char* id, JsonDocument& header) {
  char name[41];
  snprintf(name, sizeof(name), "%s.request", id);
  HalFile request = openPath(directory, name);
  size_t offset = 0;
  char hash[65];
  if (!readHeader(request, directory, header, offset, hash) || strcmp(header["id"], id) != 0) return false;
  request.close();
  snprintf(name, sizeof(name), "%s.ack", id);
  HalFile acknowledgement = openPath(directory, name);
  char marker[64];
  return acknowledgement && !acknowledgement.isDirectory() && acknowledgement.size() == sizeof(marker) &&
         acknowledgement.read(marker, sizeof(marker)) == int(sizeof(marker)) && memcmp(marker, hash, 64) == 0;
}

enum class PendingScan { Empty, Ready, Deferred };

PendingScan pendingId(const char* directory, char (&id)[33]) {
  HalFile requests = Storage.open(directory);
  if (!requests || !requests.isDirectory()) return PendingScan::Deferred;
  const uint32_t started = millis();
  for (;;) {
    HalFile file = requests.openNextFile();
    if (!file) return PendingScan::Empty;
    if (millis() - started > PLAN_BUDGET_MS) return PendingScan::Deferred;
    delay(1);
    if (file.isDirectory()) continue;
    char name[48] = {};
    file.getName(name, sizeof(name));
    if (strlen(name) != 40 || strcmp(name + 32, ".request") != 0) continue;
    name[32] = '\0';
    if (!readwiseplan::validId(name) || readwiseplan::alreadySent(directory, name)) continue;
    memcpy(id, name, 33);
    return PendingScan::Ready;
  }
}

struct Paragraph {
  size_t offset = 0;
  size_t size = 0;
  unsigned spine = 0;
  bool covered = false;
};

struct Capture {
  std::unique_ptr<char[]> source;
  std::unique_ptr<char[]> text;
  std::unique_ptr<Paragraph[]> paragraphs;
  JsonDocument doc;
  size_t count = 0;
  size_t used = 0;
  char sourceHash[65] = {};
  char bookHash[65] = {};
  bool canonical = false;
};

bool loadCapture(const char* id, Capture& capture) {
  char path[64];
  snprintf(path, sizeof(path), "/Highlights/%s.json", id);
  HalFile file = Storage.open(path);
  if (!file || file.isDirectory() || file.size() == 0 || file.size() > readwiseplan::MAX_SOURCE_BYTES) return false;
  const size_t size = file.size();
  if (!canAllocate(size + 1)) return false;
  capture.source = makeUniqueNoThrow<char[]>(size + 1);
  if (!capture.source || file.read(capture.source.get(), size) != int(size) ||
      !digest(capture.source.get(), size, capture.sourceHash))
    return false;
  capture.source[size] = '\0';
  file.close();
  auto& doc = capture.doc;
  if (deserializeJson(doc, capture.source.get(), size, DeserializationOption::NestingLimit(4)) || doc.overflowed() ||
      !validText(doc["id"], 32) || strcmp(doc["id"], id) != 0 || !validText(doc["book_title"], 4096, true) ||
      !validText(doc["author"], 4096, true) || !validText(doc["chapter"], 4096, true) || !doc["page"].is<unsigned>() ||
      doc["page"].as<unsigned>() == 0)
    return false;
  if (!doc["text_format"].isNull() && doc["text_format"] != 2) return false;
  capture.canonical = doc["text_format"] == 2;
  const JsonArrayConst pages = doc["pages"];
  if (pages.isNull() || pages.size() == 0 || pages.size() > 64) return false;
  unsigned selected = 0;
  for (const auto page : pages) {
    if (!validText(page["text"], 16 * 1024, true) || !validText(page["chapter"], 4096, true) ||
        !page["page"].is<unsigned>() || page["page"].as<unsigned>() == 0 || !page["current"].is<bool>())
      return false;
    selected += page["current"].as<bool>();
    if (capture.canonical) {
      if (!page["spine"].is<unsigned>() || !validText(page["joiner"], 3, true)) return false;
      const char* joiner = page["joiner"];
      if (*joiner && strcmp(joiner, " ") != 0 && strcmp(joiner, "\n") != 0 && strcmp(joiner, PARAGRAPH_BREAK) != 0)
        return false;
    }
  }
  if (selected != 1 || !digest(doc["book_title"], strlen(doc["book_title"]), capture.bookHash, doc["author"]))
    return false;
  // Canonical text and bounded paragraph spans are needed only while planning.
  // The prior capture is loaded one at a time; all of this is freed before TLS.
  if (!canAllocate(size + 1 + MAX_PARAGRAPHS * sizeof(Paragraph))) return false;
  capture.text = makeUniqueNoThrow<char[]>(size + 1);
  capture.paragraphs = makeUniqueNoThrow<Paragraph[]>(MAX_PARAGRAPHS);
  if (!capture.text || !capture.paragraphs) return false;
  bool boundary = true;
  auto append = [&](const char* text, unsigned spine) {
    while (*text) {
      if (capture.canonical && strncmp(text, PARAGRAPH_BREAK, 3) == 0) {
        boundary = true;
        text += 3;
        continue;
      }
      if (boundary) {
        if (capture.count == MAX_PARAGRAPHS) return false;
        if (capture.count) {
          if (capture.used + 2 > size) return false;
          capture.text[capture.used++] = '\n';
          capture.text[capture.used++] = '\n';
        }
        auto& paragraph = capture.paragraphs[capture.count++];
        paragraph.offset = capture.used;
        paragraph.spine = spine;
        boundary = false;
      }
      if (capture.used == size) return false;
      capture.text[capture.used++] = *text++;
      ++capture.paragraphs[capture.count - 1].size;
    }
    return true;
  };
  for (const auto page : pages) {
    const char* text = page["text"];
    if (!*text) continue;
    const unsigned spine = page["spine"] | 0U;
    if (capture.count && capture.canonical) {
      if (spine != capture.paragraphs[capture.count - 1].spine) boundary = true;
      if (!append(page["joiner"], spine)) return false;
    } else if (capture.count) {
      boundary = true;  // Legacy page boundaries are unknown; never deduplicate them.
    }
    if (!append(text, spine)) return false;
  }
  capture.text[capture.used] = '\0';
  return capture.count != 0;
}

bool addGroup(JsonArray groups, const char* id) {
  for (const auto group : groups) {
    if (strcmp(group, id) == 0) return true;
  }
  return groups.size() < MAX_GROUPS && groups.add(JsonString(id, false));
}

bool markCovered(const char* directory, Capture& capture, JsonArray groups) {
  if (!capture.canonical) return true;
  HalFile receipts = Storage.open(directory);
  if (!receipts || !receipts.isDirectory()) return false;
  const uint32_t started = millis();
  for (;;) {
    HalFile file = receipts.openNextFile();
    if (!file) return true;
    if (millis() - started > PLAN_BUDGET_MS) return false;
    delay(1);
    if (file.isDirectory()) continue;
    char id[40] = {};
    file.getName(id, sizeof(id));
    if (strlen(id) != 36 || strcmp(id + 32, ".ack") != 0) continue;
    id[32] = '\0';
    if (!readwiseplan::validId(id)) continue;
    file.close();
    JsonDocument header;
    if (!readReceipt(directory, id, header)) return false;
    if (strcmp(header["book"], capture.bookHash) != 0) continue;
    auto prior = makeUniqueNoThrow<Capture>();
    if (!prior || !loadCapture(id, *prior)) return false;
    if (strcmp(header["source"], prior->sourceHash) != 0 || strcmp(header["book"], prior->bookHash) != 0) return false;
    if (!prior->canonical) continue;
    bool overlap = false;
    for (size_t i = 0; i < capture.count; ++i) {
      auto& current = capture.paragraphs[i];
      for (size_t j = 0; j < prior->count; ++j) {
        const auto& previous = prior->paragraphs[j];
        if (current.spine == previous.spine && current.size == previous.size &&
            memcmp(capture.text.get() + current.offset, prior->text.get() + previous.offset, current.size) == 0) {
          current.covered = true;
          overlap = true;
          break;
        }
      }
    }
    if (overlap) {
      for (const auto group : header["groups"].as<JsonArrayConst>()) {
        if (!addGroup(groups, group)) return false;
      }
    }
  }
}

class PayloadWriter {
 public:
  PayloadWriter(char* buffer = nullptr, size_t capacity = 0) : buffer(buffer), capacity(capacity) {}
  void raw(const char* value) { raw(value, strlen(value)); }
  void raw(const char* value, size_t length) {
    if (buffer && length <= capacity - std::min(count, capacity)) memcpy(buffer + count, value, length);
    count += length;
  }
  void text(const char* value) { text(value, strlen(value)); }
  void text(const char* value, size_t length) {
    for (size_t i = 0; i < length; ++i) {
      const auto byte = static_cast<unsigned char>(value[i]);
      if (byte == '"')
        raw("\\\"");
      else if (byte == '\\')
        raw("\\\\");
      else if (byte < 32) {
        char escaped[7];
        snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
        raw(escaped);
      } else
        raw(value + i, 1);
    }
  }
  void number(unsigned value) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%u", value);
    raw(digits);
  }
  size_t size() const { return count; }

 private:
  char* buffer;
  size_t capacity;
  size_t count = 0;
};

size_t utf8Prefix(const char* text, size_t limit) {
  size_t length = std::min(strlen(text), limit);
  while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
  return length;
}

size_t chunkEnd(const char* text, size_t start, size_t limit) {
  if (limit - start <= MAX_HIGHLIGHT_BYTES) return limit;
  size_t end = start + MAX_HIGHLIGHT_BYTES;
  while (end > start && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
  size_t whitespace = 0;
  size_t paragraph = 0;
  for (size_t i = start + MAX_HIGHLIGHT_BYTES / 2; i < end; ++i) {
    if (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r') whitespace = i + 1;
    if (text[i] == '\n' && text[i - 1] == '\n') paragraph = i + 1;
  }
  return paragraph ? paragraph : whitespace ? whitespace : end;
}

// Covered paragraphs separate runs; unique edges never get joined across an
// omitted middle. Each split part has a stable capture ID and note-only IDs.
unsigned writePayload(PayloadWriter& out, const Capture& capture, JsonArrayConst groups, unsigned totalParts) {
  const auto& doc = capture.doc;
  out.raw("{\"highlights\":[");
  unsigned part = 0;
  for (size_t index = 0; index < capture.count;) {
    if (capture.paragraphs[index].covered) {
      ++index;
      continue;
    }
    size_t start = capture.paragraphs[index].offset;
    size_t end = start;
    do {
      end = capture.paragraphs[index].offset + capture.paragraphs[index].size;
      ++index;
    } while (index < capture.count && !capture.paragraphs[index].covered);
    while (start < end) {
      if (++part > MAX_PARTS) return 0;
      if (part > 1) out.raw(",");
      out.raw("{\"title\":\"");
      out.text(doc["book_title"], utf8Prefix(doc["book_title"], MAX_TITLE_BYTES));
      out.raw("\",\"author\":\"");
      out.text(doc["author"], utf8Prefix(doc["author"], MAX_AUTHOR_BYTES));
      out.raw(
          "\",\"source_type\":\"crosspoint_highlighting\",\"category\":\"books\",\"location_type\":\"page\","
          "\"location\":");
      out.number(doc["page"]);
      out.raw(",\"highlight_url\":\"https://crosspoint-highlighting.invalid/highlights/");
      out.text(doc["id"]);
      out.raw("/part-");
      out.number(part);
      out.raw("\",\"note\":\"crosspoint:");
      out.text(doc["id"]);
      out.raw(" ");
      out.number(part);
      out.raw("/");
      out.number(totalParts);
      for (const auto group : groups) {
        out.raw("\\nmerge:");
        out.text(group);
      }
      out.raw("\\n");
      out.text(doc["chapter"], utf8Prefix(doc["chapter"], MAX_TITLE_BYTES));
      out.raw(" — ");
      out.text(tr(STR_HIGHLIGHT_PAGE_LABEL));
      out.raw(" ");
      out.number(doc["page"]);
      out.raw(" ");
      out.text(tr(STR_HIGHLIGHT_SELECTED_PAGE));
      out.raw("\\n#crosspoint-highlight\",\"text\":\"");
      const size_t next = chunkEnd(capture.text.get(), start, end);
      out.text(capture.text.get() + start, next - start);
      out.raw("\"}");
      start = next;
    }
  }
  out.raw("]}");
  return part;
}

bool publish(const char* directory, JsonDocument& header, const char* payload, size_t payloadSize) {
  const size_t size = measureJson(header);
  if (header.overflowed() || size == 0 || size > MAX_HEADER_BYTES || !canAllocate(size + 1)) return false;
  auto encoded = makeUniqueNoThrow<char[]>(size + 1);
  if (!encoded || serializeJson(header, encoded.get(), size + 1) != size) return false;
  char hash[65];
  if (!digest(encoded.get(), size, hash)) return false;
  char name[41];
  snprintf(name, sizeof(name), "%s.request", header["id"].as<const char*>());
  if (pathExists(directory, name)) return false;
  HalFile file;
  if (!openWrite(directory, name, file)) return false;
  const bool wrote = file.write(hash, 64) == 64 && file.write("\n", 1) == 1 &&
                     file.write(encoded.get(), size) == size && file.write("\n", 1) == 1 &&
                     (payloadSize == 0 || file.write(payload, payloadSize) == payloadSize);
  const bool closed = file.close();
  if (!wrote || !closed) {
    removePath(directory, name);  // No POST can have used this failed preparation.
    return false;
  }
  return true;
}
}  // namespace

bool readwiseplan::validId(const char* id) { return validHex(id, 32); }

bool readwiseplan::accountDirectory(const char* token, char (&directory)[128]) {
  char hash[65];
  if (!digest(token, strlen(token), hash)) return false;
  snprintf(directory, sizeof(directory), "%s/%s", ROOT, hash);
  return Storage.ensureDirectoryExists(directory);
}

bool readwiseplan::hasPending(const char* directory) {
  char id[33];
  return pendingId(directory, id) != PendingScan::Empty;
}

bool readwiseplan::alreadySent(const char* directory, const char* id) {
  JsonDocument header;
  return readReceipt(directory, id, header);
}

bool readwiseplan::prepare(const char* directory, const char* id) {
  if (!validId(id) || hasPending(directory)) return false;
  char name[41];
  snprintf(name, sizeof(name), "%s.request", id);
  if (pathExists(directory, name)) return false;
  auto capture = makeUniqueNoThrow<Capture>();
  if (!capture || !loadCapture(id, *capture)) return false;
  JsonDocument header;
  header["version"] = 1;
  header["id"] = JsonString(id, false);
  header["account"] = JsonString(strrchr(directory, '/') + 1, false);
  header["book"] = JsonString(capture->bookHash, false);
  header["source"] = JsonString(capture->sourceHash, false);
  const JsonArray groups = header["groups"].to<JsonArray>();
  if (!markCovered(directory, *capture, groups)) return false;
  if (groups.size() == 0 && !addGroup(groups, id)) return false;
  PayloadWriter count;
  const unsigned parts = writePayload(count, *capture, groups, 0);
  bool allCovered = true;
  for (size_t i = 0; i < capture->count; ++i) allCovered &= capture->paragraphs[i].covered;
  if (!parts && !allCovered) return false;
  PayloadWriter measure;
  writePayload(measure, *capture, groups, parts);
  const size_t size = parts ? measure.size() : 0;
  if (size > MAX_PAYLOAD_BYTES || !canAllocate(size + 1)) return false;
  auto payload = makeUniqueNoThrow<char[]>(size + 1);
  if (!payload) return false;
  if (parts) {
    PayloadWriter writer(payload.get(), size);
    if (writePayload(writer, *capture, groups, parts) != parts || writer.size() != size) return false;
  }
  char hash[65];
  if (!digest(payload.get(), size, hash)) return false;
  header["parts"] = parts;
  header["payload_size"] = size;
  header["payload_hash"] = JsonString(hash, false);
  capture.reset();
  return publish(directory, header, payload.get(), size);
}

bool readwiseplan::resume(const char* directory, Upload& upload) {
  if (pendingId(directory, upload.id) != PendingScan::Ready) return false;
  char name[41];
  snprintf(name, sizeof(name), "%s.request", upload.id);
  HalFile file = openPath(directory, name);
  JsonDocument header;
  size_t offset = 0;
  if (!readHeader(file, directory, header, offset, upload.acknowledgement) || strcmp(header["id"], upload.id) != 0)
    return false;
  const size_t size = header["payload_size"];
  if (!canAllocate(size + 1)) return false;
  upload.payload = makeUniqueNoThrow<char[]>(size + 1);
  if (!upload.payload || !file.seekSet(offset) || (size && file.read(upload.payload.get(), size) != int(size)))
    return false;
  char hash[65];
  if (!digest(upload.payload.get(), size, hash) || strcmp(header["payload_hash"], hash) != 0) return false;
  upload.size = size;
  upload.parts = header["parts"];
  return true;
}

bool readwiseplan::acknowledge(const char* directory, const Upload& upload) {
  char name[37];
  snprintf(name, sizeof(name), "%s.ack", upload.id);
  HalFile file;
  if (!openWrite(directory, name, file)) return false;
  const bool wrote = file.write(upload.acknowledgement, 64) == 64;
  const bool closed = file.close();
  // An interrupted/short marker never suppresses the immutable request. A
  // complete matching marker is safe even if power failed before close returned.
  return wrote && closed;
}
