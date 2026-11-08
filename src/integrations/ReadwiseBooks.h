#pragma once

#include <string>

// Almanac's optional readwise-books-v1 mirror. Calls run on the existing
// plugin event owner; no task or Wi-Fi connection is created here.
namespace readwisebooks {
void enqueue(const std::string& event);
bool hasPending();
void poll();
}  // namespace readwisebooks
