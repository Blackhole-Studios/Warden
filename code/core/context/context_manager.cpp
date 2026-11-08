#include "context_manager.hpp"

namespace warden::core {
void ContextManager::add(ContextRole role, const std::string& text) {
    history_.push_back({role, text});
}

const std::vector<ContextEntry>& ContextManager::history() const {
    return history_;
}

std::size_t ContextManager::turn_count() const {
    std::size_t count = 0;
    for (const auto& entry : history_) if (entry.role == ContextRole::User) ++count;
    return count;
}
}  // namespace warden::core
