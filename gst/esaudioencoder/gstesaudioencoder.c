/* GStreamer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2009 Mark Nauwelaerts <mnauw@users.sourceforge.net>
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
 * SECTION:element-esaudioencoder
 * @title: esaudioencoder
 * @see_also: faac
 *
 * esaudioencoder encodes raw audio to AAC (MPEG-4 part 3) streams.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location=16.wav ! wavparse ! esaudioencoder ! "audio/mpeg, mpegversion=(int)4, stream-format=(string)adts" ! filesink location=xx.aac
 * ]| Encode a wave file as aac and write to file.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <gst/audio/audio.h>
#include <gst/pbutils/codec-utils.h>

#include "gstesaudioencoder.h"
#include "adp_aac.h"
#include "adp_itut_gxx.h"
#include "adp_amr.h"
#include "codec_api.h"

#define DEFAULT_MPEG_VERSION      2
#define MP3_MPEG_VERSION          1
#define DEFAULT_AAC_CHANNELS      2
#define DEFAULT_AAC_SAMPLES       1024
#define DEFAULT_ENCODER_CHAN_NUM  1
#define MAX_ENCODER_CHANNEL_NUM   32
#define DEFAULT_AAC_SAMPLE_RATE   48000
#define MIN_SAMPLE_RATE           8000
#define MAX_SAMPLE_RATE           48000
#define DEFAULT_AAC_BIT_RATE      64000
#define MAX_BIT_RATE              960000
#define MIN_BIT_RATE              8000
#define DEFAULT_G722_BIT_RATE     64000
#define DEFAULT_G726_BIT_RATE     32000
#define DEFAULT_AMR_NB_BIT_RATE   12200
#define DEFAULT_AMR_WB_BIT_RATE   23850
#define DEFAULT_G7XX_SAMPLES      160
#define DEFAULT_AMR_NB_SAMPLES    160
#define DEFAULT_AMR_WB_SAMPLES    320

#define DEBUG_DUMP_FILE 1
#define DUMP_ENCODER_FILE_NAME "/tmp/audio/encoder_dump.aac"

#define SRC_CAPS \
  "audio/mpeg, mpegversion = (int) {1, 2, 4 }; audio/x-alaw; audio/x-mulaw; audio/G722; audio/x-adpcm; audio/AMR; audio/AMR-WB"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
                                                                   GST_PAD_SRC,
                                                                   GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS(SRC_CAPS));
/* code type */
#define AAC_CODEC_TYPE    "aac"
#define MP3_CODEC_TYPE    "mp3"
#define G711_CODEC_TYPE   "g711"
#define G722_CODEC_TYPE   "g722"
#define G726_CODEC_TYPE   "g726"
#define MP2L2_CODEC_TYPE  "mp2l2"
#define AMR_NB_CODEC_TYPE   "amrnb"
#define AMR_WB_CODEC_TYPE   "amrwb"
#define DEFAULT_CODEC_TYPE AAC_CODEC_TYPE
/* code sub type */
#define AAC_LC    "AAC-LC"
#define AAC_HEV1  "AAC-HEv1"
#define AAC_HEV2  "AAC-HEv2"
#define X_ALAW    "audio/x-alaw"
#define X_MLAW    "audio/x-mulaw"
#define AMR_WB    "AMR-WB"
#define AMR_NB    "AMR-NB"
#define DEFAULT_CODEC_SUB_TYPE AAC_LC
enum
{
  PROP_0,
  PROP_CHANNEL,
  PROP_CODEC_TYPE,
  PROP_CODEC_SUB_TYPE,
  PROP_SAMPLE_RATE,
  PROP_BIT_RATE,
  PROP_LAST
};

static void gst_esaudioencoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_esaudioencoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstCaps *gst_esaudioencoder_enc_generate_sink_caps (void);
static gboolean gst_esaudioencoder_configure_source_pad (GstEsaudioencoder * esaudioencoder,
    GstAudioInfo * info);

static gboolean gst_esaudioencoder_stop (GstAudioEncoder * enc);
static gboolean gst_esaudioencoder_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_esaudioencoder_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);

GST_DEBUG_CATEGORY_STATIC (esaudioencoder_debug);
#define GST_CAT_DEFAULT esaudioencoder_debug

#define gst_esaudioencoder_parent_class parent_class
G_DEFINE_TYPE(GstEsaudioencoder, gst_esaudioencoder, GST_TYPE_AUDIO_ENCODER);

