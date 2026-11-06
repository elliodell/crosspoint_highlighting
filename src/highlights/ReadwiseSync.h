#pragma once

namespace readwisesync {

// Call on the main task at an idle network boundary, with SD filesystem ownership.
// Sends at most one saved excerpt over the existing Wi-Fi connection. No worker,
// radio wake, or retained file handle; callers must not overlap network jobs.
void poll();

}  // namespace readwisesync
