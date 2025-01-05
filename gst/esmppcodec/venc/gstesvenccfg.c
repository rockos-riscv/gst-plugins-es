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

#include "gstesvenccfg.h"
#include <es_mpp_rc.h>
#include <es_mpp_video.h>
#include <es_venc_def.h>
#include "gstesvenc_comm.h"

GST_DEBUG_CATEGORY_STATIC(es_venc_cfg);
#define GST_CAT_DEFAULT es_venc_cfg

static VENC_RC_MODE_E gst_es_venc_cfg_get_rc_mode(int rc_mode, MppCodingType codec_type);
static int encoder_get_crop(char *str, RECT_S *rect);

#define CFG_SET_S32(cfg, cfgstr, value)               \
    do {                                              \
        mpp_enc_cfg_set_s32(cfg, cfgstr, value);      \
        GST_INFO("%s is set to %d\n", cfgstr, value); \
    } while (0)

#define CFG_SET_U32(cfg, cfgstr, value)               \
    do {                                              \
        mpp_enc_cfg_set_u32(cfg, cfgstr, value);      \
        GST_INFO("%s is set to %u\n", cfgstr, value); \
    } while (0)

#define CFG_SET_S32_IF_USER_SET(cfg, cfgstr, value, unset_value) \
    do {                                                         \
        if (value != unset_value) {                              \
            mpp_enc_cfg_set_s32(cfg, cfgstr, value);             \
            GST_INFO("%s is set to %d\n", cfgstr, value);        \
        }                                                        \
    } while (0)

#define CFG_SET_U32_IF_USER_SET(cfg, cfgstr, value, unset_value) \
    do {                                                         \
        if (value != unset_value) {                              \
            mpp_enc_cfg_set_u32(cfg, cfgstr, value);             \
            GST_INFO("%s is set to %u\n", cfgstr, value);        \
        }                                                        \
    } while (0)

void gst_es_venc_cfg_set_venc(MppEncCfgPtr cfg, GstEsVencParam *param, MppCodingType codec_type) {
    guint plane = 0, offset[3] = {0}, stride[3] = {0};
    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "es_venc_cfg", 0, "es_venc_cfg");
    // mpp_enc_cfg_set_s32(cfg, "venc:codec_type", avctx->code_type);
    CFG_SET_S32(cfg, "venc:width", param->width);
    CFG_SET_S32(cfg, "venc:height", param->height);
    CFG_SET_U32(cfg, "venc:pixel_format", param->pix_fmt);
    CFG_SET_S32_IF_USER_SET(cfg, "venc:align", param->stride_align, -1);
    gst_es_venc_get_picbufinfo(param->pix_fmt,
                               param->width,
                               param->height,
                               param->stride_align > 0 ? param->stride_align : 1,
                               param->ver_stride > 0 ? param->stride_align : 1,
                               stride,
                               offset,
                               &plane);

    CFG_SET_S32(cfg, "venc:hor_stride", stride[0]);
    CFG_SET_S32(cfg, "venc:ver_stride", FFALIGN(param->height, param->ver_stride > 0 ? param->stride_align : 1));

    if (MPP_VIDEO_CodingAVC == codec_type || MPP_VIDEO_CodingHEVC == codec_type) {
        CFG_SET_S32_IF_USER_SET(cfg, "venc:bit_depth", param->bitdepth, -1);

        if (param->enable_deblocking > -1) {
            CFG_SET_U32(cfg, "dblk:dblk_disable", (param->enable_deblocking == 0) ? 1 : 0);
        }

        CFG_SET_S32(cfg, "venc:profile", param->profile);
        CFG_SET_S32(cfg, "venc:level", param->level);
        if (MPP_VIDEO_CodingAVC == codec_type) {
            CFG_SET_S32_IF_USER_SET(cfg, "h264:cabac", param->enable_cabac, -1);
        } else {
            CFG_SET_S32_IF_USER_SET(cfg, "venc:tier", param->tier, 0);
        }
    }

    CFG_SET_S32_IF_USER_SET(cfg, "vui:colorspace", param->color_space, -1);
    CFG_SET_S32_IF_USER_SET(cfg, "vui:colorprim", param->color_primaries, -1);
    CFG_SET_S32_IF_USER_SET(cfg, "vui:colortrc", param->color_trc, -1);
}

