#include "ReadwiseBooks.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

#include "highlights/ReadwiseRoot.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
constexpr char CONFIG[] = "/.crosspoint/readwise-books/config.json";
constexpr char API[] = "https://readwise.io/books/api/v1/sessions";
constexpr char MODEL[] = "CrossPoint / Almanac";
constexpr size_t MAX_RECORD = 4096;
constexpr size_t MAX_RESPONSE = 12 * 1024;
constexpr uint32_t INTERVAL_MS = 60000;
uint32_t nextPoll = 0;

bool digest(const std::string& data, char (&hex)[65]) {
  auto ctx = makeUniqueNoThrow<mbedtls_sha256_context>();
  if (!ctx) return false;
  unsigned char hash[32];
  mbedtls_sha256_init(ctx.get());
  const bool ok =
      mbedtls_sha256_starts(ctx.get(), 0) == 0 &&
      mbedtls_sha256_update(ctx.get(), reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 0 &&
      mbedtls_sha256_finish(ctx.get(), hash) == 0;
  mbedtls_sha256_free(ctx.get());
  if (!ok) return false;
  for (size_t i = 0; i < sizeof(hash); ++i) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  return true;
}

bool readFile(const std::string& path, size_t limit, std::string& out) {
  HalFile file = Storage.open(path.c_str());
  if (!file || file.isDirectory() || file.size() == 0 || file.size() > limit) return false;
  out.resize(file.size());
  return file.read(out.data(), out.size()) == static_cast<int>(out.size());
}

bool readJson(const std::string& path, size_t limit, JsonDocument& doc) {
  std::string raw;
  return readFile(path, limit, raw) && !deserializeJson(doc, raw, DeserializationOption::NestingLimit(6));
}

// No rename dependency: immutable files are closed and read back before use.
// A partial existing file is retained for inspection, never silently replaced.
bool writeOnce(const std::string& path, const std::string& data) {
  std::string check;
  if (Storage.exists(path.c_str())) return readFile(path, data.size(), check) && check == data;
  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT);
  if (!file) return false;
  const bool wrote = file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
  const bool closed = file.close();
  return wrote && closed && readFile(path, data.size(), check) && check == data;
}

bool config(JsonDocument& doc, std::string& root) {
  if (!readJson(CONFIG, 1024, doc) || !doc["enabled"].is<bool>() || !doc["enabled"].as<bool>()) return false;
  const char* token = doc["token"] | "";
  const size_t length = strlen(token);
  if (!length || length > 256 || doc["token"].as<JsonString>().size() != length) return false;
  for (size_t i = 0; i < length; ++i)
    if (token[i] <= ' ' || token[i] > '~') return false;
  const char* zone = doc["timezone"] | "";
  if (strlen(zone) > 63) return false;
  char hash[65];
  if (!digest(token, hash)) return false;
  root = std::string("/.crosspoint/readwise-books/") + hash;
  return true;
}

bool integer(JsonVariantConst value, int64_t& out) {
  if (value.is<int64_t>()) {
    out = value.as<int64_t>();
    return true;
  }
  const char* text = value | "";
  if (!*text || strlen(text) > 16) return false;
  out = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    out = out * 10 + (*p - '0');
  }
  return true;
}

bool iso(int64_t epoch, char (&out)[24]) {
  if (epoch < 1577836800LL || epoch > 4102444799LL) return false;
  const time_t t = static_cast<time_t>(epoch);
  if (static_cast<int64_t>(t) != epoch) return false;
  struct tm utc{};
  return gmtime_r(&t, &utc) && strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &utc) == 20;
}

bool eventPayload(JsonVariantConst event, int64_t bookId, const char* zone, JsonDocument& payload) {
  if (event["e"] != "reader.session" || bookId <= 0) return false;
  const auto vars = event["vars"];
  int64_t start, end, duration, a, b, scale;
  if (!integer(vars["start_time"], start) || !integer(vars["end_time"], end) ||
      !integer(vars["duration_seconds"], duration) || !integer(vars["start_progress_bp"], a) ||
      !integer(vars["end_progress_bp"], b) || !integer(vars["progress_scale"], scale) || start < 1577836800LL ||
      end > 4102444799LL || end <= start || duration < 10 || duration > end - start || end - start > 7 * 86400 ||
      scale != 10000 || a < 0 || b < a || b > scale)
    return false;
  char begin[24], finish[24];
  if (!iso(start, begin) || !iso(end, finish)) return false;
  auto item = payload["items"].to<JsonArray>().add<JsonObject>();
  item["book_id"] = bookId;
  item["started_at"] = begin;
  item["ended_at"] = finish;
  item["paused_seconds"] = end - start - duration;
  item["format"] = "ebook";
  item["percent_at_start"] = double(a) / scale;
  item["percent_at_end"] = double(b) / scale;
  if (*zone) item["timezone"] = zone;
  item["device"]["model"] = MODEL;
  item["device"]["type"] = "E-Reader";
  item["publish_to_feed"] = false;
  return !payload.overflowed();
}

