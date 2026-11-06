#include "ReadwiseSync.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "ReadwiseRoot.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
constexpr char CONFIG_PATH[] = "/.crosspoint/readwise/config.json";
constexpr char SENT_DIR[] = "/.crosspoint/readwise/sent";
constexpr char API_URL[] = "https://readwise.io/api/v3/save/";
constexpr size_t MAX_SOURCE_BYTES = 96 * 1024;
constexpr size_t MAX_PAYLOAD_BYTES = 6 * MAX_SOURCE_BYTES + 4096;
constexpr size_t MAX_CONFIG_BYTES = 1024;
constexpr size_t MAX_RESPONSE_BYTES = 1024;
constexpr uint32_t RETRY_MIN_MS = 30000;
constexpr uint32_t RETRY_MAX_MS = 15 * 60 * 1000;
constexpr uint32_t REQUEST_BUDGET_MS = 15000;
constexpr size_t SCAN_BATCH = 32;
uint32_t nextPollAt = 0;
uint32_t retryMs = RETRY_MIN_MS;
size_t scanPosition = 0;

// Count before allocating, then write exactly once. The JSON-escaped HTML is
// built directly into the request buffer, without a second full text copy.
class PayloadWriter {
 public:
  PayloadWriter(char* buffer = nullptr, size_t capacity = 0) : buffer(buffer), capacity(capacity) {}
  void raw(const char* value) { raw(value, strlen(value)); }
  void raw(const char* value, size_t length) {
    if (buffer && length <= capacity - std::min(count, capacity)) memcpy(buffer + count, value, length);
    count += length;
  }
  void text(const char* value, bool html = false) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
      if (html && *p == '&')
        raw("&amp;");
      else if (html && *p == '<')
        raw("&lt;");
      else if (html && *p == '>')
        raw("&gt;");
      else if (*p == '"')
        raw(html ? "&quot;" : "\\\"");
      else if (*p == '\\')
        raw("\\\\");
      else if (*p == '\n' && html)
        raw("<br/>");
      else if (*p < 0x20) {
        char escaped[7];
        snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
        raw(escaped);
      } else {
        raw(reinterpret_cast<const char*>(p), 1);
      }
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

bool validId(const char* id) {
  if (strlen(id) != 32) return false;
  for (const char* p = id; *p; ++p) {
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
  }
  return true;
}

bool validReaderId(const char* id, size_t size) {
  if (size == 0 || size > 128) return false;
  for (size_t i = 0; i < size; ++i) {
    if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9') ||
          id[i] == '-'))
      return false;
  }
  return true;
}

bool validText(JsonVariantConst value, size_t limit, bool allowEmpty = false) {
  const JsonString text = value.as<JsonString>();
  return !text.isNull() && text.size() <= limit && (allowEmpty || text.size() > 0) &&
         strlen(text.c_str()) == text.size();
}

bool validCapture(JsonDocument& doc, const char* id) {
  if (!validText(doc["id"], 32) || strcmp(doc["id"], id) != 0 || !validText(doc["book_title"], 4096, true) ||
      !validText(doc["author"], 4096, true) || !validText(doc["chapter"], 4096, true) || !doc["page"].is<unsigned>() ||
      doc["page"].as<unsigned>() == 0)
    return false;
  const JsonArrayConst pages = doc["pages"].as<JsonArrayConst>();
  if (pages.isNull() || pages.size() == 0 || pages.size() > 5) return false;
  unsigned currentCount = 0;
  for (JsonObjectConst page : pages) {
    if (!validText(page["text"], 16 * 1024, true) || !validText(page["chapter"], 4096, true) ||
        !page["page"].is<unsigned>() || page["page"].as<unsigned>() == 0 || !page["current"].is<bool>())
      return false;
    if (page["current"].as<bool>()) ++currentCount;
  }
  return currentCount == 1;
}

void writePayload(PayloadWriter& out, JsonDocument& doc) {
  out.raw("{\"url\":\"https://crosspoint-highlighting.invalid/highlights/");
  out.text(doc["id"]);
  out.raw("\",\"title\":\"");
  out.text(doc["book_title"]);
  out.raw(" — ");
  out.text(tr(STR_HIGHLIGHT_TITLE_SUFFIX));
  out.raw(" ");
  out.number(doc["page"]);
  out.raw("\",\"author\":\"");
  out.text(doc["author"]);
  out.raw(
      "\",\"should_clean_html\":false,\"category\":\"article\",\"location\":\"new\","
      "\"saved_using\":\"CrossPoint Highlighting\",\"tags\":[\"crosspoint-highlight\"],\"html\":\""
      "<!doctype html><html><body><h1>");
  out.text(doc["book_title"], true);
  out.raw("</h1><p>");
  out.text(tr(STR_HIGHLIGHT_EXCERPT_DESCRIPTION), true);
  out.raw("</p>");
  for (JsonObjectConst page : doc["pages"].as<JsonArrayConst>()) {
    out.raw("<section><h2>");
    out.text(page["chapter"], true);
    out.raw(" — ");
    out.text(tr(STR_HIGHLIGHT_PAGE_LABEL), true);
    out.raw(" ");
    out.number(page["page"]);
    if (page["current"].as<bool>()) {
      out.raw(" ");
      out.text(tr(STR_HIGHLIGHT_SELECTED_PAGE), true);
    }
    out.raw("</h2><p>");
    out.text(page["text"], true);
    out.raw("</p></section>");
  }
  out.raw("</body></html>\"}");
}

