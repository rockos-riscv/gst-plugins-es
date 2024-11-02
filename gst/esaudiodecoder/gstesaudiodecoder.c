/* GStreamer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2023 Qiang Yang <yangqiang1@eswincomputing.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-esaudiodecoder
 * @title: esaudiodecoder
 * @seealso: faad
 *
 * esaudiodecoder decodes AAC (MPEG-4 part 3) stream.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 filesrc location=LC_MPEG_2.aac ! esaudiodecoder ! filesink location=xx.pcm
 * ]| Decodes LC MPEG_2 aac file and write to file.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include "gstesaudiodecoder.h"
#include "adp_amr.h"
#include "adp_itut_gxx.h"
#include "codec_api.h"

#define DEBUG_DUMP_FILE 1
#define DUMP_DECODER_FILE_NAME "/tmp/audio/decoder_dump.pcm"
#define AAC_CODEC_TYPE      "aac"
#define MP3_CODEC_TYPE      "mp3"
#define G711_CODEC_TYPE     "g711"
#define G722_CODEC_TYPE     "g722"
#define G726_CODEC_TYPE     "g726"
#define MP2L2_CODEC_TYPE    "mp2l2"
#define AMR_NB_CODEC_TYPE   "amrnb"
#define AMR_WB_CODEC_TYPE   "amrwb"
#define X_ALAW    "audio/x-alaw"
#define X_MLAW    "audio/x-mulaw"
#define DEFAULT_CODEC_TYPE      AAC_CODEC_TYPE
#define DEFAULT_CODEC_SUB_TYPE  X_ALAW
#define DEFAULT_DECODER_CHAN_NUM 1
#define MAX_DECODER_CHAN_NUM     32
#define MAX_SAMPLE_NUM_PER_FRAME  2048
#define G7XX_FRAME_LEN 160
#define MIN_BIT_RATE              8000
#define MAX_BIT_RATE              960000
#define DEFAULT_G722_BIT_RATE     64000
#define DEFAULT_G726_BIT_RATE     32000

enum
{
  PROP_0,
  PROP_CHANNEL,
  PROP_CODEC_TYPE,
  PROP_CODEC_SUB_TYPE,
  PROP_BIT_RATE,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC(esaudiodecoder_debug);
#define GST_CAT_DEFAULT esaudiodecoder_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/mpeg; audio/x-alaw; audio/x-mulaw; audio/AMR; audio/AMR-WB"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_esaudiodecoder_reset(GstEsaudiodecoder *esaudiodecoder);

static gboolean gst_esaudiodecoder_start(GstAudioDecoder *dec);
static gboolean gst_esaudiodecoder_stop(GstAudioDecoder *dec);
static GstFlowReturn gst_esaudiodecoder_parse(GstAudioDecoder *dec,GstAdapter *adapter,
                                              gint *offset, gint *length);
static GstFlowReturn gst_esaudiodecoder_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer);
static void gst_esaudiodecoder_flush(GstAudioDecoder *dec, gboolean hard);

static gboolean gst_esaudiodecoder_open_decoder(GstEsaudiodecoder *esaudiodecoder);
static void gst_esaudiodecoder_close_decoder(GstEsaudiodecoder *esaudiodecoder);
static void gst_esaudiodecoder_set_property (GObject * object, guint prop_id,
                                             const GValue * value, GParamSpec * pspec);
static void gst_esaudiodecoder_get_property (GObject * object, guint prop_id,
                                             GValue * value, GParamSpec * pspec);

#define gst_esaudiodecoder_parent_class parent_class
G_DEFINE_TYPE(GstEsaudiodecoder, gst_esaudiodecoder, GST_TYPE_AUDIO_DECODER);

static void
gst_esaudiodecoder_class_init(GstEsaudiodecoderClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS(klass);

  gst_element_class_add_static_pad_template(element_class, &src_template);
  gst_element_class_add_static_pad_template(element_class, &sink_template);

  gst_element_class_set_static_metadata(element_class, "ES audio decoder",
                                        "Codec/Decoder/Audio",
                                        "ES audio decoder",
                                        "http://eswin.com/");

  gobject_class->set_property = gst_esaudiodecoder_set_property;
  gobject_class->get_property = gst_esaudiodecoder_get_property;
  base_class->start = GST_DEBUG_FUNCPTR(gst_esaudiodecoder_start);
  base_class->stop = GST_DEBUG_FUNCPTR(gst_esaudiodecoder_stop);
  base_class->parse = GST_DEBUG_FUNCPTR(gst_esaudiodecoder_parse);
  base_class->handle_frame = GST_DEBUG_FUNCPTR(gst_esaudiodecoder_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR(gst_esaudiodecoder_flush);

  g_object_class_install_property(gobject_class, PROP_CHANNEL,
                                  g_param_spec_int("channel", "decoder channel number",
                                                   "decoder channel number", DEFAULT_DECODER_CHAN_NUM,
                                                   MAX_DECODER_CHAN_NUM, DEFAULT_DECODER_CHAN_NUM,
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CODEC_TYPE,
                                   g_param_spec_string("type", "codec type", "codec type", DEFAULT_CODEC_TYPE,
                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_CODEC_SUB_TYPE,
                                  g_param_spec_string("subtype", "codec sub type",
                                                      "codec sub type", DEFAULT_CODEC_SUB_TYPE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property(gobject_class, PROP_BIT_RATE,
                                  g_param_spec_int("bitrate", "codec bit rate",
                                                   "codec bit rate" , MIN_BIT_RATE,
                                                   MAX_BIT_RATE, DEFAULT_G722_BIT_RATE,
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  GST_DEBUG_CATEGORY_INIT(esaudiodecoder_debug, "esaudiodecoder", 0, "ES audio decoding");
}

static void
gst_esaudiodecoder_init(GstEsaudiodecoder *esaudiodecoder)
{
  esaudiodecoder->type = g_strdup (DEFAULT_CODEC_TYPE);
  esaudiodecoder->subtype = g_strdup(DEFAULT_CODEC_SUB_TYPE);
  esaudiodecoder->chn = DEFAULT_DECODER_CHAN_NUM;
  esaudiodecoder->bit_rate = 0;
  gst_audio_decoder_set_use_default_pad_acceptcaps(GST_AUDIO_DECODER_CAST(esaudiodecoder), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE(GST_AUDIO_DECODER_SINK_PAD(esaudiodecoder));
  gst_esaudiodecoder_reset(esaudiodecoder);
  gint32 ret = es_adec_init();
  if(ret != 0) {
    GST_ERROR_OBJECT (esaudiodecoder, "es_adec_init() failed,ret: %d", ret);
  }
}

static void
gst_esaudiodecoder_reset(GstEsaudiodecoder *esaudiodecoder)
{
  esaudiodecoder->samplerate = -1;
  esaudiodecoder->channels = -1;
  esaudiodecoder->init = FALSE;
  esaudiodecoder->offset = 0;
}

static gboolean
gst_esaudiodecoder_start(GstAudioDecoder *dec)
{
  GstEsaudiodecoder *esaudiodecoder = GST_ESAUDIODECODER(dec);

  GST_DEBUG_OBJECT(dec, "start");
  gst_esaudiodecoder_reset(esaudiodecoder);

  /* call upon legacy upstream byte support (e.g. seeking) */
  gst_audio_decoder_set_estimate_rate(dec, TRUE);
  /* never mind a few errors */
  gst_audio_decoder_set_max_errors(dec, 10);

  return TRUE;
}

