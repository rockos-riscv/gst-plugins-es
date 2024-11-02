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

#ifndef __GST_ES_VENC_H__
#define __GST_ES_VENC_H__

#include <gst/video/gstvideoencoder.h>
#include "gstesmppplugin.h"
#include <mpp_venc_cfg.h>
#include <mpp_frame.h>
#include <es_venc_def.h>
#include <es_mpp_rc.h>
#include "gstesvenccfg.h"

G_BEGIN_DECLS;

#define GST_TYPE_ES_VENC (gst_es_venc_get_type())
G_DECLARE_FINAL_TYPE(GstEsVenc, gst_es_venc, GST, ES_VENC, GstVideoEncoder);

#define GST_ES_VENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_VENC, GstEsVenc))

struct _GstEsVenc {
    GstVideoEncoder parent;

    MppCodingType mpp_type;
    MppCtxPtr ctx;
    MppEncCfgPtr mcfg;
    MppFramePtr mpp_frame;

    GMutex mutex;
    GstAllocator *allocator;
    GstVideoCodecState *input_state;
    GstVideoInfo info;      /* final input video info */
    GstFlowReturn task_ret; /* flow return from pad task */

    guint pending_frames;
    GMutex event_mutex;
    GCond event_cond;

    gboolean flushing; /* stop handling new frame when flushing */
    gboolean draining; /* drop frames when flushing but not draining */
    gboolean prop_dirty;
    gboolean zero_copy_pkt;
    gboolean eos;

    guint *extradata;
    gint extradata_size;

    GstEsVencParam params;
};

/*
 * "NV12" planar 4:2:0 YUV with interleaved UV plane
 * "NV21" planar 4:2:0 YUV with interleaved VU plane
 * "I420" planar 4:2:0 YUV
 * "YV12" planar 4:2:0 YUV, Same as I420 but with U and V planes swapped
 * "YUY2" packed 4:2:2 YUV, |Y0|V0|Y1|U0| |Y2|V2|Y3|U2| ...
 * "UYVY" packed 4:2:2 YUV, |U0|Y0|V0|Y1| |U2|Y2|V2|Y3| ...
 * "I420_10LE" planar 4:2:0 YUV, 10 bits per channel LE
 * "P010_10LE" planar YUV 4:2:0, 24bpp, 1st plane for Y, 2nd plane for UV, 10bit store
 */
#define ES_VENC_SUPPORT_FORMATS "NV12, NV21, I420, YV12, YUY2, UYVY, I420_10LE, P010_10LE"

gboolean gst_es_venc_supported(MppCodingType coding);
gboolean gst_es_venc_video_info_align(GstVideoInfo *info);

gboolean gst_es_venc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
void gst_es_venc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
void gst_es_venc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
gboolean gst_es_enc_set_src_caps(GstVideoEncoder *encoder, GstCaps *caps);
G_END_DECLS;

#endif
