#include <cassert>
#include <string>

// Include the implementation to reset its scheduler between fault scenarios.
#include "highlights/ReadwiseSync.cpp"

namespace {
constexpr char ID[] = "0123456789abcdef0123456789abcdef";
constexpr char CAPTURE[] = "/Highlights/0123456789abcdef0123456789abcdef.json";
constexpr char MARKER[] = "/.crosspoint/readwise/sent/0123456789abcdef0123456789abcdef";
constexpr char TEMP_MARKER[] = "/.crosspoint/readwise/sent/0123456789abcdef0123456789abcdef.tmp";

std::string capture(const std::string& text = "Hello & <reader> \"quote\" \\ line\n世界", bool imagePage = false) {
  JsonDocument doc;
  doc["id"] = ID;
  doc["book_title"] = "A & <book>";
  doc["author"] = "Author";
  doc["chapter"] = "Chapter";
  doc["page"] = 3;
  for (int i = 1; i <= 5; ++i) {
    auto p = doc["pages"].add<JsonObject>();
    p["chapter"] = "Chapter";
    p["page"] = i;
    p["current"] = i == 3;
    p["text"] = imagePage && i == 3 ? "" : text;
  }
  std::string data;
  serializeJson(doc, data);
  return data;
}

void reset() {
  fake::reset();
  fake::now = 0;
  fake::wifiConnected = true;
  fake::storageReady = true;
  fake::heapBytes = 2 * 1024 * 1024;
  fake::requests.clear();
  fake::httpStatus = 201;
  fake::responseComplete = true;
  fake::expireRequest = false;
  fake::verified = false;
  fake::requestAborted = false;
  fake::response = "{\"id\":\"reader123\"}";
  fake::retryAfter.clear();
  fake::authorization.clear();
  nextPollAt = 0;
  retryMs = RETRY_MIN_MS;
  scanPosition = 0;
  fake::add(CONFIG_PATH, "{\"enabled\":true,\"token\":\"secret-test-token\"}");
  fake::add(CAPTURE, capture());
}

void pollLater() {
  fake::now += 24 * 60 * 60 * 1000;
  readwisesync::poll();
}
void retryUntilSent() {
  for (int i = 0; i < 4 && !Storage.exists(MARKER); ++i) pollLater();
}

void successfulUpload() {
  reset();
  readwisesync::poll();
  assert(fake::requests.size() == 1);
  assert(Storage.exists(CAPTURE) && alreadySent(ID));
  assert(!Storage.exists(TEMP_MARKER));
  assert(fake::authorization == "Token secret-test-token");
  JsonDocument doc;
  assert(!deserializeJson(doc, fake::requests.front()));
  assert(doc["url"] == "https://crosspoint-highlighting.invalid/highlights/0123456789abcdef0123456789abcdef");
  assert(doc["location"] == "new" && !doc["should_clean_html"].as<bool>());
  const std::string html = doc["html"];
  assert(html.find("&amp; &lt;reader&gt; &quot;quote&quot; \\ line<br/>世界") != std::string::npos);
  assert(html.find("page 3 (selected page)") != std::string::npos);
  for (int i = 0; i < 3; ++i) pollLater();
  assert(fake::requests.size() == 1);
}

void httpFailures() {
  for (int status : {301, 400, 401, 403, 429, 500, -1}) {
    reset();
    fake::httpStatus = status;
    readwisesync::poll();
    assert(!Storage.exists(MARKER) && Storage.exists(CAPTURE));
    assert(nextPollAt >= (status == 401 || status == 403 ? RETRY_MAX_MS : RETRY_MIN_MS));
    const auto firstRequest = fake::requests.front();
    fake::httpStatus = 200;
    retryUntilSent();
    assert(alreadySent(ID));
    assert(fake::requests.back() == firstRequest);
  }
  reset();
  fake::httpStatus = 429;
  fake::retryAfter = "3600";
  readwisesync::poll();
  assert(nextPollAt >= 3600000);
}

void interruptedWrites() {
  for (int failure = 0; failure < 4; ++failure) {
    reset();
    if (failure == 0) fake::failWrite = 0;
    if (failure == 1) fake::shortWrite = 1;
    if (failure == 2) fake::failClosePath = TEMP_MARKER;
    if (failure == 3) fake::failRename = 0;
    readwisesync::poll();
    assert(!Storage.exists(MARKER) && Storage.exists(CAPTURE));
    const auto firstRequest = fake::requests.front();
    retryUntilSent();
    assert(alreadySent(ID));
    assert(fake::requests.back() == firstRequest);
  }
}

void unacknowledgedResponse() {
  for (const auto& body : {std::string("{}"), std::string("{\"id\":\"bad id\"}"), std::string(2048, 'x')}) {
    reset();
    fake::response = body;
    readwisesync::poll();
    assert(!Storage.exists(MARKER) && Storage.exists(CAPTURE));
  }
  reset();
  fake::responseComplete = false;
  readwisesync::poll();
  assert(!Storage.exists(MARKER));
  reset();
  fake::expireRequest = true;
  readwisesync::poll();
  assert(fake::requestAborted && !Storage.exists(MARKER));
}

void configAndStorageGuards() {
  for (const char* config :
       {"{}", "not-json", "{\"enabled\":false,\"token\":\"x\"}", "{\"enabled\":true,\"token\":\"x\\r\\nInjected:yes\"}",
        "{\"enabled\":true,\"token\":\"\"}"}) {
    reset();
    fake::add(CONFIG_PATH, config);
    readwisesync::poll();
    assert(fake::requests.empty());
  }
  reset();
  fake::wifiConnected = false;
  readwisesync::poll();
  assert(fake::requests.empty() && fake::reads == 0);
  reset();
  fake::storageReady = false;
  readwisesync::poll();
  assert(fake::requests.empty() && fake::reads == 0);
  reset();
  fake::heapBytes = 4096;
  readwisesync::poll();
  assert(fake::requests.empty() && Storage.exists(CAPTURE));
}

void captureLimits() {
  reset();
  fake::add(CAPTURE, capture("context", true));
  readwisesync::poll();
  assert(alreadySent(ID));
  for (const auto& data :
       {std::string("{}"), std::string(MAX_SOURCE_BYTES + 1, 'x'), capture(std::string(16385, 'x'))}) {
    reset();
    fake::add(CAPTURE, data);
    readwisesync::poll();
    assert(fake::requests.empty() && Storage.exists(CAPTURE));
  }
  reset();
  fake::add(MARKER, "");
  readwisesync::poll();
  assert(fake::requests.size() == 1 && alreadySent(ID));
}
}  // namespace

int main() {
  successfulUpload();
  httpFailures();
  interruptedWrites();
  unacknowledgedResponse();
  configAndStorageGuards();
  captureLimits();
  puts("Readwise sync tests passed");
}