static gboolean
gst_esaudiodecoder_stop(GstAudioDecoder *dec)
{
  GstEsaudiodecoder *esaudiodecoder = GST_ESAUDIODECODER(dec);

  GST_DEBUG_OBJECT(dec, "stop");
  gst_esaudiodecoder_reset(esaudiodecoder);
  gst_esaudiodecoder_close_decoder(esaudiodecoder);

  return TRUE;
}

static gboolean
gst_esaudiodecoder_update_caps(GstEsaudiodecoder *esaudiodecoder, guint rate, guint ch, guint bit_depth)
{
  gboolean ret;
  GstAudioInfo ainfo;
  GstAudioFormat format;

  /* store new negotiation information */
  esaudiodecoder->samplerate = rate;
  esaudiodecoder->channels = ch;
  esaudiodecoder->bit_depth = bit_depth;
  switch (bit_depth) {
  case 8:
    format = GST_AUDIO_FORMAT_S8;
    break;
  case 16:
    format = GST_AUDIO_FORMAT_S16;
    break;
  case 24:
    format = GST_AUDIO_FORMAT_S24;
    break;
  case 32:
    format = GST_AUDIO_FORMAT_S32;
    break;
  default:
    format = GST_AUDIO_FORMAT_S16;
    break;
  }

  /* FIXME: Use the GstAudioInfo of GstAudioDecoder for all of this */
  gst_audio_info_init(&ainfo);
  gst_audio_info_set_format(&ainfo, format, esaudiodecoder->samplerate,
                            esaudiodecoder->channels, NULL);

  ret = gst_audio_decoder_set_output_format(GST_AUDIO_DECODER(esaudiodecoder), &ainfo);
  GST_LOG_OBJECT(esaudiodecoder, "samplerate:%d, channels:%d, bit_depth:%d, ret:%d", esaudiodecoder->samplerate,
                 esaudiodecoder->channels, esaudiodecoder->bit_depth, ret);
  return ret;
}

