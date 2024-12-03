esavsync_dep = cc.find_library('lip_sync', required : get_option('esavsync'))
esavsync_sources = [
  'gstesavsync.c'
]

gstesaudioencoder_sources = [
  'gstesaudioencoder.c'
]

gstesavsync = library('gstesavsync',
  esavsync_sources,
  c_args : gst_plugins_es_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gst_es_dep, gstaudio_dep, esavsync_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstesavsync, install_dir : plugins_pkgconfig_install_dir)

plugins += [gstesavsync]