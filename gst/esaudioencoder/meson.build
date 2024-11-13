esaudioencoder_dep = cc.find_library('es_codec', required : get_option('esaudioencoder'))

gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])

gstesaudioencoder_sources = [
  'gstesaudioencoder.c'
]
gstesaudioencoder = library('gstesaudioencoder',
  gstesaudioencoder_sources,
  #c_args : gst_plugins_base_args,
  c_args : gst_plugins_es_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gst_es_dep, gstpbutils_dep, esaudioencoder_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstesaudioencoder, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstesaudioencoder]




