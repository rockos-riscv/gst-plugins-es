foreach plugin : ['esmppcodec',
                  'esaudioencoder',
                  'esaudiodecoder'
                   ]
  if not get_option(plugin).disabled()
    subdir(plugin)
  endif
endforeach