#pragma once

#include <string>

namespace warden::core {
class OutputManager {
public:
    void initialized() const;
    void write_response(const std::string& response) const;
    void shutting_down() const;
};
}  // namespace warden::core
