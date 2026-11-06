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

#include "ReadwisePlan.h"
#include "ReadwiseRoot.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
constexpr char CONFIG_PATH[] = "/.crosspoint/readwise/config.json";
constexpr char API_URL[] = "https://readwise.io/api/v2/highlights/";
constexpr size_t MAX_CONFIG_BYTES = 1024;
constexpr size_t MAX_RESPONSE_BYTES = 16 * 1024;
constexpr uint32_t RETRY_MIN_MS = 30000;
constexpr uint32_t RETRY_MAX_MS = 15 * 60 * 1000;
constexpr uint32_t REQUEST_BUDGET_MS = 15000;
constexpr size_t SCAN_BATCH = 32;
uint32_t nextPollAt = 0;
uint32_t retryMs = RETRY_MIN_MS;
size_t scanPosition = 0;

bool validText(JsonVariantConst value, size_t limit) {
  const JsonString text = value.as<JsonString>();
  return !text.isNull() && text.size() > 0 && text.size() <= limit && strlen(text.c_str()) == text.size();
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

bool nextCapture(const char* directoryPath, char* id) {
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
    if (!readwiseplan::validId(filename) || readwiseplan::alreadySent(directoryPath, filename)) continue;
    memcpy(id, filename, 33);
    return true;
  }
  nextPollAt = millis() + 100;
  return false;
}

bool upload(const char* directory, const char* token, uint32_t& retryAfterMs) {
  readwiseplan::Upload upload;
  if (!readwiseplan::resume(directory, upload)) {
    LOG_ERR("READWISE", "Cannot read pending upload; original captures retained");
    return false;
  }
  if (upload.parts == 0) return readwiseplan::acknowledge(directory, upload);
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
      "POST", reinterpret_cast<const uint8_t*>(upload.payload.get()), upload.size,
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
  if (status != 200 || !http->responseComplete()) {
    LOG_ERR("READWISE", "Upload deferred: HTTP %d, complete=%d", status, http->responseComplete());
    if (status == 401 || status == 403) retryAfterMs = std::max(retryAfterMs, RETRY_MAX_MS);
    return false;
  }
  response[received] = '\0';
  JsonDocument doc;
  JsonDocument filter;
  filter[0]["id"] = true;
  if (filter.overflowed() ||
      deserializeJson(doc, response.get(), received, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(8)) ||
      !doc.is<JsonArray>() || doc.size() != 1 || !doc[0]["id"].is<uint64_t>() || doc[0]["id"].as<uint64_t>() == 0) {
    LOG_ERR("READWISE", "Missing upload acknowledgement");
    return false;
  }
  // Exact duplicates may have no modified_highlights; HTTP 200 acknowledges them.
  if (!readwiseplan::acknowledge(directory, upload)) return false;
  LOG_INF("READWISE", "Uploaded capture %s as %u Readwise highlights", upload.id, upload.parts);
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
  char directory[128];
  if (!readwiseplan::accountDirectory(config["token"], directory)) return;
  if (!readwiseplan::hasPending(directory)) {
    char id[33];
    if (!nextCapture(directory, id)) return;
    if (!readwiseplan::prepare(directory, id)) {
      LOG_ERR("READWISE", "Cannot prepare capture %s; original retained", id);
      return;
    }
  }
  uint32_t retryAfterMs = 0;
  if (upload(directory, config["token"], retryAfterMs)) {
    retryMs = RETRY_MIN_MS;
    nextPollAt = millis() + 3000;
  } else {
    nextPollAt = millis() + std::max(retryMs, retryAfterMs);
    retryMs = std::min(retryMs * 2, RETRY_MAX_MS);
  }
}
