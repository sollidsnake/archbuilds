project('kde-appmenu-gtk-module', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 0.60',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=debugoptimized'],
)

gtk = dependency('gtk+-3.0', version: '>= 3.22')
deps = [
  gtk,
  dependency('dbusmenu-glib-0.4'),
  dependency('dbusmenu-gtk3-0.4'),
  dependency('wayland-client'),
  dependency('x11'),
  meson.get_compiler('c').find_library('dl', required: false),
]

wayland_scanner = find_program(dependency('wayland-scanner', native: true).get_variable('wayland_scanner'))
appmenu_xml = get_option('plasma_wayland_protocols_dir') / 'appmenu.xml'
protocol_sources = [
  custom_target('appmenu-client-protocol.h',
    input: appmenu_xml, output: 'appmenu-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target('appmenu-protocol.c',
    input: appmenu_xml, output: 'appmenu-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

shared_module('kde-appmenu-gtk-module',
  'kde-appmenu-gtk-module.c', protocol_sources,
  dependencies: deps,
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: get_option('libdir') / 'gtk-3.0' / 'modules',
)

# Loads the module into every GTK 3 app of the session (systemd user environment)
install_data('60-kde-appmenu-gtk-module.conf',
  install_dir: get_option('prefix') / 'lib' / 'environment.d')
