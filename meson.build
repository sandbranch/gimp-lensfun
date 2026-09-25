project('gimp-lensfun', 'cpp',
  version: '0.3.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: ['cpp_std=c++17', 'warning_level=1', 'buildtype=debugoptimized'],
)

gimp_dep = dependency('gimp-3.0', version: '>= 3.0')
gimpui_dep = dependency('gimpui-3.0', version: '>= 3.0')
gegl_dep = dependency('gegl-0.4')

# GIMP 3 loads each plug-in from a folder of the same name as its binary.
plugindir = get_option('plugindir')
if plugindir == ''
  plugindir = gimp_dep.get_variable(pkgconfig: 'gimplibdir') / 'plug-ins'
endif
plugindir = plugindir / meson.project_name()

# Lensfun 0.3: the system library, or built from source and linked in,
# which makes the plug-in independent of the system (e.g. in Flatpak).
if get_option('bundled_lensfun').enabled()
  lensfun_dep = dependency('', required: false)
else
  lensfun_dep = dependency('lensfun', version: ['>= 0.3.0', '< 0.3.90'],
                           required: get_option('bundled_lensfun').disabled())
endif
bundle = not lensfun_dep.found()
if bundle
  cmake = import('cmake')
  opts = cmake.subproject_options()
  opts.add_cmake_defines({
    'CMAKE_POLICY_VERSION_MINIMUM': '3.5',
    'CMAKE_BUILD_TYPE': 'Release',
    'BUILD_STATIC': true,
    'CMAKE_POSITION_INDEPENDENT_CODE': true,
    'BUILD_TESTS': false,
    'BUILD_LENSTOOL': false,
    'BUILD_DOC': false,
    'INSTALL_PYTHON_MODULE': false,
    'INSTALL_HELPER_SCRIPTS': false,
  })
  opts.set_install(false)
  lensfun_sub = cmake.subproject('lensfun', options: opts)
  lensfun_dep = [lensfun_sub.dependency('lensfun'),
                 dependency('glib-2.0')]
endif

executable(meson.project_name(),
  'src/correct.cpp',
  'src/gimplensfun.cpp',
  'src/lensdb.cpp',
  'src/lensdb-exif.cpp',
  dependencies: [gimp_dep, gimpui_dep, gegl_dep, lensfun_dep],
  cpp_args: bundle ? ['-DCONF_LENSFUN_STATIC'] : [],
  install: true,
  install_dir: plugindir,
)

# The database goes next to the plug-in: the one of the given folder, or
# the one of the bundled lensfun. Newer updates by lensfun-update-data in
# the user's data folder are used when they are newer.
db_source = get_option('lensfun_db')
if db_source == '' and bundle
  db_source = meson.current_source_dir() / 'subprojects' / 'lensfun' / 'data' / 'db'
endif
if db_source != ''
  install_subdir(db_source, install_dir: plugindir / 'lensfun-db',
                 strip_directory: true)
endif

# lensfun:correct, the same correction as a GEGL operation. Its database
# goes next to (not into) the folder of GEGL operations, which GEGL scans
# for modules.
if not get_option('gegl_op').disabled()
  moduledir = get_option('moduledir')
  if moduledir == ''
    moduledir = gegl_dep.get_variable(pkgconfig: 'pluginsdir')
  endif
  op_db_dir = moduledir / '..' / 'lensfun-db'

  shared_module('lensfun-correct',
    'gegl/lensfun-correct.cc',
    'src/correct.cpp',
    'src/lensdb.cpp',
    name_prefix: '',
    include_directories: include_directories('src', 'gegl'),
    dependencies: [gegl_dep, lensfun_dep],
    cpp_args: [
      '-DGETTEXT_PACKAGE="gimp-lensfun"',
      '-DLENSFUN_OP_DB_DIR="' + op_db_dir + '"',
    ] + (bundle ? ['-DCONF_LENSFUN_STATIC'] : []),
    install: true,
    install_dir: moduledir,
  )

  if db_source != ''
    install_subdir(db_source, install_dir: op_db_dir, strip_directory: true)
  endif
endif