int64_t bookId(JsonVariantConst event, const std::string& root) {
  const char* book = event["vars"]["book"] | "";
  if (*book != '/' || strlen(book) > 400 || strstr(book, "..")) return 0;
  JsonDocument meta;
  int64_t id = 0;
  if (!readJson(std::string(book) + ".meta.json", 2048, meta) ||
      meta["readwise_books_account"] != root.substr(root.size() - 64) ||
      !meta["readwise_books_document"].is<const char*>() ||
      meta["readwise_books_document"] != event["vars"]["document"] || !integer(meta["readwise_books_id"], id))
    return 0;
  return id > 0 ? id : 0;
}

bool saveCursor(const std::string& root, size_t position) {
  const std::string path = root + "/cursor";
  const std::string value = std::to_string(position);
  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  const bool wrote = file.write(reinterpret_cast<const uint8_t*>(value.data()), value.size()) == value.size();
  const bool closed = file.close();
  std::string check;
  return wrote && closed && readFile(path, 24, check) && check == value;
}

bool nextEvent(const std::string& root, std::string& name, bool advance = true) {
  size_t position = 0;
  std::string cursor;
  if (advance && readFile(root + "/cursor", 10, cursor) &&
      cursor.find_first_not_of("0123456789") == std::string::npos) {
    const uint64_t parsed = strtoull(cursor.c_str(), nullptr, 10);
    if (parsed <= UINT32_MAX) position = static_cast<size_t>(parsed);
  }
  HalFile dir = Storage.open((root + "/queue").c_str());
  if (!dir || !dir.isDirectory()) return false;
  if (position && !dir.seekSet(position)) dir.rewindDirectory();
  for (unsigned n = 0; n < 32; ++n) {
    HalFile entry = dir.openNextFile();
    position = dir.position();
    if (!entry) {
      if (advance) saveCursor(root, 0);
      return false;
    }
    char file[80];
    entry.getName(file, sizeof(file));
    if (entry.isDirectory() || strlen(file) != 69 || strcmp(file + 64, ".json")) continue;
    file[64] = '\0';
    if (strspn(file, "0123456789abcdef") != 64) continue;
    // Deep sleep resets RAM: advance durably even when this entry is blocked.
    if (advance && !saveCursor(root, position)) return false;
    name = file;
    return true;
  }
  if (advance) saveCursor(root, position);
  return false;
}

// API responses are UTC, with optional fractional zeroes. Reject other times.
bool sameTime(const char* actual, const char* expected) {
  if (strlen(actual) < 20 || strlen(expected) != 20 || strncmp(actual, expected, 19)) return false;
  const char* tail = actual + 19;
  if (*tail == '.') {
    ++tail;
    while (*tail == '0') ++tail;
  }
  return strcmp(tail, "Z") == 0 || strcmp(tail, "+00:00") == 0;
}

bool matches(JsonVariantConst remote, JsonVariantConst item) {
  if (remote["book_id"].as<int64_t>() != item["book_id"].as<int64_t>() || remote["origin"] != "api" ||
      remote["format"] != item["format"] || remote["device"]["model"] != item["device"]["model"] ||
      !sameTime(remote["started_at"] | "", item["started_at"] | "") ||
      !sameTime(remote["ended_at"] | "", item["ended_at"] | "") || !remote["paused_seconds"].is<int64_t>() ||
      remote["paused_seconds"].as<int64_t>() != item["paused_seconds"].as<int64_t>())
    return false;
  for (const char* key : {"percent_at_start", "percent_at_end"}) {
    if (!remote[key].is<double>() || std::fabs(remote[key].as<double>() - item[key].as<double>()) > 0.000001)
      return false;
  }
  return remote["id"].is<int64_t>() && remote["id"].as<int64_t>() > 0;
}

