#include "stop_codes.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> running{true};

void handle_signal(int) {
    running = false;
}
}  // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "[Core] Starting...\n";
    std::cout << "[Core] Initialization complete.\n";
    std::cout << "[Core] Waiting for shutdown.\n";

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[Core] Shutting down.\n";
    return warden::core::SafeExit;
}
