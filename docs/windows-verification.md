# Windows port verification

Verified locally on 2026-10-08. Source baseline:
[`7b19d22c2d8e4b7b2295cc625c9fdf87adcabbcd`](https://github.com/areofyl/fetch/commit/7b19d22c2d8e4b7b2295cc625c9fdf87adcabbcd).
The upstream ISC license and attribution are unchanged.

## Platform changes

| Existing operation | Windows replacement |
| --- | --- |
| `termios` input modes | Saved/restored Windows console modes |
| `poll`, `read`, mouse escape parsing | `PeekConsoleInputW` / `ReadConsoleInputW` keyboard and mouse records |
| `ioctl`, `SIGWINCH` | Visible console dimensions and resize events, with per-frame size detection |
| `SIGINT` | Console control handler; the render loop performs cleanup |
| Batched `write`, ANSI output | Batched `WriteFile`, UTF-8, virtual terminal output, alternate screen |
| `usleep` | `Sleep`; `GetTickCount64` schedules dynamic refresh |
| Shell-based Fastfetch `popen` | Direct `CreateProcessW`, quoted arguments, drained stdout pipe, bounded timeout |
| HOME-based config files | APPDATA and `_wfopen` for Unicode paths |
| POSIX system collectors | Fastfetch formatted static fields; native memory, commit, uptime, and extra disks |

Renderer data and drawing stay shared. POSIX headers and collectors are guarded
out of the Windows build. Numeric validation, CLI precedence, bounded info
boxing, removal of output NUL bytes, and minimal-logo gradients are shared fixes
covered by regression checks.

## Results

Environment: Windows 11 x86_64; LLVM/MinGW 20260908 with LLVM 23.1.1;
Fastfetch 2.69.0; PowerShell 7.6.5 and Windows PowerShell 5.1.
Linux checks used the available Ubuntu WSL environment.

| Check | Result |
| --- | --- |
| `build.ps1 -Test`, PowerShell 7 | Passed: warning-free C11 build, C unit tests, hidden-console tests, 31 process checks |
| `build.ps1 -Test`, PowerShell 5.1 | Passed: same build and test suite |
| Installed Fastfetch | Windows fields, Windows 11 logo, disk mountpoint labels verified |
| Fastfetch fixtures | Missing and nonzero-exit fallback, valid names with spaces, invalid names, Unicode/quoted arguments, 200 KiB pipe output, ten-second timeout passed |
| Configuration | Unicode APPDATA path, UTF-8 BOM, custom logo, field order, repeated GPU lines, custom values, and CLI precedence passed |
| Shading | ASCII, blocks, sextants, and a Unicode CLI shading character produced UTF-8 output |
| Minimal logos | One point, one row, and one column produce finite points and normals |
| Redirected output | One frame, no ANSI escapes or NUL bytes; `--infinite` exits after the snapshot |
| Hidden-console input | Native mouse press/drag/release, retained fling velocity, resize event, and keypress passthrough passed |
| Hidden-console exit | Bounded completion, keypress, Ctrl+C, input/output mode restoration, code-page restoration, original hidden cursor, and idempotent cleanup passed |
| Rendering refresh | No Fastfetch launch during a 26-frame animation; memory and uptime refresh in place |
| Clang static analyzer | Passed with no findings after the minimal-logo fix |
| Linux `make test` | Passed all four regression tests, including CLI validation, shading, config precedence, and minimal logos |
| Linux warnings | GCC `-O2 -Wall -Wextra` emitted 31 warnings; the untouched baseline also emitted 31 warnings of the same warning categories |
| `git diff --check` | Passed |

There is no separate lint/type-check configuration in the upstream project.
Windows warnings are treated as errors. Linux's existing warnings remain;
they include formatting/truncation diagnostics and unused parameters in
unchanged POSIX collectors.

## Remaining manual checks

The integration harness runs in its own hidden Windows console. It does not
visually verify a Windows Terminal tab. Before marking Windows support fully
verified, run the following in Windows Terminal under PowerShell 5.1 and 7:

```powershell
.\fetch.exe --logo "Windows 11" --frames 100
.\fetch.exe --shading-mode blocks --frames 100
.\fetch.exe --shading-mode sextants --frames 100
.\fetch.exe --infinite
```

Check the animated silhouette and colors, drag/release rotation, narrow/wide
resizing, font glyph coverage, and smooth dynamic refresh. Exit with a key and
Ctrl+C, then confirm normal interactive shell input and output. Ctrl+Break is
handled in code but was not separately exercised by the harness. An unsupported
terminal's VT-mode failure path was not reproduced on this machine.

A native macOS build was unavailable and remains unverified. No PowerShell
profile or PATH changes, package publishing, or remote Git writes were made.