int request(const char* token, const std::string& url, const char* method, const std::string& body,
            JsonDocument& result, const std::string& attempt = "", const char* hash = "") {
  const auto heap = HalMemory::getInternalHeap();
  if (heap.freeBytes < 75 * 1024 || heap.largestBlockBytes < 24 * 1024) return -1;
  auto client = makeUniqueNoThrow<freeink::SecureHttpClient>();
  auto response = makeUniqueNoThrow<char[]>(MAX_RESPONSE + 1);
  if (!client || !response) return -1;
  client->setCACert(READWISE_ROOT_CA);
  client->setTimeout(8000);
  client->setReuse(false);
  client->setRetryStaleConnections(false);
  client->setUserAgent("CrossPoint-Almanac-ReadwiseBooks");
  if (!client->begin(url)) return -1;
  const uint32_t started = millis();
  const auto aborted = [started] { return WiFi.status() != WL_CONNECTED || millis() - started >= 15000; };
  WifiPowerSaveGuard power;
  if (!client->connect(aborted) || aborted()) return -1;
  client->addHeader("Authorization", std::string("Token ") + token);
  client->addHeader("Content-Type", "application/json");
  if (!attempt.empty() && !writeOnce(attempt, hash)) return -1;
  size_t received = 0;
  const int status = client->sendRequest(
      method, reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t size) {
        if (size > MAX_RESPONSE - received) return false;
        memcpy(response.get() + received, data, size);
        received += size;
        return true;
      },
      aborted);
  const std::string* retry = client->getUniqueHeader("retry-after");
  if (retry && !retry->empty() && retry->size() <= 6 && retry->find_first_not_of("0123456789") == std::string::npos) {
    const uint32_t seconds = std::min<uint32_t>(strtoul(retry->c_str(), nullptr, 10), 86400);
    nextPoll = millis() + std::max<uint32_t>(INTERVAL_MS, seconds * 1000);
  }
  if (!client->responseComplete()) return -1;
  if (status != 200) return status;
  // Filter before parsing: a session response can include long feed captions.
  JsonDocument filter;
  for (const char* key : {"id", "book_id", "origin", "format", "device", "started_at", "ended_at", "paused_seconds",
                          "percent_at_start", "percent_at_end", "status", "session_id", "error"})
    filter["results"][0][key] = true;
  filter["next"] = true;
  if (deserializeJson(result, response.get(), received, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(6)) ||
      result.overflowed())
    return -1;
  return status;
}

bool receipt(const std::string& path, const char* hash, int64_t id) {
  JsonDocument doc;
  doc["request_sha256"] = hash;
  doc["session_id"] = id;
  std::string data;
  serializeJson(doc, data);
  return writeOnce(path, data);
}

bool acknowledged(const std::string& path, const char* hash) {
  JsonDocument doc;
  return readJson(path, 256, doc) && doc["request_sha256"] == hash && doc["session_id"].is<int64_t>() &&
         doc["session_id"].as<int64_t>() > 0;
}

