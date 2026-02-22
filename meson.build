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
    dependencies: [x11_dep, xkbcommon_dep, x11_lib],
    install: true,
    install_dir: join_paths(get_option('prefix'), 'bin')
)

# Test
test('basic test', window_toggle)
