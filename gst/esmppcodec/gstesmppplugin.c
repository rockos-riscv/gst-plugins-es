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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstesmppplugin.h"
#include "gstesh264enc.h"
#include "gstesh265enc.h"
#include "gstesvideodec.h"
#include "gstesjpegenc.h"
#include "gstesjpegdec.h"

GST_DEBUG_CATEGORY_STATIC(esmpp_debug);
#define GST_CAT_DEFAULT esmpp_debug

#define ENTRY_GST_FMT_INFO(gst_fmt, mpp_fmt, stride) {GST_VIDEO_FORMAT_##gst_fmt, MPP_FMT_##mpp_fmt, stride}

static struct gst_format_info {
    GstVideoFormat gst_fmt;
    MppFrameFormat mpp_fmt;
    gint stride;
} gst_format_map[] = {
    ENTRY_GST_FMT_INFO(I420, I420, 1),
    ENTRY_GST_FMT_INFO(NV12, NV12, 1),
    ENTRY_GST_FMT_INFO(NV21, NV21, 1),
    ENTRY_GST_FMT_INFO(YV12, YV12, 1),       // planar 4:2:0 YUV, Same as I420 but with U and V planes swapped
    ENTRY_GST_FMT_INFO(YUY2, YUY2, 2),       // packed 4:2:2 YUV, |Y0|U0|Y1|V0| |Y2|U2|Y3|V2| ...
    ENTRY_GST_FMT_INFO(YVYU, YVY2, 2),       // packed 4:2:2 YUV, |Y0|V0|Y1|U0| |Y2|V2|Y3|U2| ...
    ENTRY_GST_FMT_INFO(UYVY, UYVY, 2),       // packed 4:2:2 YUV, |U0|Y0|V0|Y1| |U2|Y2|V2|Y3| ...
    ENTRY_GST_FMT_INFO(VYUY, VYUY, 2),       // packed 4:2:2 YUV, |V0|Y0|U0|Y1| |V2|Y2|U2|Y3| ...
    ENTRY_GST_FMT_INFO(NV16, NV16, 2),       // planar 4:2:2 YUV with interleaved UV plane
    ENTRY_GST_FMT_INFO(NV61, NV61, 2),       // planar 4:2:2 YUV with interleaved VU plane
    ENTRY_GST_FMT_INFO(I420_10LE, I010, 1),  // yuv420 planar, 10bit
    ENTRY_GST_FMT_INFO(P010_10LE, P010, 1),  // nv12, 10bit
    ENTRY_GST_FMT_INFO(GRAY8, GRAY8, 1),
    ENTRY_GST_FMT_INFO(RGB, R8G8B8, 3),
    ENTRY_GST_FMT_INFO(BGR, B8G8R8, 3),
    ENTRY_GST_FMT_INFO(ARGB, A8R8G8B8, 4),
    ENTRY_GST_FMT_INFO(ABGR, A8B8G8R8, 4),
    ENTRY_GST_FMT_INFO(RGBA, R8G8B8A8, 4),
    ENTRY_GST_FMT_INFO(BGRA, B8G8R8A8, 4),
    ENTRY_GST_FMT_INFO(xRGB, X8R8G8B8, 4),
    ENTRY_GST_FMT_INFO(xBGR, X8B8G8R8, 4),
    ENTRY_GST_FMT_INFO(RGBx, R8G8B8X8, 4),
    ENTRY_GST_FMT_INFO(BGRx, B8G8R8X8, 4),
};
static guint map_size = sizeof(gst_format_map) / sizeof(gst_format_map[0]);

GstVideoFormat gst_es_mpp_format_to_gst_format(MppFrameFormat mpp_format) {
    for (guint i = 0; i < map_size; i++) {
        if (gst_format_map[i].mpp_fmt == mpp_format) {
            return gst_format_map[i].gst_fmt;
        }
    }
    return GST_VIDEO_FORMAT_UNKNOWN;
}

MppFrameFormat gst_es_gst_format_to_mpp_format(GstVideoFormat gst_format) {
    for (guint i = 0; i < map_size; i++) {
        if (gst_format_map[i].gst_fmt == gst_format) {
            return gst_format_map[i].mpp_fmt;
        }
    }
    return MPP_FMT_BUTT;
}

