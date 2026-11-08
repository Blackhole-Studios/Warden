#include "core.hpp"

namespace warden::core {
void Core::run(const std::atomic<bool>& running) {
    output_.initialized();
    while (running.load()) {
        const auto input = input_.read();
        if (!input || !running.load()) break;

        if (*input == "exit") break;

        context_.add(ContextRole::User, *input);
        const ProcessingRoute route = routing_.choose(context_);
        const std::string reply = response_.respond(*input, route);
        context_.add(ContextRole::Assistant, reply);
        output_.write_response(reply);
    }
    output_.shutting_down();
}
}  // namespace warden::core
