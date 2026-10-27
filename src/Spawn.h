#pragma once

#include <map>
#include <string>
#include <vector>

// Starting and finding processes, kept apart from Luau so the waiting can happen off the state
namespace Luwow::Process {
    enum class Stdio { Default, Inherit, None };

    struct SpawnOptions {
        std::string cwd;
        // Merged over the current environment
        std::map<std::string, std::string> env;
        Stdio stdio = Stdio::Default;
        // Seconds, 0 for none
        double timeout = 0;
    };

    struct RunResult {
        bool started = false;
        std::string error;
        int exitCode = -1;
        bool timedOut = false;
        std::string stdoutText;
        std::string stderrText;
    };

    struct ProcessInfo {
        int pid = 0;
        std::string name;
        bool hasWindow = false;
    };

    // Runs a program with its arguments and waits for it, capturing its output unless stdio says otherwise
    RunResult run(const std::vector<std::string>& args, const SpawnOptions& options);

    // Starts a program without waiting for it, returns its pid, or 0 with the error set
    int spawn(const std::vector<std::string>& args, const SpawnOptions& options, std::string& error);

    // Opens a file, folder or URL with what the system opens it with
    bool open(const std::string& target, std::string& error);

    // The system's shell, and the argument it runs a command with
    std::vector<std::string> shellCommand(const std::string& command);

    // The running processes, only the ones named name (with or without .exe) when it isn't empty
    std::vector<ProcessInfo> list(const std::string& name);

    bool kill(int pid);

    // Asks a process's windows to close, like Process.CloseMainWindow. Returns whether it had one.
    bool closeWindows(int pid);

    // Brings a process's window to the front and flashes it. Returns whether it had one.
    bool focusWindow(int pid);

    // The arguments the program was started with, as UTF-8
    std::vector<std::string> commandLine();

    // The running executable's full path
    std::string executablePath();

    // The current working directory's full path
    std::string currentDirectory();

    int currentPid();
} // namespace Luwow::Process
