#include "Spawn.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#endif

#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

namespace Luwow::Process {
#if defined(_WIN32)
    static std::wstring toWide(const std::string& text) {
        if (text.empty()) return {};
        int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    static std::string toUtf8(const std::wstring& wide) {
        if (wide.empty()) return {};
        int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string text(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), length, nullptr, nullptr);
        return text;
    }

    static std::string lastError() {
        DWORD code = GetLastError();
        wchar_t* message = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
            reinterpret_cast<wchar_t*>(&message), 0, nullptr);
        std::string text = message ? toUtf8(message) : "error " + std::to_string(code);
        if (message) LocalFree(message);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        return text;
    }

    // An argument quoted the way programs split their command line back into arguments
    static std::wstring quoteArgument(const std::wstring& argument) {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
        std::wstring quoted = L"\"";
        for (auto it = argument.begin();; ++it) {
            size_t backslashes = 0;
            while (it != argument.end() && *it == L'\\') {
                ++it;
                ++backslashes;
            }
            if (it == argument.end()) {
                quoted.append(backslashes * 2, L'\\');
                break;
            }
            if (*it == L'"') {
                quoted.append(backslashes * 2 + 1, L'\\');
            } else {
                quoted.append(backslashes, L'\\');
            }
            quoted.push_back(*it);
        }
        quoted.push_back(L'"');
        return quoted;
    }

    static std::wstring commandLineOf(const std::vector<std::string>& args) {
        if (args.size() == 5 && args[1] == "/d" && args[2] == "/s" && args[3] == "/c") {
            return quoteArgument(toWide(args[0])) + L" /d /s /c \"" + toWide(args[4]) + L"\"";
        }
        std::wstring line;
        for (const std::string& argument : args) {
            if (!line.empty()) line.push_back(L' ');
            line += quoteArgument(toWide(argument));
        }
        return line;
    }

    // The current environment with the options' variables over it, as a CreateProcess block. Empty when
    // there's nothing to change, so the child inherits the environment as it is.
    static std::wstring environmentBlock(const SpawnOptions& options) {
        if (options.env.empty()) return {};
        struct CaseInsensitive {
            bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
        };
        std::map<std::wstring, std::wstring, CaseInsensitive> variables;
        if (LPWCH block = GetEnvironmentStringsW()) {
            for (LPWCH entry = block; *entry; entry += wcslen(entry) + 1) {
                std::wstring line(entry);
                size_t equals = line.find(L'=', 1);
                if (equals != std::wstring::npos) variables[line.substr(0, equals)] = line.substr(equals + 1);
            }
            FreeEnvironmentStringsW(block);
        }
        for (const auto& [name, value] : options.env) variables[toWide(name)] = toWide(value);

        std::wstring result;
        for (const auto& [name, value] : variables) {
            result += name + L"=" + value;
            result.push_back(L'\0');
        }
        result.push_back(L'\0');
        return result;
    }

    static HANDLE nulDevice(bool write) {
        SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        return CreateFileW(L"NUL", write ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    }

    static void readAll(HANDLE pipe, std::string& into) {
        char chunk[8192];
        DWORD read = 0;
        while (ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) && read > 0) into.append(chunk, read);
    }

    RunResult run(const std::vector<std::string>& args, const SpawnOptions& options) {
        RunResult result;
        SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr, input = nullptr;

        STARTUPINFOW startup{ sizeof(STARTUPINFOW) };
        bool capture = options.stdio == Stdio::Default;
        if (options.stdio != Stdio::Inherit) {
            startup.dwFlags = STARTF_USESTDHANDLES;
            input = nulDevice(false);
            startup.hStdInput = input;
            if (capture) {
                if (!CreatePipe(&outRead, &outWrite, &security, 0) || !CreatePipe(&errRead, &errWrite, &security, 0)) {
                    result.error = lastError();
                    return result;
                }
                SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
                SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
                startup.hStdOutput = outWrite;
                startup.hStdError = errWrite;
            } else {
                outWrite = nulDevice(true);
                startup.hStdOutput = outWrite;
                startup.hStdError = outWrite;
            }
        }

        std::wstring commandLine = commandLineOf(args);
        std::wstring environment = environmentBlock(options);
        std::wstring cwd = toWide(options.cwd);
        PROCESS_INFORMATION process{};
        DWORD flags = CREATE_UNICODE_ENVIRONMENT | (options.stdio == Stdio::Inherit ? 0 : CREATE_NO_WINDOW);
        BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, flags,
            environment.empty() ? nullptr : environment.data(), cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
        if (!created) result.error = lastError();

        // The child has its own copies of the write ends now, so reading ends once it's done
        for (HANDLE handle : { outWrite, errWrite, input }) {
            if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        }

        if (created) {
            result.started = true;
            std::thread errReader;
            if (capture) errReader = std::thread(readAll, errRead, std::ref(result.stderrText));
            std::thread outReader;
            if (capture) outReader = std::thread(readAll, outRead, std::ref(result.stdoutText));

            DWORD wait = options.timeout > 0 ? static_cast<DWORD>(options.timeout * 1000) : INFINITE;
            if (WaitForSingleObject(process.hProcess, wait) == WAIT_TIMEOUT) {
                result.timedOut = true;
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, INFINITE);
            }
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            result.exitCode = static_cast<int>(code);

            if (outReader.joinable()) outReader.join();
            if (errReader.joinable()) errReader.join();
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }

        if (outRead) CloseHandle(outRead);
        if (errRead) CloseHandle(errRead);
        return result;
    }

    int spawn(const std::vector<std::string>& args, const SpawnOptions& options, std::string& error) {
        std::wstring commandLine = commandLineOf(args);
        std::wstring environment = environmentBlock(options);
        std::wstring cwd = toWide(options.cwd);

        STARTUPINFOW startup{ sizeof(STARTUPINFOW) };
        PROCESS_INFORMATION process{};
        DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | (options.stdio == Stdio::Inherit ? 0 : DETACHED_PROCESS);
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, flags,
                environment.empty() ? nullptr : environment.data(), cwd.empty() ? nullptr : cwd.c_str(), &startup, &process)) {
            error = lastError();
            return 0;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(process.dwProcessId);
    }

    bool open(const std::string& target, std::string& error) {
        HINSTANCE result = ShellExecuteW(nullptr, L"open", toWide(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) > 32) return true;
        error = lastError();
        return false;
    }

    std::vector<std::string> shellCommand(const std::string& command) {
        wchar_t comspec[MAX_PATH];
        DWORD size = GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
        std::string shell = size > 0 && size < MAX_PATH ? toUtf8(std::wstring(comspec, size)) : "cmd.exe";
        return { shell, "/d", "/s", "/c", command };
    }

    // A process's main windows: shown, top-level, without an owner
    static std::vector<HWND> mainWindowsOf(int pid) {
        struct Search {
            DWORD pid;
            std::vector<HWND> windows;
        } search{ static_cast<DWORD>(pid), {} };
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto* search = reinterpret_cast<Search*>(data);
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == search->pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) search->windows.push_back(window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.windows;
    }

    std::vector<ProcessInfo> list(const std::string& name) {
        std::vector<ProcessInfo> processes;
        std::wstring wanted = toWide(name);
        if (!wanted.empty() && (wanted.size() < 4 || _wcsicmp(wanted.c_str() + wanted.size() - 4, L".exe") != 0)) wanted += L".exe";

        // One pass over the windows finds which processes have one
        std::set<DWORD> withWindows;
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) reinterpret_cast<std::set<DWORD>*>(data)->insert(owner);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&withWindows));

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return processes;
        PROCESSENTRY32W entry{ sizeof(PROCESSENTRY32W) };
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
            if (!wanted.empty() && _wcsicmp(entry.szExeFile, wanted.c_str()) != 0) continue;
            processes.push_back({ static_cast<int>(entry.th32ProcessID), toUtf8(entry.szExeFile), withWindows.count(entry.th32ProcessID) > 0 });
        }
        CloseHandle(snapshot);
        return processes;
    }

    bool kill(int pid) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
        if (!process) return false;
        BOOL ok = TerminateProcess(process, 1);
        CloseHandle(process);
        return ok != FALSE;
    }

    bool closeWindows(int pid) {
        std::vector<HWND> windows = mainWindowsOf(pid);
        for (HWND window : windows) PostMessageW(window, WM_CLOSE, 0, 0);
        return !windows.empty();
    }

    bool focusWindow(int pid) {
        std::vector<HWND> windows = mainWindowsOf(pid);
        if (windows.empty()) return false;
        if (IsIconic(windows[0])) ShowWindow(windows[0], SW_RESTORE);
        SetForegroundWindow(windows[0]);
        FlashWindow(windows[0], TRUE);
        return true;
    }

    std::vector<std::string> commandLine() {
        std::vector<std::string> args;
        int count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) return args;
        for (int i = 0; i < count; ++i) args.push_back(toUtf8(arguments[i]));
        LocalFree(arguments);
        return args;
    }

    std::string executablePath() {
        std::wstring path(MAX_PATH, L'\0');
        while (true) {
            DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length < path.size()) {
                path.resize(length);
                return toUtf8(path);
            }
            path.resize(path.size() * 2);
        }
    }

    std::string currentDirectory() {
        DWORD length = GetCurrentDirectoryW(0, nullptr);
        std::wstring path(length, L'\0');
        path.resize(GetCurrentDirectoryW(length, path.data()));
        return toUtf8(path);
    }

    int currentPid() {
        return static_cast<int>(GetCurrentProcessId());
    }
