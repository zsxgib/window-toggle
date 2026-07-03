# Repository Guidelines

Contributor guide for **window-toggle**, a GNOME/X11 utility that toggles windows on per-key shortcuts.

## Project Structure & Module Organization

Flat C99 codebase built with Meson; the working tree root is the project root.

- `window-toggle.c` — CLI entry point, argument parsing, subcommand dispatch.
- `config.c` / `.h` — JSON config at `/tmp/window-toggle-config.json`; slot lookup/save.
- `window-manager.c` / `.h` — X11 window scan, state, show/hide via `_NET_ACTIVE_WINDOW` and `XIconifyWindow`.
- `daemon.c` / `.h` — persistent X-connection daemon (PID at `/tmp/window-toggle-daemon.pid`).
- `ipc.c` / `.h` — UNIX-socket protocol between CLI client and daemon.
- `app-binding.c` / `.h` — **App binding** module (v1.9+): registers "launch cmd + wm_class" pairs and anchors the first-launched XID. Persisted in the same config file under a `### app_bindings ###` section delimiter; the slot parser stops at this delimiter, so slot data and app bindings are isolated.
- `doc/` — design notes (`IMPLEMENT_DAEMON_MODE.md`, `GITHUB_WORKFLOW.md`, `GITHUB_STATUS.md`).
- `build/` — Meson/Ninja output; gitignored.
- `README.md` (English) and `README_zh.md` (Chinese) — keep both in sync on user-facing changes.

## Build, Test, and Development Commands

```bash
meson setup build            # configure once; re-run with --reconfigure if needed
meson compile -C build       # build the `window-toggle` binary
sudo meson install -C build  # install to ${prefix}/bin (default /usr/local/bin)
```

Smoke checks: `./build/window-toggle --version`, `--status`, `--show`, `--show-app`. Re-run `meson compile -C build` after edits; reinstall only when the system binary needs refreshing.

## Coding Style & Naming Conventions

- C, GNU99 (`-std=gnu99` in `meson.build`); implicit declarations are intentional — do not "fix" them.
- `warning_level=1`; keep the build warning-clean.
- 4-space indent, no tabs; brace style matches existing files.
- Functions: `snake_case` (e.g. `save_shortcut_mapping`). Types: `PascalCase` (e.g. `WindowState`, `IPCRequest`, `AppBinding`). Macros/constants: `UPPER_SNAKE_CASE` (e.g. `CONFIG_PATH`, `APP_BINDINGS_DELIM`).
- No external formatter or linter is configured; do not add one. Match the surrounding style.

## Testing Guidelines

No dedicated test suite. `meson.build` registers only a smoke `test('basic test', window_toggle)` confirming the binary exists. Validate by:

- Running `./build/window-toggle --version` after each build.
- Exercising new shortcuts end-to-end on GNOME (configure → trigger → hide/show → reconfigure).
- Verifying `--start`, `--status`, `--stop`, and a subsequent toggle when touching the daemon or IPC.
- For app bindings: register with `--bind-app <key> <cmd> <wm_class>`, verify anchor is updated on first launch and reused on subsequent toggles. `pkill <cmd>` to test relaunch semantics.

## Commit & Pull Request Guidelines

History follows Conventional Commits in English: `feat:`, `fix:`, `chore:`, `docs:`. PRs should:

- Use a Conventional Commits title.
- Describe the user-visible change and testing performed; reference any related issue.
- Include a screenshot or terminal transcript when CLI output or shortcut behavior changes.
- For `feat:` / `fix:` that change observable behavior: bump the version string in `window-toggle.c` and add a changelog entry to both `README.md` and `README_zh.md`.

## Architecture Overview

Two modes share the binary. **Direct mode** (`--run`, `--configure`, `--show`, …) opens a short-lived X11 connection. **Daemon mode** (`--start` / `--stop` / `--status`) keeps a persistent `Display *`; the CLI talks to it over a UNIX socket via `ipc.c` using the `IPCRequest` / `IPCResponse` framing. Use daemon mode when the host (e.g. Chrome) opens enough X connections to exhaust the default limit.

### App bindings (v1.9+; v1.9.1 moves storage to XDG)

A second binding type, kept strictly separate from the slot pipeline:

- **CLI subcommands**: `--bind-app <key> <cmd> <wm_class>` registers a dconf shortcut (action = `window-toggle --run-app --key <key>`) and writes one row into the `app_bindings` section. `--unbind-app <key>` removes both. `--show-app` lists them. `--run-app` is the dconf callback.
- **Storage**: same config file as slots. The `### app_bindings ###` delimiter marks the section start; `load_config` / `find_window_by_key` stop reading at the delimiter, so the slot pipeline is unaffected. `app_binding_add` / `app_binding_remove` / `app_binding_update_anchor` rewrite the file via a `<path>.app.tmp` atomic rename. `save_shortcut_mapping` (used by `--configure`) preserves the app section across its own rewrite by appending the post-delimiter bytes into its temp file before rename.
- **Runtime (`--run-app`)**: opens a direct X connection (does not go through the daemon — see rationale below). If the anchored XID is still alive, runs the existing `minimize_window` / `activate_window` toggle. If the anchor is dead or absent, `fork+execlp`s the cmd, polls `_NET_CLIENT_LIST` for a window whose `WM_CLASS` matches, anchors the first match (XID written back to the config), and exits without toggling — a freshly-launched window is visible by default, so toggling would be counter-intuitive.
- **Multi-instance semantics**: the anchor is set on first launch and never drifts. If the user opens a second instance of the same class manually, the binding still acts on the anchored window.
- **Why not daemon**: launching an application is a lifecycle event, not a toggle. Letting the daemon fork apps would expand its role into "application supervisor". `--run-app` opens a fresh X connection per press, which is acceptable for low-frequency key presses.

### `XErrorEvent` handling for stale anchors

`run_app_mode_with_path` and `show_app_mode_with_path` install a silent `XErrorHandler` (`silent_xerror_handler`) right after `XOpenDisplay`. This swallows `BadWindow` from `XGetWindowAttributes` against a dead anchor instead of letting Xlib dump a fatal error. The handler is local to those code paths and is reset to default at process exit.
