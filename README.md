# fetch

A donut.c-inspired fetch tool that spins your distro logo in 3D.

![demo](demo.gif)

Takes any ASCII/Unicode distro logo, turns each character into a point cloud
based on its visual density, and renders it as a rotating 3D relief with
Blinn-Phong shading. System info from [fastfetch](https://github.com/fastfetch-cli/fastfetch)
is shown alongside it.

Based on [gentoo.c](https://github.com/areofyl/gentoo.c).

## Build & run

```
make
./fetch
```

Press any key to stop — the keypress passes through to the shell, so it
works as a startup fetch. Ctrl-C works too.

## Logos

By default it auto-detects your distro and grabs the logo from fastfetch.
Works with any distro, including Unicode logos (NixOS, etc.).

You can also specify one directly:

```
./fetch -l arch
./fetch -l NixOS
```

Or drop a custom logo in `~/.config/fetch/logo.txt`:

```
# distro: gentoo
         -/oyddmdhs+:.
     -odNMMMMMMMMNNmhy+-`
...
```

The `# distro:` header sets the color scheme. Supported schemes: gentoo,
arch, ubuntu, debian, fedora, fedora-asahi-remix, nixos, void, alpine,
opensuse.

## Options

| Flag | Description |
|------|-------------|
| `-l`, `--logo <name>` | Use a logo from fastfetch by name |
| `--rotate-x` | Lock rotation to X axis only |
| `--rotate-y` | Lock rotation to Y axis only |
| `-s`, `--speed <float>` | Speed multiplier (default 1.0) |
| `--no-info` | Just the logo, no system info |
| `--no-color` | Disable coloring |
| `--frames <n>` | Stop after n frames |
| `--infinite` | Run forever |
| `--shading-chars <str>` | Custom shading ramp, supports UTF-8 (default `.,-~:;=!*#$@`) |
| `-h`, `--help` | Show help |

fastfetch is optional — without it you just get the spinning logo.

## How it works

Each character in the logo gets a weight based on its visual density — `M` is
heavy, `.` is light, `█` is full, `░` is thin. That weight becomes a height,
turning the flat logo into a 3D relief. Surface normals come from the height
gradient, and everything gets rotated + projected + shaded every frame with a
z-buffer. ~700 lines of C, no deps beyond libm.

## Install

```
sudo make install
```

`PREFIX=~/.local make install` if you don't want it system-wide.
