project('es-win2030-0.1', 'c', 'cpp',
  version : '1.24.4',
  meson_version : '>= 0.53.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debug' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()
gst_version_micro = version_arr[2].to_int()
 if version_arr.length() == 4
  gst_version_nano = version_arr[3].to_int()
else
  gst_version_nano = 0
endif

api_version = '1.0'

glib_req = '>= 2.44.0'
orc_req = '>= 0.4.17'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
host_system = host_machine.system()

if host_system in ['ios', 'darwin']
  have_objc = add_languages('objc', native: false)
  have_objcpp = add_languages('objcpp', native: false)
else
  have_objc = false
  have_objcpp = false
endif

cdata = configuration_data()

check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_FCNTL_H', 'fcntl.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_NETINET_IN_H', 'netinet/in.h'],
  ['HAVE_NETINET_IP_H', 'netinet/ip.h'],
  ['HAVE_NETINET_TCP_H', 'netinet/tcp.h'],
  ['HAVE_PTHREAD_H', 'pthread.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_PARAM_H', 'sys/param.h'],
  ['HAVE_SYS_SOCKET_H', 'sys/socket.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TIME_H', 'sys/time.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_SYS_UTSNAME_H', 'sys/utsname.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
  ['HAVE_MATH_H', 'math.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach



#add_project_arguments('-std=c99', language: 'c')
#using the built-in option for language standard version instead of using "-std=c99".

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wold-style-definition',
  '-Wredundant-decls',
  '-Wundef',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Wno-multichar',
  '-Waggregate-return',
  #'-Wdeclaration-after-statement',
  '-Wno-deprecated-declarations',
  '-Wvla',
  '-Werror',
  '-Wpointer-arith',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif

endforeach


# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  required : get_option('tests'),
  fallback : ['gstreamer', 'gst_check_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'allocators_dep'])
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'app_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])

gst_es_dep = []
plugins_install_dir = join_paths(get_option('prefix'), get_option('libdir') + '/gstreamer-1.0')
plugins_third_party_package_dir = join_paths(get_option('prefix'), 'lib')
fs = import('fs')

gst_plugins_es_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')

python3 = import('python').find_installation()

presetdir = join_paths(get_option('datadir'), 'gstreamer-' + api_version, 'presets')

pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

# extract_release_date = find_program('scripts/gst-env.py')
# run_result = run_command(extract_release_date)

plugins = []

subdir('gst')

configure_file(output: 'config.h', configuration: cdata)

if meson.version().version_compare('>= 0.54')
  plugin_names = []
  foreach plugin: plugins
    # FIXME: Use str.subtring() when we can depend on Meson 0.56
    split = plugin.name().split('gst')
    if split.length() == 2
      plugin_names += [split[1]]
    else
      warning('Need substring API in meson >= 0.56 to properly parse plugin name: ' + plugin.name())
      plugin_names += [plugin.name()]
    endif
  endforeach
  summary({'Plugins':plugin_names}, list_sep: ', ')
endif