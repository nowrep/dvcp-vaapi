project(
  'dvcp-vaapi',
  'cpp',
  version: '0.3.0',
  license: 'MIT',
  meson_version: '>=0.58.0',
  default_options: [
    'prefer_static=true',
    'cpp_std=c++20',
    'warning_level=3',
  ],
)

add_project_arguments(
  [
    '-DRESOLVE_VAAPI_VERSION="@0@"'.format(meson.project_version()),
    '-Wno-unused',
  ],
  language: 'cpp',
)

libdrm = dependency('libdrm')
libva = dependency('libva')

ffmpeg = subproject(
  'ffmpeg',
  default_options: [
    'gpl=enabled',
    'default_library=static',
    'programs=disabled',
    'libdrm=enabled',
    'vaapi=enabled',
    'vaapi_drm=enabled',
    'h264_vaapi_encoder=enabled',
    'hevc_vaapi_encoder=enabled',
    'av1_vaapi_encoder=enabled',
  ]
)
libavcodec = ffmpeg.get_variable('libavcodec_dep')
libavutil = ffmpeg.get_variable('libavutil_dep')

srcs = files(
  'wrapper/host_api.cpp',
  'wrapper/plugin_api.cpp',
  'plugin.cpp',
  'vaapi_encoder.cpp',
)

shared_module(
  'vaapi_encoder',
  srcs,
  include_directories: ['include'],
  dependencies: [libdrm, libva, libavcodec, libavutil],
  name_prefix: '',
  name_suffix: 'dvcp',
  link_args: [
    '-Wl,-Bsymbolic',
    '-Wl,--no-undefined',
    '-Wl,--exclude-libs=ALL',
    '-static-libstdc++',
    '-static-libgcc',
  ]
)
