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

```bash
# Configure a shortcut for a window
./window-toggle --configure

# Toggle window visibility
./window-toggle --run

# Show current configuration
./window-toggle --show

# Remove all configurations
./window-toggle --clean
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
