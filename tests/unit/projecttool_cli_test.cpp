#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::filesystem::path MakePluginProject(bool invalid_plugin = true) {
    const auto root = std::filesystem::temp_directory_path() /
                      (invalid_plugin ? "jrpgmaker_projecttool_invalid_plugin_fixture"
                                      : "jrpgmaker_projecttool_valid_plugin_fixture");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto source = std::filesystem::path(JRPGMAKER_ASSET_DIR).parent_path();
    std::filesystem::copy(source / "assets", root / "assets",
                          std::filesystem::copy_options::recursive, error);
    REQUIRE_FALSE(error);
    std::filesystem::copy(source / "plugins", root / "plugins",
                          std::filesystem::copy_options::recursive, error);
    REQUIRE_FALSE(error);
    std::filesystem::copy_file(source / "assets/data/project_demo.json", root / "project.json",
                               std::filesystem::copy_options::overwrite_existing, error);
    REQUIRE_FALSE(error);
    if (invalid_plugin)
        std::ofstream(root / "plugins/sample_instant/data/encounters_demo.json", std::ios::trunc)
            << R"({"schema":0,"encounters":[]})";
    return root;
}

void RequireCommandRejectsInvalidPluginData(std::string_view command_name) {
    const auto root = MakePluginProject();
    const auto output =
        root.parent_path() / ("jrpgmaker_projecttool_" + std::string(command_name) + "_output.txt");
    const auto command = std::filesystem::path(JRPGMAKER_PROJECTTOOL_EXECUTABLE).string() + " " +
                         std::string(command_name) + " " + root.string() + " > " + output.string() +
                         " 2>&1";

    const int exit_code = std::system(command.c_str());
    const auto text = ReadText(output);
    INFO(text);
    REQUIRE(exit_code != 0);
    REQUIRE(text.find("sample.instant.data") != std::string::npos);
    REQUIRE(text.find("plugins/sample_instant/data/encounters_demo.json") != std::string::npos);

    std::error_code error;
    std::filesystem::remove(output, error);
    std::filesystem::remove_all(root, error);
}

} // namespace

TEST_CASE("projecttool validate rejects invalid declared plugin data",
          "[projecttool][plugin][p13]") {
    RequireCommandRejectsInvalidPluginData("validate");
}

TEST_CASE("projecttool diagnosis commands reject invalid declared plugin data",
          "[projecttool][plugin][p13]") {
    RequireCommandRejectsInvalidPluginData("diagnose");
    RequireCommandRejectsInvalidPluginData("preview");
}

TEST_CASE("projecttool data-write preserves plugin data when plugin validation rejects the patch",
          "[projecttool][plugin][p13][data-write]") {
    const auto root = MakePluginProject();
    const auto relative = std::filesystem::path("plugins/sample_instant/data/encounters_demo.json");
    const auto target = root / relative;
    const auto before = ReadText(target);
    const auto patch = root / "plugin_patch.json";
    std::ofstream(patch) << R"({"encounters":[{"id":"changed"}]})";
    const auto output = root.parent_path() / "jrpgmaker_projecttool_data_write_output.txt";
    const auto command = std::filesystem::path(JRPGMAKER_PROJECTTOOL_EXECUTABLE).string() +
                         " data-write " + root.string() + " " + relative.generic_string() + " " +
                         patch.string() + " > " + output.string() + " 2>&1";

    const int exit_code = std::system(command.c_str());
    const auto text = ReadText(output);
    INFO(text);
    REQUIRE(exit_code != 0);
    REQUIRE(text.find("sample.instant.data") != std::string::npos);
    REQUIRE(ReadText(target) == before);
    REQUIRE_FALSE(std::filesystem::exists(target.string() + ".tmp"));
    REQUIRE_FALSE(std::filesystem::exists(target.string() + ".bak"));

    std::error_code error;
    std::filesystem::remove(output, error);
    std::filesystem::remove_all(root, error);
}

TEST_CASE(
    "projecttool data-write preserves an unrelated target when project plugin data is invalid",
    "[projecttool][plugin][p13][data-write]") {
    const auto root = MakePluginProject();
    const auto relative = std::filesystem::path("assets/data/calendar_demo.json");
    const auto target = root / relative;
    const auto before = ReadText(target);
    const auto patch = root / "calendar_patch.json";
    std::ofstream(patch) << R"({"id":"must.not.commit"})";
    const auto output = root.parent_path() / "jrpgmaker_projecttool_unrelated_output.txt";
    const auto command = std::filesystem::path(JRPGMAKER_PROJECTTOOL_EXECUTABLE).string() +
                         " data-write " + root.string() + " " + relative.generic_string() + " " +
                         patch.string() + " > " + output.string() + " 2>&1";

    const int exit_code = std::system(command.c_str());
    const auto text = ReadText(output);
    INFO(text);
    REQUIRE(exit_code != 0);
    REQUIRE(text.find("sample.instant.data") != std::string::npos);
    REQUIRE(ReadText(target) == before);
    REQUIRE_FALSE(std::filesystem::exists(target.string() + ".tmp"));
    REQUIRE_FALSE(std::filesystem::exists(target.string() + ".bak"));

    std::error_code error;
    std::filesystem::remove(output, error);
    std::filesystem::remove_all(root, error);
}

TEST_CASE("projecttool data commands use workspace diff and commit semantics",
          "[projecttool][p13][data-write]") {
    const auto root = MakePluginProject(false);
    const auto relative = std::filesystem::path("assets/data/calendar_demo.json");
    const auto target = root / relative;
    const auto before = ReadText(target);
    const auto patch = root / "calendar_patch.json";
    std::ofstream(patch) << R"({"id":"calendar.blackbox"})";
    const auto output = root.parent_path() / "jrpgmaker_projecttool_calendar_output.txt";
    const auto command_prefix = std::filesystem::path(JRPGMAKER_PROJECTTOOL_EXECUTABLE).string();
    const auto arguments = " " + root.string() + " " + relative.generic_string() + " " +
                           patch.string() + " > " + output.string() + " 2>&1";

    REQUIRE(std::system((command_prefix + " data-diff" + arguments).c_str()) == 0);
    auto text = ReadText(output);
    INFO(text);
    REQUIRE(text.find("/id:") != std::string::npos);
    REQUIRE(ReadText(target) == before);
    REQUIRE_FALSE(std::filesystem::exists(target.string() + ".tmp"));
    REQUIRE_FALSE(std::filesystem::exists(target.string() + ".bak"));

    REQUIRE(std::system((command_prefix + " data-write" + arguments).c_str()) == 0);
    text = ReadText(output);
    INFO(text);
    REQUIRE(text.find("data written; backup=") != std::string::npos);
    nlohmann::json written;
    std::ifstream(target) >> written;
    REQUIRE(written["id"] == "calendar.blackbox");
    REQUIRE(ReadText(target.string() + ".bak") == before);
    REQUIRE_FALSE(std::filesystem::exists(target.string() + ".tmp"));

    std::error_code error;
    std::filesystem::remove(output, error);
    std::filesystem::remove_all(root, error);
}
