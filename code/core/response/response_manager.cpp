#include "response_manager.hpp"

namespace warden::core {
std::string ResponseManager::respond(const std::string& input, ProcessingRoute route) const {
    (void)route;
    return "Input received: " + input;
}
}  // namespace warden::core
