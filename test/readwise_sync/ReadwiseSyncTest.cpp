#include <cassert>
#include <string>

// Include the implementation to reset its scheduler between fault scenarios.
#include "highlights/ReadwiseSync.cpp"

namespace {
constexpr char ID[] = "0123456789abcdef0123456789abcdef";
constexpr char CAPTURE[] = "/Highlights/0123456789abcdef0123456789abcdef.json";
char ACCOUNT[128];
std::string markerPath;
std::string tempPath;
#define MARKER markerPath.c_str()
#define TEMP_MARKER tempPath.c_str()
using readwiseplan::MAX_SOURCE_BYTES;
bool alreadySent(const char* id) { return readwiseplan::alreadySent(ACCOUNT, id); }

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
  fake::httpStatus = 200;
  fake::responseComplete = true;
  fake::expireRequest = false;
  fake::verified = false;
  fake::requestAborted = false;
  fake::response = "[{\"id\":123,\"modified_highlights\":[456]}]";
  fake::retryAfter.clear();
  fake::authorization.clear();
  nextPollAt = 0;
  retryMs = RETRY_MIN_MS;
  scanPosition = 0;
  fake::add(CONFIG_PATH, "{\"enabled\":true,\"token\":\"secret-test-token\"}");
  fake::add(CAPTURE, capture());
  assert(readwiseplan::accountDirectory("secret-test-token", ACCOUNT));
  markerPath = std::string(ACCOUNT) + "/" + ID + ".ack";
  tempPath = std::string(ACCOUNT) + "/" + ID + ".request";
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
  assert(Storage.exists(TEMP_MARKER));
  assert(fake::authorization == "Token secret-test-token");
  JsonDocument doc;
  assert(!deserializeJson(doc, fake::requests.front()));
  assert(doc["highlights"].size() == 1);
  const auto highlight = doc["highlights"][0];
  assert(highlight["highlight_url"] ==
         "https://crosspoint-highlighting.invalid/highlights/0123456789abcdef0123456789abcdef/part-1");
  assert(highlight["category"] == "books" && highlight["source_type"] == "crosspoint_highlighting");
  assert(highlight["title"] == "A & <book>" && highlight["author"] == "Author");
  assert(highlight["location"] == 3 && highlight["location_type"] == "page");
  assert(highlight["source_url"].isNull());
  const std::string text = highlight["text"];
  assert(text.find("Hello & <reader> \"quote\" \\ line\n世界") != std::string::npos);
  const std::string note = highlight["note"];
  assert(note.find("crosspoint:0123456789abcdef0123456789abcdef 1/1") != std::string::npos);
  assert(note.find("page 3 (selected page)") != std::string::npos);
  for (int i = 0; i < 3; ++i) pollLater();
  assert(fake::requests.size() == 1);
}

void completeParagraphUploads() {
  for (int pageCount : {6, 64, 65}) {
    reset();
    JsonDocument source;
    assert(!deserializeJson(source, capture()));
    JsonArray pages = source["pages"].as<JsonArray>();
    pages.clear();
    for (int i = 1; i <= pageCount; ++i) {
      auto page = pages.add<JsonObject>();
      page["chapter"] = "Chapter";
      page["page"] = i;
      page["current"] = i == 3;
      page["text"] = "Paragraph page " + std::to_string(i) + " & text.";
    }
    std::string data;
    serializeJson(source, data);
    assert(data.size() < MAX_SOURCE_BYTES);
    fake::add(CAPTURE, data);
    readwisesync::poll();
    assert(Storage.exists(CAPTURE));
    if (pageCount > 64) {
      assert(fake::requests.empty() && !Storage.exists(MARKER));
      continue;
    }
    assert(fake::requests.size() == 1 && alreadySent(ID));
    JsonDocument request;
    assert(!deserializeJson(request, fake::requests.front()));
    const std::string textBody = request["highlights"][0]["text"];
    size_t previousEnd = 0;
    for (int i = 1; i <= pageCount; ++i) {
      const std::string text = "Paragraph page " + std::to_string(i) + " & text.";
      const size_t position = textBody.find(text, previousEnd);
      assert(position != std::string::npos);
      previousEnd = position + text.size();
    }
    assert(request["highlights"][0]["title"] == "A & <book>");
  }
}

