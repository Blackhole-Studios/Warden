#pragma once

#include "context/context_manager.hpp"
#include "input/input_manager.hpp"
#include "output/output_manager.hpp"
#include "response/response_manager.hpp"
#include "routing/routing_manager.hpp"

#include <atomic>

namespace warden::core {
class Core {
public:
    void run(const std::atomic<bool>& running);

private:
    InputManager input_;
    ContextManager context_;
    RoutingManager routing_;
    ResponseManager response_;
    OutputManager output_;
};
}  // namespace warden::core
