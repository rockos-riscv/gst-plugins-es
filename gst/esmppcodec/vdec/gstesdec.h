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

#ifndef __GST_ES_DEC_H__
#define __GST_ES_DEC_H__

#include <gst/video/gstvideodecoder.h>

#include "gstesmppplugin.h"
#include "mpp_vdec_cfg.h"

G_BEGIN_DECLS;

#define GST_TYPE_ES_DEC (gst_es_dec_get_type())
#define GST_ES_DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_DEC, GstEsDec))
#define GST_ES_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ES_DEC, GstEsDecClass))
typedef struct _GstEsDec GstEsDec;
typedef struct _GstEsDecClass GstEsDecClass;

#define GST_SEND_PACKET_SUCCESS (0)
#define GST_SEND_PACKET_BAD (1)
#define GST_SEND_PACKET_TIMEOUT (2)
#define GST_SEND_PACKET_FAIL (-1)

struct _GstEsDec {
    GstVideoDecoder parent;

    MppCodingType mpp_coding_type;
    MppCtxPtr mpp_ctx;
    MppDecCfgPtr mpp_dec_cfg;
    MppParamPtr mpp_param;
    MppBufferGroupPtr buf_grp;

    GMutex mutex;
    GstAllocator *allocator;
    GstVideoCodecState *input_state;
    GstVideoInfo gst_info;

    GstVideoFormat out_format; /* config output format */
    gint out_width;            /* config output width */
    gint out_height;           /* config output height */
    gint extra_hw_frames;      /* config extra hardware frame buffer count*/
    guint crop_x;              /* config crop x */
    guint crop_y;              /* config crop y */
    guint crop_w;              /* config crop w */
    guint crop_h;              /* config crop h */
    guint stride_align;        /* config output stride align */
    gboolean buf_cache;        /* config the buffer cache mode */
    gboolean memset_output;    /* config if memset padding buffer */

    gboolean is_flushing;
    gboolean is_draining;
    GstFlowReturn return_code;
    guint32 frame_cnt;

    gboolean found_valid_pts;
    GstStateChange gst_state;
};

struct _GstEsDecClass {
    GstVideoDecoderClass parent_class;
    gboolean (*set_extra_data)(GstVideoDecoder *decoder);
    MppPacketPtr (*prepare_mpp_packet)(GstVideoDecoder *decoder, GstMapInfo *mapinfo);
    gint (*send_mpp_packet)(GstVideoDecoder *decoder, MppPacketPtr mpkt, gint timeout_ms);
    MppFramePtr (*get_mpp_frame)(GstVideoDecoder *decoder, gint timeout_ms);
    gboolean (*shutdown)(GstVideoDecoder *decoder, gboolean drain);
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstEsDec, gst_object_unref);
GType gst_es_dec_get_type(void);

#define ES_DEC_FORMATS "NV12, NV21, I420, GRAY8, P010LE, BGR, RGB, BGRA, RGBA, BGRx, RGBx"

G_END_DECLS;

#endif