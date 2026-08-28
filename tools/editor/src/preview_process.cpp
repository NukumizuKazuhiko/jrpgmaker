#include "jrpgmaker/editor/preview_process.hpp"

#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace jrpgmaker::editor {

struct PreviewProcess::Impl {
#if defined(_WIN32)
    PROCESS_INFORMATION process{};
#else
    pid_t pid = -1;
#endif
};

#if defined(_WIN32)
namespace {

std::wstring NativePath(const std::filesystem::path& path) {
    return path.wstring();
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (const wchar_t character : value) {
        if (character == L'\"')
            quoted += L'\\';
        quoted += character;
    }
    quoted += L'\"';
    return quoted;
}

} // namespace
#endif

PreviewProcess::~PreviewProcess() { Stop(); }

bool PreviewProcess::Start(const std::filesystem::path& executable,
                           const std::filesystem::path& project_root) {
    Stop();
    state_ = {};
    std::error_code error;
    if (executable.empty() || project_root.empty() ||
        !std::filesystem::is_regular_file(executable, error) || error ||
        !std::filesystem::is_directory(project_root, error) || error) {
        state_.error = "editor.preview.executable_or_project_invalid";
        return false;
    }
    impl_ = new Impl;
#if defined(_WIN32)
    const auto command = QuoteArgument(NativePath(executable)) + L" " +
                         QuoteArgument(NativePath(project_root));
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(NativePath(executable).c_str(), mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &impl_->process)) {
        delete impl_;
        impl_ = nullptr;
        state_.error = "editor.preview.process_start_failed";
        return false;
    }
#else
    const std::string executable_string = executable.string();
    const std::string root_string = project_root.string();
    char* arguments[] = {const_cast<char*>(executable_string.c_str()),
                         const_cast<char*>(root_string.c_str()), nullptr};
    if (posix_spawn(&impl_->pid, executable_string.c_str(), nullptr, nullptr, arguments, environ) != 0) {
        delete impl_;
        impl_ = nullptr;
        state_.error = "editor.preview.process_start_failed";
        return false;
    }
#endif
    state_.running = true;
    return true;
}

void PreviewProcess::Poll() {
    if (impl_ == nullptr || !state_.running)
        return;
#if defined(_WIN32)
    const DWORD result = WaitForSingleObject(impl_->process.hProcess, 0);
    if (result != WAIT_OBJECT_0)
        return;
    DWORD exit_code = 1;
    (void) GetExitCodeProcess(impl_->process.hProcess, &exit_code);
    state_.exit_code = static_cast<int>(exit_code);
    state_.running = false;
    CloseHandle(impl_->process.hThread);
    CloseHandle(impl_->process.hProcess);
#else
    int status = 0;
    const auto result = waitpid(impl_->pid, &status, WNOHANG);
    if (result == 0)
        return;
    state_.running = false;
    state_.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
    delete impl_;
    impl_ = nullptr;
}

void PreviewProcess::Stop() {
    if (impl_ == nullptr)
        return;
#if defined(_WIN32)
    if (state_.running)
        (void) TerminateProcess(impl_->process.hProcess, 1);
    CloseHandle(impl_->process.hThread);
    CloseHandle(impl_->process.hProcess);
#else
    if (state_.running) {
        (void) kill(impl_->pid, SIGTERM);
        (void) waitpid(impl_->pid, nullptr, 0);
    }
#endif
    delete impl_;
    impl_ = nullptr;
    state_.running = false;
}

} // namespace jrpgmaker::editor