void httpFailures() {
  for (int status : {201, 301, 400, 401, 403, 429, 500, -1}) {
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
    if (failure == 3) fake::failWrite = 4;
    readwisesync::poll();
    assert(!Storage.exists(MARKER) && Storage.exists(CAPTURE));
    // The frozen request must reach storage before any network call.
    assert(fake::requests.empty());
    retryUntilSent();
    assert(alreadySent(ID));
    assert(fake::requests.size() == 1);
  }
}

void unacknowledgedResponse() {
  for (const auto& body :
       {std::string("{}"), std::string("{\"id\":\"bad id\"}"), std::string(MAX_RESPONSE_BYTES + 1, 'x')}) {
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

bool validUtf8(const std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    const size_t bytes = first < 0x80 ? 1 : first >= 0xf0 ? 4 : first >= 0xe0 ? 3 : first >= 0xc0 ? 2 : 0;
    if (!bytes || i + bytes > text.size()) return false;
    for (size_t j = 1; j < bytes; ++j) {
      if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) return false;
    }
    i += bytes;
  }
  return true;
}

void splitHighlights() {
  std::string multilingual;
  for (int i = 0; i < 1600; ++i) multilingual += "é世🙂";
  for (const auto& text : {std::string(8191, 'x'), std::string(16384, 'x'), multilingual,
                           std::string(4500, 'a') + "\n\n" + std::string(4000, 'b'),
                           std::string(4500, 'a') + " " + std::string(4000, 'b')}) {
    reset();
    fake::add(CAPTURE, capture(text, true));
    readwisesync::poll();
    assert(alreadySent(ID) && fake::requests.size() == 1);
    JsonDocument request;
    assert(!deserializeJson(request, fake::requests.front()));
    JsonArrayConst highlights = request["highlights"];
    assert(highlights.size() > 1);
    std::string joined;
    size_t part = 0;
    for (JsonObjectConst highlight : highlights) {
      ++part;
      const std::string segment = highlight["text"];
      assert(!segment.empty() && segment.size() <= 8191 && validUtf8(segment));
      joined += segment;
      const std::string note = highlight["note"];
      assert(note.find(std::string("crosspoint:") + ID + " " + std::to_string(part) + "/" +
                       std::to_string(highlights.size())) != std::string::npos);
      assert(highlight["highlight_url"] ==
             std::string("https://crosspoint-highlighting.invalid/highlights/") + ID + "/part-" + std::to_string(part));
      assert(highlight["title"] == "A & <book>" && highlight["source_url"].isNull());
    }
    assert(joined == text + "\n\n" + text + "\n\n" + text + "\n\n" + text);
    if (text.starts_with(std::string(4500, 'a'))) {
      const std::string first = highlights[0]["text"];
      assert(first == text.substr(0, text.find('b')));
    }
    Storage.remove(MARKER);
    fake::response = "[{\"id\":123,\"modified_highlights\":[]}]";
    retryUntilSent();
    assert(alreadySent(ID) && fake::requests.back() == fake::requests.front());
  }
}

void migrationAndAcknowledgements() {
  reset();
  const char* legacy = "/.crosspoint/readwise/sent/0123456789abcdef0123456789abcdef";
  fake::add(legacy, "reader123");
  readwisesync::poll();
  assert(alreadySent(ID) && Storage.exists(legacy) && fake::requests.size() == 1);
  for (const char* response :
       {"[]", "[{\"id\":0}]", "[{\"id\":-1}]", "[{\"id\":\"123\"}]", "[{\"id\":123},{\"id\":456}]", "[{}]"}) {
    reset();
    fake::response = response;
    readwisesync::poll();
    assert(!alreadySent(ID) && Storage.exists(CAPTURE));
  }
  reset();
  fake::response = "[{\"id\":123,\"document_note\":\"" + std::string(6000, 'n') +
                   "\",\"tags\":[{\"id\":12,\"name\":\"sample\"}],\"modified_highlights\":[]}]";
  readwisesync::poll();
  assert(alreadySent(ID));
}

