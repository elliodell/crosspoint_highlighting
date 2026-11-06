#pragma once

namespace readwisesync {

// Call on the main task at an idle network boundary, with SD filesystem ownership.
// Sends at most one capture as a batch of Readwise highlights. No worker,
// radio wake, or retained file handle; callers must not overlap network jobs.
void poll();

}  // namespace readwisesync
