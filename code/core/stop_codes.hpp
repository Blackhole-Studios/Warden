#pragma once

namespace warden::core {
// Process exit codes used by the Warden supervisor.
enum StopCode {
    Update = 10,
    Restart = 11,
    SafeExit = 12,
};
}  // namespace warden::core