static VENC_GOP_MODE_E gst_es_venc_cfg_get_gop_mode(MPP_ENC_GOP_MODE gop_mode) {
    VENC_GOP_MODE_E mode = VENC_GOPMODE_BUTT;
    switch (gop_mode) {
        case MPP_ENC_GOP_MODE_NORMALP:
            mode = VENC_GOPMODE_NORMALP;
            break;
        case MPP_ENC_GOP_MODE_DUALREF:
            mode = VENC_GOPMODE_DUALREF;
            break;
        case MPP_ENC_GOP_MODE_SMARTREF:
            mode = VENC_GOPMODE_SMARTREF;
            break;
        case MPP_ENC_GOP_MODE_ADVSMARTREF:
            mode = VENC_GOPMODE_ADVSMARTREF;
            break;
        case MPP_ENC_GOP_MODE_BIPREDB:
            mode = VENC_GOPMODE_BIPREDB;
            break;
        case MPP_ENC_GOP_MODE_LOWDELAYB:
            mode = VENC_GOPMODE_LOWDELAYB;
            break;
        default:
            break;
    }

    return mode;
}

void gst_es_venc_cfg_set_venc_gop(MppEncCfgPtr cfg, GstEsVencParam *param, MppCodingType codec_type) {
    if (MPP_VIDEO_CodingAVC == codec_type || MPP_VIDEO_CodingHEVC == codec_type) {
        if (param->gop_mode == MPP_ENC_GOP_MODE_BUTT) {
            return;
        }
        VENC_GOP_MODE_E gop_mode = gst_es_venc_cfg_get_gop_mode(param->gop_mode);
        CFG_SET_S32_IF_USER_SET(cfg, "gop:gop_mode", gop_mode, VENC_GOPMODE_BUTT);
        switch (gop_mode) {
            case VENC_GOPMODE_NORMALP: {
                CFG_SET_S32(cfg, "normalp:ip_qp_delta", param->ip_qp_delta);
                break;
            }
            case VENC_GOPMODE_DUALREF: {
                CFG_SET_S32(cfg, "dualp:sb_interval", param->sb_interval);
                CFG_SET_S32(cfg, "dualp:sp_qp_delta", param->sp_qp_delta);
                CFG_SET_S32(cfg, "dualp:ip_qp_delta", param->ip_qp_delta);
                break;
            }
            case VENC_GOPMODE_SMARTREF: {
                CFG_SET_S32(cfg, "smart:bg_interval", param->bg_interval);
                CFG_SET_S32(cfg, "smart:bg_qp_delta", param->bg_qp_delta);
                CFG_SET_S32(cfg, "smart:vi_qp_delta", param->vi_qp_delta);
                break;
            }
            case VENC_GOPMODE_ADVSMARTREF: {
                CFG_SET_S32(cfg, "advance:bg_interval", param->bg_interval);
                CFG_SET_S32(cfg, "advance:bg_qp_delta", param->bg_qp_delta);
                CFG_SET_S32(cfg, "advance:vi_qp_delta", param->vi_qp_delta);
                break;
            }
            case VENC_GOPMODE_BIPREDB: {
                CFG_SET_S32(cfg, "bipredb:b_frm_num", param->b_frm_num);
                CFG_SET_S32(cfg, "bipredb:b_qp_delta", param->b_qp_delta);
                CFG_SET_S32(cfg, "bipredb:ip_qp_delta", param->ip_qp_delta);
                break;
            }
            case VENC_GOPMODE_LOWDELAYB: {
                CFG_SET_S32(cfg, "lowdelayb:b_frm_num", param->b_frm_num);
                CFG_SET_S32(cfg, "lowdelayb:i_qp_delta", param->i_qp_delta);
                break;
            }
            default:
                GST_WARNING("gop_mode is set to %d\n", param->gop_mode);
                break;
        }
    }
}

