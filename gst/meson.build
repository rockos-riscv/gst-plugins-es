foreach plugin : ['esmppcodec',
                  'esaudioencoder',
                  'esaudiodecoder',
                  'esavsync'
                   ]
  if not get_option(plugin).disabled()
    subdir(plugin)
  endif
endforeach