void separateCaptureIdentities() {
  reset();
  const char* secondId = "fedcba9876543210fedcba9876543210";
  JsonDocument second;
  assert(!deserializeJson(second, capture(std::string(10000, 'b'))));
  second["id"] = secondId;
  std::string data;
  serializeJson(second, data);
  fake::add(std::string("/Highlights/") + secondId + ".json", data);
  for (int i = 0; i < 4; ++i) pollLater();
  assert(fake::requests.size() == 2 && alreadySent(ID) && alreadySent(secondId));
  for (size_t i = 0; i < fake::requests.size(); ++i) {
    JsonDocument request;
    assert(!deserializeJson(request, fake::requests[i]));
    for (JsonObjectConst highlight : request["highlights"].as<JsonArrayConst>()) {
      const std::string note = highlight["note"];
      const std::string url = highlight["highlight_url"];
      const bool first = note.find(ID) != std::string::npos;
      const bool secondCapture = note.find(secondId) != std::string::npos;
      assert(first != secondCapture);
      assert(url.find(first ? ID : secondId) != std::string::npos);
      assert(highlight["title"] == "A & <book>" && highlight["author"] == "Author");
    }
  }
}

void metadataAndEmptyText() {
  reset();
  JsonDocument source;
  assert(!deserializeJson(source, capture()));
  std::string name;
  for (int i = 0; i < 400; ++i) name += "世界🙂";
  source["book_title"] = name;
  source["author"] = name;
  source["chapter"] = name;
  std::string encoded;
  serializeJson(source, encoded);
  fake::add(CAPTURE, encoded);
  readwisesync::poll();
  assert(alreadySent(ID));
  JsonDocument request;
  assert(!deserializeJson(request, fake::requests.front()));
  const std::string title = request["highlights"][0]["title"];
  const std::string author = request["highlights"][0]["author"];
  assert(title.size() <= 511 && author.size() <= 1024 && validUtf8(title) && validUtf8(author));
  reset();
  fake::add(CAPTURE, capture(""));
  readwisesync::poll();
  assert(fake::requests.empty() && !alreadySent(ID));
}

constexpr char SECOND[] = "22222222222222222222222222222222";
constexpr char THIRD[] = "33333333333333333333333333333333";

std::string formattedCapture(const char* id, const std::vector<std::string>& paragraphs, unsigned spine = 0,
                             const char* title = "A & <book>") {
  JsonDocument doc;
  doc["text_format"] = 2;
  doc["id"] = id;
  doc["book_title"] = title;
  doc["author"] = "Author";
  doc["chapter"] = "Chapter";
  doc["page"] = 1;
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    auto page = doc["pages"].add<JsonObject>();
    page["spine"] = spine;
    page["page"] = i + 1;
    page["current"] = i == 0;
    page["chapter"] = "Chapter";
    page["text"] = paragraphs[i];
    page["joiner"] = i ? "\xe2\x80\xa9" : "";
  }
  std::string result;
  serializeJson(doc, result);
  return result;
}

void addCapture(const char* id, const std::string& data) {
  fake::add(std::string("/Highlights/") + id + ".json", data);
}

void sendCapture(const char* id, const std::vector<std::string>& paragraphs, unsigned spine = 0,
                 const char* title = "A & <book>") {
  addCapture(id, formattedCapture(id, paragraphs, spine, title));
  for (int i = 0; i < 5 && !alreadySent(id); ++i) pollLater();
  assert(alreadySent(id));
}

JsonDocument lastRequest() {
  JsonDocument doc;
  assert(!fake::requests.empty());
  assert(!deserializeJson(doc, fake::requests.back()));
  return doc;
}

void canonicalParagraphs() {
  reset();
  JsonDocument source;
  assert(!deserializeJson(source, formattedCapture(ID, {"A para", "graph", "", "with a break", "Next"})));
  source["pages"][1]["joiner"] = "";
  source["pages"][2]["joiner"] = "";
  source["pages"][3]["joiner"] = "\n";
  std::string encoded;
  serializeJson(source, encoded);
  fake::add(CAPTURE, encoded);
  readwisesync::poll();
  assert(alreadySent(ID));
  assert(lastRequest()["highlights"][0]["text"] == "A paragraph\nwith a break\n\nNext");
  // Same source paragraph with different screen pagination must be recognized.
  sendCapture(SECOND, {"A paragraph\nwith a break", "Next", "New text"});
  assert(fake::requests.size() == 2);
  assert(lastRequest()["highlights"][0]["text"] == "New text");

  reset();
  fake::add(CAPTURE, formattedCapture(ID, {"First\xe2\x80\xa9Second\nline\n\nline", "Third"}));
  readwisesync::poll();
  assert(lastRequest()["highlights"][0]["text"] == "First\n\nSecond\nline\n\nline\n\nThird");
  sendCapture(SECOND, {"Second\nline\n\nline", "Third", "Fourth"});
  assert(lastRequest()["highlights"][0]["text"] == "Fourth");
}

