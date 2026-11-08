#pragma once

#include "../routing/routing_manager.hpp"

#include <string>

namespace warden::core {
class ResponseManager {
public:
    std::string respond(const std::string& input, ProcessingRoute route) const;
};
}  // namespace warden::core
