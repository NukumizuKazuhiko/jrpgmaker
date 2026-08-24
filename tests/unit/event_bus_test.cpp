#include <catch2/catch_test_macros.hpp>

#include <string>

#include "jrpgmaker/core/event_bus.hpp"

namespace {

struct FirstEvent {
    int value = 0;
};

struct SecondEvent {
    std::string label;
};

} // namespace

TEST_CASE("event bus delivers to subscribers of a type", "[core][event_bus]") {
    jrpgmaker::core::EventBus bus;
    int received = 0;
    bus.Subscribe<FirstEvent>([&](const FirstEvent& event) { received = event.value; });

    bus.Publish(FirstEvent{.value = 42});

    REQUIRE(received == 42);
}

TEST_CASE("event bus dispatches in subscription order", "[core][event_bus]") {
    jrpgmaker::core::EventBus bus;
    std::string order;
    bus.Subscribe<FirstEvent>([&](const FirstEvent&) { order += "a"; });
    bus.Subscribe<FirstEvent>([&](const FirstEvent&) { order += "b"; });

    bus.Publish(FirstEvent{.value = 1});

    REQUIRE(order == "ab");
}

TEST_CASE("event bus isolates distinct event types", "[core][event_bus]") {
    jrpgmaker::core::EventBus bus;
    int first_count = 0;
    int second_count = 0;
    bus.Subscribe<FirstEvent>([&](const FirstEvent&) { ++first_count; });
    bus.Subscribe<SecondEvent>([&](const SecondEvent&) { ++second_count; });

    bus.Publish(FirstEvent{.value = 1});
    bus.Publish(SecondEvent{.label = "x"});

    REQUIRE(first_count == 1);
    REQUIRE(second_count == 1);
    REQUIRE(bus.subscribed_type_count() == 2);
}

TEST_CASE("event bus publish without subscribers is a no-op", "[core][event_bus]") {
    jrpgmaker::core::EventBus bus;
    REQUIRE_NOTHROW(bus.Publish(FirstEvent{.value = 1}));
    REQUIRE(bus.subscribed_type_count() == 0);
}