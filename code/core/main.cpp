#include "core.hpp"
#include "stop_codes.hpp"

#include <atomic>
#include <csignal>

namespace {
std::atomic<bool> running{true};

void handle_signal(int) {
    running = false;
}
}  // namespace

int main() {
    struct sigaction action{};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    warden::core::Core core;
    core.run(running);
    return warden::core::SafeExit;
}
