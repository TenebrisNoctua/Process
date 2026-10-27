# Process
A library for starting and finding processes for Luwow-Project.

This repository is not stand-alone. You must build it externally from within another
repository like https://github.com/Luwow-Project/Release

## Project Overview

This project contains the sources for Luwow Process library. Programs are started directly, not through a shell, unless `process.system` is used. The library only supports the `SERIAL` run mode.

`process.run` and `process.system` wait for the program on a thread of their own while the calling coroutine yields. 

## Library Documentation

### Options

`run`, `system` and `spawn` take these options:

- `cwd: string?`, the folder the program starts in.
- `env: { [string]: string }?`, variables set over this program's environment.
- `stdio: "default" | "inherit" | "none"?`. `"default"` captures the output for `run` and discards it for `spawn`, `"inherit"` shares this program's terminal, `"none"` discards it.
- `timeout: number?`, seconds before a program `run` waits on is killed.

### `process.run(args: { string }, options?) -> { ok, exitCode, stdout, stderr, timedOut }`

Runs a program, found on `PATH` when it isn't a path, and waits for it. `ok` is whether it exited with 0 in time. Raises when it can't be started.

### `process.system(command: string, options?) -> { ok, exitCode, stdout, stderr, timedOut }`

Runs a command line through the system's shell, `cmd /d /s /c` on Windows and `$SHELL -c` (or `/bin/sh`) elsewhere.

### `process.spawn(args: { string }, options?) -> number`

Starts a program without waiting for it and returns its id. It keeps running after this program exits.

### `process.open(target: string)`

Opens a file, folder or URL with what the system opens it with (`ShellExecute`, `open`, or `xdg-open`).

### `process.list(name: string?) -> { { pid: number, name: string, hasWindow: boolean } }`

The running processes, only the ones with the executable name `name` when it's given (the `.exe` is optional on Windows). `hasWindow` is whether it has a visible top-level window, always false outside of Windows.

### `process.kill(pid: number) -> boolean`

Ends a process at once.

### `process.closeWindows(pid: number) -> boolean`

Asks a process to close as its windows' close buttons do. Returns whether it had a window.

### `process.focusWindow(pid: number) -> boolean`

Brings a process's window to the front and flashes it. Windows only.

### `process.args() -> { string }`

The arguments given to the binary, after the arguments that were first consumed by the binary.

### `process.commandLine() -> { string }`

Every argument the program was started with. The returned table is a copy of the `GlobalArgs` table.

### `process.executablePath() -> string`

### `process.cwd() -> string`

The working directory, which relative paths are resolved from.

### `process.pid() -> number`

### `process.exit(code: number?)`

Ends the program at once, after flushing the output.
