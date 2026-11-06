#pragma once
inline constexpr int WL_CONNECTED = 3;
namespace fake {
inline bool wifiConnected = true;
}
struct FakeWifi {
  int status() const { return fake::wifiConnected ? WL_CONNECTED : 0; }
};
inline FakeWifi WiFi;
