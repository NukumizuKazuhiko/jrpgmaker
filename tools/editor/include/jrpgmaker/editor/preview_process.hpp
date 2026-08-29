#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace jrpgmaker::editor {

struct PreviewProcessState {
    static constexpr std::size_t kMaxOutputBytes = 64u * 1024u;
    bool running = false;
    int exit_code = 0;
    std::string error;
    std::string standard_output;
    std::string standard_error;
};

class PreviewProcess final {
public:
    PreviewProcess() = default;
    ~PreviewProcess();

    PreviewProcess(const PreviewProcess&) = delete;
    PreviewProcess& operator=(const PreviewProcess&) = delete;

    [[nodiscard]] bool Start(const std::filesystem::path& executable,
                              const std::filesystem::path& project_root);
    void Poll();
    void Stop();
    [[nodiscard]] const PreviewProcessState& state() const { return state_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    PreviewProcessState state_;
};

} // namespace jrpgmaker::editor
