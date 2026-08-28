#include <filesystem>
#include <iostream>

#include <SDL3/SDL.h>

#include "jrpgmaker/project/workspace.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"

namespace {

void PrintStartupDiagnostics(
    const std::vector<jrpgmaker::ui::EditorStartupDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics)
        std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 3)
        return 2;
    const bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
    if (argc == 3 && std::string(argv[2]) != "--smoke")
        return 2;

    const auto resources = jrpgmaker::ui::LoadEditorResources(
        std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_ROOT));
    if (!resources) {
        PrintStartupDiagnostics(resources.diagnostics);
        return 1;
    }

    if (argc == 2 && !smoke || argc == 3) {
        const auto project_argument = std::filesystem::path(argv[1]);
        jrpgmaker::project::ProjectWorkspace workspace{project_argument};
        const auto opened = workspace.Open();
        if (!opened) {
            for (const auto& diagnostic : opened.diagnostics)
                std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
            return 1;
        }
        const auto diagnosed = workspace.Diagnose(*opened.snapshot);
        if (!diagnosed) {
            for (const auto& diagnostic : diagnosed.diagnostics)
                std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
            return 1;
        }
    }

    const auto title = resources.bundle->locale.strings.at("editor.window.title");
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;
    SDL_Window* window = SDL_CreateWindow(title.c_str(), 1280, 720, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }

    bool running = true;
    SDL_Event event{};
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                running = false;
        }
        if (smoke)
            break;
        SDL_Delay(8);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