void deliver(const std::string& root, const std::string& name, JsonDocument& cfg) {
  const std::string queued = root + "/queue/" + name + ".json";
  const std::string prefix = root + "/history/" + name;
  std::string record;
  JsonDocument envelope;
  if (!readFile(queued, MAX_RECORD, record) || deserializeJson(envelope, record)) return;
  // The captured book ID is immutable. Unmatched books wait for the setup helper.
  int64_t id = 0;
  if (!integer(envelope["book_id"], id) || id <= 0) return;
  JsonDocument payload;
  JsonDocument expected;
  if (!eventPayload(envelope["event"], id, envelope["timezone"] | "", expected)) return;
  std::string body;
  if (Storage.exists((prefix + ".request").c_str())) {
    if (!readFile(prefix + ".request", MAX_RECORD, body) ||
        deserializeJson(payload, body, DeserializationOption::NestingLimit(6)) || payload["items"].size() != 1)
      return;
    const auto saved = payload["items"][0];
    const auto original = expected["items"][0];
    for (const char* key : {"book_id", "started_at", "ended_at", "paused_seconds", "format", "timezone"}) {
      if (saved[key] != original[key]) return;
    }
    for (const char* key : {"percent_at_start", "percent_at_end"}) {
      if (!saved[key].is<double>() || std::fabs(saved[key].as<double>() - original[key].as<double>()) > 0.000001)
        return;
    }
    const char* model = saved["device"]["model"] | "";
    if (!*model || strlen(model) > 100 || saved["device"]["type"] != "E-Reader" ||
        !saved["publish_to_feed"].is<bool>() || saved["publish_to_feed"].as<bool>())
      return;
  } else {
    payload = expected;
    serializeJson(payload, body);
    if (!Storage.ensureDirectoryExists((root + "/history").c_str()) || !writeOnce(prefix + ".request", body)) return;
  }
  if (!writeOnce(prefix + ".event.json", record)) return;
  char hash[65];
  if (!digest(body, hash)) return;
  if (acknowledged(prefix + ".receipt.json", hash)) {
    Storage.remove(queued.c_str());
    return;
  }
  const auto item = payload["items"][0];
  const char* token = cfg["token"];
  JsonDocument response;
  const std::string attempted = prefix + ".attempt";
  if (Storage.exists(attempted.c_str())) {
    std::string marker;
    if (!readFile(attempted, 64, marker) || marker != hash) return;
    // An uncertain POST is never repeated. Reconcile a narrow time window;
    // an incomplete result or more than one match stays pending for inspection.
    std::string url = std::string(API) + "?book_ids=" + std::to_string(id) +
                      "&started_after=" + item["started_at"].as<std::string>() +
                      "&started_before=" + item["ended_at"].as<std::string>() + "&include_deleted=true&limit=8";
    if (request(token, url, "GET", "", response) != 200 || !response["results"].is<JsonArray>() ||
        !response["next"].isNull())
      return;
    int64_t found = 0;
    for (JsonVariantConst remote : response["results"].as<JsonArrayConst>()) {
      if (!matches(remote, item)) continue;
      if (found) return;
      found = remote["id"].as<int64_t>();
    }
    if (found && receipt(prefix + ".receipt.json", hash, found)) Storage.remove(queued.c_str());
    return;
  }
  // The existence of even a partial attempt marker prevents another POST.
  const int status = request(token, API, "POST", body, response, attempted, hash);
  if (status == 400 || status == 401 || status == 403 || status == 404 || status == 429) {
    Storage.remove(attempted.c_str());  // A complete rejection cannot have created a session.
    return;
  }
  const auto results = response["results"].as<JsonArrayConst>();
  if (status != 200 || results.size() != 1) return;
  if (results[0]["status"] == "error" && results[0]["error"].is<JsonObjectConst>()) {
    Storage.remove(attempted.c_str());
    return;
  }
  const int64_t remoteId = results[0]["session_id"] | int64_t(0);
  if ((results[0]["status"] == "created" || results[0]["status"] == "existing") && remoteId > 0 &&
      receipt(prefix + ".receipt.json", hash, remoteId))
    Storage.remove(queued.c_str());
}
}  // namespace

void readwisebooks::enqueue(const std::string& event) {
  JsonDocument cfg, source;
  std::string root;
  if (event.size() > 512 || !config(cfg, root) || deserializeJson(source, event) || source["e"] != "reader.session")
    return;
  char name[65];
  if (!digest(event, name)) return;
  JsonDocument envelope;
  envelope["event"] = source;
  envelope["book_id"] = bookId(source.as<JsonVariantConst>(), root);
  envelope["timezone"] = cfg["timezone"] | "";
  std::string data;
  serializeJson(envelope, data);
  if (envelope.overflowed() || data.size() > MAX_RECORD || !Storage.ensureDirectoryExists((root + "/queue").c_str()) ||
      !writeOnce(root + "/queue/" + name + ".json", data)) {
    LOG_ERR("RWBOOKS", "Could not preserve reading session on SD");
  }
}

bool readwisebooks::hasPending() {
  JsonDocument cfg;
  std::string root, name;
  return config(cfg, root) && nextEvent(root, name, false);
}

void readwisebooks::poll() {
  if (WiFi.status() != WL_CONNECTED || !Storage.ready() || time(nullptr) < 1704067200 ||
      static_cast<int32_t>(millis() - nextPoll) < 0)
    return;
  nextPoll = millis() + INTERVAL_MS;
  JsonDocument cfg;
  std::string root, name;
  if (config(cfg, root) && nextEvent(root, name)) deliver(root, name, cfg);
}