static VENC_RC_MODE_E gst_es_venc_cfg_get_rc_mode(int rc_mode, MppCodingType codec_type) {
    VENC_RC_MODE_E mode = VENC_RC_MODE_BUTT;
    switch (rc_mode) {
        case MPP_ENC_RC_MODE_CBR: {
            if (codec_type == MPP_VIDEO_CodingAVC) {
                mode = VENC_RC_MODE_H264CBR;
            } else if (codec_type == MPP_VIDEO_CodingHEVC) {
                mode = VENC_RC_MODE_H265CBR;
            } else if (codec_type == MPP_VIDEO_CodingMJPEG) {
                mode = VENC_RC_MODE_MJPEGCBR;
            }
            break;
        }
        case MPP_ENC_RC_MODE_VBR: {
            if (codec_type == MPP_VIDEO_CodingAVC) {
                mode = VENC_RC_MODE_H264VBR;
            } else if (codec_type == MPP_VIDEO_CodingHEVC) {
                mode = VENC_RC_MODE_H265VBR;
            } else if (codec_type == MPP_VIDEO_CodingMJPEG) {
                mode = VENC_RC_MODE_MJPEGVBR;
            }
            break;
        }
        case MPP_ENC_RC_MODE_FIXQP: {
            if (codec_type == MPP_VIDEO_CodingAVC) {
                mode = VENC_RC_MODE_H264FIXQP;
            } else if (codec_type == MPP_VIDEO_CodingHEVC) {
                mode = VENC_RC_MODE_H265FIXQP;
            } else if (codec_type == MPP_VIDEO_CodingMJPEG) {
                mode = VENC_RC_MODE_MJPEGFIXQP;
            }
            break;
        }
        case MPP_ENC_RC_MODE_QPMAP: {
            if (codec_type == MPP_VIDEO_CodingAVC) {
                mode = VENC_RC_MODE_H264QPMAP;
            } else if (codec_type == MPP_VIDEO_CodingHEVC) {
                mode = VENC_RC_MODE_H265QPMAP;
            }
            break;
        }
        default:
            break;
    }

    return mode;
}

static unsigned int gst_framerate_to_es_framerate(int fps_d, int fps_n) {
    unsigned int framerate = 30;

    if (fps_d > 1) {
        fps_d = fps_d << 16;
        framerate = fps_d | fps_n;
    } else {
        framerate = fps_n;
    }

    return framerate;
}

