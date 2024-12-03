/*
 * GStreamer
 * Copyright (C) 2015 Vivia Nikolaidou <vivia@toolsonair.com>
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

/* Copyright © 2023 ESWIN. All rights reserved.
*
* Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
* all intellectual property and proprietary rights in and to this software. Except as expressly
* authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
* modified, adapted, translated, or created derivative work of, in whole or in part.
 */

#ifndef __GST_ES_AVSYNC_H__
#define __GST_ES_AVSYNC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS
#define GST_TYPE_ES_AVSYNC                    (gst_es_avsync_get_type())
#define GST_ES_AVSYNC(obj)                    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ES_AVSYNC,GstEsAvSync))
#define GST_IS_ES_AVSYNC(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ES_AVSYNC))
#define GST_ES_AVSYNC_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_ES_AVSYNC,GstEsAvSyncClass))
#define GST_IS_ES_AVSYNC_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_ES_AVSYNC))
#define GST_ES_AVSYNC_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_ES_AVSYNC,GstEsAvSyncClass))
typedef struct _GstEsAvSync GstEsAvSync;
typedef struct _GstEsAvSyncClass GstEsAvSyncClass;

struct _GstEsAvSync
{
  GstElement parent;

  GstPad *asrcpad, *asinkpad, *vsrcpad, *vsinkpad;

  gint chanId;

  gboolean audio_eos_received;
  gboolean video_eos_received;
};

struct _GstEsAvSyncClass
{
  GstElementClass parent_class;
};

GType gst_es_avsync_get_type (void);

G_END_DECLS
#endif /* __GST_ES_AVSYNC_H__ */
