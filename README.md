# window-toggle

A keyboard shortcut utility for GNOME that lets you instantly show or hide any window with a single key press.

## What is this?

Ever wanted to hide a distracting window with one key press and bring it back just as quickly? **window-toggle** gives you a keyboard shortcut for each window, so you can:

- Hide a terminal or browser that's in your way
- Bring it back instantly with the same shortcut
- Works like "hide app" on macOS, but per-window

## Use Case

```
You have 3 terminals open on different workspaces.
You assign Ctrl+Alt+F1 to terminal 1, F2 to terminal 2, F3 to terminal 3.

Now you can:
- Press Ctrl+Alt+F1 → terminal 1 appears (was hidden)
- Press Ctrl+Alt+F1 → terminal 1 minimizes (was visible)
- Repeat anywhere in GNOME
```

## Installation

```bash
# Build and install to /usr/local/bin/
meson setup build
meson install -C build

# Now you can run from anywhere
window-toggle --configure
```

## Quick Start

### 1. Configure a window

```bash
./window-toggle --configure
```

The program will:
1. Ask you to press a key combination (e.g., `Ctrl+Alt+F1`)
2. Show a list of open windows - select one
3. Automatically register the shortcut in GNOME

### 2. Use it

Press `Ctrl+Alt+F1` anywhere in GNOME:
- **Window hidden?** → It appears on screen
- **Window visible?** → It minimizes

### 3. Check shortcuts

```bash
./window-toggle --show
```

## Commands

| Command | Description |
|---------|-------------|
| `--configure` | Add a new shortcut for a window |
| `--run` | Toggle window (used by GNOME shortcut) |
| `--show` | List all configured shortcuts |
| `--clean` | Remove all shortcuts |
| `--start` | Clean and start fresh |

## How It Works

- Uses X11 `_NET_WM_STATE_HIDDEN` to detect window state
- Sends `_NET_ACTIVE_WINDOW` to show, `XIconifyWindow` to hide
- Stores config in `/tmp/window-toggle-config.json`
- Registers shortcuts via GNOME's `dconf` settings

## Requirements

- GNOME Desktop (for keyboard shortcuts)
- libX11
- xkbcommon
- GCC
- meson, ninja (for building)

## Tested On

- Ubuntu 24.04 with GNOME

## License

MIT