/*
 * Find syncpoint in ADTS/ADIF stream. Doesn't work for raw,
 * packetized streams. Be careful when calling.
 * Returns FALSE on no-sync, fills offset/length if one/two
 * syncpoints are found, only returns TRUE when it finds two
 * subsequent syncpoints (similar to mp3 typefinding in
 * gst/typefind/) for ADTS because 12 bits isn't very reliable.
 */
static gboolean
gst_esaacddecoder_sync(GstEsaudiodecoder *esaudiodecoder, const guint8 *data, guint size, gboolean next,
                        gint *off, gint *length)
{
  guint n = 0;
  gint snc;
  gboolean ret = FALSE;
  guint len = 0;

  GST_LOG_OBJECT(esaudiodecoder, "Finding syncpoint");

  /* check for too small a buffer */
  if (size < 3)
    goto exit;

  for (n = 0; n < size - 3; n++)
  {
    snc = GST_READ_UINT16_BE(&data[n]);
    if ((snc & 0xfff6) == 0xfff0)
    {
      /* we have an ADTS syncpoint. Parse length and find
       * next syncpoint. */
      GST_LOG_OBJECT(esaudiodecoder,
                     "Found one ADTS syncpoint at offset 0x%x, tracing next...", n);

      if (size - n < 5)
      {
        GST_LOG_OBJECT(esaudiodecoder, "Not enough data to parse ADTS header");
        break;
      }

      len = ((data[n + 3] & 0x03) << 11) |
            (data[n + 4] << 3) | ((data[n + 5] & 0xe0) >> 5);
      if (n + len + 2 >= size)
      {
        GST_LOG_OBJECT(esaudiodecoder, "Frame size %d, next frame is not within reach",
                       len);
        if (next)
        {
          break;
        }
        else if (n + len <= size)
        {
          GST_LOG_OBJECT(esaudiodecoder, "but have complete frame and no next frame; "
                                         "accept ADTS syncpoint at offset 0x%x (framelen %u)",
                         n, len);
          ret = TRUE;
          break;
        }
      }

      snc = GST_READ_UINT16_BE(&data[n + len]);
      if ((snc & 0xfff6) == 0xfff0)
      {
        GST_LOG_OBJECT(esaudiodecoder,
                       "Found ADTS syncpoint at offset 0x%x (framelen %u)", n, len);
        ret = TRUE;
        break;
      }

      GST_LOG_OBJECT(esaudiodecoder, "No next frame found... (should be at 0x%x)",
                     n + len);
    }
    else if (!memcmp(&data[n], "ADIF", 4))
    {
      /* we have an ADIF syncpoint. 4 bytes is enough. */
      GST_LOG_OBJECT(esaudiodecoder, "Found ADIF syncpoint at offset 0x%x", n);
      ret = TRUE;
      break;
    }
  }
exit:
  *off = n;
  if (ret)
  {
    *length = len;
  }
  else
  {
    GST_LOG_OBJECT(esaudiodecoder, "Found no syncpoint");
  }

  return ret;
}


/*
 * related with mp3
 */
