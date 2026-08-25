// eventlint CLI: validates event script JSON files against the schema v1
// contract plus cross-event consistency checks (duplicate ids, unwritten
// branch flags, blocking instructions nested in branch/option).
//
// Usage:
//   eventlint <file.json> [file2.json ...]
//
// Exit code:
//   0  all files clean (schema parses, no lint errors; warnings allowed)
//   1  at least one file has a parse error or lint error
//   2  usage error

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/domain/event_lint.hpp"
#include "jrpgmaker/domain/event_script.hpp"

namespace {

// Lints a single file. Returns false if the file fails to parse or has at
// least one lint error. Warnings are printed but do not fail the file.
bool LintFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << path.string() << ": cannot open file\n";
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    nlohmann::json document;
    try {
        document = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::exception& error) {
        std::cerr << path.string() << ": JSON parse error: " << error.what() << '\n';
        return false;
    }

    jrpgmaker::domain::EventScript script;
    try {
        script = jrpgmaker::domain::ParseEventScript(document);
    } catch (const std::invalid_argument& error) {
        std::cerr << path.string() << ": " << error.what() << '\n';
        return false;
    }

    const std::vector<jrpgmaker::domain::LintIssue> issues =
        jrpgmaker::domain::LintEventScript(script);
    if (issues.empty()) {
        std::cout << path.string() << ": clean\n";
        return true;
    }

    bool ok = true;
    for (const auto& issue : issues) {
        const std::string location =
            issue.event_id.empty() ? "(file)" : "event '" + issue.event_id + "'";
        std::cout << path.string() << ": " << location << ": "
                  << jrpgmaker::domain::ToString(issue.severity) << ": " << issue.message << '\n';
        if (issue.severity == jrpgmaker::domain::LintSeverity::kError) {
            ok = false;
        }
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: eventlint <file.json> [file2.json ...]\n";
        return 2;
    }

    bool all_ok = true;
    for (int i = 1; i < argc; ++i) {
        if (!LintFile(argv[i])) {
            all_ok = false;
        }
    }
    return all_ok ? 0 : 1;
}