#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/localization.hpp"

TEST_CASE("localization covers nested event text keys", "[localization][p9]") {
    const auto script = jrpgmaker::domain::ParseEventScriptText(R"json({
        "schema":1,"events":[{"id":"e","instructions":[
          {"op":"choice","prompt_text_key":"prompt","options":[
            {"text_key":"yes","instructions":[]},
            {"text_key":"no","instructions":[{"op":"dialog","speaker":"n","text_key":"reply"}]}
          ]}
        ]}]
    })json");
    const auto table = jrpgmaker::domain::ParseLocalizationTable(nlohmann::json::parse(R"json({
        "schema":1,"locale":"zh-CN","strings":{"prompt":"要帮忙吗？","yes":"是","no":"否","reply":"谢谢。"}
    })json"));
    REQUIRE(table);
    REQUIRE(jrpgmaker::domain::ValidateLocalizationCoverage(script, *table.table).empty());
}

TEST_CASE("localization rejects malformed UTF-8 and reports missing keys", "[localization][p9]") {
    const auto malformed = jrpgmaker::domain::ParseLocalizationTable(
        nlohmann::json{{"schema", 1}, {"locale", "en"}, {"strings", {{"bad", "\xFF"}}}});
    REQUIRE_FALSE(malformed);

    const auto script = jrpgmaker::domain::ParseEventScriptText(R"json({
        "schema":1,"events":[{"id":"e","instructions":[
          {"op":"dialog","speaker":"n","text_key":"missing"}
        ]}]
    })json");
    const auto table = jrpgmaker::domain::ParseLocalizationTable(
        nlohmann::json{{"schema", 1}, {"locale", "en"}, {"strings", {{"other", "ok"}}}});
    REQUIRE(table);
    const auto issues = jrpgmaker::domain::ValidateLocalizationCoverage(script, *table.table);
    REQUIRE(issues.size() == 1);
    REQUIRE(issues.front().key == "missing");
}
