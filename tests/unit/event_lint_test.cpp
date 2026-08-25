#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "jrpgmaker/domain/event_lint.hpp"
#include "jrpgmaker/domain/event_script.hpp"

using jrpgmaker::domain::EventScript;
using jrpgmaker::domain::LintEventScript;
using jrpgmaker::domain::LintIssue;
using jrpgmaker::domain::LintSeverity;
using jrpgmaker::domain::ParseEventScriptText;

namespace {

std::vector<LintIssue> Lint(const std::string& json_text) {
    return LintEventScript(ParseEventScriptText(json_text));
}

bool HasError(const std::vector<LintIssue>& issues, const std::string& needle) {
    for (const auto& issue : issues) {
        if (issue.severity == LintSeverity::kError &&
            issue.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasWarning(const std::vector<LintIssue>& issues, const std::string& needle) {
    for (const auto& issue : issues) {
        if (issue.severity == LintSeverity::kWarning &&
            issue.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("lint passes a well-formed script", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [{
            "id": "a",
            "instructions": [
                {"op": "set_flag", "flag": "alice.met", "value": true},
                {"op": "branch", "flag": "alice.met",
                 "if_set": [{"op": "set_flag", "flag": "already", "value": true}],
                 "if_not_set": []},
                {"op": "dialog", "speaker": "alice", "text_key": "hello"}
            ]
        }]
    })");
    REQUIRE(issues.empty());
}

TEST_CASE("lint flags duplicate event ids", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [
            {"id": "dup", "instructions": [{"op": "wait", "seconds": 1.0}]},
            {"id": "dup", "instructions": [{"op": "wait", "seconds": 1.0}]}
        ]
    })");
    REQUIRE(HasError(issues, "duplicate event id"));
}

TEST_CASE("lint flags empty event id", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [{"id": "", "instructions": []}]
    })");
    REQUIRE(HasError(issues, "empty id"));
}

TEST_CASE("lint warns when a branch flag is never written", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [{
            "id": "a",
            "instructions": [
                {"op": "branch", "flag": "ghost", "if_set": [], "if_not_set": []}
            ]
        }]
    })");
    REQUIRE(HasWarning(issues, "ghost"));
    REQUIRE_FALSE(HasError(issues, "ghost"));
}

TEST_CASE("lint does not warn when a branch flag is written elsewhere", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [
            {"id": "a", "instructions": [{"op": "set_flag", "flag": "shared"}]},
            {"id": "b", "instructions": [{"op": "branch", "flag": "shared",
                                          "if_set": [], "if_not_set": []}]}
        ]
    })");
    REQUIRE(issues.empty());
}

TEST_CASE("lint flags blocking instructions nested in a branch arm", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [{
            "id": "a",
            "instructions": [
                {"op": "branch", "flag": "f", "if_set": [{"op": "wait", "seconds": 1.0}],
                 "if_not_set": []}
            ]
        }]
    })");
    REQUIRE(HasError(issues, "if_set contains a blocking instruction"));
}

TEST_CASE("lint flags blocking instructions nested in a choice option", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [{
            "id": "a",
            "instructions": [{
                "op": "choice", "prompt_text_key": "ask",
                "options": [{
                    "text_key": "yes",
                    "instructions": [{"op": "dialog", "speaker": "alice", "text_key": "ok"}]
                }]
            }]
        }]
    })");
    REQUIRE(HasError(issues, "choice option 'yes' contains a blocking instruction"));
}

TEST_CASE("lint flags empty flag name and empty dialog fields", "[domain][event_lint]") {
    const auto issues = Lint(R"({
        "schema": 1,
        "events": [{
            "id": "a",
            "instructions": [
                {"op": "set_flag", "flag": ""},
                {"op": "dialog", "speaker": "", "text_key": ""}
            ]
        }]
    })");
    REQUIRE(HasError(issues, "empty flag name"));
    REQUIRE(HasError(issues, "empty speaker"));
    REQUIRE(HasError(issues, "empty text_key"));
}