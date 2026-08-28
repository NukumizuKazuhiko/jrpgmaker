#include <filesystem>
#include <iostream>

#include "jrpgmaker/ui/editor_resources.hpp"

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;

    const auto result = jrpgmaker::ui::LoadEditorResources(std::filesystem::path(argv[1]));
    for (const auto& diagnostic : result.diagnostics)
        std::cout << diagnostic.code << '\t' << diagnostic.path << '\n';
    return result ? 0 : 1;
}
