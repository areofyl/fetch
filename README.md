# fetch

An animated 3D fetch tool for your terminal. Takes your distro's ASCII logo,
turns it into a spinning 3D object, and displays system info alongside it.

![demo](demo.gif)

Built on top of [gentoo.c](https://github.com/areofyl/gentoo.c) and
[fastfetch](https://github.com/fastfetch-cli/fastfetch).

## Build

```
make
```

## Run

```
./fetch
```

Any keypress stops the animation. Ctrl-C also works.

## Custom logo

Put ASCII art in `~/.config/fetch/logo.txt`. Add a header line to set colors:

```
# distro: gentoo
         -/oyddmdhs+:.
     -odNMMMMMMMMNNmhy+-`
...
```

Supported distro color schemes: gentoo, arch, ubuntu, debian, fedora,
fedora-asahi-remix, nixos, void, alpine, opensuse.

You can also pass `--logo <name>` / `-l <name>` to pull any logo from fastfetch:

```
./fetch -l arch
```

Without a config file or flag, it auto-detects your distro from `/etc/os-release`.

## Options

| Flag | Description |
|------|-------------|
| `-l`, `--logo <name>` | Use a logo from fastfetch by name |
| `--rotate-x` | Only rotate on the X axis |
| `--rotate-y` | Only rotate on the Y axis |
| `-s`, `--speed <float>` | Rotation speed multiplier (default 1.0) |
| `--no-info` | Hide system info, only show the logo |
| `--no-color` | Disable logo coloring |
| `--frames <n>` | Stop after n frames |
| `--infinite` | Never auto-stop (keypress or Ctrl-C to exit) |
| `--shading-chars <str>` | Custom ASCII shading ramp (default `".,-~:;=!*#$@"`) |

fastfetch is optional — without it, fetch shows just the spinning logo with no
system info. The `--logo` flag and auto-detection also need fastfetch; without it,
use `~/.config/fetch/logo.txt` or the built-in Gentoo logo.

## How it works

- Reads an ASCII logo (from config, fastfetch, or built-in)
- Each character's visual density maps to a height value, creating a 3D relief
- Surface normals are derived from the height field gradient
- Blinn-Phong shading with diffuse + specular highlights
- Every frame: rotate, project with perspective, z-buffer, and shade
- System info is captured from fastfetch and drawn alongside the animation

## Install

```
sudo make install
```

Installs to `/usr/local/bin` by default. Override with `PREFIX=~/.local make install`.
