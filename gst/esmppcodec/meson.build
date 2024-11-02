plugin_c_args = ['-DHAVE_CONFIG_H']

esmppcodec_sources = [
  'gstesmppplugin.c',
  'gstesallocator.c',
  './venc/gstesvenc.c',
  './venc/gstesvenccfg.c',
  './venc/gstesh264enc.c',
  './venc/gstesh265enc.c',
  './venc/gstesjpegenc.c',
  './venc/gstesvenc_comm.c',
  './vdec/gstesdec.c',
  './vdec/gstesvideodec.c'
]

vencinc = include_directories('venc')
vdecinc = include_directories('vdec')
es_mpp_lib = cc.find_library('es_mpp')
es_mpp_dep = [es_mpp_lib]

gstesmppcodec = library('gstesmppcodec',
  esmppcodec_sources,
  c_args: plugin_c_args,
  include_directories : [configinc, vencinc, vdecinc],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep, es_mpp_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstesmppcodec, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstesmppcodec]