void overlappingCaptures() {
  reset();
  sendCapture(ID, {"A", "B", "C"});
  sendCapture(SECOND, {"B", "C", "D", "E"});
  assert(fake::requests.size() == 2);
  auto request = lastRequest();
  assert(request["highlights"].size() == 1 && request["highlights"][0]["text"] == "D\n\nE");
  const std::string note = request["highlights"][0]["note"];
  assert(note.find(std::string("crosspoint:") + SECOND + " 1/1") != std::string::npos);
  assert(note.find(std::string("merge:") + ID) != std::string::npos);
  sendCapture(THIRD, {"C", "D"});
  assert(fake::requests.size() == 2);  // Fully covered: no additional API call.
  assert(!readwiseplan::hasPending(ACCOUNT));

  reset();
  sendCapture(ID, {"B", "D"});
  sendCapture(SECOND, {"A", "B", "C", "D", "E"});
  request = lastRequest();
  assert(request["highlights"].size() == 3);
  assert(request["highlights"][0]["text"] == "A");
  assert(request["highlights"][1]["text"] == "C");
  assert(request["highlights"][2]["text"] == "E");
  for (size_t i = 0; i < 3; ++i) {
    const std::string partNote = request["highlights"][i]["note"];
    assert(partNote.find(std::string("crosspoint:") + SECOND + " " + std::to_string(i + 1) + "/3") !=
           std::string::npos);
    assert(partNote.find(std::string("merge:") + ID) != std::string::npos);
  }

  reset();
  sendCapture(ID, {"A", "B"});
  sendCapture(SECOND, {"E", "F"});
  sendCapture(THIRD, {"B", "C", "D", "E"});
  request = lastRequest();
  assert(request["highlights"][0]["text"] == "C\n\nD");
  const std::string bridge = request["highlights"][0]["note"];
  assert(bridge.find(std::string("merge:") + ID) != std::string::npos);
  assert(bridge.find(std::string("merge:") + SECOND) != std::string::npos);
}

void distinctBooksAccountsAndLegacy() {
  reset();
  sendCapture(ID, {"same text"});
  sendCapture(SECOND, {"same text"}, 1);  // Different chapter.
  sendCapture(THIRD, {"same text"}, 0, "A different book");
  assert(fake::requests.size() == 3);
  fake::add(CONFIG_PATH, "{\"enabled\":true,\"token\":\"second-account\"}");
  assert(readwiseplan::accountDirectory("second-account", ACCOUNT));
  for (int i = 0; i < 8; ++i) pollLater();
  assert(fake::requests.size() == 6);
  assert(fake::authorization == "Token second-account");

  reset();
  readwisesync::poll();  // A legacy capture has no reliable source paragraph markers.
  sendCapture(SECOND, {"Hello & <reader> \"quote\" \\ line\n世界"});
  assert(fake::requests.size() == 2);
  assert(lastRequest()["highlights"][0]["text"] == "Hello & <reader> \"quote\" \\ line\n世界");
}

