# Meson build configuration for window-toggle

project('window-toggle', 'c',
    default_options: [
        'warning_level=1',
    ]
)

# Disable C99 strict mode to allow implicit declarations (like Makefile)
add_project_arguments('-std=gnu99', language: 'c')

# Get C compiler (needed for find_library)
c_compiler = meson.get_compiler('c')

# Dependencies
x11_dep = dependency('xcb')
xkbcommon_dep = dependency('xkbcommon')

# Find X11 library for linking
x11_lib = c_compiler.find_library('X11', required: true)

# Build executable
window_toggle = executable('window-toggle',
    'window-toggle.c',
    'config.c',
    'window-manager.c',
    'daemon.c',
    'ipc.c',
    'app-binding.c',
    dependencies: [x11_dep, xkbcommon_dep, x11_lib],
    install: true,
    install_dir: join_paths(get_option('prefix'), 'bin')
)

# Install the GTK4 viewer script alongside the binary.
# Runtime: PyGObject + GTK4 + libadwaita provided by the host distribution.
install_data(
    'window-toggle-viewer',
    install_dir: join_paths(get_option('prefix'), 'bin'),
    install_mode: 'rwxr-xr-x',
)

# Install the dconf registration helper under libexec so uninstall can find it.
install_data(
    'scripts/meson-viewer-install.sh',
    install_dir: join_paths(get_option('prefix'), 'libexec', 'window-toggle'),
    install_mode: 'rwxr-xr-x',
)
install_data(
    'scripts/meson-viewer-uninstall.sh',
    install_dir: join_paths(get_option('prefix'), 'libexec', 'window-toggle'),
    install_mode: 'rwxr-xr-x',
)

# Install the app icon (SVG source + raster fallbacks for old desktop
# environments that don't render SVG icons in file managers).
install_data(
    'data/icons/hicolor/scalable/apps/window-toggle.svg',
    install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps'),
)
install_data(
    'data/icons/hicolor/128x128/apps/window-toggle.png',
    install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', '128x128', 'apps'),
)
install_data(
    'data/icons/hicolor/48x48/apps/window-toggle.png',
    install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', '48x48', 'apps'),
)

# .desktop file so the tool shows up in the application launcher
# (icon name "window-toggle" resolves to the SVG/PNG above).
install_data(
    'data/applications/window-toggle.desktop',
    install_dir: join_paths(get_option('datadir'), 'applications'),
)

# Run the dconf registration script at install time. The script is
# skip_if_destdir: false so package builds (with DESTDIR) still get it
# executed against the real dconf database.
meson.add_install_script(
    'scripts/meson-viewer-install.sh',
)

# Test
test('basic test', window_toggle)
