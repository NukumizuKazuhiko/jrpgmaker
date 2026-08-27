#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/audio/audio.hpp"
#include "jrpgmaker/core/cutscene.hpp"
#include "jrpgmaker/ui/theme.hpp"

TEST_CASE("cutscene timeline orders and activates data cues", "[cutscene][p6]") {
    const auto timeline = jrpgmaker::core::ParseCutsceneTimeline(nlohmann::json{
        {"schema", 1},
        {"cues",
         {{{"id", "late"}, {"start", 1.0}, {"duration", 1.0}, {"event_id", "e2"}},
          {{"id", "early"}, {"start", 0.0}, {"duration", 0.5}, {"event_id", "e1"}}}}});
    jrpgmaker::core::CutscenePlayer player(timeline);
    player.Advance(0.0);
    REQUIRE(player.DrainTriggeredEvents() == std::vector<std::string>{"e1"});
    REQUIRE(player.ActiveCueIds() == std::vector<std::string>{"early"});
    player.Advance(0.6);
    REQUIRE(player.ActiveCueIds().empty());
    player.Advance(0.5);
    REQUIRE(player.ActiveCueIds() == std::vector<std::string>{"late"});
    REQUIRE(player.DrainTriggeredEvents() == std::vector<std::string>{"e2"});
}

TEST_CASE("audio mixer bus is bounded and rejects invalid voices", "[audio][p6]") {
    jrpgmaker::audio::MixerBus bus(1);
    REQUIRE(bus.Play("ui.confirm", 0.5f));
    REQUIRE_FALSE(bus.Play("music", 1.0f));
    REQUIRE_FALSE(bus.Play("", 0.5f));
    REQUIRE(bus.Stop("ui.confirm"));
    REQUIRE_FALSE(bus.Stop("missing"));

    REQUIRE(bus.Play("tone", std::vector<float>{0.8f, 0.8f}, 0.5f));
    std::vector<float> mixed(3, 0.0f);
    bus.Mix(mixed);
    REQUIRE(mixed[0] == Catch::Approx(0.4f));
    REQUIRE(mixed[1] == Catch::Approx(0.4f));
    REQUIRE(mixed[2] == Catch::Approx(0.0f));
    REQUIRE(bus.voices().empty());
}

TEST_CASE("ui animation remains presentation-local", "[ui][p6]") {
    jrpgmaker::ui::AnimationValue animation;
    jrpgmaker::ui::AdvanceAnimation(animation, 1.0f, 1.0f / 60.0f);
    REQUIRE(animation.value > 0.0f);
    REQUIRE(animation.value < 1.0f);
    const auto theme = jrpgmaker::ui::ParseTheme(nlohmann::json{{"schema", 1},
                                                                {"id", "demo"},
                                                                {"accent", {0.1, 0.2, 0.3, 1.0}},
                                                                {"text_pixel_height", 24}});
    REQUIRE(theme);
    REQUIRE(theme.theme->text_pixel_height == 24);
    REQUIRE_FALSE(jrpgmaker::ui::ParseTheme(nlohmann::json{{"schema", 1},
                                                           {"id", "bad"},
                                                           {"accent", {2.0, 0.0, 0.0, 1.0}},
                                                           {"text_pixel_height", 24}}));
}
