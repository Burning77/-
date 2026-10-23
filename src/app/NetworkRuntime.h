#pragma once

namespace NetworkRuntime {
// Call before starting HTTPS requests; retain the native backend if the bundle is unavailable.
void initializeTls();
} // namespace NetworkRuntime
