project(
  'rawedit',
  'cpp',
  version: '0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.63',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
  ],
)

add_project_arguments('-Wno-unused-parameter', language: 'cpp')

qt6 = import('qt6')

# The core library needs no widgets; only the application links those.
qt_core_dep = dependency('qt6', modules: ['Core', 'Gui', 'OpenGL', 'Concurrent'])
qt_widgets_dep = dependency('qt6', modules: ['Widgets', 'OpenGLWidgets'])

libraw_dep = dependency('libraw', version: '>= 0.21')
lensfun_dep = dependency('lensfun', version: '>= 0.3.4')
lcms2_dep = dependency('lcms2')
tiff_dep = dependency('libtiff-4')
jpeg_dep = dependency('libjpeg')
png_dep = dependency('libpng')
exiv2_dep = dependency('exiv2')

inc = include_directories('src')

# ---------------------------------------------------------------- core library

core_sources = files(
  'src/core/EditParams.cpp',
  'src/core/ColourMath.cpp',
  'src/core/CurveModel.cpp',
  'src/core/History.cpp',
  'src/core/Geometry.cpp',
  'src/core/LensModel.cpp',
  'src/core/OutputProfiles.cpp',
  'src/io/RawLoader.cpp',
  'src/io/MetadataReader.cpp',
  'src/io/Exporter.cpp',
  'src/gpu/GLBackend.cpp',
  'src/gpu/ShaderProgram.cpp',
  'src/gpu/TexturePool.cpp',
)

# The GLSL sources are compiled into the binary as Qt resources (:/shaders/...).
shader_resources = qt6.compile_resources(
  name: 'shaders',
  sources: 'src/gpu/shaders/shaders.qrc',
)

core_lib = static_library(
  'rawedit_core',
  core_sources,
  shader_resources,
  include_directories: inc,
  dependencies: [
    qt_core_dep,
    libraw_dep,
    lensfun_dep,
    lcms2_dep,
    tiff_dep,
    jpeg_dep,
    png_dep,
    exiv2_dep,
  ],
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: inc,
  dependencies: [
    qt_core_dep,
    libraw_dep,
    lensfun_dep,
    lcms2_dep,
    tiff_dep,
    jpeg_dep,
    png_dep,
    exiv2_dep,
  ],
)

# ---------------------------------------------------------------- application

ui_sources = files(
  'src/main.cpp',
  'src/ui/EditorSession.cpp',
  'src/ui/MainWindow.cpp',
  'src/ui/CanvasWidget.cpp',
  'src/ui/ExportDialog.cpp',
  'src/ui/panels/PanelBase.cpp',
  'src/ui/panels/BasicPanel.cpp',
  'src/ui/panels/LensPanel.cpp',
  'src/ui/panels/GeometryPanel.cpp',
  'src/ui/panels/CurvePanel.cpp',
  'src/ui/panels/DetailPanel.cpp',
  'src/ui/widgets/SliderRow.cpp',
  'src/ui/widgets/CurveEditor.cpp',
  'src/ui/widgets/HistogramWidget.cpp',
  'src/ui/widgets/CollapsibleGroup.cpp',
)

# Every header declaring Q_OBJECT. Add new ones here; there is no AUTOMOC.
moc_headers = files(
  'src/ui/EditorSession.h',
  'src/ui/MainWindow.h',
  'src/ui/CanvasWidget.h',
  'src/ui/ExportDialog.h',
  'src/ui/panels/BasicPanel.h',
  'src/ui/panels/LensPanel.h',
  'src/ui/panels/GeometryPanel.h',
  'src/ui/panels/CurvePanel.h',
  'src/ui/panels/DetailPanel.h',
  'src/ui/widgets/SliderRow.h',
  'src/ui/widgets/CurveEditor.h',
  'src/ui/widgets/HistogramWidget.h',
  'src/ui/widgets/CollapsibleGroup.h',
)

moc_files = qt6.compile_moc(
  headers: moc_headers,
  include_directories: inc,
  dependencies: [qt_core_dep, qt_widgets_dep],
)

executable(
  'photoshop',
  ui_sources,
  moc_files,
  include_directories: inc,
  dependencies: [core_dep, qt_widgets_dep],
  install: true,
)

# Desktop integration: launcher entry, icon, raw MIME associations.
install_data('packaging/photoshop.desktop', install_dir: get_option('datadir') / 'applications')
install_data('packaging/photoshop.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
# Refresh the launcher caches after `meson install` (no-op if the tools are missing).
meson.add_install_script('sh', '-c',
  'command -v update-desktop-database >/dev/null && update-desktop-database -q "$MESON_INSTALL_DESTDIR_PREFIX/share/applications" || true; ' +
  'command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$MESON_INSTALL_DESTDIR_PREFIX/share/icons/hicolor" || true')

# ---------------------------------------------------------------------- tests

test_core_exe = executable('test_core', 'tests/test_core.cpp', dependencies: core_dep)
test('core', test_core_exe, timeout: 120)

# Needs a GL 4.3 context; Qt's offscreen platform provides one on Mesa.
test_gpu_exe = executable('test_gpu', 'tests/test_gpu.cpp', dependencies: core_dep)
test('gpu', test_gpu_exe, env: {'QT_QPA_PLATFORM': 'offscreen'}, timeout: 600)