const guint bitrate_index[16][3] = {
    {0, 0, 0},
    {32, 32, 32},
    {64, 48, 40},
    {96, 56, 48},
    {128, 64, 56},
    {160, 80, 64},
    {192, 96, 80},
    {224, 112, 96},
    {256, 128, 112},
    {288, 160, 128},
    {320, 192, 160},
    {352, 224, 192},
    {384, 256, 224},
    {416, 320, 256},
    {448, 384, 320},
    {0, 0, 0}
};
const gfloat sampling_frequency[4] = {44.1, 48, 32, 0};
const guint layer[4] = {3, 2, 1, 0};
struct mp3_header {
    guint emphasis : 2;
    guint original : 1;
    guint copyright : 1;
    guint mode_extension : 2;
    guint mode : 2;
    guint private_bit : 1;
    guint padding_bit : 1;
    guint sampling_frequency : 2;
    guint bitrate_index : 4;
    guint protection_bit : 1;
    guint layer : 2;
    guint ID : 1;
    guint syncword : 12;
};
/*
 * Find syncpoint in mp3 stream. Doesn't work for raw,
 * packetized streams. Be careful when calling.
 * Returns FALSE on no-sync, fills offset/length if one/two
 * syncpoints are found, only returns TRUE when it finds two
 * subsequent syncpoints (similar to mp3 typefinding in
 * gst/typefind/) for ADTS because 12 bits isn't very reliable.
 */

static gboolean
gst_esmp3_sync (GstEsaudiodecoder *esaudiodecoder, const guint8 * data, guint size, gboolean next,
    gint * off, gint * length)
{
  guint n = 0;
  gint snc;
  gboolean ret = FALSE;
  guint len = 0;

  GST_LOG_OBJECT (esaudiodecoder, "Finding syncpoint");

  /* check for too small a buffer */
  if (size < 3)
    goto exit;

  for (n = 0; n < size - 3; n++) {
    snc = GST_READ_UINT16_BE (&data[n]);
    if ((snc & 0xfff0) == 0xfff0) {
      /* we have an mp3 syncpoint. Parse length and find
       * next syncpoint. */
      GST_LOG_OBJECT (esaudiodecoder,
          "Found one mp3 syncpoint at offset 0x%x, tracing next...", n);

      if (size - n < 4) {
        GST_LOG_OBJECT (esaudiodecoder, "Not enough data to parse mp3 header");
        break;
      }

      struct mp3_header header;
      guint8 *h = (guint8*)&header;
      h[0] = data[n+3];
      h[1] = data[n+2];
      h[2] = data[n+1];
      h[3] = data[n];
      gint lay = layer[header.layer];
      gint bitrate = bitrate_index[header.bitrate_index][lay];
      gfloat frequency = sampling_frequency[header.sampling_frequency];
      if (bitrate == 0 || frequency == 0) {
        n += 3;
        continue;
      }
      len = 144 * (bitrate / frequency) + header.padding_bit;
      GST_LOG_OBJECT (esaudiodecoder, "bitrate %d, frequency: %f, len: %d", bitrate, frequency, len);
      if (n + len + 4 >= size) {
        GST_LOG_OBJECT (esaudiodecoder, "Frame size %d, next frame is not within reach",
            len);
        if (next) {
          break;
        } else if (n + len <= size) {
          GST_LOG_OBJECT (esaudiodecoder, "but have complete frame and no next frame; "
              "accept ADTS syncpoint at offset 0x%x (framelen %u)", n, len);
          ret = TRUE;
          break;
        }
      }

      snc = GST_READ_UINT16_BE (&data[n + len]);
      if ((snc & 0xfff0) == 0xfff0) {
        GST_LOG_OBJECT (esaudiodecoder,
            "Found mp3 syncpoint at offset 0x%x (framelen %u)", n, len);
        ret = TRUE;
        break;
      }

      GST_LOG_OBJECT (esaudiodecoder, "No next frame found... (should be at 0x%x)",
          n + len);
    }
  }

exit:

  *off = n;

  if (ret) {
    *length = len;
  } else {
    GST_LOG_OBJECT (esaudiodecoder, "Found no syncpoint");
  }

  return ret;
}

