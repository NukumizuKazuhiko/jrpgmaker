#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <variant>

#include <nlohmann/json.hpp>

#include "jrpgmaker/domain/event_script.hpp"

using jrpgmaker::domain::BranchInstruction;
using jrpgmaker::domain::ChoiceInstruction;
using jrpgmaker::domain::DialogInstruction;
using jrpgmaker::domain::EventScript;
using jrpgmaker::domain::Instruction;
using jrpgmaker::domain::ParseEventScript;
using jrpgmaker::domain::SetFlagInstruction;
using jrpgmaker::domain::WaitInstruction;
using nlohmann::json;

TEST_CASE("event script parses a minimal document", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {
                "id": "meet_npc",
                "instructions": [
                    {"op": "set_flag", "flag": "npc.met", "value": true},
                    {"op": "dialog", "speaker": "alice", "text_key": "npc.greeting"},
                    {"op": "wait", "seconds": 0.5}
                ]
            }
        ]
    })"_json;

    const EventScript script = ParseEventScript(document);
    REQUIRE(script.schema == 1);
    REQUIRE(script.events.size() == 1);
    REQUIRE(script.events[0].id == "meet_npc");
    REQUIRE(script.events[0].instructions.size() == 3);

    const auto& first = script.events[0].instructions[0];
    const auto* set_flag = std::get_if<SetFlagInstruction>(&first.op);
    REQUIRE(set_flag != nullptr);
    REQUIRE(set_flag->flag == "npc.met");
    REQUIRE(set_flag->value);

    const auto& second = script.events[0].instructions[1];
    const auto* dialog = std::get_if<DialogInstruction>(&second.op);
    REQUIRE(dialog != nullptr);
    REQUIRE(dialog->speaker == "alice");
    REQUIRE(dialog->text_key == "npc.greeting");

    const auto& third = script.events[0].instructions[2];
    const auto* wait = std::get_if<WaitInstruction>(&third.op);
    REQUIRE(wait != nullptr);
    REQUIRE(wait->seconds == 0.5);
}

TEST_CASE("event script parses branch with sub-sequences", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {
                "id": "choice",
                "instructions": [
                    {
                        "op": "branch",
                        "flag": "npc.met",
                        "if_set": [{"op": "dialog", "speaker": "alice", "text_key": "npc.again"}],
                        "if_not_set": [{"op": "set_flag", "flag": "npc.met", "value": true}]
                    }
                ]
            }
        ]
    })"_json;

    const EventScript script = ParseEventScript(document);
    REQUIRE(script.events[0].instructions.size() == 1);
    const auto* branch = std::get_if<BranchInstruction>(&script.events[0].instructions[0].op);
    REQUIRE(branch != nullptr);
    REQUIRE(branch->flag == "npc.met");
    REQUIRE(branch->if_set.size() == 1);
    REQUIRE(branch->if_not_set.size() == 1);
    REQUIRE(std::holds_alternative<DialogInstruction>(branch->if_set[0].op));
    REQUIRE(std::holds_alternative<SetFlagInstruction>(branch->if_not_set[0].op));
}

TEST_CASE("event script rejects wrong schema version", "[domain][event_script]") {
    const json document = R"({
        "schema": 99,
        "events": []
    })"_json;

    REQUIRE_THROWS_AS(ParseEventScript(document), std::invalid_argument);
}

TEST_CASE("event script rejects unknown ops", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {"id": "bad", "instructions": [{"op": "teleport"}]}
        ]
    })"_json;

    REQUIRE_THROWS_AS(ParseEventScript(document), std::invalid_argument);
}

TEST_CASE("event script rejects missing required fields", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {"id": "bad", "instructions": [{"op": "dialog", "speaker": "alice"}]}
        ]
    })"_json;

    REQUIRE_THROWS_AS(ParseEventScript(document), std::invalid_argument);
}

TEST_CASE("event script rejects negative wait", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {"id": "bad", "instructions": [{"op": "wait", "seconds": -1.0}]}
        ]
    })"_json;

    REQUIRE_THROWS_AS(ParseEventScript(document), std::invalid_argument);
}

TEST_CASE("event script rejects set_flag without an explicit value", "[domain][event_script]") {
    // set_flag requires an explicit boolean value; silently defaulting to true
    // would hide a misspelled/omitted field (docs/01 schema v1: missing
    // required fields raise at parse time).
    const json document = R"({
        "schema": 1,
        "events": [
            {"id": "bad", "instructions": [{"op": "set_flag", "flag": "quest.done"}]}
        ]
    })"_json;

    REQUIRE_THROWS_AS(ParseEventScript(document), std::invalid_argument);
}

TEST_CASE("event script rejects wait without a seconds field", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {"id": "bad", "instructions": [{"op": "wait"}]}
        ]
    })"_json;

    REQUIRE_THROWS_AS(ParseEventScript(document), std::invalid_argument);
}

TEST_CASE("event script clear_flag shorthand parses to false", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {"id": "clear", "instructions": [{"op": "clear_flag", "flag": "npc.met"}]}
        ]
    })"_json;

    const EventScript script = ParseEventScript(document);
    const auto* set_flag = std::get_if<SetFlagInstruction>(&script.events[0].instructions[0].op);
    REQUIRE(set_flag != nullptr);
    REQUIRE_FALSE(set_flag->value);
}

TEST_CASE("event script parses the committed demo data file", "[domain][event_script][data]") {
#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif
    const std::filesystem::path path =
        std::filesystem::path(JRPGMAKER_ASSET_DIR) / "data" / "events_demo.json";
    std::ifstream file(path);
    REQUIRE(file.is_open());
    const EventScript script = ParseEventScript(nlohmann::json::parse(file));
    REQUIRE(script.events.size() == 6);
    REQUIRE(script.events[0].id == "intro");
    REQUIRE(script.events[1].id == "meet_alice");
    REQUIRE(script.events[2].id == "alice_ask_help");
    REQUIRE(script.events[3].id == "chest_west");
    REQUIRE(script.events[4].id == "alice_reward");
    REQUIRE(script.events[5].id == "chest_west_echo");
}

TEST_CASE("event script parses choice with inline option sequences", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {
                "id": "ask",
                "instructions": [
                    {
                        "op": "choice", "prompt_text_key": "ask.help",
                        "options": [
                            {"text_key": "opt.yes", "instructions": [{"op": "set_flag", "flag": "help.yes", "value": true}]},
                            {"text_key": "opt.no", "instructions": [{"op": "dialog", "speaker": "alice", "text_key": "declined"}]}
                        ]
                    }
                ]
            }
        ]
    })"_json;

    const EventScript script = ParseEventScript(document);
    const auto* choice = std::get_if<ChoiceInstruction>(&script.events[0].instructions[0].op);
    REQUIRE(choice != nullptr);
    REQUIRE(choice->prompt_text_key == "ask.help");
    REQUIRE(choice->options.size() == 2);
    REQUIRE(choice->options[0].text_key == "opt.yes");
    REQUIRE(std::holds_alternative<SetFlagInstruction>(choice->options[0].instructions[0].op));
    REQUIRE(choice->options[1].text_key == "opt.no");
    REQUIRE(std::holds_alternative<DialogInstruction>(choice->options[1].instructions[0].op));
}

TEST_CASE("event script rejects choice with empty options", "[domain][event_script]") {
    const json document = R"({
        "schema": 1,
        "events": [
            {
                "id": "ask",
                "instructions": [{"op": "choice", "prompt_text_key": "ask", "options": []}]
            }
        ]
    })"_json;

    REQUIRE_THROWS_AS(ParseEventScript(document), std::invalid_argument);
}
