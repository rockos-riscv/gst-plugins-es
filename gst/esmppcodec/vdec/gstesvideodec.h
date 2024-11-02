/*
 * Copyright (C) <2024> Beijing ESWIN Computing Technology Co., Ltd.
 *     Author: Liujie <liujie@eswincomputing.com>
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

#ifndef __GST_ES_VIDEO_DEC_H__
#define __GST_ES_VIDEO_DEC_H__

#include "gstesdec.h"

G_BEGIN_DECLS;

G_DECLARE_FINAL_TYPE(GstEsVideoDec, gst_es_video_dec, GST, ES_VIDEO_DEC, GstEsDec);

gboolean gst_es_video_dec_register(GstPlugin* plugin, guint rank);

G_END_DECLS;

#endif
