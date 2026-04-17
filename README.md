# fetch

A donut.c-inspired fetch tool that spins your distro logo in 3D with live-updating system info.

![demo](demo.gif)

Takes any ASCII/Unicode distro logo, turns each character into a point cloud
based on its visual density, and renders it as a rotating 3D relief with
Blinn-Phong shading. System info is gathered natively from `/proc`, `/sys`,
and GTK config — no external dependencies required.

Based on [gentoo.c](https://github.com/areofyl/gentoo.c).

## Build & run

```
make
./fetch
```

Press any key to stop — the keypress passes through to the shell, so it
works as a startup fetch. Ctrl-C works too.

## Install

```
sudo make install
```

`PREFIX=~/.local make install` if you don't want it system-wide.

### Nix Flake
Add this repo to your ```flake.nix```. The package is built using the unstable channel. You can overwrite this by setting ```inputs.nixpkgs.follows = "nixpkgs"``` (if your default is 25.11).

```nix
inputs = {
  ...
areofyl-fetch.url = "github:areofyl/fetch";
  ...
}
```
#### Home-manager
You also need to import the nix package in your ```home.nix```. Check ```nix/home-module.nix``` for the options. Most are the same but hyphens can not be used so camel-case has been used for those options instead.

```nix
{ pkgs, inputs, ... }: 

{
  import = [ inputs.areofyl-fetch.homeManagerModules.default ];
  
  programs.fetch = {
    enable = true;
    labelColor = "red";
    info = [];
    speed = 1.0;
    spin = "xy";
  };
  
}
```

#### Issues
* GPU will not print full name, just generic "AMD GPU".

## Logos

By default it auto-detects your distro and grabs the logo from fastfetch
(if installed) with its original per-character colors preserved. Works with
any of fastfetch's 500+ distro logos!

You can also specify one directly:

```
./fetch -l arch
./fetch -l NixOS
./fetch -l asahi
```

Or drop a custom logo in `~/.config/fetch/logo.txt`:

```
# distro: gentoo
         -/oyddmdhs+:.
     -odNMMMMMMMMNNmhy+-`
...
```

Without fastfetch, the built-in Gentoo logo is used.

## System info

All system info is gathered natively — no fastfetch or neofetch needed:

- **OS** — `/etc/os-release`
- **Host** — `/proc/device-tree/model` or `/sys/class/dmi/id/product_name`
- **Kernel** — `uname()`
- **Uptime** — `/proc/uptime`
- **Packages** — emerge, pacman, dpkg, rpm, xbps, apk
- **Shell** — `$SHELL` + version
- **Display** — `/sys/class/drm/card*/modes`
- **WM** — env vars + process detection
- **Theme/Icons/Font** — `~/.config/gtk-3.0/settings.ini`
- **CPU** — `/proc/cpuinfo` or device-tree (Apple Silicon)
- **GPU** — DRM device uevent
- **Memory/Swap** — `/proc/meminfo`
- **Disk** — `statvfs()`
- **Battery** — `/sys/class/power_supply` (energy_now/energy_full)
- **Local IP** — `ip addr`

Stats like memory, battery, and uptime update in real-time while the logo spins.

## Config

Create `~/.config/fetch/config` to customize:

```
# fields — list to show, in this order
# remove or comment out to hide
os
host
kernel
uptime
packages
shell
display
wm
theme
icons
font
terminal
cpu
gpu
memory
swap
disk
ip
battery
locale
colors

# appearance
# label_color=magenta   (red, green, yellow, blue, magenta, cyan, white)
# separator=─           (character for the title separator)
# shading=.,-~:;=!*#$@  (characters for 3D shading, supports UTF-8)

# 3d
# light=top-left        (top-left, top-right, top, left, right, front, bottom-left, bottom-right)
# spin=xy               (x, y, or xy)
# speed=1.0             (rotation speed)
# size=1.0              (logo scale, e.g. 2.0 for double size)
# height=36             (override render height in rows)
```

## Options

| Flag | Description |
|------|-------------|
| `-l`, `--logo <name>` | Use a logo from fastfetch by name |
| `--rotate-x` | Lock rotation to X axis only |
| `--rotate-y` | Lock rotation to Y axis only |
| `-s`, `--speed <float>` | Speed multiplier (default 1.0) |
| `--size <float>` | Scale the logo (e.g. 2.0 for double size) |
| `--height <n>` | Override render height in rows |
| `--no-info` | Just the logo, no system info |
| `--no-color` | Disable coloring |
| `--frames <n>` | Stop after n frames |
| `--infinite` | Run forever |
| `--shading-chars <str>` | Custom shading ramp, supports UTF-8 |
| `--ff-config [path]` | Use a fastfetch config for the info layout |
| `--no-ff-config` | Force-disable fastfetch config mode |
| `-h`, `--help` | Show help |

CLI flags override config file settings.

## Using a fastfetch config

If you already maintain a fastfetch config (or want to drop one of the many
community presets in), you can reuse it for the info panel without giving up
fetch's spinning 3D logo:

```
fetch --ff-config                          # auto-discover ~/.config/fastfetch/config.jsonc
fetch --ff-config ~/presets/neofetch.jsonc # explicit path
fetch --ff-config=/path/to/preset.jsonc    # inline form
```

Or wire it in fetch's own config (`~/.config/fetch/config`):

```
fastfetch_config=auto     # auto-discover
fastfetch_config=/abs/path/to/preset.jsonc
fastfetch_config=off      # default
```

CLI wins over config. Use `--no-ff-config` to force it off for a single run.

### What gets borrowed (and what doesn't)

From the `modules` array each entry contributes:

- `type` → which gatherer runs (and in what order)
- `key` → the label. Supports the `{icon}` token, which expands to the
  distro's Nerd Font glyph
- `keyColor` → label color, accepts both color names (`"yellow"`) and raw
  ANSI params (`"38;5;200"`)
- `format` → token string like `"{2} [{6}]"` for GPU or `"{1}"` for
  theme/icons/font. Per-module token semantics mirror fastfetch's where
  implemented (see below).
- `custom` modules: `format` is rendered verbatim as a line with
  `{$N}` (substituted from the top-level `display.constants[N-1]`) and
  `{#N}` (ANSI SGR escape, e.g. `{#1}` → bold, `{#31}` → red, `{#}` → reset).
  `outputColor` wraps the whole line in an ANSI color.

From the top-level `display` block:

- `separator` → the key/value separator (default `": "`)
- `constants` → strings referenced by `{$N}` in custom format strings

**Intentionally ignored (on purpose):**

- `logo`, `padding`, `$schema`, everything else under `display`, and all
  per-module tuning knobs beyond type/key/keyColor/format/outputColor.
  Fetch's 3D spinning logo always wins. Your waifu wallpaper stays in
  `~/.config/fastfetch/`, it does not hijack the animation.
- `format` tokens beyond what each gatherer exposes (see supported list).
- Modules without a native equivalent (`publicip`, `gamepad`,
  `terminalsize`, `camera`, etc.) are silently skipped.

### Supported modules

Layout: `title`, `separator`, `break`, `colors`, `custom`.

System: `os`, `host`, `kernel`, `uptime`, `packages`, `shell`, `locale`,
`terminal`, `terminalfont` (aliased to `font`).

Desktop: `de`, `lm`, `wm`, `theme`/`wmtheme`, `icons`, `font`.

Hardware: `cpu`, `gpu` (enumerated one line per card, vendor + name via
`lspci`), `display` (enumerated one line per connected connector),
`memory`, `swap`, `disk`, `battery`, `brightness`, `poweradapter`.

Network / audio / media: `localip`/`ip`, `wifi` (via `nmcli` or `iw`),
`sound` (via `wpctl` / `pactl`), `bluetooth` (via `bluetoothctl`), `player`
and `media` (via `playerctl` with a `busctl` fallback — MPRIS).

### Format tokens

Where token-based `format` strings are honoured, these are the currently
exposed tokens (match fastfetch's most common conventions):

- `gpu`: `{1}` vendor, `{2}` name, `{3}` driver, `{6}` type.
- `theme`, `icons`, `font`: `{1}` name, `{2}` backend (`GTK3`).

Everywhere else, `format` is ignored in v1. Fetch emits each field as its
default rendered string, and `key`/`keyColor` still apply.

## How it works

Each character in the logo gets a weight based on its visual density — `M` is
heavy, `.` is light, `█` is full, `░` is thin. That weight becomes a height,
turning the flat logo into a 3D relief. Surface normals come from the height
gradient, and everything gets rotated + projected + shaded every frame with a
z-buffer. Single file C, no deps beyond libm.
