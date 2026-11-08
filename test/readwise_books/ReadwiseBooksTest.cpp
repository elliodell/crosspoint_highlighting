#include <cassert>
#include <iostream>

#include "integrations/ReadwiseBooks.cpp"

namespace {
std::string root, queue, prefix;
std::string event(const char* id = "test-1") {
  JsonDocument doc;
  doc["e"] = "reader.session";
  doc["id"] = id;
  doc["ts"] = 1760000100;
  auto vars = doc["vars"].to<JsonObject>();
  vars["book"] = "/books/example.epub";
  vars["document"] = "synthetic-document";
  vars["start_time"] = "1760000000";
  vars["end_time"] = "1760000060";
  vars["duration_seconds"] = "50";
  vars["start_progress_bp"] = "1200";
  vars["end_progress_bp"] = "1250";
  vars["progress_scale"] = "10000";
  std::string line;
  serializeJson(doc, line);
  return line;
}
void reset() {
  fake::reset();
  fake::now = 0;
  nextPoll = 0;
  fake::connectOk = true;
  fake::wifiConnected = true;
  fake::storageReady = true;
  fake::heapBytes = 2 * 1024 * 1024;
  fake::requests.clear();
  fake::urls.clear();
  fake::methods.clear();
  fake::httpStatus = 200;
  fake::responseComplete = true;
  fake::expireRequest = false;
  fake::verified = false;
  fake::response = R"({"results":[{"status":"created","session_id":42,"error":null}]})";
  fake::add(CONFIG, R"({"enabled":true,"token":"synthetic-test-token","timezone":"Etc/UTC"})");
  JsonDocument cfg;
  assert(config(cfg, root));
  fake::add(
      "/books/example.epub.meta.json",
      "{\"readwise_books_id\":123,\"readwise_books_document\":\"synthetic-document\",\"readwise_books_account\":\"" +
          root.substr(root.size() - 64) + "\"}");
  char name[65];
  assert(digest(event(), name));
  queue = root + "/queue/" + name + ".json";
  prefix = root + "/history/" + name;
}
void later() {
  fake::now += 60001;
  readwisebooks::poll();
}
void pollNext() {
  later();
  later();
}
void successful() {
  reset();
  readwisebooks::enqueue(event());
  assert(readwisebooks::hasPending());
  readwisebooks::poll();
  assert(fake::requests.size() == 1 && fake::methods[0] == "POST" && fake::verified);
  assert(!Storage.exists(queue.c_str()));
  assert(Storage.exists((prefix + ".receipt.json").c_str()));
  JsonDocument body;
  assert(!deserializeJson(body, fake::requests[0]));
  auto item = body["items"][0];
  assert(item["book_id"] == 123 && item["paused_seconds"] == 10);
  assert(std::fabs(item["percent_at_start"].as<double>() - 0.12) < 0.000001 &&
         std::fabs(item["percent_at_end"].as<double>() - 0.125) < 0.000001);
  assert(item["publish_to_feed"] == false && item["timezone"] == "Etc/UTC");
  // The same event and lost queue deletion cannot double-count time.
  readwisebooks::enqueue(event());
  pollNext();
  assert(fake::requests.size() == 1);
}
void interrupted() {
  reset();
  readwisebooks::enqueue(event());
  fake::responseComplete = false;
  readwisebooks::poll();
  assert(Storage.exists(queue.c_str()) && fake::requests.size() == 1);
  fake::responseComplete = true;
  fake::response = R"({"results":[],"next":null})";
  pollNext();
  assert(fake::methods.back() == "GET" && Storage.exists(queue.c_str()));
  // A missing result is not permission to repeat an uncertain POST.
  pollNext();
  for (size_t i = 1; i < fake::methods.size(); ++i) assert(fake::methods[i] == "GET");
  JsonDocument remote, original;
  assert(!deserializeJson(original, fake::requests[0]));
  remote["results"].to<JsonArray>().add(original["items"][0]);
  remote["results"][0]["id"] = 42;
  remote["results"][0]["origin"] = "api";
  remote["results"][0]["started_at"] = "2025-10-09T08:53:20.000000Z";
  remote["next"] = nullptr;
  fake::response.clear();
  serializeJson(remote, fake::response);
  pollNext();
  assert(!Storage.exists(queue.c_str()));
}
void noDuplicateOnStorageFault() {
  for (const std::string suffix : {".request", ".attempt", ".receipt.json"}) {
    reset();
    readwisebooks::enqueue(event());
    fake::failWritePath = prefix + suffix;
    readwisebooks::poll();
    const size_t posts = std::count(fake::methods.begin(), fake::methods.end(), "POST");
    fake::response = R"({"results":[],"next":null})";
    pollNext();
    assert(std::count(fake::methods.begin(), fake::methods.end(), "POST") == static_cast<int>(posts));
  }
}
void rejectionAndBoundaries() {
  for (int status : {400, 401, 403, 404, 429}) {
    reset();
    readwisebooks::enqueue(event());
    fake::httpStatus = status;
    readwisebooks::poll();
    assert(!Storage.exists((prefix + ".attempt").c_str()));
    fake::httpStatus = 200;
    pollNext();
    assert(!Storage.exists(queue.c_str()));
  }
  reset();
  readwisebooks::enqueue(event());
  fake::heapBytes = 100;
  readwisebooks::poll();
  assert(fake::methods.empty());
  assert(!Storage.exists((prefix + ".attempt").c_str()));
  reset();
  fake::add("/books/example.epub.meta.json", R"({"readwise_books_id":0})");
  readwisebooks::enqueue(event());
  readwisebooks::poll();
  assert(Storage.exists(queue.c_str()) && fake::methods.empty());
  reset();
  readwisebooks::enqueue(event());
  fake::add("/books/example.epub.meta.json", R"({"readwise_books_id":999})");
  readwisebooks::poll();
  JsonDocument sent;
  assert(!deserializeJson(sent, fake::requests[0]));
  assert(sent["items"][0]["book_id"] == 123);
  reset();
  readwisebooks::enqueue(event());
  fake::add(CONFIG, R"({"enabled":true,"token":"different-account"})");
  readwisebooks::poll();
  assert(fake::methods.empty() && Storage.exists(queue.c_str()));
}
void queueAndValidation() {
  reset();
  for (int i = 0; i < 40; ++i) readwisebooks::enqueue(event(std::to_string(i).c_str()));
  size_t count = 0;
  for (const auto& [path, node] : fake::files)
    if (path.starts_with(root + "/queue/") && !node->directory) ++count;
  assert(count == 40);  // Independent of Almanac's 4 KiB queue.
  JsonDocument source, output;
  assert(!deserializeJson(source, event()));
  source["vars"]["duration_seconds"] = "999";
  assert(!eventPayload(source.as<JsonVariantConst>(), 1, "", output));
  source["vars"]["duration_seconds"] = "20";
  source["vars"]["start_progress_bp"] = "20000";
  assert(!eventPayload(source.as<JsonVariantConst>(), 1, "", output));
  assert(sameTime("2025-10-09T08:53:20+00:00", "2025-10-09T08:53:20Z"));
  assert(!sameTime("2025-10-09T08:53:20.500Z", "2025-10-09T08:53:20Z"));
}

void reconnectAndReboot() {
  reset();
  readwisebooks::enqueue(event());
  fake::connectOk = false;
  readwisebooks::poll();
  assert(fake::requests.empty() && !Storage.exists((prefix + ".attempt").c_str()));
  fake::connectOk = true;
  pollNext();
  assert(fake::requests.size() == 1 && !Storage.exists(queue.c_str()));

  reset();
  readwisebooks::enqueue(event());
  std::string record;
  assert(readFile(queue, MAX_RECORD, record));
  Storage.remove(queue.c_str());
  JsonDocument blocked;
  assert(!deserializeJson(blocked, record));
  blocked["book_id"] = 0;
  std::string blockedRecord;
  serializeJson(blocked, blockedRecord);
  const std::string first = root + "/queue/" + std::string(64, '0') + ".json";
  const std::string second = root + "/queue/" + std::string(64, '1') + ".json";
  fake::add(first, blockedRecord);
  fake::add(second, record);
  readwisebooks::poll();
  assert(fake::requests.empty());
  // Simulate loss of all volatile scheduler state on wake. The SD cursor survives.
  nextPoll = 0;
  fake::now = 0;
  readwisebooks::poll();
  assert(fake::requests.size() == 1 && Storage.exists(first.c_str()) && !Storage.exists(second.c_str()));
}

void frozenRequestSurvivesUpgrade() {
  reset();
  readwisebooks::enqueue(event());
  JsonDocument envelope, payload;
  assert(readJson(queue, MAX_RECORD, envelope));
  assert(eventPayload(envelope["event"], 123, "Etc/UTC", payload));
  payload["items"][0]["device"]["model"] = "Earlier CrossPoint adapter";
  std::string saved;
  serializeJsonPretty(payload, saved);
  fake::add(prefix + ".request", saved);
  readwisebooks::poll();
  assert(fake::requests.size() == 1 && fake::requests[0] == saved);
  assert(!Storage.exists(queue.c_str()));
}
}  // namespace
int main() {
  successful();
  interrupted();
  noDuplicateOnStorageFault();
  rejectionAndBoundaries();
  queueAndValidation();
  reconnectAndReboot();
  frozenRequestSurvivesUpgrade();
  std::cout << "Readwise Books session and fault tests passed\n";
}