static GstFlowReturn
gst_esaacdecoder_parse(GstAudioDecoder *dec, GstAdapter *adapter,
                         gint *offset, gint *length)
{
  GstEsaudiodecoder *esaudiodecoder;
  const guint8 *data;
  guint size;
  gboolean sync, eos;

  esaudiodecoder = GST_ESAUDIODECODER(dec);

  size = gst_adapter_available(adapter);
  g_return_val_if_fail(size > 0, GST_FLOW_ERROR);

  gst_audio_decoder_get_parse_state(dec, &sync, &eos);

  gboolean ret;
  data = gst_adapter_map(adapter, size);
  ret = gst_esaacddecoder_sync(esaudiodecoder, data, size, !eos, offset, length);
  gst_adapter_unmap(adapter);
  return (ret ? GST_FLOW_OK : GST_FLOW_EOS);
}

static GstFlowReturn
gst_esmp3decoder_parse(GstAudioDecoder *dec, GstAdapter *adapter,
                         gint *offset, gint *length)
{
  GstEsaudiodecoder *esaudiodecoder;
  const guint8 *data;
  guint size;
  gboolean sync, eos;

  esaudiodecoder = GST_ESAUDIODECODER(dec);

  size = gst_adapter_available(adapter);
  g_return_val_if_fail(size > 0, GST_FLOW_ERROR);

  gst_audio_decoder_get_parse_state(dec, &sync, &eos);

  gboolean ret;
  data = gst_adapter_map (adapter, size);
  ret = gst_esmp3_sync(esaudiodecoder, data, size, !eos, offset, length);
  gst_adapter_unmap (adapter);
  return (ret ? GST_FLOW_OK : GST_FLOW_EOS);
}

static gboolean
is_g711_type(GstAudioDecoder *dec)
{
  GstEsaudiodecoder *esaudiodecoder;
  esaudiodecoder = GST_ESAUDIODECODER(dec);
  const gchar *type = esaudiodecoder->type;
    if ((g_strcmp0(type, G711_CODEC_TYPE) == 0) || (g_strcmp0(type, G722_CODEC_TYPE) == 0)
        || (g_strcmp0(type, G726_CODEC_TYPE) == 0)) {
        return TRUE;
    }
    return FALSE;
}

static const int32_t nb_sizes[] = {12, 13, 15, 17, 19, 20, 26, 31, 5, 6, 5, 5, 0, 0, 0, 0};
static const int32_t wb_sizes[] = {17, 23, 32, 36, 40, 46, 50, 58, 60, 5, -1, -1, -1, -1, -1, 0};

static GstFlowReturn
gst_esrawdecoder_parse(GstAudioDecoder *dec, GstAdapter *adapter,
                         gint *offset, gint *length)
{
  guint size;
  gboolean sync, eos;

  size = gst_adapter_available(adapter);
  g_return_val_if_fail(size > 0, GST_FLOW_ERROR);

  gst_audio_decoder_get_parse_state(dec, &sync, &eos);

  *offset = 0;
  if(is_g711_type(dec) && (size > G7XX_FRAME_LEN)) {
    *length = G7XX_FRAME_LEN;
  } else {
    *length = size;
  }
  return GST_FLOW_OK;

}

