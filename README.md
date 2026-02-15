# window-toggle

A lightweight X11 utility for GNOME that allows you to toggle window visibility with a keyboard shortcut.

## Features

- **Quick Toggle**: Press a hotkey to show/hide any window
- **Interactive Configuration**: Easy setup with `--configure` mode
- **GNOME Integration**: Automatically registers custom keybindings

## Installation

```bash
# Build with meson
meson setup build
meson compile -C build

# Or build with gcc
gcc -Wall -O2 -o window-toggle window-toggle.c config.c window-manager.c -lX11 -lxkbcommon
```

## Usage

### Configure a Shortcut

```bash
./window-toggle --configure
```

This interactive mode will:
1. Prompt you to press a keyboard shortcut (e.g., `Ctrl+Alt+F1`)
2. Let you select a window from the list
3. Register the shortcut in GNOME automatically

### Toggle Window

After configuration, press your assigned shortcut (e.g., `Ctrl+Alt+F1`) to toggle the window:

- If hidden → activates and shows the window
- If visible → minimizes the window

Or run manually:
```bash
./window-toggle --run --key F1
```

### Other Commands

```bash
# Show current configuration
./window-toggle --show

# Remove all configurations
./window-toggle --clean

# Start fresh (clean conflicting shortcuts)
./window-toggle --start
```

## Keyboard Shortcuts

After running `--configure` and setting up a shortcut (e.g., `Ctrl+Alt+F1`):

1. The shortcut is automatically registered in GNOME's keyboard settings
2. Press `Ctrl+Alt+F1` anywhere to toggle the window:
   - **Hidden window** → Press shortcut → Window appears
   - **Visible window** → Press shortcut → Window minimizes

You can view all configured shortcuts with:
```bash
./window-toggle --show
```

## How It Works

- Hidden window → activates and raises it
- Visible window → minimizes it

## Dependencies

- libX11
- xkbcommon
- GCC

## License

MIT
