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

#ifndef _GST_ESAUDIODECODER_H_
#define _GST_ESAUDIODECODER_H_

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

G_BEGIN_DECLS

#define MAX_STREAM_LEN              2048

#define GST_TYPE_ESAUDIODECODER   (gst_esaudiodecoder_get_type())
#define GST_ESAUDIODECODER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ESAUDIODECODER,GstEsaudiodecoder))
#define GST_ESAUDIODECODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ESAUDIODECODER,GstEsaudiodecoderClass))
#define GST_IS_ESAUDIODECODER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ESAUDIODECODER))
#define GST_IS_ESAUDIODECODER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ESAUDIODECODER))

typedef struct _GstEsaudiodecoder GstEsaudiodecoder;
typedef struct _GstEsaudiodecoderClass GstEsaudiodecoderClass;

struct _GstEsaudiodecoder
{
  GstAudioDecoder element;
  guint samplerate; /* sample rate of the last MPEG frame */
  guint channels;   /* number of channels of the last frame */
  guint bit_depth;  /* bytes depth */
  gboolean init;
  gchar *type;
  gchar *subtype;
  guint chn;
  guint bit_rate;
  guint8 stream[MAX_STREAM_LEN * 2];
  guint offset;
};

struct _GstEsaudiodecoderClass
{
  GstAudioDecoderClass parent_class;
};

GType gst_esaudiodecoder_get_type (void);

G_END_DECLS

#endif
