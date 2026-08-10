# Configuration

fetch is configured through `~/.config/fetch/config`. If the file doesn't exist, all fields are shown in the default order.

## Fields

List field names one per line to show them, in the order you want. Comment out or remove fields to hide them. If a config file exists, only the fields listed in it are shown.

```
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
cursor
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
```

All fields are optional. You can reorder them however you want.

## Appearance

```
label_color=magenta
```

Color of the labels (the "CPU:", "Memory:", etc. text). Accepts: `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, or an ANSI color number.

```
separator=-
```

Character used for the title separator line. Default is `-`.

```
box=1
```

Draw a Unicode box around the system info. `0` = off (default), `1` = on. Also accepts `yes`/`no`/`true`/`false`.

```
shading=.,-~:;=!*#$@
```

Custom shading ramp for the 3D rendering. Characters go from dimmest to brightest. Supports UTF-8. This implies ASCII shading mode.

```
shading_mode=ascii
```

Shading mode. `ascii` (default) uses the character ramp. `blocks` uses 2x2 quadrant blocks. `sextants` uses 2x3 sub-cell blocks for the sharpest edges (needs kitty, ghostty, foot, or wezterm). See [shading-modes.md](shading-modes.md).

## Logo colors

For logos without their own ANSI colors (custom logos, some distros), you can set two-tone coloring:

```
logo_outer=magenta
logo_inner=white
```

`logo_outer` is the extruded side color, `logo_inner` is the front/back face color. Accepts the same color names as `label_color`, or an ANSI number.

## 3D settings

```
light=top-left
```

Light direction. Options: `top-left` (default), `top-right`, `top`, `left`, `right`, `front`, `bottom-left`, `bottom-right`.

```
spin=xy
```

Which axes to rotate around. `x`, `y`, or `xy` (default).

```
speed=1.0
```

Rotation speed multiplier. Higher = faster.

```
size=1.0
```

Logo scale. `2.0` for double size, `0.5` for half. Range: 0.5 to 5.0.

```
depth=1.0
```

3D extrusion depth. Higher = chunkier relief. Range: 0.1 to 10.0. If not set, fetch auto-scales depth based on the logo's character variance so flat logos don't look paper-thin.

```
thickness=1
```

Logo extrusion layers. `1` renders a flat bas-relief that preserves the logo silhouette. Higher values extrude the logo into a thicker relief (up to 16). If unset, fetch defaults to standard 3D extrusion.


```
height=36
```

Override the render height in rows. Default is auto (matches the number of info lines).

## Extra disks

Show additional mount points beyond the root filesystem:

```
disk=/home
disk=/data
```

Up to 8 extra mount points. Each one adds a line to the info output.

## Notes

- Lines starting with `#` are comments
- You don't need to remove the hint text in parentheses after values (e.g. `label_color=white (red, green, ...)`). The parser strips those automatically, except for `shading=`, `separator=`, and `disk=` which accept freeform strings
- CLI flags override config file settings
- If no config file exists, everything uses defaults
