eslipsync_dep = cc.find_library('lip_sync', required : get_option('eslipsync'))
eslipsync_sources = [
  'gsteslipsync.c'
]

gstesaudioencoder_sources = [
  'gstesaudioencoder.c'
]

gsteslipsync = library('gsteslipsync',
  eslipsync_sources,
  c_args : gst_plugins_es_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gst_es_dep, gstaudio_dep, eslipsync_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gsteslipsync, install_dir : plugins_pkgconfig_install_dir)

plugins += [gsteslipsync]