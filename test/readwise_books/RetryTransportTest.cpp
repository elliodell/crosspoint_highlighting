// Fixture derived from the MIT-licensed FreeInk SecureNet host tests.
// Production HTTP/resume control flow over scripted TLS transport, with a
// reservation spy. Actual wolfSSL ownership is tested by test_record_reservation.py.
#include <ResumableFetch.h>

#include <cassert>
#include <cstdio>

namespace {
int reservations = 0, liveReservations = 0;
bool failWrite = false;
}  // namespace
namespace freeink {
SecureClient::~SecureClient() { stop(); }
void SecureClient::setCACert(const char*) {}
void SecureClient::setInsecure() {}
int SecureClient::connect(IPAddress, uint16_t) { return 0; }
int SecureClient::connect(const char* host, uint16_t port) { return _transport.connect(host, port); }
size_t SecureClient::write(uint8_t b) { return write(&b, 1); }
size_t SecureClient::write(const uint8_t* data, size_t n) {
  if (failWrite) return 0;
  return _transport.write(data, n);
}
void SecureClient::reserveFullRecord() {
  assert(!FakeNet::requests().empty());
  const auto& req = FakeNet::requests().back();
  assert(req.size() >= 4 && req.substr(req.size() - 4) == "\r\n\r\n");
  assert(_transport.peek() == 'H');  // No response read before reservation.
  ++reservations;
  if (!_ctx) {
    _ctx = this;
    ++liveReservations;
  }
}
int SecureClient::available() { return _transport.available(); }
int SecureClient::read() { return _transport.read(); }
int SecureClient::read(uint8_t* buf, size_t size) { return _transport.read(buf, size); }
int SecureClient::peek() { return _transport.peek(); }
void SecureClient::flush() {}
void SecureClient::stop() {
  if (_ctx) {
    --liveReservations;
    _ctx = nullptr;
  }
  _transport.stop();
}
uint8_t SecureClient::connected() { return _transport.connected(); }
bool SecureClient::tls13Available() { return true; }
bool hostResolves(const char*) { return true; }
}  // namespace freeink

void reset(std::initializer_list<const char*> replies) {
  assert(liveReservations == 0);
  reservations = 0;
  failWrite = false;
  FakeNet::replies().assign(replies.begin(), replies.end());
  FakeNet::requests().clear();
  FakeNet::hosts().clear();
}
int main() {
  for (bool retry : {false, true}) {
    reset({"H", "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}"});
    freeink::SecureHttpClient http;
    http.setReuse(false);
    http.setRetryStaleConnections(retry);
    assert(http.begin("https://readwise.io/books/api/v1/sessions"));
    assert(http.connect());
    const int status = http.POST(std::string("{\"items\":[]}"));
    assert(FakeNet::requests().size() == (retry ? 2 : 1));
    assert((status == 200) == retry);
  }
  puts("Preconnected POST retry opt-out passed against production transport");
}
