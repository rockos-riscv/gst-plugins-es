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

#ifndef _GST_ESAUDIOENCODER_H_
#define _GST_ESAUDIOENCODER_H_

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>

G_BEGIN_DECLS

#define GST_TYPE_ESAUDIOENCODER \
  (gst_esaudioencoder_get_type ())
#define GST_ESAUDIOENCODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ESAUDIOENCODER, GstEsaudioencoder))
#define GST_ESAUDIOENCODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ESAUDIOENCODER, GstEsaudioencoderClass))
#define GST_IS_ESAUDIOENCODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ESAUDIOENCODER))
#define GST_IS_ESAUDIOENCODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ESAUDIOENCODER))

typedef struct _GstEsaudioencoder GstEsaudioencoder;
typedef struct _GstEsaudioencoderClass GstEsaudioencoderClass;

struct _GstEsaudioencoder {
  GstAudioEncoder element;

  /* input frame size */
  gint samples;
  /* required output buffer size */
  gint bytes;

  /* negotiated */
  gint mpegversion;
  gint outputformat;

  /* properties */
  gchar *type;
  gchar *subtype;
  guint chn;
  guint sample_rate;
  guint bit_rate;
};

struct _GstEsaudioencoderClass {
  GstAudioEncoderClass parent_class;
};

GType gst_esaudioencoder_get_type (void);

G_END_DECLS

#endif /* __GST_ESAUDIOENCODER_H__ */