static void
gst_esaudioencoder_class_init(GstEsaudioencoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
  GstAudioEncoderClass *base_class = GST_AUDIO_ENCODER_CLASS(klass);
  GstCaps *sink_caps;
  GstPadTemplate *sink_templ;

  gobject_class->set_property = gst_esaudioencoder_set_property;
  gobject_class->get_property = gst_esaudioencoder_get_property;

  GST_DEBUG_CATEGORY_INIT(esaudioencoder_debug, "esaudioencoder", 0, "ES audio encoder");

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  sink_caps = gst_esaudioencoder_enc_generate_sink_caps();
  sink_templ = gst_pad_template_new("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, sink_caps);
  gst_element_class_add_pad_template (gstelement_class, sink_templ);
  gst_caps_unref (sink_caps);

  gst_element_class_set_static_metadata(gstelement_class, "ES audio decoder",
                                        "Codec/Encoder/Audio",
                                        "ES audio decoder",
                                        "http://eswin.com/");


  base_class->stop = GST_DEBUG_FUNCPTR(gst_esaudioencoder_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR(gst_esaudioencoder_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR(gst_esaudioencoder_handle_frame);

  /* properties */
  g_object_class_install_property(gobject_class, PROP_CHANNEL,
                                  g_param_spec_int("channel", "encoder channel number",
                                                   "encoder channel number" , DEFAULT_ENCODER_CHAN_NUM,
                                                   MAX_ENCODER_CHANNEL_NUM, DEFAULT_ENCODER_CHAN_NUM,
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_CODEC_TYPE,
                                  g_param_spec_string("type", "codec type",
                                                      "codec type", DEFAULT_CODEC_TYPE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property(gobject_class, PROP_CODEC_SUB_TYPE,
                                  g_param_spec_string("subtype", "codec sub type",
                                                      "codec sub type", DEFAULT_CODEC_SUB_TYPE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_SAMPLE_RATE,
                                  g_param_spec_int("samplerate", "encoder sample rate",
                                                   "encoder sample rate" , MIN_SAMPLE_RATE,
                                                   MAX_SAMPLE_RATE, DEFAULT_AAC_SAMPLE_RATE,
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_BIT_RATE,
                                  g_param_spec_int("bitrate", "encoder bit rate",
                                                   "encoder bit rate" , MIN_BIT_RATE,
                                                   MAX_BIT_RATE, DEFAULT_AAC_BIT_RATE,
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_esaudioencoder_init (GstEsaudioencoder * esaudioencoder)
{
  esaudioencoder->chn = DEFAULT_ENCODER_CHAN_NUM;
  esaudioencoder->type = g_strdup(DEFAULT_CODEC_TYPE);
  esaudioencoder->subtype = g_strdup(DEFAULT_CODEC_SUB_TYPE);
  esaudioencoder->sample_rate = 0;
  esaudioencoder->bit_rate = 0;
  GST_PAD_SET_ACCEPT_TEMPLATE(GST_AUDIO_ENCODER_SINK_PAD(esaudioencoder));
  gint ret = es_aenc_init();
  if (ret != 0) {
    GST_ERROR_OBJECT (esaudioencoder, "es_aenc_init failed,ret:%d", ret);
  }
}

static void
gst_esaudioencoder_close_encoder (GstEsaudioencoder * esaudioencoder)
{
  es_aenc_destroy(esaudioencoder->chn);
}


static gboolean
gst_esaudioencoder_stop (GstAudioEncoder * enc)
{
  GstEsaudioencoder *esaudioencoder = GST_ESAUDIOENCODER (enc);

  GST_DEBUG_OBJECT (esaudioencoder, "stop");
  gst_esaudioencoder_close_encoder (esaudioencoder);
  return TRUE;
}

static const GstAudioChannelPosition aac_channel_positions[][8] = {
  {GST_AUDIO_CHANNEL_POSITION_MONO},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      },
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1}
};

static GstCaps *
gst_esaudioencoder_enc_generate_sink_caps (void)
{
  GstCaps *caps = gst_caps_new_empty ();
  GstStructure *s, *t;
  gint i, c;
  static const int rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000
  };
  GValue rates_arr = { 0, };
  GValue tmp_v = { 0, };

  g_value_init (&rates_arr, GST_TYPE_LIST);
  g_value_init (&tmp_v, G_TYPE_INT);
  for (i = 0; i < G_N_ELEMENTS (rates); i++) {
    g_value_set_int (&tmp_v, rates[i]);
    gst_value_list_append_value (&rates_arr, &tmp_v);
  }
  g_value_unset (&tmp_v);

  s = gst_structure_new ("audio/x-raw",
      "layout", G_TYPE_STRING, "interleaved", NULL);
  gst_structure_set_value (s, "rate", &rates_arr);

  t = gst_structure_copy (s);
  gst_structure_set (t, "channels", G_TYPE_INT, 1, NULL);
  gst_caps_append_structure (caps, t);

  for (i = 2; i <= 6; i++) {
    guint64 channel_mask = 0;
    t = gst_structure_copy (s);

    gst_structure_set (t, "channels", G_TYPE_INT, i, NULL);
    for (c = 0; c < i; c++)
      channel_mask |= G_GUINT64_CONSTANT (1) << aac_channel_positions[i - 1][c];

    gst_structure_set (t, "channel-mask", GST_TYPE_BITMASK, channel_mask, NULL);
    gst_caps_append_structure (caps, t);
  }
  gst_structure_free (s);
  g_value_unset (&rates_arr);

  GST_DEBUG ("Generated sinkcaps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_esaudioencoder_set_format (GstAudioEncoder * enc, GstAudioInfo * info)
{
  GstEsaudioencoder *esaudioencoder = GST_ESAUDIOENCODER (enc);
  gboolean result = FALSE;

  /* finish up */
  result = gst_esaudioencoder_configure_source_pad (esaudioencoder, info);
  if (!result)
    goto done;

  /* report needs to base class */
  gst_audio_encoder_set_frame_samples_min (enc, esaudioencoder->samples);
  gst_audio_encoder_set_frame_samples_max (enc, esaudioencoder->samples);
  gst_audio_encoder_set_frame_max (enc, 1);

done:
  return result;
}

/* check downstream caps to configure format */
static void
gst_esaudioencoder_negotiate (GstEsaudioencoder * esaudioencoder)
{
  GstCaps *caps;

  /* default setup */
  if (!g_strcmp0 (esaudioencoder->type, AAC_CODEC_TYPE)) {
    esaudioencoder->mpegversion = DEFAULT_MPEG_VERSION;
  } else if (!g_strcmp0 (esaudioencoder->type, MP3_CODEC_TYPE)) {
    esaudioencoder->mpegversion = MP3_MPEG_VERSION;
  }

  esaudioencoder->outputformat = 0;

  caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (esaudioencoder));

  GST_DEBUG_OBJECT (esaudioencoder, "allowed caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;
    gint i = DEFAULT_MPEG_VERSION;
    if (!g_strcmp0 (esaudioencoder->type, AAC_CODEC_TYPE)) {
      if ((str = gst_structure_get_string(s, "stream-format"))) {
        if (strcmp(str, "adts") == 0) {
          GST_DEBUG_OBJECT(esaudioencoder, "use ADTS format for output");
          esaudioencoder->outputformat = 1;
        }
        else if (strcmp(str, "raw") == 0) {
          GST_DEBUG_OBJECT(esaudioencoder, "use RAW format for output");
          esaudioencoder->outputformat = 0;
        } else {
          GST_DEBUG_OBJECT(esaudioencoder, "unknown stream-format: %s", str);
          esaudioencoder->outputformat = 0;
        }
      }
    }
    if (gst_structure_get_int(s, "mpegversion", &i)) {
      esaudioencoder->mpegversion = i;
    }
  }

  if (caps)
    gst_caps_unref (caps);
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
    }
    else {
        return UNKNOW;
    }
}

static gboolean
gst_esaudioencoder_open_encoder (GstEsaudioencoder * esaudioencoder, GstAudioInfo * info)
{
  g_return_val_if_fail (info->rate != 0 && info->channels != 0, FALSE);

  void *attr = NULL;
  audio_aacenc_attr aac_attr;
  audio_g711_attr g711_attr;
  audio_g722_attr g722_attr;
  audio_g726_attr g726_attr;
  audio_amr_encoder_attr amr_attr;

  memset(&aac_attr, 0, sizeof(aac_attr));
  memset(&g711_attr, 0, sizeof(g711_attr));
  memset(&g722_attr, 0, sizeof(g722_attr));
  memset(&g726_attr, 0, sizeof(g726_attr));
  memset(&amr_attr, 0, sizeof(amr_attr));

  es_codec_type type = convert_codec_type(esaudioencoder->type);
  switch (type) {
      case AAC:
          memset(&aac_attr, 0, sizeof(aac_attr));
          /* bit rate */
          if(esaudioencoder->bit_rate == 0) {
            aac_attr.bit_rate = DEFAULT_AAC_BIT_RATE;
          } else {
            aac_attr.bit_rate = esaudioencoder->bit_rate;
          }
          /* profile */
          if (!g_strcmp0 (esaudioencoder->subtype, AAC_LC)) {
            aac_attr.aot = AOT_LC;
          } else if (!g_strcmp0 (esaudioencoder->subtype, AAC_HEV1)) {
            aac_attr.aot = AOT_HE;
          } else if (!g_strcmp0 (esaudioencoder->subtype, AAC_HEV2)) {
            aac_attr.aot = AOT_HEV2;
          } else{
            aac_attr.aot = AOT_LC;
          }
          aac_attr.channels = info->channels;
          /* sample rate*/
          if(esaudioencoder->sample_rate == 0) {
            aac_attr.sample_rate = DEFAULT_AAC_SAMPLE_RATE;
          } else {
            aac_attr.sample_rate = esaudioencoder->sample_rate;
          }
          esaudioencoder->samples = DEFAULT_AAC_SAMPLES;
          attr = &aac_attr;
          break;
      case MP3:
          break;
      case G711:
          if (!g_strcmp0 (esaudioencoder->subtype, X_ALAW)) {
            g711_attr.type = ALAW;
          } else if (!g_strcmp0 (esaudioencoder->subtype, X_MLAW)) {
            g711_attr.type = ULAW;
          } else{
            g711_attr.type = ALAW;
          }
          attr = &g711_attr;
          esaudioencoder->samples = DEFAULT_G7XX_SAMPLES;
          break;
      case G722:
          if(esaudioencoder->bit_rate == 0) {
            g722_attr.bit_rate = DEFAULT_G722_BIT_RATE;
          } else {
            g722_attr.bit_rate = esaudioencoder->bit_rate;
          }
          attr = &g722_attr;
          esaudioencoder->samples = DEFAULT_G7XX_SAMPLES;
          break;
      case G726:
          if(esaudioencoder->bit_rate == 0) {
            g726_attr.bit_rate = DEFAULT_G726_BIT_RATE;
          } else {
            g726_attr.bit_rate = esaudioencoder->bit_rate;
          }
          attr = &g726_attr;
          esaudioencoder->samples = DEFAULT_G7XX_SAMPLES;
          break;
      case AMR:
          if(!g_strcmp0 (esaudioencoder->type, AMR_NB_CODEC_TYPE)) {
            amr_attr.is_wb = 0;
            esaudioencoder->samples = DEFAULT_AMR_NB_SAMPLES;
          } else {
            amr_attr.is_wb = 1;
            esaudioencoder->samples = DEFAULT_AMR_WB_SAMPLES;
          }
          if(esaudioencoder->bit_rate == 0) {
            if(amr_attr.is_wb) {
              amr_attr.bit_rate = DEFAULT_AMR_WB_BIT_RATE;
            } else {
              amr_attr.bit_rate = DEFAULT_AMR_NB_BIT_RATE;
            }
          } else {
            amr_attr.bit_rate = esaudioencoder->bit_rate;
          }
          attr = &amr_attr;
          break;
      default:
          break;
  }
  gint ret = es_aenc_create(esaudioencoder->chn, type, attr);
  if (ret != 0) {
    GST_ERROR_OBJECT (esaudioencoder, "es_aenc_create failed,ret:%d", ret);
    return FALSE;
  }

  GST_DEBUG_OBJECT (esaudioencoder, "esaudioencoder chn:%d, type %s, samples:%d",
                    esaudioencoder->chn, esaudioencoder->type, esaudioencoder->samples);
  return TRUE;
}

static gboolean
gst_esaudioencoder_configure_source_pad (GstEsaudioencoder * esaudioencoder, GstAudioInfo * info)
{
  GstCaps *srccaps;
  gboolean ret;

  if (!g_strcmp0 (esaudioencoder->type, AAC_CODEC_TYPE)
      || !g_strcmp0 (esaudioencoder->type, MP3_CODEC_TYPE)) {
    /* negotiate stream format */
    gst_esaudioencoder_negotiate(esaudioencoder);
  }

  /* now create a caps for it all */
  if (!g_strcmp0 (esaudioencoder->type, AAC_CODEC_TYPE)) {
    srccaps = gst_caps_new_simple("audio/mpeg",
                                  "mpegversion", G_TYPE_INT, esaudioencoder->mpegversion,
                                  "channels", G_TYPE_INT, info->channels,
                                  "rate", G_TYPE_INT, info->rate,
                                  "stream-format", G_TYPE_STRING, (esaudioencoder->outputformat ? "adts" : "raw"),
                                  "framed", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if ((!g_strcmp0 (esaudioencoder->type, MP3_CODEC_TYPE)) || (!g_strcmp0 (esaudioencoder->type, MP2L2_CODEC_TYPE))) {
    srccaps = gst_caps_new_simple("audio/mpeg",
                                  "mpegversion", G_TYPE_INT, esaudioencoder->mpegversion,
                                  "channels", G_TYPE_INT, info->channels,
                                  "rate", G_TYPE_INT, info->rate, NULL);
  }else {
    gchar *media_type = NULL;
    if(!g_strcmp0 (esaudioencoder->type, G711_CODEC_TYPE)) {
      if(!g_strcmp0 (esaudioencoder->subtype, X_ALAW)) {
        media_type = g_strdup ("audio/x-alaw");
      } else if(!g_strcmp0 (esaudioencoder->subtype, X_MLAW)) {
        media_type = g_strdup ("audio/x-mulaw");
      } else {
        media_type = g_strdup ("audio/x-alaw");
      }
    } else if(!g_strcmp0 (esaudioencoder->type, G722_CODEC_TYPE)) {
      media_type = g_strdup ("audio/G722");
    } else if(!g_strcmp0 (esaudioencoder->type, G726_CODEC_TYPE)) {
      media_type = g_strdup ("audio/x-adpcm");
    } else if(!g_strcmp0 (esaudioencoder->type, AMR_NB_CODEC_TYPE)) {
      media_type = g_strdup ("audio/AMR");
    } else if(!g_strcmp0 (esaudioencoder->type, AMR_WB_CODEC_TYPE)) {
      media_type = g_strdup ("audio/AMR-WB");
    } else {
      GST_ERROR_OBJECT (esaudioencoder, "Invalid codec type:%s", esaudioencoder->type);
      return FALSE;
    }
    srccaps = gst_caps_new_simple(media_type, "channels", G_TYPE_INT, info->channels,
                                  "rate", G_TYPE_INT, info->rate, NULL);
    g_free (media_type);
  }

  GST_DEBUG_OBJECT (esaudioencoder, "src pad caps: %" GST_PTR_FORMAT, srccaps);

  ret = gst_audio_encoder_set_output_format(GST_AUDIO_ENCODER(esaudioencoder), srccaps);
  gst_caps_unref(srccaps);
  if (!ret) {
    GST_ERROR_OBJECT (esaudioencoder, "gst_audio_encoder_set_output_format failed");
    return ret;
  }
  ret = gst_esaudioencoder_open_encoder(esaudioencoder, info);
  if (!ret) {
    GST_ERROR_OBJECT(esaudioencoder, "gst_esaudioencoder_open_encoder failed");
  }
  return ret;
}

static void
gst_esaudioencoder_dump_data(const char *path, guint8 *buf, gsize bytes)
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
gst_esaudioencoder_handle_frame (GstAudioEncoder * enc, GstBuffer * in_buf)
{
  GstEsaudioencoder *esaudioencoder = GST_ESAUDIOENCODER (enc);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf = NULL;
  gsize size;
  guint out_size;
  gint enc_ret = 0;
  GstMapInfo map, omap;
  guint8 *data;

  /* no fancy draining */
  if (G_UNLIKELY (!in_buf)) {
    return GST_FLOW_OK;
  }

  gst_buffer_map (in_buf, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;

  out_buf = gst_buffer_new_and_alloc (size);
  gst_buffer_map (out_buf, &omap, GST_MAP_WRITE);

  enc_ret = es_aenc_encode_frame(esaudioencoder->chn, data, size, omap.data, &out_size);

  /* unmap in_buf at once */
  gst_buffer_unmap (in_buf, &map);

  if (G_UNLIKELY(enc_ret != 0)) {
    goto encode_failed;
  }

  GST_LOG_OBJECT (esaudioencoder, "size: %lu, out_size: %u", size, out_size);

  if (out_size > 0) {
    if (DEBUG_DUMP_FILE) {
      gst_esaudioencoder_dump_data(DUMP_ENCODER_FILE_NAME, omap.data, out_size);
    }
    gst_buffer_unmap (out_buf, &omap);
    gst_buffer_resize (out_buf, 0, out_size);
    ret = gst_audio_encoder_finish_frame (enc, out_buf, esaudioencoder->samples);
  } else {
    gst_buffer_unmap (out_buf, &omap);
    gst_buffer_unref (out_buf);
  }
  return ret;

  /* ERRORS */
encode_failed:
  {
    GST_ELEMENT_ERROR (esaudioencoder, LIBRARY, ENCODE, (NULL), (NULL));
    if (in_buf) {
      gst_buffer_unmap (in_buf, &map);
    }
    if (out_buf) {
      gst_buffer_unmap (out_buf, &omap);
      gst_buffer_unref (out_buf);
    }
    return GST_FLOW_ERROR;
  }
}

static void
gst_esaudioencoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstEsaudioencoder *esaudioencoder = GST_ESAUDIOENCODER (object);

  GST_OBJECT_LOCK (esaudioencoder);

  switch (prop_id) {
  case PROP_CHANNEL:
    esaudioencoder->chn = g_value_get_int(value);
    GST_DEBUG_OBJECT (esaudioencoder, "chn:%d", esaudioencoder->chn);
    break;
  case PROP_CODEC_TYPE:
    g_free(esaudioencoder->type);
    esaudioencoder->type = g_value_dup_string(value);
    /* setting NULL restores the default device */
    if (esaudioencoder->type == NULL)
    {
      esaudioencoder->type = g_strdup(DEFAULT_CODEC_TYPE);
    }
    GST_DEBUG_OBJECT(esaudioencoder, "type:%s", esaudioencoder->type);
    break;
  case PROP_CODEC_SUB_TYPE:
    g_free(esaudioencoder->subtype);
    esaudioencoder->subtype = g_value_dup_string(value);
    /* setting NULL restores the default device */
    if (esaudioencoder->subtype == NULL)
    {
      esaudioencoder->subtype = g_strdup(DEFAULT_CODEC_SUB_TYPE);
    }
    GST_DEBUG_OBJECT(esaudioencoder, "subtype:%s", esaudioencoder->subtype);
    break;
  case PROP_SAMPLE_RATE:
    esaudioencoder->sample_rate = g_value_get_int(value);
    GST_DEBUG_OBJECT (esaudioencoder, "sample_rate:%d", esaudioencoder->sample_rate);
    break;
  case PROP_BIT_RATE:
    esaudioencoder->bit_rate = g_value_get_int(value);
    GST_DEBUG_OBJECT (esaudioencoder, "bit_rate:%d", esaudioencoder->bit_rate);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }

  GST_OBJECT_UNLOCK (esaudioencoder);
}

static void
gst_esaudioencoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstEsaudioencoder *esaudioencoder = GST_ESAUDIOENCODER (object);

  GST_OBJECT_LOCK (esaudioencoder);

  switch (prop_id) {
  case PROP_CHANNEL:
    g_value_set_int (value, esaudioencoder->chn);
    break;
  case PROP_CODEC_TYPE:
    g_value_set_string(value, esaudioencoder->type);
    break;
  case PROP_CODEC_SUB_TYPE:
    g_value_set_string(value, esaudioencoder->subtype);
    break;
  case PROP_SAMPLE_RATE:
    g_value_set_int (value, esaudioencoder->sample_rate);
    break;
  case PROP_BIT_RATE:
    g_value_set_int (value, esaudioencoder->bit_rate);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }

  GST_OBJECT_UNLOCK (esaudioencoder);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "esaudioencoder", GST_RANK_SECONDARY,
      GST_TYPE_ESAUDIOENCODER);
}

#ifndef VERSION
#define VERSION "0.1"
#endif

#ifndef PACKAGE
#define PACKAGE "esaudioencoder"
#endif

#ifndef PACKAGE_NAME
#define PACKAGE_NAME "esaudioencoder"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://eswin.com/"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    esaudioencoder,
    "Free AAC Encoder (FAAC)",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
