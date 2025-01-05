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

#ifndef __GST_ES_VENC_CFG_H__
#define __GST_ES_VENC_CFG_H__

#include <gst/gst.h>
#include <mpp_venc_cfg.h>
#include <mpp_frame.h>

G_BEGIN_DECLS

typedef enum _MPP_ENC_RC_MODE {
    MPP_ENC_RC_MODE_CBR = 0,
    MPP_ENC_RC_MODE_VBR,
    MPP_ENC_RC_MODE_FIXQP,
    MPP_ENC_RC_MODE_QPMAP,
    MPP_ENC_RC_MODE_BUTT,
} MPP_ENC_RC_MODE;

typedef enum {
    GST_ES_VENC_ROTATION_0,
    GST_ES_VENC_ROTATION_90,
    GST_ES_VENC_ROTATION_180,
    GST_ES_VENC_ROTATION_270,
    GST_ES_VENC_ROTATION_LAST
} GstEsVencRotation;

typedef enum _MPP_ENC_GOP_MODE {
    MPP_ENC_GOP_MODE_NORMALP = 0,
    MPP_ENC_GOP_MODE_DUALREF,
    MPP_ENC_GOP_MODE_SMARTREF,
    MPP_ENC_GOP_MODE_ADVSMARTREF,
    MPP_ENC_GOP_MODE_BIPREDB,
    MPP_ENC_GOP_MODE_LOWDELAYB,
    MPP_ENC_GOP_MODE_BUTT,
} MPP_ENC_GOP_MODE;

typedef struct {
    // common setting
    gint width;
    gint height;
    gint ver_stride;
    MppFrameFormat pix_fmt;
    gint fps_n;
    gint fps_d;

    gint profile;
    gint tier;
    gint level;
    gint stride_align;
    gint bitdepth;
    gint enable_cabac;

    // preprocessing setting
    gint rotation;
    gchar* crop_str;  // cx:N,cy:N,cw:N,ch:N, mean crop xoffset,yoffset,out_width,out_heigh

    // rc setting
    MPP_ENC_RC_MODE rc_mode;
    gint gop;
    gint stat_time;     // [1, 60]; the rate statistic time,  unit is sec
    gint start_qp;
    guint bitrate;      // kbps
    guint max_bitrate;  // VBR
    gint cpb_size;
    gint iqp;
    gint pqp;
    gint bqp;

    gint qp_init;
    gint qp_max;
    gint qp_min;
    gint qp_max_i;
    gint qp_min_i;

    // mjpeg
    gint qfactor;
    gint qfactor_max;
    gint qfactor_min;

    // gop setting
    MPP_ENC_GOP_MODE gop_mode;
    gint ip_qp_delta;
    gint sb_interval;
    gint sp_qp_delta;
    gint bg_interval;
    gint bg_qp_delta;
    gint vi_qp_delta;
    gint b_frm_num;
    gint b_qp_delta;
    gint i_qp_delta;

    // protocal
    gint enable_deblocking;
    MppFrameColorSpace color_space;
    MppFrameColorTransferCharacteristic color_trc;
    MppFrameColorPrimaries color_primaries;
} GstEsVencParam;

#define DEFAULT_PROP_RC_MODE MPP_ENC_RC_MODE_CBR
#define DEFAULT_BITRATE (20000)       // kbps
#define DEFAULT_MAX_BITRATE (200000)  // kbps
#define DEFAULT_PROP_ROTATION GST_ES_VENC_ROTATION_0
#define DEFAULT_STRIDE_ALIGN 1
#define DEFAULT_PROP_GOP 30
#define DEFAULT_PROP_GOP_MODE MPP_ENC_GOP_MODE_NORMALP

void gst_es_venc_cfg_set_default(GstEsVencParam* param);

void gst_es_venc_cfg_set_venc(MppEncCfgPtr cfg, GstEsVencParam* param, MppCodingType codec_type);
void gst_es_venc_cfg_set_venc_gop(MppEncCfgPtr cfg, GstEsVencParam* param, MppCodingType codec_type);
void gst_es_venc_cfg_set_venc_rc(MppEncCfgPtr cfg, GstEsVencParam* param, MppCodingType codec_type);
void gst_es_venc_cfg_set_venc_pp(MppEncCfgPtr cfg, GstEsVencParam* param, MppCodingType codec_type);
int ges_es_venc_support_pix_fmt(MppFrameFormat pix_fmt);

G_END_DECLS

#endif