bool readConfig(char* buffer, JsonDocument& config) {
  HalFile file = Storage.open(CONFIG_PATH);
  if (!file) return false;  // No config is the normal disabled state.
  const size_t size = file.size();
  if (file.isDirectory() || size == 0 || size > MAX_CONFIG_BYTES || file.read(buffer, size) != int(size)) {
    LOG_ERR("READWISE", "Cannot read bounded configuration file");
    return false;
  }
  if (deserializeJson(config, buffer, size, DeserializationOption::NestingLimit(2))) {
    LOG_ERR("READWISE", "Invalid configuration JSON");
    return false;
  }
  if (!config["enabled"].is<bool>() || !config["enabled"].as<bool>()) return false;
  if (!validText(config["token"], 256)) {
    LOG_ERR("READWISE", "Missing or oversized access token");
    return false;
  }
  const char* token = config["token"];
  for (const char* p = token; *p; ++p) {
    if (*p <= ' ' || *p > '~') {
      LOG_ERR("READWISE", "Invalid access token characters");
      return false;
    }
  }
  return true;
}

bool alreadySent(const char* id) {
  char path[80];
  snprintf(path, sizeof(path), "%s/%s", SENT_DIR, id);
  HalFile file = Storage.open(path);
  if (!file || file.isDirectory() || file.size() == 0 || file.size() > 128) return false;
  char marker[129];
  const size_t size = file.size();
  if (file.read(marker, size) != int(size)) return false;
  return validReaderId(marker, size);
}

bool markSent(const char* id, const char* readerId) {
  char path[80];
  char temp[88];
  snprintf(path, sizeof(path), "%s/%s", SENT_DIR, id);
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  if (!Storage.ensureDirectoryExists(SENT_DIR)) return false;
  HalFile file;
  if (!Storage.openFileForWrite("READWISE", temp, file)) return false;
  const size_t length = strlen(readerId);
  const bool wrote = file.write(reinterpret_cast<const uint8_t*>(readerId), length) == length;
  const bool closed = file.close();
  if (!wrote || !closed || !Storage.replaceFile(temp, path)) {
    LOG_ERR("READWISE", "Unable to save upload acknowledgement");
    return false;
  }
  return true;
}

bool nextCapture(char* id) {
  HalFile directory = Storage.open("/Highlights");
  if (!directory || !directory.isDirectory()) {
    scanPosition = 0;
    return false;
  }
  if (scanPosition && !directory.seekSet(scanPosition)) scanPosition = 0;
  for (size_t i = 0; i < SCAN_BATCH; ++i) {
    HalFile file = directory.openNextFile();
    scanPosition = directory.position();
    if (!file) {
      scanPosition = 0;
      nextPollAt = millis() + RETRY_MIN_MS;
      return false;
    }
    if (file.isDirectory()) continue;
    char filename[40] = {};
    file.getName(filename, sizeof(filename));
    if (strlen(filename) != 37 || strcmp(filename + 32, ".json") != 0) continue;
    filename[32] = '\0';
    if (!validId(filename) || alreadySent(filename)) continue;
    memcpy(id, filename, 33);
    return true;
  }
  nextPollAt = millis() + 100;
  return false;
}

