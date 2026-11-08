#pragma once

#include <optional>
#include <string>

namespace warden::core {
class InputManager {
public:
    std::optional<std::string> read();

private:
    bool first_prompt_{true};
};
}  // namespace warden::core
