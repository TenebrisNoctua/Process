-- This is the type system for the Process API as it exists in C++ code.

--[=[
	How a started program's standard streams are connected.
	"default" captures its output for `run`, and discards it for `spawn`.
	"inherit" shares this program's terminal. "none" discards everything.
]=]
export type Stdio = "default" | "inherit" | "none"

--[=[
	Options for starting a program.
	`cwd` is the folder it starts in, this program's by default.
	`env` holds variables set over this program's environment.
	`timeout`, in seconds, kills a program `run` waits on once it's over.
]=]
export type SpawnOptions = {
	cwd: string?,
	env: { [string]: string }?,
	stdio: Stdio?,
	timeout: number?
}

--[=[
	What a program that ran left behind. `ok` is whether it exited with 0 in time.
]=]
export type RunResult = {
	ok: boolean,
	exitCode: number,
	stdout: string,
	stderr: string,
	timedOut: boolean
}

--[=[
	A running process, returned by `process.list`.
]=]
export type ProcessInfo = {
	pid: number,
	name: string,
	hasWindow: boolean
}

local process = {}

--[=[
	Runs a program and waits for it to exit. The calling thread yields meanwhile, so other threads keep
	running. Raises when the program can't be started.

	@param args The program, found on PATH when it isn't a path, followed by its arguments.
	@param options See `SpawnOptions`.
]=]
function process.run(args: { string }, options: SpawnOptions?): RunResult
    return nil :: any
end

--[=[
	Runs a command through the system's shell (cmd on Windows, $SHELL or /bin/sh elsewhere) and waits for
	it, like `run`.

	@param command The command line, such as "git status && git log -1".
	@param options See `SpawnOptions`.
]=]
function process.system(command: string, options: SpawnOptions?): RunResult
    return nil :: any
end

--[=[
	Starts a program without waiting for it. It keeps running after this program exits.

	@param args The program followed by its arguments.
	@param options See `SpawnOptions`. `timeout` doesn't apply.
	@return The new process's id.
]=]
function process.spawn(args: { string }, options: SpawnOptions?): number
    return nil :: any
end

--[=[
	Opens a file, folder or URL with what the system opens it with, such as Explorer or the browser.
]=]
function process.open(target: string): ()
    return nil :: any
end

--[=[
	The running processes.

	@param name Only the ones with this executable name. The .exe is optional.
]=]
function process.list(name: string?): { ProcessInfo }
    return nil :: any
end

--[=[
	Ends a process at once.

	@return Whether it could be ended.
]=]
function process.kill(pid: number): boolean
    return nil :: any
end

--[=[
	Asks a process to close, as its windows' close buttons do, so it can save first.

	@return Whether it had a window to ask.
]=]
function process.closeWindows(pid: number): boolean
    return nil :: any
end

--[=[
	Brings a process's window to the front and flashes it. Windows only.

	@return Whether it had a window.
]=]
function process.focusWindow(pid: number): boolean
    return nil :: any
end

--[=[
	The arguments given to the binary, after the arguments that were first consumed by the binary.
]=]
function process.args(): { string }
    return nil :: any
end

--[=[
	Every argument the program was started with. The returned table is a copy of the `GlobalArgs` table.
]=]
function process.commandLine(): { string }
    return nil :: any
end

--[=[
	The running executable's full path, with forward slashes.
]=]
function process.executablePath(): string
    return nil :: any
end

--[=[
	The working directory's full path, with forward slashes. Relative paths are resolved from it.
]=]
function process.cwd(): string
    return nil :: any
end

--[=[
	This process's id.
]=]
function process.pid(): number
    return nil :: any
end

--[=[
	Ends the program at once with the exit code, 0 by default. Output is flushed first.
]=]
function process.exit(code: number?): never
    return nil :: any
end

return table.freeze(process)
