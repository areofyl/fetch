# Native Windows build

Windows 11, Windows Terminal, and PowerShell 5.1 or 7 are the supported target.
The renderer, shading ramps, point cloud, rotation, and config format are shared
with the Linux and macOS builds. PowerShell launches a native C executable.

## Prerequisites and build

Use the Windows x86_64 UCRT archive from the official
[LLVM/MinGW releases](https://github.com/mstorsjo/llvm-mingw/releases).
Unpack it into a directory of your choice; no system installation or PATH
change is needed. MSVC and LLVM built for the MSVC ABI are not supported by this
build script.

From the checkout:

```powershell
.\build.ps1 -Compiler C:\tools\llvm-mingw\bin\clang.exe
.\fetch.exe
.\fetch.exe --logo "Windows 11" --frames 100
.\fetch.exe --shading-mode blocks
```

If LLVM/MinGW's `clang.exe` is already on PATH, or its archive is unpacked under
`.tools\llvm-mingw-*`, `build.ps1` can find it automatically. The script builds
x86_64 with C11, optimization, and `-Wall -Wextra -Werror`. `-Output` chooses an
alternative output path. It does not download tools or change shell settings.

The verified compiler is LLVM/MinGW 20260908, LLVM 23.1.1:

- Archive: `llvm-mingw-20260908-ucrt-x86_64.zip`
- SHA-256: `1bcf74d06b724aeecaa6412ca85f5b26fb1da770e7cdcefa9263c9c5c3ad34b6`

Install [Fastfetch for Windows](https://github.com/fastfetch-cli/fastfetch) and
make `fastfetch.exe` available on PATH. Fastfetch 2.69.0 was verified. Without
Fastfetch, fetch prints a diagnostic and uses the built-in Gentoo logo with
native OS, host, uptime, memory, and commit information.

## Configuration and logos

Use these Windows locations:

| File | Purpose |
| --- | --- |
| `%APPDATA%\fetch\config` | Field order, custom fields, and appearance |
| `%APPDATA%\fetch\logo.txt` | Custom ASCII/Unicode logo |

Both files are UTF-8; a UTF-8 BOM is accepted. Spaces and Unicode characters
in APPDATA paths are supported. The program reads these files without creating
or changing them. CLI flags override config values.

For example, create a config using either PowerShell version:

```powershell
New-Item -ItemType Directory -Force "$env:APPDATA\fetch" | Out-Null
@'
os
host
kernel
uptime
---
cpu
gpu
memory
swap
disk
ip
speed=1.0
size=1.0
depth=1.0
shading_mode=ascii
'@ | Set-Content -Encoding utf8 "$env:APPDATA\fetch\config"
```

Named Fastfetch logos are checked against `fastfetch --list-logos`. Unknown
names produce a diagnostic and the built-in fallback. Fastfetch is launched
directly with quoted arguments, without a shell. Named logos use its `builtin`
logo type; use `logo.txt` for custom art. The default Windows logo name is
`Windows 11`; `--logo Windows` selects the older multicolor Windows logo.

See the existing [configuration](configuration.md),
[custom logo](custom-logos.md), and [shading](shading-modes.md) references.

## System information

Fastfetch collects formatted static fields once, before rendering starts.
A temporary JSON config gives each field a fixed key; fetch reads the formatted
text rather than parsing JSON output. Display names, disk mountpoints, and
network interface names are kept in their labels. Multiple displays, GPUs, and
disks retain multiple lines, subject to fetch's 32-line information limit.

| Config field | Windows behavior |
| --- | --- |
| `uptime` | `GetTickCount64`, refreshed every second |
| `memory` | Physical usage from `GlobalMemoryStatusEx`, refreshed every second |
| `swap` | Windows commit usage and limit, labeled `Commit`, refreshed every second |
| `disk` | Fastfetch volumes; extra `disk=C:\` entries use `GetDiskFreeSpaceExW` |
| `displaymanager` | Omitted; the Unix login manager field has no direct Windows counterpart |
| `powerprofile` | Omitted; the Linux firmware profile collector is not ported |
| Other fields | Fastfetch's Windows modules; unavailable values are omitted |

Static fields respect the configured order. Memory, commit, and uptime update
in place without invoking Fastfetch or launching any subprocess. Each Fastfetch
command is limited to ten seconds and 1 MiB of captured output. Failure falls
back to native fields and does not block rendering indefinitely.

## Terminal behavior

Windows console APIs provide mouse input and visible window dimensions.
Click and drag to rotate the logo; release to retain angular velocity.
Any key stops the animation. Its key-down record is left in the console input
queue for the shell, although shell handling of navigation and modifier keys
can vary. Quick Edit is disabled while fetch runs so selection does not pause it.

Fetch enables UTF-8 and virtual terminal output and uses the alternate screen
for animation. Normal completion, keypress exit, Ctrl+C, and Ctrl+Break return
to the previous screen and restore input/output modes, code pages, and the
original cursor visibility. The animation disappears when it exits. Forced
process termination is outside the normal cleanup path.

Resizing recomputes the layout; narrow windows stack or clip information.
ASCII is the default shading mode. Blocks and sextants are optional, and their
appearance depends on the terminal font's glyph coverage.

When stdout is redirected, fetch emits one UTF-8 frame with no ANSI escapes or
NUL bytes and exits, including with `--infinite`. It does not alter console
modes or code pages in that case. A console that cannot enable virtual terminal
output gets a clear error and restored modes.

Numeric options require complete finite values:

| Option | Accepted range |
| --- | --- |
| `--frames` | Integer 0 through 2147483647; 0 means unlimited in a console |
| `--height` | Integer 1 through 200 |
| `--size` | 0.5 through 5 |
| `--depth` | 0.1 through 10 |
| `--speed` | -1000 through 1000; negative reverses rotation, 0 freezes it |

Invalid CLI values exit with an error. Invalid numeric config values produce
a diagnostic and leave the previous/default value intact.

## Tests

```powershell
.\build.ps1 -Test -Compiler C:\tools\llvm-mingw\bin\clang.exe
```

The test run includes C unit tests, process tests with a fake Fastfetch,
an installed-Fastfetch smoke test when available, and isolated hidden-console
integration tests for mouse input, resizing, bounded completion, key passthrough,
Ctrl+C, and console restoration. It writes generated fixtures under
`tests\.work` and does not modify the user's APPDATA configuration.

Linux/macOS regression tests use `make test` and Python 3.
See [the local verification report](windows-verification.md) for results and
the remaining manual Windows Terminal checks.