void gst_es_venc_cfg_set_venc_rc(MppEncCfgPtr cfg, GstEsVencParam *param, MppCodingType codec_type) {
    if (codec_type == MPP_VIDEO_CodingAVC || codec_type == MPP_VIDEO_CodingHEVC
        || codec_type == MPP_VIDEO_CodingMJPEG) {
        VENC_RC_MODE_E rc_mode;
        unsigned int bitrate;

        CFG_SET_U32(cfg, "rc:gop", param->gop);
        CFG_SET_U32_IF_USER_SET(
            cfg, "rc:dst_frame_rate", gst_framerate_to_es_framerate(param->fps_d, param->fps_n), -1);

        rc_mode = gst_es_venc_cfg_get_rc_mode(param->rc_mode, codec_type);
        if (rc_mode < VENC_RC_MODE_H264CBR || rc_mode >= VENC_RC_MODE_BUTT) {
            GST_WARNING("unsupported rc:mode %d", rc_mode);
            return;
        }

        CFG_SET_S32(cfg, "rc:mode", rc_mode);

        switch (rc_mode) {
            case VENC_RC_MODE_H264CBR:
            case VENC_RC_MODE_H265CBR: {
                CFG_SET_U32(cfg, "cbr:bitrate", param->bitrate);
                if (param->cpb_size == -1) {
                    param->cpb_size = param->bitrate * 1.25;
                }
                CFG_SET_U32(cfg, "cbr:cpb_size", (unsigned int)param->cpb_size);
                CFG_SET_U32(cfg, "rc:stat_time", param->stat_time);
                CFG_SET_S32(cfg, "rc_adv:first_frame_start_qp", param->start_qp);

                CFG_SET_S32_IF_USER_SET(cfg, "cbr_adv:iprop", param->qp_init, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "cbr_adv:max_qp", param->qp_max, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "cbr_adv:min_qp", param->qp_min, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "cbr_adv:max_iqp", param->qp_max_i, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "cbr_adv:min_iqp", param->qp_min_i, -1);

                break;
            }
            case VENC_RC_MODE_H264VBR:
            case VENC_RC_MODE_H265VBR: {
                bitrate = (unsigned int)(param->max_bitrate);
                CFG_SET_U32(cfg, "vbr:max_bitrate", bitrate);
                CFG_SET_U32(cfg, "rc:stat_time", param->stat_time);
                CFG_SET_S32(cfg, "rc_adv:first_frame_start_qp", param->start_qp);

                CFG_SET_S32_IF_USER_SET(cfg, "vbr_adv:iprop", param->qp_init, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "vbr_adv:max_qp", param->qp_max, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "vbr_adv:min_qp", param->qp_min, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "vbr_adv:max_iqp", param->qp_max_i, -1);
                CFG_SET_S32_IF_USER_SET(cfg, "vbr_adv:min_iqp", param->qp_min_i, -1);
                break;
            }
            case VENC_RC_MODE_H264FIXQP:
            case VENC_RC_MODE_H265FIXQP: {
                CFG_SET_U32(cfg, "fixqp:iqp", param->iqp);
                CFG_SET_U32(cfg, "fixqp:pqp", param->pqp);
                CFG_SET_U32(cfg, "fixqp:bqp", param->bqp);
                break;
            }
            case VENC_RC_MODE_MJPEGCBR: {
                bitrate = (unsigned int)(param->bitrate);
                CFG_SET_U32(cfg, "cbr:bitrate", bitrate);
                CFG_SET_U32(cfg, "rc:stat_time", param->stat_time);
                CFG_SET_U32_IF_USER_SET(cfg, "cbr_adv:max_qfactor", param->qfactor_max, -1);
                CFG_SET_U32_IF_USER_SET(cfg, "cbr_adv:min_qfactor", param->qfactor_min, -1);
                break;
            }
            case VENC_RC_MODE_MJPEGVBR: {
                bitrate = (unsigned int)(param->max_bitrate);
                CFG_SET_U32(cfg, "vbr:max_bitrate", bitrate);
                CFG_SET_U32(cfg, "rc:stat_time", param->stat_time);
                CFG_SET_U32_IF_USER_SET(cfg, "vbr_adv:max_qfactor", param->qfactor_max, -1);
                CFG_SET_U32_IF_USER_SET(cfg, "vbr_adv:min_qfactor", param->qfactor_min, -1);
                break;
            }
            case VENC_RC_MODE_MJPEGFIXQP: {
                CFG_SET_U32_IF_USER_SET(cfg, "fixqp:qfactor", param->qfactor, -1);
                break;
            }

            default:
                GST_WARNING("rc_mode is set to %d", param->rc_mode);
                return;
        }
    }
}

/* parse crop str*/
static int encoder_get_crop(char *str, RECT_S *rect) {
    char *p;

    if (!str || !rect) {
        return -1;
    }
    if (((p = strstr(str, "cx")) == NULL) && ((p = strstr(str, "cy")) == NULL) && ((p = strstr(str, "cw")) == NULL)
        && ((p = strstr(str, "ch")) == NULL)) {
        return 0;
    }

    if ((p = strstr(str, "cx")) != NULL) {
        rect->x = atoi(p + 3);
    } else {
        return -1;
    }

    if ((p = strstr(str, "cy")) != NULL) {
        rect->y = atoi(p + 3);
    } else {
        return -1;
    }

    if ((p = strstr(str, "cw")) != NULL) {
        rect->width = atoi(p + 3);
    } else {
        return -1;
    }

    if ((p = strstr(str, "ch")) != NULL) {
        rect->height = atoi(p + 3);
    } else {
        return -1;
    }
    GST_INFO("rect[x:%d,y:%d,w:%u,y:%u]\n", rect->x, rect->y, rect->width, rect->height);

    return 0;
}

