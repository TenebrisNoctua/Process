#include "Library.h"
#include "Spawn.h"
#include "OffThread.h"
#include "lualib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Luwow::Process {
    static Library* getModuleInstance(lua_State* L) {
        return static_cast<Library*>(lua_touserdata(L, lua_upvalueindex(1)));
    }

    ILuauModule* Library::initialize(ILuauHost* host) {
        Library* process = new Library();
        process->host = host;
        return process;
    }

    Luwow::Engine::RunMode Library::getRunMode() const {
        return LUWOW_MODULE_RUN_MODE;
    }

    static void pushStrings(lua_State* L, const std::vector<std::string>& strings) {
        lua_createtable(L, static_cast<int>(strings.size()), 0);
        for (size_t i = 0; i < strings.size(); ++i) {
            lua_pushlstring(L, strings[i].data(), strings[i].size());
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
    }

    // A program and its arguments
    static std::vector<std::string> checkArgs(lua_State* L, int index) {
        luaL_checktype(L, index, LUA_TTABLE);
        std::vector<std::string> args;
        int count = lua_objlen(L, index);
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, index, i);
            size_t length = 0;
            const char* argument = lua_tolstring(L, -1, &length);
            if (!argument) luaL_error(L, "argument %d must be a string", i);
            args.emplace_back(argument, length);
            lua_pop(L, 1);
        }
        if (args.empty()) luaL_argerror(L, index, "expected the program to run");
        return args;
    }

    // { cwd: string?, env: { [string]: string }?, stdio: "default" | "inherit" | "none"?, timeout: number? }
    static SpawnOptions checkOptions(lua_State* L, int index) {
        SpawnOptions options;
        if (lua_isnoneornil(L, index)) return options;
        luaL_checktype(L, index, LUA_TTABLE);

        lua_getfield(L, index, "cwd");
        if (!lua_isnil(L, -1)) options.cwd = luaL_checkstring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "env");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) != LUA_TSTRING || !lua_isstring(L, -1)) luaL_error(L, "env must map names to strings");
                options.env[lua_tostring(L, -2)] = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, index, "stdio");
        if (!lua_isnil(L, -1)) {
            std::string stdio = luaL_checkstring(L, -1);
            if (stdio == "inherit") options.stdio = Stdio::Inherit;
            else if (stdio == "none") options.stdio = Stdio::None;
            else if (stdio != "default") luaL_error(L, "stdio must be \"default\", \"inherit\" or \"none\"");
        }
        lua_pop(L, 1);

        lua_getfield(L, index, "timeout");
        if (!lua_isnil(L, -1)) options.timeout = luaL_checknumber(L, -1);
        lua_pop(L, 1);
        return options;
    }

    // Runs the program off the state and resumes with { ok, exitCode, stdout, stderr, timedOut }
    static int runAsync(lua_State* L, std::vector<std::string> args, SpawnOptions options) {
        return Luwow::Engine::runOffThread(L, getModuleInstance(L)->getHost(), [args, options]() {
            auto result = std::make_shared<RunResult>(run(args, options));
            Luwow::Engine::OffThreadResult offThread;
            if (!result->started) {
                offThread.error = "could not run " + args[0] + ": " + result->error;
                return offThread;
            }
            offThread.push = [result](lua_State* L) {
                lua_createtable(L, 0, 5);
                lua_pushboolean(L, result->exitCode == 0 && !result->timedOut);
                lua_setfield(L, -2, "ok");
                lua_pushinteger(L, result->exitCode);
                lua_setfield(L, -2, "exitCode");
                lua_pushlstring(L, result->stdoutText.data(), result->stdoutText.size());
                lua_setfield(L, -2, "stdout");
                lua_pushlstring(L, result->stderrText.data(), result->stderrText.size());
                lua_setfield(L, -2, "stderr");
                lua_pushboolean(L, result->timedOut);
                lua_setfield(L, -2, "timedOut");
                return 1;
            };
            return offThread;
        });
    }

    // process.run(args: { string }, options?) -> RunResult, the coroutine yields until the program exits
    static int process_run(lua_State* L) {
        std::vector<std::string> args = checkArgs(L, 1);
        return runAsync(L, std::move(args), checkOptions(L, 2));
    }

    // process.system(command: string, options?) -> RunResult, the command runs through the system's shell
    static int process_system(lua_State* L) {
        std::string command = luaL_checkstring(L, 1);
        return runAsync(L, shellCommand(command), checkOptions(L, 2));
    }

    // process.spawn(args: { string }, options?) -> number, starts the program without waiting for it. Its
    // output goes nowhere unless stdio is "inherit".
    static int process_spawn(lua_State* L) {
        std::vector<std::string> args = checkArgs(L, 1);
        SpawnOptions options = checkOptions(L, 2);
        std::string error;
        int pid = spawn(args, options, error);
        if (pid == 0) luaL_error(L, "could not start %s: %s", args[0].c_str(), error.c_str());
        lua_pushinteger(L, pid);
        return 1;
    }

    // process.open(target: string), a file, folder or URL, with what the system opens it with
    static int process_open(lua_State* L) {
        std::string target = luaL_checkstring(L, 1);
        std::string error;
        if (!open(target, error)) luaL_error(L, "could not open %s: %s", target.c_str(), error.c_str());
        return 0;
    }

    // process.list(name: string?) -> { { pid: number, name: string, hasWindow: boolean } }
    static int process_list(lua_State* L) {
        std::vector<ProcessInfo> processes = list(luaL_optstring(L, 1, ""));
        lua_createtable(L, static_cast<int>(processes.size()), 0);
        for (size_t i = 0; i < processes.size(); ++i) {
            lua_createtable(L, 0, 3);
            lua_pushinteger(L, processes[i].pid);
            lua_setfield(L, -2, "pid");
            lua_pushlstring(L, processes[i].name.data(), processes[i].name.size());
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, processes[i].hasWindow);
            lua_setfield(L, -2, "hasWindow");
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
        return 1;
    }

    // process.kill(pid: number) -> boolean
    static int process_kill(lua_State* L) {
        lua_pushboolean(L, kill(luaL_checkinteger(L, 1)));
        return 1;
    }

    // process.closeWindows(pid: number) -> boolean, asks the program to close, as its close button does
    static int process_closeWindows(lua_State* L) {
        lua_pushboolean(L, closeWindows(luaL_checkinteger(L, 1)));
        return 1;
    }

    // process.focusWindow(pid: number) -> boolean
    static int process_focusWindow(lua_State* L) {
        lua_pushboolean(L, focusWindow(luaL_checkinteger(L, 1)));
        return 1;
    }

    // process.args() -> { string }: the arguments after the ones the program takes itself
    static int process_args(lua_State* L) {
        // Where the program's own arguments end is the host's to say. 
        std::vector<std::string> all = commandLine();
        size_t first = static_cast<size_t>(std::max(1, getModuleInstance(L)->getHost()->getStartingArgsCount()));
        std::vector<std::string> args(all.begin() + std::min(first, all.size()), all.end());
        pushStrings(L, args);
        return 1;
    }

    // process.commandLine() -> { string }, pretty much copies the GlobalArgs global table, and returns it.
    static int process_commandLine(lua_State* L) {
        lua_getglobal(L, "GlobalArgs");
        int count = lua_istable(L, -1) ? lua_objlen(L, -1) : 0;
        lua_createtable(L, count, 0);
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, -2, i);
            lua_rawseti(L, -2, i);
        }
        lua_remove(L, -2);
        return 1;
    }

    static int process_executablePath(lua_State* L) {
        std::string path = executablePath();
        for (char& c : path) if (c == '\\') c = '/';
        lua_pushlstring(L, path.data(), path.size());
        return 1;
    }

    // process.cwd() -> string, the working directory, with forward slashes
    static int process_cwd(lua_State* L) {
        std::string path = currentDirectory();
        for (char& c : path) if (c == '\\') c = '/';
        lua_pushlstring(L, path.data(), path.size());
        return 1;
    }

    static int process_pid(lua_State* L) {
        lua_pushinteger(L, currentPid());
        return 1;
    }

    // process.exit(code: number?), ends the program at once
    static int process_exit(lua_State* L) {
        int code = luaL_optinteger(L, 1, 0);
        fflush(stdout);
        fflush(stderr);
        std::exit(code);
        return 0;
    }

    const char* Library::getModuleName() const {
        return "process";
    }

    const char* Library::getModuleAlias() const {
        return "Luwow";
    }

    static LuauExport exports[] = {
        { "run", process_run, Luwow::Engine::finishOffThread },
        { "system", process_system, Luwow::Engine::finishOffThread },
        { "spawn", process_spawn },
        { "open", process_open },
        { "list", process_list },
        { "kill", process_kill },
        { "closeWindows", process_closeWindows },
        { "focusWindow", process_focusWindow },
        { "args", process_args },
        { "commandLine", process_commandLine },
        { "executablePath", process_executablePath },
        { "cwd", process_cwd },
        { "pid", process_pid },
        { "exit", process_exit },
        { nullptr, nullptr }
    };

    const LuauExport* Library::getExports() const {
        return exports;
    }
} // namespace Luwow::Process

LUWOW_REGISTER_MODULE(Luwow::Process::Library)