static GstFlowReturn
gst_esamrdecoder_parse(GstAudioDecoder *dec, GstAdapter *adapter,
                         gint *offset, gint *length)
{
  GstEsaudiodecoder *esaudiodecoder;
  guint8 header[1];
  guint size;
  gboolean sync, eos;
  gint block, mode;

  esaudiodecoder = GST_ESAUDIODECODER(dec);

  gboolean is_sb = !!(g_strcmp0(esaudiodecoder->type, AMR_WB_CODEC_TYPE) == 0);

  size = gst_adapter_available (adapter);
  if (size < 1) {
    return GST_FLOW_ERROR;
  }

  gst_audio_decoder_get_parse_state (dec, &sync, &eos);

  /* need to peek data to get the size */
  gst_adapter_copy (adapter, header, 0, 1);
  mode = (header[0] >> 3) & 0x0F;
  if (is_sb) {
    block = wb_sizes[mode] + 1 ;
  } else {
    block = nb_sizes[mode] + 1;
  }

  GST_DEBUG_OBJECT (esaudiodecoder, "mode %d, block %d", mode, block);

  if (block) {
    if (block > size)
      return GST_FLOW_EOS;
    *offset = 0;
    *length = block;
  } else {
    /* no frame yet, skip one byte */
    GST_LOG_OBJECT (esaudiodecoder, "skipping byte");
    *offset = 1;
    return GST_FLOW_EOS;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_esaudiodecoder_parse(GstAudioDecoder *dec, GstAdapter *adapter,
                         gint *offset, gint *length)
{
  GstEsaudiodecoder *esaudiodecoder;

  esaudiodecoder = GST_ESAUDIODECODER(dec);

  if (!g_strcmp0 (esaudiodecoder->type, AAC_CODEC_TYPE)) {
    return gst_esaacdecoder_parse(dec, adapter, offset, length);
  } else if ((!g_strcmp0 (esaudiodecoder->type, MP3_CODEC_TYPE)) ||
             (!g_strcmp0 (esaudiodecoder->type, MP2L2_CODEC_TYPE))) {
    return gst_esmp3decoder_parse(dec, adapter, offset, length);
  } else if ((!g_strcmp0 (esaudiodecoder->type, AMR_NB_CODEC_TYPE)) ||
             (!g_strcmp0 (esaudiodecoder->type, AMR_WB_CODEC_TYPE))) {
    return gst_esamrdecoder_parse(dec, adapter, offset, length);
  } else {
    return gst_esrawdecoder_parse(dec, adapter, offset, length);
  }
}

static void
gst_esaudiodecoder_dump_data(const char *path, guint8 *buf, gsize bytes)
{
    if (!path) {
      return;
    }
    FILE *fp = fopen(path, "a+");
    if (fp) {
      fwrite(buf, 1, bytes, fp);
      fclose(fp);
    }
    return;
}

static GstFlowReturn
gst_esaudiodecoder_handle_frame(GstAudioDecoder *dec, GstBuffer *buffer)
{
  GstEsaudiodecoder *esaudiodecoder;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  gsize input_size;
  guchar *input_data;
  GstBuffer *out_buf = NULL;
  GstMapInfo omap;
  guint out_size;
  gint result;

  esaudiodecoder = GST_ESAUDIODECODER(dec);

  /* no fancy draining */
  if (G_UNLIKELY(!buffer)){
    return GST_FLOW_OK;
  }

  gst_buffer_map(buffer, &map, GST_MAP_READ);

if ((!g_strcmp0 (esaudiodecoder->type, MP3_CODEC_TYPE)) ||
    (!g_strcmp0 (esaudiodecoder->type, MP2L2_CODEC_TYPE))) {
    memcpy(esaudiodecoder->stream + esaudiodecoder->offset, map.data, map.size);
    input_size = esaudiodecoder->offset;
    input_data = esaudiodecoder->stream;
    esaudiodecoder->offset = map.size;
  } else {
    input_data = map.data;
    input_size = map.size;
  }

  if (input_size == 0) {
    return GST_FLOW_OK;
  }

  /* init if not already done during capsnego */
  if (!esaudiodecoder->init){
    es_frame_info info;
    if (!gst_esaudiodecoder_open_decoder(esaudiodecoder)){
      GST_ERROR_OBJECT(esaudiodecoder, "Failed to open decoder");
      goto failed_unmap;
    }
    result = es_adec_parse_packets(esaudiodecoder->chn, input_data, input_size, &info);
    if (result < 0) {
      GST_ERROR_OBJECT(esaudiodecoder, "es_adec_parse_packets failed,input_size:%lu, result:%d", input_size, result);
      goto failed;
    }
    GST_DEBUG_OBJECT(esaudiodecoder, "es_adec_parse_packets ok: rate=%u,ch=%d, bit_depth=%d",
                     info.sample_rate,  info.channels, info.bit_depth);
    if (!gst_esaudiodecoder_update_caps (esaudiodecoder, info.sample_rate, info.channels, info.bit_depth)){
          GST_ERROR_OBJECT(esaudiodecoder, "Failed to update caps");
          goto failed;
    }
    esaudiodecoder->init = TRUE;
  }

  /* FIXME, add bufferpool and allocator support to the base class */
  out_buf = gst_buffer_new_and_alloc (MAX_SAMPLE_NUM_PER_FRAME * esaudiodecoder->channels *
                                      esaudiodecoder->bit_depth / 8);
  gst_buffer_map (out_buf, &omap, GST_MAP_READWRITE);
  out_size = omap.size;

  result = es_adec_decode_stream(esaudiodecoder->chn, input_data, input_size, omap.data, &out_size);

  if ((!g_strcmp0 (esaudiodecoder->type, MP3_CODEC_TYPE)) ||
    (!g_strcmp0 (esaudiodecoder->type, MP2L2_CODEC_TYPE))) {
    memmove(esaudiodecoder->stream, esaudiodecoder->stream + input_size, map.size);
  }

  /* unmap input buffer */
  gst_buffer_unmap(buffer, &map);
  buffer = NULL;

  if (0 != result){
    GST_ERROR_OBJECT(esaudiodecoder, "es_adec_decode_stream result:%d", result);
    /* give up on frame and bail out */
    gst_audio_decoder_finish_frame(dec, NULL, 1);
    goto failed;
  }
  GST_LOG_OBJECT (esaudiodecoder, "input_size:%lu, out_size:%u", input_size, out_size);

  if (out_size == 0) {
    GST_WARNING_OBJECT(esaudiodecoder, "out_size:%u", out_size);
    gst_buffer_unmap(out_buf, &omap);
    return ret;
  }
  if (DEBUG_DUMP_FILE) {
    gst_esaudiodecoder_dump_data(DUMP_DECODER_FILE_NAME,omap.data, out_size);
  }
  gst_buffer_unmap(out_buf, &omap);
  gst_buffer_resize (out_buf, 0, out_size);
  ret = gst_audio_decoder_finish_frame(dec, out_buf, 1);
  GST_DEBUG_OBJECT(esaudiodecoder, "gst_audio_decoder_finish_frame ret:%d", ret);

  return ret;

failed:
  es_adec_destroy(esaudiodecoder->chn);
failed_unmap:
  if (buffer) {
    gst_buffer_unmap(buffer, &map);
  }
  if (out_buf) {
    gst_buffer_unmap (out_buf, &omap);
    gst_buffer_unref (out_buf);
  }
  return GST_FLOW_ERROR;
}

static void
gst_esaudiodecoder_flush(GstAudioDecoder *dec, gboolean hard)
{
  GST_DEBUG_OBJECT(dec, "%s: entry", __func__);
}

static es_codec_type
convert_codec_type(const gchar *type)
{
    if (g_strcmp0(type, AAC_CODEC_TYPE) == 0) {
        return AAC;
    } else if ((g_strcmp0(type, MP3_CODEC_TYPE) == 0) || (g_strcmp0(type, MP2L2_CODEC_TYPE) == 0)) {
        return MP3;
    } else if (g_strcmp0(type, G711_CODEC_TYPE) == 0) {
        return G711;
    } else if (g_strcmp0(type, G722_CODEC_TYPE) == 0) {
        return G722;
    } else if (g_strcmp0(type, G726_CODEC_TYPE) == 0) {
        return G726;
    } else if ((g_strcmp0(type, AMR_NB_CODEC_TYPE) == 0) || (g_strcmp0(type, AMR_WB_CODEC_TYPE) == 0)) {
        return AMR;
    } else {
        return UNKNOW;
    }
}
static gboolean
hw_open_decoder(GstEsaudiodecoder *esaudiodecoder)
{
  GST_DEBUG_OBJECT(esaudiodecoder, "%s: chn:%d, type:%s", __func__,
                    esaudiodecoder->chn, esaudiodecoder->type);

  void *attr = NULL;
  audio_amr_decoder_attr amr_attr;
  audio_g711_attr g711_attr;
  audio_g722_attr g722_attr;
  audio_g726_attr g726_attr;
  memset(&amr_attr, 0, sizeof(amr_attr));
  memset(&g711_attr, 0, sizeof(g711_attr));
  memset(&g722_attr, 0, sizeof(g722_attr));
  memset(&g726_attr, 0, sizeof(g726_attr));

  es_codec_type type = convert_codec_type(esaudiodecoder->type);

  if (type == AMR) {
    if (g_strcmp0(esaudiodecoder->type, AMR_NB_CODEC_TYPE) == 0) {
      amr_attr.is_wb = 0;
    } else {
      amr_attr.is_wb = 1;
    }
    attr = &amr_attr;
  } else if (type == G711) {
    if (g_strcmp0(esaudiodecoder->subtype, X_ALAW) == 0) {
      g711_attr.type = ALAW;
    } else if (!g_strcmp0 (esaudiodecoder->subtype, X_MLAW)) {
      g711_attr.type = ULAW;
    } else{
      g711_attr.type = ALAW;
    }
    attr = &g711_attr;
  } else if (type == G722) {
    if(esaudiodecoder->bit_rate == 0) {
      g722_attr.bit_rate = DEFAULT_G722_BIT_RATE;
    } else {
      g722_attr.bit_rate = esaudiodecoder->bit_rate;
    }
    attr = &g722_attr;
  } else if (type == G726) {
    if(esaudiodecoder->bit_rate == 0) {
      g726_attr.bit_rate = DEFAULT_G726_BIT_RATE;
    } else {
      g726_attr.bit_rate = esaudiodecoder->bit_rate;
    }
    attr = &g726_attr;
  }

  gint32 ret = es_adec_create(esaudiodecoder->chn, convert_codec_type(esaudiodecoder->type), attr);
  if (ret != 0) {
    GST_ERROR_OBJECT (esaudiodecoder, "es_adec_create() failed,ret: %d", ret);
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_esaudiodecoder_open_decoder(GstEsaudiodecoder *esaudiodecoder)
{
  return hw_open_decoder(esaudiodecoder);
}

static void
gst_esaudiodecoder_close_decoder(GstEsaudiodecoder *esaudiodecoder)
{
  es_adec_destroy(esaudiodecoder->chn);
}

void
gst_esaudiodecoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEsaudiodecoder *esaudiodecoder = GST_ESAUDIODECODER(object);

  switch (property_id) {
  case PROP_CHANNEL:
    esaudiodecoder->chn = g_value_get_int(value);
    GST_DEBUG_OBJECT (esaudiodecoder, "chn:%d", esaudiodecoder->chn);
    break;
  case PROP_CODEC_TYPE:
    g_free (esaudiodecoder->type);
    esaudiodecoder->type = g_value_dup_string(value);
    /* setting NULL restores the default device */
    if (esaudiodecoder->type == NULL) {
      esaudiodecoder->type = g_strdup(DEFAULT_CODEC_TYPE);
    }
    GST_DEBUG_OBJECT (esaudiodecoder, "type:%s", esaudiodecoder->type);
    break;
  case PROP_CODEC_SUB_TYPE:
    g_free(esaudiodecoder->subtype);
    esaudiodecoder->subtype = g_value_dup_string(value);
    /* setting NULL restores the default device */
    if (esaudiodecoder->subtype == NULL) {
      esaudiodecoder->subtype = g_strdup(DEFAULT_CODEC_SUB_TYPE);
    }
    GST_DEBUG_OBJECT(esaudiodecoder, "subtype:%s", esaudiodecoder->subtype);
    break;
  case PROP_BIT_RATE:
    esaudiodecoder->bit_rate = g_value_get_int(value);
    GST_DEBUG_OBJECT (esaudiodecoder, "bit_rate:%d", esaudiodecoder->bit_rate);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void
gst_esaudiodecoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEsaudiodecoder *esaudiodecoder = GST_ESAUDIODECODER(object);

  switch (prop_id) {
  case PROP_CHANNEL:
    g_value_set_int (value, esaudiodecoder->chn);
    break;
  case PROP_CODEC_TYPE:
    g_value_set_string (value, esaudiodecoder->type);
    break;
  case PROP_CODEC_SUB_TYPE:
    g_value_set_string(value, esaudiodecoder->subtype);
    break;
  case PROP_BIT_RATE:
    g_value_set_int (value, esaudiodecoder->bit_rate);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static gboolean
plugin_init(GstPlugin *plugin)
{
  return gst_element_register(plugin, "esaudiodecoder", GST_RANK_SECONDARY,
                              GST_TYPE_ESAUDIODECODER);
}

#ifndef VERSION
#define VERSION "0.1"
#endif

#ifndef PACKAGE
#define PACKAGE "esaudiodecoder"
#endif

#ifndef PACKAGE_NAME
#define PACKAGE_NAME "esaudiodecoder"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://eswin.com/"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  esaudiodecoder,
                  "esw AAC Decoder",
                  plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
