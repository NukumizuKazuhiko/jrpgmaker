#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::filesystem::path MakeInvalidPluginProject() {
    const auto root =
        std::filesystem::temp_directory_path() / "jrpgmaker_projecttool_invalid_plugin_fixture";
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
    std::ofstream(root / "plugins/sample_instant/data/encounters_demo.json", std::ios::trunc)
        << R"({"schema":0,"encounters":[]})";
    return root;
}

void RequireCommandRejectsInvalidPluginData(std::string_view command_name) {
    const auto root = MakeInvalidPluginProject();
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
