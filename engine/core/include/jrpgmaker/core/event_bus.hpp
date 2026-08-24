#pragma once

#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jrpgmaker::core {

// Type-erased event bus (P3 contract): domain publishes structured commands /
// state projections, presentation layers (ui/render/audio) subscribe. Subscribers
// of a given T receive a const ref; publish copies T into the bus then invokes
// each subscriber. Unsubscribing is not supported in v0 (consumers live for the
// whole process); the bus holds plain function objects, so consumers must keep
// any captured state alive for the bus's lifetime.
//
// Cross-layer direction is contractually one-way (docs/01 owner map): only
// domain emits, presentation consumes; presentation never publishes business
// truth back.
class EventBus {
public:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Subscribes a handler for event type T. Handlers run in subscription order.
    template <typename T, typename Handler> void Subscribe(Handler&& handler) {
        handlers_[std::type_index(typeid(T))].push_back(
            [h = std::forward<Handler>(handler)](const void* event) {
                h(*static_cast<const T*>(event));
            });
    }

    // Publishes an event to all handlers of T.
    template <typename T> void Publish(const T& event) {
        const auto it = handlers_.find(std::type_index(typeid(T)));
        if (it == handlers_.end()) {
            return;
        }
        for (const auto& handler : it->second) {
            handler(&event);
        }
    }

    // Number of distinct subscribed event types (test/diagnostic probe).
    std::size_t subscribed_type_count() const { return handlers_.size(); }

private:
    using Handler = std::function<void(const void*)>;
    std::unordered_map<std::type_index, std::vector<Handler>> handlers_;
};

} // namespace jrpgmaker::core