bool upload(const char* id, const char* token, uint32_t& retryAfterMs) {
  char path[64];
  snprintf(path, sizeof(path), "/Highlights/%s.json", id);
  HalFile source = Storage.open(path);
  if (!source || source.isDirectory() || source.size() == 0 || source.size() > MAX_SOURCE_BYTES) {
    LOG_ERR("READWISE", "Invalid highlight file %s", id);
    return false;
  }
  const size_t sourceSize = source.size();
  // These cold-path buffers live only for one upload. Guard both the largest
  // contiguous block and internal TLS working space before allocation.
  const auto heap = HalMemory::getDefaultHeap();
  if (heap.largestBlockBytes < sourceSize + 1 || heap.freeBytes < sourceSize + 64 * 1024) {
    LOG_ERR("READWISE", "Insufficient memory for %zu-byte capture", sourceSize);
    return false;
  }
  auto sourceBuffer = makeUniqueNoThrow<char[]>(sourceSize + 1);
  if (!sourceBuffer) {
    LOG_ERR("READWISE", "Cannot allocate %zu capture bytes", sourceSize + 1);
    return false;
  }
  if (source.read(sourceBuffer.get(), sourceSize) != int(sourceSize)) {
    LOG_ERR("READWISE", "Incomplete highlight file");
    return false;
  }
  sourceBuffer[sourceSize] = '\0';
  source.close();
  JsonDocument doc;
  if (deserializeJson(doc, sourceBuffer.get(), sourceSize, DeserializationOption::NestingLimit(4)) ||
      !validCapture(doc, id)) {
    LOG_ERR("READWISE", "Invalid capture JSON %s", id);
    return false;
  }
  PayloadWriter measure;
  writePayload(measure, doc);
  const size_t payloadSize = measure.size();
  const auto payloadHeap = HalMemory::getDefaultHeap();
  if (payloadSize > MAX_PAYLOAD_BYTES || payloadHeap.largestBlockBytes < payloadSize ||
      payloadHeap.freeBytes < payloadSize + 64 * 1024) {
    LOG_ERR("READWISE", "Insufficient memory for %zu-byte request", payloadSize);
    return false;
  }
  auto payload = makeUniqueNoThrow<char[]>(payloadSize);
  if (!payload) {
    LOG_ERR("READWISE", "Cannot allocate %zu request bytes", payloadSize);
    return false;
  }
  PayloadWriter writer(payload.get(), payloadSize);
  writePayload(writer, doc);
  doc.clear();
  sourceBuffer.reset();

  auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
  auto response = makeUniqueNoThrow<char[]>(MAX_RESPONSE_BYTES + 1);
  const auto internal = HalMemory::getInternalHeap();
  if (!http || !response || internal.freeBytes < 50 * 1024 || internal.largestBlockBytes < 20 * 1024) {
    LOG_ERR("READWISE", "Insufficient internal memory for secure upload");
    return false;
  }
  http->setCACert(READWISE_ROOT_CA);
  http->setTimeout(8000);
  http->setReuse(false);
  http->setUserAgent("CrossPoint-Highlighting");
  if (!http->begin(API_URL)) return false;
  http->addHeader("Authorization", std::string("Token ") + token);
  http->addHeader("Content-Type", "application/json");
  size_t received = 0;
  const uint32_t start = millis();
  WifiPowerSaveGuard powerGuard;
  const int status = http->sendRequest(
      "POST", reinterpret_cast<const uint8_t*>(payload.get()), payloadSize,
      [&](const uint8_t* bytes, size_t length) {
        if (length > MAX_RESPONSE_BYTES - received) return false;
        memcpy(response.get() + received, bytes, length);
        received += length;
        return true;
      },
      [start] { return WiFi.status() != WL_CONNECTED || millis() - start >= REQUEST_BUDGET_MS; });
  const std::string* retryHeader = http->getUniqueHeader("retry-after");
  if (retryHeader && !retryHeader->empty() && retryHeader->size() <= 6 &&
      retryHeader->find_first_not_of("0123456789") == std::string::npos) {
    retryAfterMs =
        std::min<uint32_t>(uint32_t(strtoul(retryHeader->c_str(), nullptr, 10)) * 1000U, 24U * 60 * 60 * 1000);
  }
  if ((status != 200 && status != 201) || !http->responseComplete()) {
    LOG_ERR("READWISE", "Upload deferred: HTTP %d, complete=%d", status, http->responseComplete());
    if (status == 401 || status == 403) retryAfterMs = std::max(retryAfterMs, RETRY_MAX_MS);
    return false;
  }
  response[received] = '\0';
  if (deserializeJson(doc, response.get(), received, DeserializationOption::NestingLimit(2)) ||
      !validText(doc["id"], 128) || !validReaderId(doc["id"], doc["id"].as<JsonString>().size())) {
    LOG_ERR("READWISE", "Missing upload acknowledgement");
    return false;
  }
  if (!markSent(id, doc["id"])) return false;
  LOG_INF("READWISE", "Uploaded highlight %s", id);
  return true;
}
}  // namespace

void readwisesync::poll() {
  if (WiFi.status() != WL_CONNECTED || !Storage.ready() || static_cast<int32_t>(millis() - nextPollAt) < 0) return;
  nextPollAt = millis() + RETRY_MIN_MS;
  // A valid clock is required for certificate date validation. Wi-Fi selection
  // already synchronizes the device clock before the network-idle hook.
  if (time(nullptr) < 1704067200) {
    LOG_ERR("READWISE", "Waiting for a valid device clock");
    return;
  }
  auto configBuffer = makeUniqueNoThrow<char[]>(MAX_CONFIG_BYTES + 1);
  if (!configBuffer) {
    LOG_ERR("READWISE", "Cannot allocate %zu configuration bytes", MAX_CONFIG_BYTES + 1);
    return;
  }
  JsonDocument config;
  if (!readConfig(configBuffer.get(), config)) return;
  char id[33];
  if (!nextCapture(id)) return;
  uint32_t retryAfterMs = 0;
  if (upload(id, config["token"], retryAfterMs)) {
    retryMs = RETRY_MIN_MS;
    nextPollAt = millis() + 3000;
  } else {
    nextPollAt = millis() + std::max(retryMs, retryAfterMs);
    retryMs = std::min(retryMs * 2, RETRY_MAX_MS);
  }
}
