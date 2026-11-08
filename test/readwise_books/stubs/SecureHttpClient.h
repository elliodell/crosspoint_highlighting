#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
namespace fake {
inline std::vector<std::string> requests;
inline std::vector<std::string> methods;
inline std::vector<std::string> urls;
inline bool connectOk = true;
inline bool retryStaleConnections = true;
inline int httpStatus = 200;
inline bool responseComplete = true;
inline bool expireRequest = false;
inline bool verified = false;
inline bool requestAborted = false;
inline std::string response = "[{\"id\":123,\"modified_highlights\":[456]}]";
inline std::string retryAfter;
inline std::string authorization;
}  // namespace fake
namespace freeink {
class SecureHttpClient {
 public:
  void setCACert(const char* root) { fake::verified = std::string(root).starts_with("-----BEGIN CERTIFICATE-----"); }
  void setTimeout(uint32_t) {}
  void setReuse(bool) {}
  void setRetryStaleConnections(bool retry) { fake::retryStaleConnections = retry; }
  template <typename Abort>
  bool connect(Abort abort) {
    return fake::connectOk && !abort();
  }
  void setUserAgent(const char*) {}
  bool begin(const std::string& url) {
    assert(url.starts_with("https://readwise.io/books/api/v1/sessions"));
    fake::urls.push_back(url);
    return true;
  }
  void addHeader(const std::string& name, const std::string& value) {
    if (name == "Authorization") fake::authorization = value;
  }
  template <typename Sink, typename Abort>
  int sendRequest(const char* method, const uint8_t* data, size_t length, Sink sink, Abort abort) {
    assert(std::string(method) == "POST" || std::string(method) == "GET");
    fake::methods.emplace_back(method);
    assert(fake::verified);
    assert(!fake::retryStaleConnections);
    if (fake::expireRequest) fake::now += 20000;
    if (abort()) {
      fake::requestAborted = true;
      return -1;
    }
    fake::requests.emplace_back(reinterpret_cast<const char*>(data), length);
    complete = fake::responseComplete;
    for (size_t offset = 0; offset < fake::response.size(); offset += 97) {
      const size_t chunk = std::min<size_t>(97, fake::response.size() - offset);
      if (!sink(reinterpret_cast<const uint8_t*>(fake::response.data() + offset), chunk)) {
        complete = false;
        break;
      }
    }
    return fake::httpStatus;
  }
  const std::string* getUniqueHeader(const char*) const {
    return fake::retryAfter.empty() ? nullptr : &fake::retryAfter;
  }
  bool responseComplete() const { return complete; }

 private:
  bool complete = false;
};
}  // namespace freeink