static ROTATION_E encoder_get_rotation(int ratation) {
    switch (ratation) {
        case 0:
            return ROTATION_0;
        case 1:
            return ROTATION_90;
        case 2:
            return ROTATION_180;
        case 3:
            return ROTATION_270;
        default:
            return ROTATION_BUTT;
    }
}

void gst_es_venc_cfg_set_venc_pp(MppEncCfgPtr cfg, GstEsVencParam *param, MppCodingType codec_type) {
    GST_INFO("codec_type:%d\n ", codec_type);
    if (MPP_VIDEO_CodingAVC == codec_type || MPP_VIDEO_CodingHEVC == codec_type
        || MPP_VIDEO_CodingMJPEG == codec_type) {
        if (param->rotation != -1) {
            CFG_SET_S32(cfg, "pp:rotation", encoder_get_rotation(param->rotation));
        }
        if (!param->crop_str) {
            GST_INFO("no crop_str to set\n ");
            return;
        }
        if (strlen(param->crop_str) >= 12) {
            RECT_S rect;
            if (encoder_get_crop(param->crop_str, &rect)) {
                GST_WARNING("Crop params error %s\n", param->crop_str);
            } else {
                mpp_enc_cfg_set_s32(cfg, "pp:enable", 1);
                mpp_enc_cfg_set_st(cfg, "pp:rect", (void *)&rect);
                GST_INFO("Crop ret is set to %s\n", param->crop_str);
            }
        }
    }
    GST_INFO("gst_es_venc_cfg_set_venc_pp done\n ");
}

int ges_es_venc_support_pix_fmt(MppFrameFormat pix_fmt) {
    switch (pix_fmt) {
        case MPP_FMT_NV12:
        case MPP_FMT_NV21:
        case MPP_FMT_I420:
        case MPP_FMT_YV12:  // planar 4:2:0 YUV, Same as I420 but with U and V planes swapped
        case MPP_FMT_YUY2:  // packed 4:2:2 YUV, |Y0|U0|Y1|V0| |Y2|U2|Y3|V2| ...
        case MPP_FMT_UYVY:  // packed 4:2:2 YUV, |U0|Y0|V0|Y1| |U2|Y2|V2|Y3| ...
        case MPP_FMT_I010:  // yuv420 planar, 10bit
        case MPP_FMT_P010:  // nv12, 10bit
            return 1;
        default:
            return 0;
    }
}

void gst_es_venc_cfg_set_default(GstEsVencParam *param) {
    param->stride_align = -1;
    param->bitdepth = 8;
    param->enable_cabac = -1;
    param->rotation = DEFAULT_PROP_ROTATION;
    param->crop_str = NULL;
    param->profile = -1;
    param->level = -1;
    param->ver_stride = -1;

    param->rc_mode = DEFAULT_PROP_RC_MODE;
    param->gop = DEFAULT_PROP_GOP;
    param->stat_time = 1;
    param->start_qp = -1;
    param->bitrate = DEFAULT_BITRATE;
    param->max_bitrate = DEFAULT_MAX_BITRATE;
    param->cpb_size = -1;
    param->iqp = 30;
    param->pqp = 32;
    param->bqp = 32;

    param->qp_init = -1;
    param->qp_max = -1;
    param->qp_min = -1;
    param->qp_max_i = -1;
    param->qp_min_i = -1;

    param->qfactor = -1;
    param->qfactor_max = -1;
    param->qfactor_min = -1;

    param->gop_mode = VENC_GOPMODE_NORMALP;
    param->ip_qp_delta = 2;
    param->sb_interval = 0;
    param->sp_qp_delta = 0;
    param->bg_interval = -1;
    param->bg_qp_delta = 5;
    param->vi_qp_delta = 3;
    param->b_frm_num = 2;
    param->b_qp_delta = 0;
    param->i_qp_delta = 2;

    param->enable_deblocking = 0;
    param->color_space = -1;
    param->color_primaries = -1;
    param->color_trc = -1;
}