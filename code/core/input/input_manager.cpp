#include "input_manager.hpp"

#include <iostream>

namespace warden::core {
std::optional<std::string> InputManager::read() {
    std::cout << (first_prompt_ ? "You: " : "\nYou: ") << std::flush;
    first_prompt_ = false;
    std::string input;
    if (!std::getline(std::cin, input)) return std::nullopt;
    return input;
}
}  // namespace warden::core
