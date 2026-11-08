#include "output_manager.hpp"

#include <iostream>

namespace warden::core {
void OutputManager::initialized() const {
    std::cout << "Core initialized.\nCore is ready.\n";
}

void OutputManager::write_response(const std::string& response) const {
    std::cout << "Core: " << response << '\n';
}

void OutputManager::shutting_down() const {
    std::cout << "\nCore shutting down.\n";
}
}  // namespace warden::core
