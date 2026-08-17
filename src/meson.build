vaxp_src = [
  'main.c',
  'theme_manager.c',
  'core/color.c',
  'core/palette.c',
  'ui/window.c',
  'ui/headerbar.c',
  'portal/picker.c',
  resources,
]

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

  vaxp_src += 'portal/wayland_picker.c'
endif

executable('vcpicker', vaxp_src,
  dependencies: vaxp_deps,
  install: true,
)
