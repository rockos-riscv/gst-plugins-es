esaudiodecoder_dep = cc.find_library('es_codec', required : get_option('esaudiodecoder'))
esaudiodecoder_sources = [
  'gstesaudiodecoder.c'
]
gstesaudiodecoder = library('gstesaudiodecoder',
  esaudiodecoder_sources,
  #c_args : gst_plugins_base_args,
  c_args : gst_plugins_es_args,
  include_directories : [configinc],
  #dependencies : [glib_deps, audio_dep, gst_dep,gst_base_dep, esaudiodecoder_dep],
  dependencies : [gst_dep, gstbase_dep, gst_es_dep, gstaudio_dep, esaudiodecoder_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstesaudiodecoder, install_dir : plugins_pkgconfig_install_dir)

plugins += [gstesaudiodecoder]
