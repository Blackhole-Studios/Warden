#pragma once

#include <string>
#include <cstddef>
#include <vector>

namespace warden::core {
enum class ContextRole { User, Assistant };

struct ContextEntry {
    ContextRole role;
    std::string text;
};

class ContextManager {
public:
    void add(ContextRole role, const std::string& text);
    const std::vector<ContextEntry>& history() const;
    std::size_t turn_count() const;

private:
    std::vector<ContextEntry> history_;
};
}  // namespace warden::core
