#include "routing_manager.hpp"

#include "../context/context_manager.hpp"

namespace warden::core {
ProcessingRoute RoutingManager::choose(const ContextManager& context) const {
    (void)context;
    return ProcessingRoute::Placeholder;
}
}  // namespace warden::core
