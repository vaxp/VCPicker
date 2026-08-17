project('vcpicker', 'c',
          version: '0.1.0',
    meson_version: '>= 0.62.0',
  default_options: [ 'warning_level=2', 'werror=false', 'c_std=gnu11', ],
)

i18n = import('i18n')

cc = meson.get_compiler('c')

dependency('gtk4', version: '>= 4.10.0')
dependency('glib-2.0')
dependency('gio-2.0')
dependency('gio-unix-2.0')
m_dep = cc.find_library('m', required : true)

vaxp_deps = [
  dependency('gtk4'),
  dependency('glib-2.0'),
  dependency('gio-2.0'),
  dependency('gio-unix-2.0'),
  m_dep
]

x11_dep = dependency('x11', required: false)
if x11_dep.found()
  vaxp_deps += x11_dep
  add_project_arguments('-DHAVE_X11', language: 'c')
endif

wl_client_dep = dependency('wayland-client', required: false)
wl_cursor_dep = dependency('wayland-cursor', required: false)
wayland_scanner = find_program('wayland-scanner', required: false)

if wl_client_dep.found() and wayland_scanner.found()
  vaxp_deps += wl_client_dep
  if wl_cursor_dep.found()
    vaxp_deps += wl_cursor_dep
  endif
  add_project_arguments('-DHAVE_WAYLAND', language: 'c')
  have_wayland = true
else
  have_wayland = false
endif

subdir('data')
subdir('src')

