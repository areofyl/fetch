# Custom logos

## Using a custom logo

Create `~/.config/fetch/logo.txt` with your ASCII/Unicode art. fetch will use it instead of detecting your distro.

On Windows, use `%APPDATA%\fetch\logo.txt`, saved as UTF-8. Paths containing
spaces and Unicode characters and files with a UTF-8 BOM are supported.

## Distro color scheme

Add `# distro: <name>` as the first line to use that distro's color scheme:

```
# distro: gentoo
         -/oyddmdhs+:.
     -odNMMMMMMMMNNmhy+-`
   -yNMMMMMMMMMMMNNNmmdhy+-
 `omMMMMMMMMMMMMNmdmmmmddhhy/`
 omMMMMMMMMMMMNhhyyyohmdddhhhdo`
.ydMMMMMMMMMMdhs++so/smdddhhhhdm+`
 oyhdmNMMMMMMMNdyooydMddddhhhhyhNd.
  :oyhhdNNMMMMMMMNNMMMdddhhhhhyymMh
    .:+sydNMMMMMNNMMMMdddhhhhhhmMmy
       /mMMMMMMNNNMMMdddhhhhhmMNhs:
    `oNMMMMMMMNNNMMMddddhhdmMNhs+`
  `sNMMMMMMMMNNNMMMdddddmNMmhs/.
 /NMMMMMMMMNNNNMMMdddmNMNdso:`
+MMMMMMMNNNNNMMMMdMNMNdso/-
yMMNNNNNNNMMMMMNNMmhs+/-`
/hMMNNNNNNNNMNdhs++/-`
`/ohdmmddhys+++/:.`
  `-//////:--.
```

Without this line, the logo uses the default two-tone colors (`logo_outer` and `logo_inner` from config).

## Tips for good logos

- Characters with more visual density (`@`, `M`, `#`) become taller in the 3D relief
- Characters with less density (`.`, `-`, ` `) become shorter or flat
- Varying density across the logo creates a more interesting 3D shape
- Very uniform logos (all the same character weight) will look flat. fetch auto-scales the depth to compensate but a mix of weights looks better
- The logo can be any size but keep in mind it needs to fit your terminal

## Using fastfetch logos

You can use any of fastfetch's 500+ logos with `--logo <name>` or `-l <name>`:

```
fetch -l arch
fetch -l nixos
fetch -l debian
```

Run `fastfetch --list-logos` to see all available logos. fastfetch is not a dependency but is needed for this feature.
