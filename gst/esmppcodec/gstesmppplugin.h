/*
 * Copyright (C) <2024> Beijing ESWIN Computing Technology Co., Ltd.
 *     Author: Tangdaoyong <tangdaoyong@eswincomputing.com>
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

#ifndef __GST_ES_MPP_PLUGIN_H__
#define __GST_ES_MPP_PLUGIN_H__

#include "gst/video/video.h"
#include <es_mpp.h>
#include <mpp_frame.h>

G_BEGIN_DECLS;

#define GST_ES_VIDEO_INFO_HSTRIDE(i) GST_VIDEO_INFO_PLANE_STRIDE(i, 0)
#define GST_ES_VIDEO_INFO_VSTRIDE(i)                            \
    (GST_VIDEO_INFO_N_PLANES(i) == 1 ? GST_VIDEO_INFO_HEIGHT(i) \
                                     : (gint)(GST_VIDEO_INFO_PLANE_OFFSET(i, 1) / GST_ES_VIDEO_INFO_HSTRIDE(i)))

#define GST_ES_ALIGN(v) GST_ROUND_UP_N(v, 16)  // Hardcode, set to 16 as default

GstVideoFormat gst_es_mpp_format_to_gst_format(MppFrameFormat mpp_format);
MppFrameFormat gst_es_gst_format_to_mpp_format(GstVideoFormat gst_format);
gboolean gst_es_video_info_align(GstVideoInfo* info, gint hstride, gint vstride);
guint gst_es_get_pixel_stride(GstVideoInfo* info);
const char* gst_es_mpp_format_to_string(MppFrameFormat pix_fmt);

G_END_DECLS;

#endif