#else
    static std::vector<char*> argvOf(const std::vector<std::string>& args) {
        std::vector<char*> argv;
        for (const std::string& argument : args) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        return argv;
    }

    static void applyEnvironment(const SpawnOptions& options) {
        for (const auto& [name, value] : options.env) setenv(name.c_str(), value.c_str(), 1);
    }

    RunResult run(const std::vector<std::string>& args, const SpawnOptions& options) {
        RunResult result;
        int out[2] = { -1, -1 }, err[2] = { -1, -1 };
        bool capture = options.stdio == Stdio::Default;
        if (capture && (pipe(out) != 0 || pipe(err) != 0)) {
            result.error = strerror(errno);
            return result;
        }

        pid_t child = fork();
        if (child < 0) {
            result.error = strerror(errno);
            return result;
        }
        if (child == 0) {
            if (options.stdio != Stdio::Inherit) {
                int devNull = ::open("/dev/null", O_RDWR);
                dup2(devNull, STDIN_FILENO);
                dup2(capture ? out[1] : devNull, STDOUT_FILENO);
                dup2(capture ? err[1] : devNull, STDERR_FILENO);
            }
            if (!options.cwd.empty() && chdir(options.cwd.c_str()) != 0) _exit(127);
            applyEnvironment(options);
            std::vector<char*> argv = argvOf(args);
            execvp(argv[0], argv.data());
            _exit(127);
        }

        result.started = true;
        if (capture) {
            close(out[1]);
            close(err[1]);
        }

        auto started = std::chrono::steady_clock::now();
        pollfd fds[2] = { { out[0], POLLIN, 0 }, { err[0], POLLIN, 0 } };
        int open = capture ? 2 : 0;
        char chunk[8192];
        while (open > 0) {
            int wait = 100;
            if (poll(fds, 2, wait) > 0) {
                for (int i = 0; i < 2; ++i) {
                    if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP))) continue;
                    ssize_t read = ::read(fds[i].fd, chunk, sizeof(chunk));
                    if (read > 0) {
                        (i == 0 ? result.stdoutText : result.stderrText).append(chunk, read);
                    } else {
                        close(fds[i].fd);
                        fds[i].fd = -1;
                        --open;
                    }
                }
            }
            if (options.timeout > 0 && std::chrono::steady_clock::now() - started > std::chrono::duration<double>(options.timeout)) {
                result.timedOut = true;
                ::kill(child, SIGKILL);
                break;
            }
        }
        for (pollfd& fd : fds) if (fd.fd >= 0) close(fd.fd);

        int status = 0;
        while (true) {
            pid_t done = waitpid(child, &status, options.timeout > 0 && !result.timedOut ? WNOHANG : 0);
            if (done == child) break;
            if (options.timeout > 0 && std::chrono::steady_clock::now() - started > std::chrono::duration<double>(options.timeout)) {
                result.timedOut = true;
                ::kill(child, SIGKILL);
                waitpid(child, &status, 0);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return result;
    }

    int spawn(const std::vector<std::string>& args, const SpawnOptions& options, std::string& error) {
        // A grandchild nobody waits on, so no zombie is left behind
        int ids[2];
        if (pipe(ids) != 0) {
            error = strerror(errno);
            return 0;
        }
        pid_t child = fork();
        if (child < 0) {
            error = strerror(errno);
            return 0;
        }
        if (child == 0) {
            setsid();
            pid_t grandchild = fork();
            if (grandchild == 0) {
                close(ids[0]);
                if (options.stdio != Stdio::Inherit) {
                    int devNull = ::open("/dev/null", O_RDWR);
                    dup2(devNull, STDIN_FILENO);
                    dup2(devNull, STDOUT_FILENO);
                    dup2(devNull, STDERR_FILENO);
                }
                if (!options.cwd.empty() && chdir(options.cwd.c_str()) != 0) _exit(127);
                applyEnvironment(options);
                std::vector<char*> argv = argvOf(args);
                execvp(argv[0], argv.data());
                _exit(127);
            }
            write(ids[1], &grandchild, sizeof(grandchild));
            _exit(0);
        }
        close(ids[1]);
        pid_t grandchild = 0;
        read(ids[0], &grandchild, sizeof(grandchild));
        close(ids[0]);
        waitpid(child, nullptr, 0);
        return static_cast<int>(grandchild);
    }

    bool open(const std::string& target, std::string& error) {
#if defined(__APPLE__)
        RunResult result = run({ "open", target }, {});
#else
        RunResult result = run({ "xdg-open", target }, {});
#endif
        if (result.started && result.exitCode == 0) return true;
        error = result.error.empty() ? result.stderrText : result.error;
        return false;
    }

    std::vector<std::string> shellCommand(const std::string& command) {
        const char* shell = getenv("SHELL");
        return { shell ? shell : "/bin/sh", "-c", command };
    }

    std::vector<ProcessInfo> list(const std::string& name) {
        std::vector<ProcessInfo> processes;
        RunResult result = run({ "ps", "-axo", "pid=,comm=" }, {});
        size_t start = 0;
        while (start < result.stdoutText.size()) {
            size_t end = result.stdoutText.find('\n', start);
            if (end == std::string::npos) end = result.stdoutText.size();
            std::string line = result.stdoutText.substr(start, end - start);
            start = end + 1;
            size_t first = line.find_first_not_of(' ');
            if (first == std::string::npos) continue;
            size_t space = line.find(' ', first);
            if (space == std::string::npos) continue;
            int pid = std::atoi(line.c_str() + first);
            std::string command = line.substr(line.find_first_not_of(' ', space));
            std::string base = command.substr(command.find_last_of('/') + 1);
            if (!name.empty() && base != name) continue;
            processes.push_back({ pid, base, false });
        }
        return processes;
    }

    bool kill(int pid) {
        return ::kill(pid, SIGKILL) == 0;
    }

    bool closeWindows(int pid) {
        return ::kill(pid, SIGTERM) == 0;
    }

    bool focusWindow(int pid) {
        (void)pid;
        return false;
    }

    std::vector<std::string> commandLine() {
        std::vector<std::string> args;
#if defined(__APPLE__)
        int count = *_NSGetArgc();
        char** argv = *_NSGetArgv();
        for (int i = 0; i < count; ++i) args.push_back(argv[i]);
#else
        std::ifstream file("/proc/self/cmdline", std::ios::binary);
        std::string argument;
        while (std::getline(file, argument, '\0')) args.push_back(argument);
#endif
        return args;
    }

    std::string executablePath() {
#if defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string path(size, '\0');
        _NSGetExecutablePath(path.data(), &size);
        path.resize(strlen(path.c_str()));
        return path;
#else
        char path[4096];
        ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
        return length > 0 ? std::string(path, length) : std::string();
#endif
    }

    std::string currentDirectory() {
        char path[4096];
        return getcwd(path, sizeof(path)) ? std::string(path) : std::string();
    }

    int currentPid() {
        return static_cast<int>(getpid());
    }
#endif
} // namespace Luwow::Process
