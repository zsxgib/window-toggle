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
| `--clean` | Remove every window-toggle shortcut (slot, app binding, viewer) |
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

## App Bindings (v1.9+)

Bind a modifier shortcut to "launch this app + toggle this window":

```bash
# Bind Ctrl+F12 to launch nautilus, anchored to the first launched window
window-toggle --bind-app Ctrl+F12 nautilus org.gnome.Nautilus

# List bindings
window-toggle --show-app

# Remove a binding
window-toggle --unbind-app Ctrl+F12
```

Behavior on press:

- If the anchored window is alive — toggle (minimize / activate), same as a normal slot binding.
- If the anchored window is gone — `fork+execlp <cmd>`, poll for a window matching `WM_CLASS`, anchor the first match, and stop. The newly-launched window is visible by default, so it is not toggled.
- Multi-instance: opening a second instance of the same class does **not** drift the binding; the anchor stays on the originally launched window.

Storage: the same `/tmp/window-toggle-config.json`, in a section marked `### app_bindings ###`. The slot pipeline stops at this delimiter, so app bindings and slot bindings do not interfere. `--clean` now also removes `--bind-app` bindings and the auto-registered viewer bindings. The `bindings.json` file is kept on disk but its `### app_bindings ###` section is emptied; the slot section is removed as before.

## Changelog

### v1.9.5 (2026-07-11)
- feat: `--bind-app <Fx> <cmd> <wm_class>` now registers three dconf shortcuts — `Ctrl+Fx`, `Super+Fx`, and `Alt+Fx` — instead of just one. Any of the three modifiers toggles the same window, so the user no longer has to pick one and stick with it. Bindings with an explicit modifier (e.g. `Ctrl+Shift+F7`) still register as a single row, so existing scripts keep working.
- feat: `--show-app` and the viewer popup now collapse the three siblings of one app into a single row. Modifier set is shown as `Ctrl(S+A)+Fx` (Ctrl + Super + Alt), `Ctrl+S+Fx` (Ctrl + Super only), or `Ctrl+Fx` (one entry), depending on which modifiers are present. Viewer badge state for the merged row is `started > hidden > not-started`, so a row never says "all dead" while one of the three siblings is alive.
- fix: when launching a fresh app, the new anchor XID is also written back to the `Super+Fx` and `Alt+Fx` siblings (same cmd + wm_class), so all three modifiers always point at the same window. Previously each modifier carried its own anchor that could go stale independently, leading to one of the three stopping to work after the window was closed and reopened.

### v1.9.4 (2026-07-10)
- fix: pressing the hotkey now distinguishes "window is the topmost normal window" from "window is visible but buried under other windows". Previously both cases hit the minimize branch, so a hotkey press on a buried window did nothing the user could see (mutter only marks `_NET_WM_STATE_HIDDEN`, not z-order). Now the hotkey first asks whether the window is on top of normal windows; if it is, minimize (unchanged); if it is buried, raise it to the front instead. Affects F1/F3/F6 (slot) and Ctrl+F10/F11/F12 (app binding).
- fix: raising a window now sends `_NET_ACTIVE_WINDOW` with `source=2` (pager) instead of `source=1` (app). Mutter honors the pager source by raising + giving focus; for app source it only gives focus and leaves the window where it was.

### v1.9.3 (2026-07-06)
- fix: viewer popup now distinguishes "visible" from "minimized/hidden" from "gone". Previously both hidden and gone windows were rendered the same way, so pressing Ctrl+Fx to minimize a window looked like the key had no effect.
  - Three states: `started` (green dot, full opacity), `hidden` (gray dot, 40% opacity), `not-started` (orange dot, 25% opacity).
  - The popup polls every second while open, so closing/minimizing a window via Ctrl+Fx updates the popup in place — no need to close and reopen it.
- fix: `--clean` now wipes every `window-toggle*` shortcut: slot bindings, `--bind-app` bindings, and the auto-registered viewer bindings (Pause / Scroll_Lock / Print). Empty-slot residue in the dconf custom-keybindings array is also trimmed. The `bindings.json` file is kept; its `### app_bindings ###` section is left empty so a subsequent `--bind-app` is the only way to repopulate it.

### v1.9.1 (2026-07-03)
- fix: dconf action parameter order (`--key X --run-app` instead of `--run-app --key X`) so the dconf callback actually fires
- fix: app binding config now lives at `~/.config/window-toggle/bindings.json` (XDG) instead of `/tmp/window-toggle-config.json`, so bindings survive reboot
- chore: ensure parent dir is created on first write

### v1.9 (2026-07-03)
- New: `--bind-app <key> <cmd> <wm_class>` to bind a shortcut to "launch app + toggle window"
- New: `--unbind-app <key>`, `--show-app`, `--run-app` (dconf callback)
- Anchor semantics: first launched window is remembered, never drifts
- Config: app bindings stored in a `### app_bindings ###` section, isolated from slot data
- `--clean` wipes the app section as well; use `--unbind-app <Ctrl+Fx>` to remove a single app binding without touching everything else
- Silent XErrorHandler added around stale-anchor checks

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
