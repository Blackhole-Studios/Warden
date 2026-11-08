#pragma once

namespace warden::core {
class ContextManager;

enum class ProcessingRoute { Placeholder };

class RoutingManager {
public:
    ProcessingRoute choose(const ContextManager& context) const;
};
}  // namespace warden::core
