#include "jrpgmaker/editor/preview_process.hpp"

#include <algorithm>
#include <array>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace jrpgmaker::editor {

struct PreviewProcess::Impl {
#if defined(_WIN32)
    PROCESS_INFORMATION process{};
    HANDLE output_read = nullptr;
    HANDLE error_read = nullptr;
#else
    pid_t pid = -1;
    int output_read = -1;
    int error_read = -1;
#endif
};

namespace {

void AppendBounded(std::string& destination, const char* data, std::size_t size) {
    if (destination.size() >= PreviewProcessState::kMaxOutputBytes || size == 0)
        return;
    const auto available = PreviewProcessState::kMaxOutputBytes - destination.size();
    destination.append(data, (std::min)(available, size));
}

} // namespace

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
    SECURITY_ATTRIBUTES security_attributes{.nLength = sizeof(SECURITY_ATTRIBUTES),
                                            .lpSecurityDescriptor = nullptr,
                                            .bInheritHandle = TRUE};
    HANDLE output_write = nullptr;
    HANDLE error_write = nullptr;
    if (!CreatePipe(&impl_->output_read, &output_write, &security_attributes, 0) ||
        !CreatePipe(&impl_->error_read, &error_write, &security_attributes, 0) ||
        !SetHandleInformation(impl_->output_read, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(impl_->error_read, HANDLE_FLAG_INHERIT, 0)) {
        if (output_write != nullptr)
            CloseHandle(output_write);
        if (error_write != nullptr)
            CloseHandle(error_write);
        if (impl_->output_read != nullptr)
            CloseHandle(impl_->output_read);
        if (impl_->error_read != nullptr)
            CloseHandle(impl_->error_read);
        delete impl_;
        impl_ = nullptr;
        state_.error = "editor.preview.output_pipe_failed";
        return false;
    }
    const auto command = QuoteArgument(NativePath(executable)) + L" " +
                         QuoteArgument(NativePath(project_root));
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output_write;
    startup.hStdError = error_write;
    if (!CreateProcessW(NativePath(executable).c_str(), mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &impl_->process)) {
        CloseHandle(output_write);
        CloseHandle(error_write);
        CloseHandle(impl_->output_read);
        CloseHandle(impl_->error_read);
        delete impl_;
        impl_ = nullptr;
        state_.error = "editor.preview.process_start_failed";
        return false;
    }
    CloseHandle(output_write);
    CloseHandle(error_write);
#else
    int output_pipe[2] = {-1, -1};
    int error_pipe[2] = {-1, -1};
    if (pipe(output_pipe) != 0 || pipe(error_pipe) != 0) {
        if (output_pipe[0] >= 0)
            close(output_pipe[0]);
        if (output_pipe[1] >= 0)
            close(output_pipe[1]);
        if (error_pipe[0] >= 0)
            close(error_pipe[0]);
        if (error_pipe[1] >= 0)
            close(error_pipe[1]);
        delete impl_;
        impl_ = nullptr;
        state_.error = "editor.preview.output_pipe_failed";
        return false;
    }
    (void) fcntl(output_pipe[0], F_SETFL, fcntl(output_pipe[0], F_GETFL) | O_NONBLOCK);
    (void) fcntl(error_pipe[0], F_SETFL, fcntl(error_pipe[0], F_GETFL) | O_NONBLOCK);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, error_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, error_pipe[0]);
    const std::string executable_string = executable.string();
    const std::string root_string = project_root.string();
    char* arguments[] = {const_cast<char*>(executable_string.c_str()),
                         const_cast<char*>(root_string.c_str()), nullptr};
    const auto spawn_result = posix_spawn(&impl_->pid, executable_string.c_str(), &actions, nullptr,
                                         arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(output_pipe[1]);
    close(error_pipe[1]);
    if (spawn_result != 0) {
        close(output_pipe[0]);
        close(error_pipe[0]);
        delete impl_;
        impl_ = nullptr;
        state_.error = "editor.preview.process_start_failed";
        return false;
    }
    impl_->output_read = output_pipe[0];
    impl_->error_read = error_pipe[0];
#endif
    state_.running = true;
    return true;
}

void PreviewProcess::Poll() {
    if (impl_ == nullptr || !state_.running)
        return;
#if defined(_WIN32)
    auto drain = [](HANDLE handle, std::string& destination) {
        std::array<char, 4096> buffer{};
        DWORD available = 0;
        while (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD read = 0;
            if (!ReadFile(handle, buffer.data(),
                          static_cast<DWORD>((std::min<std::size_t>)(buffer.size(), available)), &read,
                          nullptr) || read == 0)
                break;
            AppendBounded(destination, buffer.data(), read);
        }
    };
    drain(impl_->output_read, state_.standard_output);
    drain(impl_->error_read, state_.standard_error);
    const DWORD result = WaitForSingleObject(impl_->process.hProcess, 0);
    if (result != WAIT_OBJECT_0)
        return;
    drain(impl_->output_read, state_.standard_output);
    drain(impl_->error_read, state_.standard_error);
    DWORD exit_code = 1;
    (void) GetExitCodeProcess(impl_->process.hProcess, &exit_code);
    state_.exit_code = static_cast<int>(exit_code);
    state_.running = false;
    CloseHandle(impl_->process.hThread);
    CloseHandle(impl_->process.hProcess);
    CloseHandle(impl_->output_read);
    CloseHandle(impl_->error_read);
#else
    auto drain = [](int descriptor, std::string& destination) {
        std::array<char, 4096> buffer{};
        for (;;) {
            const auto read = ::read(descriptor, buffer.data(), buffer.size());
            if (read <= 0)
                break;
            AppendBounded(destination, buffer.data(), static_cast<std::size_t>(read));
        }
    };
    drain(impl_->output_read, state_.standard_output);
    drain(impl_->error_read, state_.standard_error);
    int status = 0;
    const auto result = waitpid(impl_->pid, &status, WNOHANG);
    if (result == 0)
        return;
    drain(impl_->output_read, state_.standard_output);
    drain(impl_->error_read, state_.standard_error);
    state_.running = false;
    state_.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    close(impl_->output_read);
    close(impl_->error_read);
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
    CloseHandle(impl_->output_read);
    CloseHandle(impl_->error_read);
#else
    if (state_.running) {
        (void) kill(impl_->pid, SIGTERM);
        (void) waitpid(impl_->pid, nullptr, 0);
    }
    close(impl_->output_read);
    close(impl_->error_read);
#endif
    delete impl_;
    impl_ = nullptr;
    state_.running = false;
}

} // namespace jrpgmaker::editor
