# wayland-crosshair

A minimal crosshair overlay for Wayland compositors (Hyprland, Sway, etc.).

Displays a small green dot at the exact center of your screen as a fullscreen transparent overlay. Designed for gaming — it is completely invisible to the input system: no focus, no keyboard grabs, no mouse interception. Clicks and cursor movement pass straight through to the game underneath.

Optionally launches `wlsunset` alongside it for a gamma boost, killing it automatically on exit.

## Features

- Zero input interference — uses an empty Wayland input region so the overlay cannot be focused or clicked
- `wlr-layer-shell` overlay layer — renders above fullscreen games on Wayland
- True screen center — anchors to all four edges so the dot is always at exact pixel center regardless of resolution
- Optional gamma boost via `wlsunset` — starts and stops with the crosshair
- Negligible resource usage — static surface, redrawn once at startup

## Dependencies

- `gtk3`
- `gtk-layer-shell`
- `wlsunset` *(optional, for gamma boost)*

Install on Arch Linux:

```bash
sudo pacman -S gtk3 gtk-layer-shell
# optional
sudo pacman -S wlsunset
```

## Build

```bash
make
```

## Usage

```bash
# Crosshair only
./crosshair

# Crosshair + gamma boost (value is passed to wlsunset -g)
./crosshair 1.2   # subtle
./crosshair 1.3   # moderate
./crosshair 1.5   # strong
```

Kill it (and wlsunset) with:

```bash
pkill crosshair
```

## Why not a Python/Electron/etc. script?

Most simple overlay examples steal input focus because they don't set `keyboard_interactivity = NONE` on the layer shell surface, and don't set an empty `wl_input_region`. This tool sets both, making it safe to run during gameplay.

## License

MIT
