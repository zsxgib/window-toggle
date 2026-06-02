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

### Build and Install

```bash
# Setup build directory (only needed once)
meson setup build

# Compile the project
meson compile -C build

# Install to system (requires sudo)
sudo meson install -C build
```

After installation, run from anywhere:
```bash
window-toggle --configure
```

### Re-build after code changes

If you modify the source code and want to re-install:

```bash
# Recompile and install
meson compile -C build && sudo meson install -C build
```

Or in two steps:
```bash
meson compile -C build
sudo meson install -C build
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
| `--stop` | Stop the daemon |
| `--status` | Show whether the daemon is running |
| `--version` | Print version and release highlights |
| `--key KEY` | Specify which key was pressed (used by the daemon-callback command) |
| `--config PATH` | Use a non-default config file path |

## Supported Hotkey Modifiers

Starting with v1.8, the same Fx key (e.g. F1) can be bound to five different
modifier combinations, each toggling an independent window:

- **bare Fx** — e.g. `F1`
- **Ctrl+Fx** — e.g. `Ctrl+F1`
- **Ctrl+Alt+Fx** — e.g. `Ctrl+Alt+F1`
- **Ctrl+Shift+Fx** — e.g. `Ctrl+Shift+F1`
- **Super+Fx** — e.g. `Super+F1`

Run `window-toggle --configure` once per binding. The tool writes a separate
dconf slot for each one and the run-time `--key` argument carries the modifier
through to the lookup, so pressing `Ctrl+F1` toggles the Ctrl+F1 binding
while pressing plain `F1` toggles the bare-F1 binding.

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

## Changelog

### v1.8 (2026-06-02)
- Add `--version` flag with bilingual release notes
- See v1.7 for the bulk of the modifier-matrix work

### v1.7 (2026-06-02)
- **Five modifier combinations** can now coexist for the same Fx key
  (bare Fx, Ctrl+Fx, Ctrl+Alt+Fx, Ctrl+Shift+Fx, Super+Fx), each
  toggling an independent window
- Add persistent-X daemon mode (`--start` / `--stop` / `--status`) to
  avoid the X-connection-exhaustion issue when Chrome has many windows.
  See `doc/IMPLEMENT_DAEMON_MODE.md` for the design notes
- dconf command writes the full shortcut string (e.g. `--key Ctrl+F1`)
  so the run-time lookup can recover the modifier
- Fix dedup state-machine bug in `save_shortcut_mapping` that was
  corrupting the config file (the modifiers: line lost its trailing
  newline through `strcspn`)
- Increase `line[]` buffer from 512 to 4096 bytes in the config
  reader; the previous size truncated long `window_title` fields

### v1.6 (2026-05-03)
- Show `window_class` in `--show` output

### v1.4-v1.5 (2026)
- Daemon-mode plumbing and assorted fixes; see
  `doc/GITHUB_STATUS.md` for the historical commit list

### v1.3 (2026-02-22)
- Fix config file format corruption when saving shortcuts
- Fix newline preservation in config file
- Support multiple independent shortcuts (F1, F2, etc.)
- Support shortcut override (reconfigure same key to different window)
- Add slot_id to config file for better tracking

### v1.1 (2026-01-11)
- Add duplicate shortcut override support

### v1.0 (2026-01-08)
- Initial release show
- Basic/hide window toggle functionality

## License

MIT
