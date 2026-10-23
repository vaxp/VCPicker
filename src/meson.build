vaxp_src = files(
  'main.cpp',
  'ui/vcpicker_app.cpp',
  'portal/screen_picker.cpp',
)

if have_wayland
  wayland_scanner_c = generator(
    wayland_scanner,
    output: '@BASENAME@-protocol.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@']
  )
  wayland_scanner_h = generator(
    wayland_scanner,
    output: '@BASENAME@-client-protocol.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@']
  )

  wl_protocols = [
    '../protocols/xdg-shell.xml',
    '../protocols/wlr-screencopy-unstable-v1.xml',
    '../protocols/wlr-layer-shell-unstable-v1.xml',
  ]

  foreach p : wl_protocols
    vaxp_src += wayland_scanner_c.process(p)
    vaxp_src += wayland_scanner_h.process(p)
  endforeach
endif

vaxp_inc = [
  include_directories('.', 'core', 'portal', 'ui'),
]

executable('vcpicker', vaxp_src,
  include_directories: vaxp_inc,
  dependencies: vaxp_deps,
  cpp_args: ['-Wno-missing-field-initializers'],
  install: true,
)