guint gst_es_get_pixel_stride(GstVideoInfo* info) {
    GstVideoFormat gst_format = GST_VIDEO_INFO_FORMAT(info);
    guint hstride = GST_ES_VIDEO_INFO_HSTRIDE(info);
    gint stride = -1;
    for (guint i = 0; i < map_size; i++) {
        if (gst_format_map[i].gst_fmt == gst_format) {
            stride = gst_format_map[i].stride;
            break;
        }
    }
    return (stride == -1) ? hstride : (hstride / stride);
}

const char* gst_es_mpp_format_to_string(MppFrameFormat pix_fmt) {
    switch (pix_fmt) {
        case MPP_FMT_NV12:
            return "nv12";
        case MPP_FMT_NV21:
            return "nv21";
        case MPP_FMT_I420:
            return "i420";
        case MPP_FMT_YV12:
            return "yv12";
        case MPP_FMT_YUY2:
            return "YUY2";
        case MPP_FMT_YVY2:
            return "yvy2";
        case MPP_FMT_UYVY:
            return "uyvy";
        case MPP_FMT_VYUY:
            return "vyuy";
        case MPP_FMT_I010:
            return "i010";
        case MPP_FMT_P010:
            return "p010";
        default:
            return "unknown pixel format";
    }
}

gboolean gst_es_video_info_align(GstVideoInfo* info, gint hstride, gint vstride) {
    GstVideoAlignment align;
    guint stride = 0;
    guint i = 0;
    gint h_stride = 0;
    gint v_stride = 0;

    if (0 == hstride) {
        h_stride = GST_ES_ALIGN(GST_ES_VIDEO_INFO_HSTRIDE(info));
    } else {
        h_stride = hstride;
    }
    if (0 == vstride) {
        v_stride = GST_ES_ALIGN(GST_ES_VIDEO_INFO_VSTRIDE(info));
    } else {
        v_stride = vstride;
    }

    gst_video_alignment_reset(&align);
    align.padding_bottom = v_stride - GST_VIDEO_INFO_HEIGHT(info);
    if (!gst_video_info_align(info, &align)) {
        return FALSE;
    }
    if (GST_VIDEO_INFO_N_PLANES(info) == 1) {
        GST_VIDEO_INFO_SIZE(info) = GST_VIDEO_INFO_PLANE_STRIDE(info, 0) * v_stride;
    }
    if (GST_VIDEO_INFO_PLANE_STRIDE(info, 0) == h_stride) {
        return TRUE;
    }

    stride = GST_VIDEO_INFO_PLANE_STRIDE(info, 0);
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES(info); i++) {
        GST_VIDEO_INFO_PLANE_STRIDE(info, i) = GST_VIDEO_INFO_PLANE_STRIDE(info, i) * h_stride / stride;
        GST_VIDEO_INFO_PLANE_OFFSET(info, i) = GST_VIDEO_INFO_PLANE_OFFSET(info, i) / stride * h_stride;
    }
    GST_VIDEO_INFO_SIZE(info) = GST_VIDEO_INFO_SIZE(info) / stride * h_stride;

    return TRUE;
}

static gboolean plugin_init(GstPlugin* plugin) {
    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esplugin", 0, "ESWIN video plugin");
    gst_es_h264_enc_register(plugin, GST_RANK_PRIMARY + 1);
    gst_es_h265_enc_register(plugin, GST_RANK_PRIMARY + 1);
    gst_es_video_dec_register(plugin, GST_RANK_PRIMARY + 1);
    gst_es_jpeg_enc_register(plugin, GST_RANK_PRIMARY + 1);
    gst_es_jpeg_dec_register(plugin, GST_RANK_PRIMARY + 1);
    return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "esmppcodecplugin"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "www.eswincomputing.com"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  esmppcodec,
                  "Eswin Video Codec Plugin",
                  plugin_init,
                  "1.0",
                  "Proprietary",
                  "esmppcodec",
                  GST_PACKAGE_ORIGIN)