void frozenRetryAndCorruption() {
  reset();
  fake::add(CAPTURE, formattedCapture(ID, {"A", "B", "C"}));
  fake::failWritePath = MARKER;  // Immutable request succeeds; acknowledgement write fails after HTTP 200.
  readwisesync::poll();
  assert(!alreadySent(ID) && readwiseplan::hasPending(ACCOUNT));
  const std::string originalRequest = fake::requests.front();
  // Simulate a reboot, a modified export, and a newer capture before retry.
  nextPollAt = 0;
  scanPosition = 0;
  fake::add(CAPTURE, formattedCapture(ID, {"edited externally"}));
  addCapture(SECOND, formattedCapture(SECOND, {"C", "D"}));
  pollLater();
  assert(alreadySent(ID));
  assert(fake::requests.size() == 2 && fake::requests.back() == originalRequest);
  for (int i = 0; i < 3; ++i) pollLater();
  assert(!alreadySent(SECOND) && fake::requests.size() == 2);  // Changed source cannot suppress new text.

  for (bool corruptHeader : {false, true}) {
    reset();
    fake::add(CAPTURE, formattedCapture(ID, {"A", "B"}));
    fake::httpStatus = 500;
    readwisesync::poll();
    const std::string pending = std::string(ACCOUNT) + "/" + ID + ".request";
    const auto saved = fake::files[pending]->bytes;
    fake::files[pending]->bytes[corruptHeader ? 70 : saved.size() - 5] ^= 1;
    fake::httpStatus = 200;
    pollLater();
    assert(fake::requests.size() == 1 && !alreadySent(ID));
    fake::files[pending]->bytes = saved;
    pollLater();
    assert(alreadySent(ID) && fake::requests.size() == 2);
    assert(fake::requests.front() == fake::requests.back());
  }
}

void acknowledgementRecovery() {
  for (size_t persistedBytes : {size_t(0), size_t(17), size_t(63), size_t(64)}) {
    reset();
    fake::failRename = 0;  // This protocol must not depend on FAT rename atomicity.
    sendCapture(ID, {"A", "B"});
    const auto request = fake::requests.front();
    assert(fake::failRename == 0 && !fake::failureTriggered);
    assert(fake::files[MARKER] != fake::files[TEMP_MARKER]);
    // Power loss while writing the acknowledgement, then reboot. Request stays intact.
    fake::files[MARKER]->bytes.resize(persistedBytes);
    nextPollAt = 0;
    scanPosition = 0;
    pollLater();
    assert(alreadySent(ID));
    assert(fake::requests.size() == (persistedBytes == 64 ? 1 : 2));
    assert(fake::requests.back() == request);
    sendCapture(SECOND, {"B", "C"});
    assert(lastRequest()["highlights"][0]["text"] == "C");
    assert(fake::failRename == 0);
  }
  reset();
  fake::add(CAPTURE, formattedCapture(ID, {"A", "B"}));
  fake::failClosePath = MARKER;  // All acknowledgement bytes survive, close reports failure.
  readwisesync::poll();
  assert(alreadySent(ID) && fake::requests.size() == 1);
  nextPollAt = 0;
  scanPosition = 0;
  sendCapture(SECOND, {"B", "C"});
  assert(fake::requests.size() == 2 && lastRequest()["highlights"][0]["text"] == "C");
}

void longUniqueOverlap() {
  reset();
  sendCapture(ID, {"Already saved"});
  std::string unicode;
  for (int i = 0; i < 1300; ++i) unicode += "é世🙂";
  const std::string unique = unicode + " first end\n\n" + std::string(9000, 'b') + " second end";
  sendCapture(SECOND, {"Already saved", unicode + " first end", std::string(9000, 'b') + " second end"});
  const auto request = lastRequest();
  std::string joined;
  const JsonArrayConst parts = request["highlights"];
  assert(parts.size() >= 3);
  for (JsonObjectConst part : parts) {
    const std::string text = part["text"];
    assert(!text.empty() && text.size() <= 8191 && validUtf8(text));
    assert(text.find(ID) == std::string::npos && text.find(SECOND) == std::string::npos);
    assert(std::string(part["note"].as<const char*>()).find(std::string("merge:") + ID) != std::string::npos);
    joined += text;
  }
  assert(joined == unique);
}
}  // namespace

int main() {
  successfulUpload();
  completeParagraphUploads();
  httpFailures();
  interruptedWrites();
  unacknowledgedResponse();
  configAndStorageGuards();
  captureLimits();
  splitHighlights();
  migrationAndAcknowledgements();
  separateCaptureIdentities();
  metadataAndEmptyText();
  canonicalParagraphs();
  overlappingCaptures();
  distinctBooksAccountsAndLegacy();
  frozenRetryAndCorruption();
  longUniqueOverlap();
  acknowledgementRecovery();
  puts("Readwise sync tests passed");
}
