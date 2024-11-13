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
#include <stdio.h>
#include <es_mpp_cmd.h>
#include "gstesvenc.h"
#include "gstesallocator.h"
#include "gstesh264enc.h"
#include "gstesjpegenc.h"

#define GST_CAT_DEFAULT es_enc_debug
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

#define parent_class gst_es_venc_parent_class
G_DEFINE_ABSTRACT_TYPE(GstEsVenc, gst_es_venc, GST_TYPE_VIDEO_ENCODER);

#define GST_ES_VENC_TASK_STARTED(encoder) (gst_pad_get_task_state((encoder)->srcpad) == GST_TASK_STARTED)

#define GST_ES_VENC_EVENT_MUTEX(encoder) (&GST_ES_VENC(encoder)->event_mutex)
#define GST_ES_VENC_EVENT_COND(encoder) (&GST_ES_VENC(encoder)->event_cond)

#define GST_ES_VENC_BROADCAST(encoder)                 \
    g_mutex_lock(GST_ES_VENC_EVENT_MUTEX(encoder));    \
    g_cond_broadcast(GST_ES_VENC_EVENT_COND(encoder)); \
    g_mutex_unlock(GST_ES_VENC_EVENT_MUTEX(encoder));

#define GST_ES_VENC_WAIT(encoder, condition)                                            \
    g_mutex_lock(GST_ES_VENC_EVENT_MUTEX(encoder));                                     \
    while (!(condition)) {                                                              \
        g_cond_wait(GST_ES_VENC_EVENT_COND(encoder), GST_ES_VENC_EVENT_MUTEX(encoder)); \
    }                                                                                   \
    g_mutex_unlock(GST_ES_VENC_EVENT_MUTEX(encoder));

#define GST_ES_VENC_MUTEX(encoder) (&GST_ES_VENC(encoder)->mutex)
#define GST_ES_VENC_LOCK(encoder)             \
    GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder); \
    g_mutex_lock(GST_ES_VENC_MUTEX(encoder)); \
    GST_VIDEO_ENCODER_STREAM_LOCK(encoder);

#define GST_ES_VENC_UNLOCK(encoder) g_mutex_unlock(GST_ES_VENC_MUTEX(encoder));
#define MPP_PENDING_MAX 6 /* Max number of MPP pending frame */
#define H26X_HEADER_SIZE 1024

enum {
    PROP_0,
    PROP_STRIDE_ALIGN,
    PROP_RC_MODE,
    PROP_BITRATE,
    PROP_MAX_BITRATE,
    PROP_ROTATION,
    PROP_GOP,
    PROP_CROP,
    PROP_BIT_DEPTH,
    PROP_DBLK,
    PROP_STAT_TIME,
    PROP_CPB_SIZE,
    RC_IQP,
    RC_PQP,
    RC_BQP,
    RC_QP_INIT,
    RC_QP_MAX,
    RC_QP_MIN,
    RC_QP_MAXI,
    RC_QP_MINI,
    GOP_MODE,
    GOP_IP_QP_DELTA,
    GOP_BG_QP_DELTA,
    GOP_VI_QP_DELTA,
    GOP_B_QP_DELTA,
    GOP_I_QP_DELTA,
    GOP_SP_QP_DELTA,
    GOP_SB_INTERVAL,
    GOP_BG_INTERVAL,
    GOP_B_FRM_NUM,
    VUI_COLOR_SPACE,
    VUI_COLOR_PRIMARIES,
    VUI_COLOR_TRC,
};

gboolean gst_es_venc_supported(MppCodingType coding) {
    MppCtxPtr ctx = NULL;

    if (esmpp_create(&ctx, MPP_CTX_ENC, coding)) {
        return FALSE;
    }

    esmpp_destroy(&ctx);
    return TRUE;
}

static gboolean gst_es_venc_start(GstVideoEncoder *encoder) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GST_DEBUG_OBJECT(self, "starting es encoder, type=%d", self->mpp_type);

    self->allocator = gst_es_allocator_new(FALSE);
    if (!self->allocator) {
        GST_ERROR_OBJECT(self, "create allocator failed");
        return FALSE;
    }

    if (MPP_OK != esmpp_create(&self->ctx, MPP_CTX_ENC, self->mpp_type)) {
        GST_ERROR_OBJECT(self, "create esmpp failed, type=%d", self->mpp_type);
        goto err_unref_alloc;
    }

    if (MPP_OK != esmpp_init(self->ctx)) {
        GST_ERROR_OBJECT(self, "init esmpp failed, type=%d", self->mpp_type);
        goto err_destroy_mpp;
    }

    self->task_ret = GST_FLOW_OK;
    self->input_state = NULL;
    self->pending_frames = 0;
    self->flushing = FALSE;
    self->draining = FALSE;
    self->prop_dirty = FALSE;
    self->eos = FALSE;

    g_mutex_init(&self->mutex);
    g_mutex_init(&self->event_mutex);
    g_cond_init(&self->event_cond);
    GST_DEBUG_OBJECT(self, "start es encoder done");
    return TRUE;
err_destroy_mpp:
    esmpp_destroy(&self->ctx);

err_unref_alloc:
    gst_object_unref(self->allocator);
    return FALSE;
}

static gboolean gst_es_venc_stop(GstVideoEncoder *encoder) {
    GstEsVenc *self = GST_ES_VENC(encoder);

    GST_DEBUG_OBJECT(self, "stopping es encoder, type=%d", self->mpp_type);

    GST_VIDEO_ENCODER_STREAM_LOCK(encoder);

    if (self->extradata) {
        g_free(self->extradata);
        self->extradata = NULL;
        self->extradata_size = 0;
    }

    if (self->ctx) {
        esmpp_close(self->ctx);
        esmpp_deinit(self->ctx);
    }
    GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder);

    g_cond_clear(&self->event_cond);
    g_mutex_clear(&self->event_mutex);
    g_mutex_clear(&self->mutex);

    if (self->params.crop_str) {
        g_free(self->params.crop_str);
        self->params.crop_str = NULL;
    }

    if (self->mcfg) {
        mpp_enc_cfg_deinit(self->mcfg);
        self->mcfg = NULL;
    }

    esmpp_destroy(self->ctx);
    self->ctx = NULL;
    gst_object_unref(self->allocator);
    if (self->input_state) {
        gst_video_codec_state_unref(self->input_state);
        self->input_state = NULL;
    }
    self->flushing = FALSE;
    self->draining = FALSE;
    self->pending_frames = 0;

    GST_DEBUG_OBJECT(self, "stopped es encoder, type=%d", self->mpp_type);

    return TRUE;
}

static void gst_es_venc_stop_task(GstVideoEncoder *encoder, gboolean drain) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstTask *task = encoder->srcpad->task;

    if (!GST_ES_VENC_TASK_STARTED(encoder)) {
        return;
    }

    GST_DEBUG_OBJECT(self, "stopping encoding thread");

    /* Discard pending frames */
    if (!drain) {
        self->pending_frames = 0;
    }

    GST_ES_VENC_BROADCAST(encoder);

    GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder);
    /* Wait for task thread to pause */
    if (task) {
        GST_OBJECT_LOCK(task);
        while (GST_TASK_STATE(task) == GST_TASK_STARTED) {
            GST_TASK_WAIT(task);
        }
        GST_OBJECT_UNLOCK(task);
    }

    gst_pad_stop_task(encoder->srcpad);
    GST_VIDEO_ENCODER_STREAM_LOCK(encoder);
}

static void gst_es_venc_reset(GstVideoEncoder *encoder, gboolean drain, gboolean final) {
    GstEsVenc *self = GST_ES_VENC(encoder);

    GST_ES_VENC_LOCK(encoder);
    GST_DEBUG_OBJECT(self, "resetting");

    self->flushing = TRUE;
    self->draining = drain;

    gst_es_venc_stop_task(encoder, drain);

    self->flushing = final;
    self->draining = FALSE;

    // self->mpi->reset (self->mpp_ctx);
    self->task_ret = GST_FLOW_OK;
    self->pending_frames = 0;

    /* Force re-apply prop */
    self->prop_dirty = TRUE;

    GST_ES_VENC_UNLOCK(encoder);
}

static gboolean gst_es_venc_flush(GstVideoEncoder *encoder) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GST_DEBUG_OBJECT(encoder, "flushing, type=%d", self->mpp_type);
    gst_es_venc_reset(encoder, FALSE, FALSE);
    GST_WARNING_OBJECT(encoder, "TODO: flushing");

    return TRUE;
}

static gboolean gst_es_venc_finish(GstVideoEncoder *encoder) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GST_DEBUG_OBJECT(encoder, "finishing, type=%d", self->mpp_type);
    esmpp_put_frame(self->ctx, NULL);
    gst_es_venc_reset(encoder, TRUE, FALSE);

    return GST_FLOW_OK;
}

static void gst_es_venc_default_values(MppCodingType mpp_type, GstEsVencParam *params) {
    if (mpp_type == MPP_VIDEO_CodingAVC) {
        if (params->profile == -1) {
            params->profile = PROFILE_H264_HIGH;
        }
        if (params->level == -1) {
            params->level = ES_H264_LEVEL_5_1;
        }
    } else {
        if (params->profile == -1) {
            params->profile = PROFILE_H265_MAIN;
        }
        if (params->level == -1) {
            params->level = ES_HEVC_LEVEL_6;
        }
    }
}

static void gst_es_venc_cfg_codec(GstVideoEncoder *encoder, GstEsVencParam *params) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    MppPacketPtr mpp_pkt = NULL;
    // GstVideoInfo *info = &self->info;

    if (MPP_OK != mpp_enc_cfg_init(&self->mcfg)) {
        GST_ERROR_OBJECT(self, "init esmpp cfg failed, type=%d", self->mpp_type);
        return;
    }

    if (MPP_OK != esmpp_control(self->ctx, MPP_ENC_GET_CFG, self->mcfg)) {
        GST_ERROR_OBJECT(self, "get esmpp cfg failed, type=%d", self->mpp_type);
        return;
    }

    gst_es_venc_default_values(self->mpp_type, params);
    gst_es_venc_cfg_set_venc(self->mcfg, params, self->mpp_type);
    gst_es_venc_cfg_set_venc_pp(self->mcfg, params, self->mpp_type);
    gst_es_venc_cfg_set_venc_gop(self->mcfg, params, self->mpp_type);
    gst_es_venc_cfg_set_venc_rc(self->mcfg, params, self->mpp_type);

    if (MPP_OK != esmpp_control(self->ctx, MPP_ENC_SET_CFG, self->mcfg)) {
        GST_ERROR_OBJECT(self, "MPP_ENC_SET_CFG failed, type=%d", self->mpp_type);
        return;
    }

    if (MPP_OK != esmpp_open(self->ctx)) {
        GST_ERROR_OBJECT(self, "open esmpp failed, type=%d", self->mpp_type);
        return;
    }

    if (self->mpp_type == MPP_VIDEO_CodingAVC || self->mpp_type == MPP_VIDEO_CodingHEVC) {
        guint8 enc_hdr_buf[H26X_HEADER_SIZE];
        gint pkt_len = 0;
        void *pkt_pos = NULL;
        gint ret;

        memset(enc_hdr_buf, 0, H26X_HEADER_SIZE);

        if ((ret = mpp_packet_init(&mpp_pkt, (void *)enc_hdr_buf, H26X_HEADER_SIZE)) != MPP_OK || !mpp_pkt) {
            GST_ERROR_OBJECT(self, "Failed to init extra info packet: %d\n", ret);
            goto fail;
        }

        mpp_packet_set_length(mpp_pkt, 0);
        if ((ret = esmpp_control(self->ctx, MPP_ENC_GET_HDR_SYNC, mpp_pkt)) != MPP_OK) {
            GST_ERROR_OBJECT(self, "Failed to get header sync: %d\n", ret);
            goto fail;
        }

        pkt_pos = mpp_packet_get_pos(mpp_pkt);
        pkt_len = mpp_packet_get_length(mpp_pkt);
        MppBufferPtr out_mpp_buf = mpp_packet_get_buffer(mpp_pkt);
        if (out_mpp_buf) {
            GST_ERROR_OBJECT(self, "out_mpp_buf fd:%d\n", mpp_buffer_get_fd(out_mpp_buf));
        }

        if (self->extradata) {
            g_free(self->extradata);
            self->extradata = NULL;
        }
        self->extradata = g_malloc0(pkt_len);
        if (!self->extradata) {
            GST_ERROR_OBJECT(self, "malloc pps failed\n");
            goto fail;
        }
        self->extradata_size = pkt_len;
        memcpy(self->extradata, pkt_pos, pkt_len);
        GST_DEBUG_OBJECT(self, "Save extradata:%p pos:%p, size:%d\n", self->extradata, pkt_pos, pkt_len);
        mpp_packet_deinit(&mpp_pkt);
    }
    return;
fail:
    if (self->extradata) {
        g_free(self->extradata);
        self->extradata = NULL;
    }
    if (mpp_pkt) {
        mpp_packet_deinit(&mpp_pkt);
    }
    return;
}

gboolean gst_es_venc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstEsVencParam *params = &self->params;
    GstVideoInfo *info = &self->info;
    gint hstride;
    MppFrameFormat pix_fmt;

    GST_DEBUG_OBJECT(self, "setting format: %" GST_PTR_FORMAT, state->caps);

    if (self->input_state) {
        if (gst_caps_is_strictly_equal(self->input_state->caps, state->caps)) {
            return TRUE;
        }

        gst_es_venc_reset(encoder, TRUE, FALSE);
        gst_video_codec_state_unref(self->input_state);
        self->input_state = NULL;
    }

    self->input_state = gst_video_codec_state_ref(state);
    *info = state->info;
    if (!gst_es_venc_video_info_align(info)) {
        return FALSE;
    }

    pix_fmt = gst_es_gst_format_to_mpp_format(GST_VIDEO_INFO_FORMAT(info));
    if (!ges_es_venc_support_pix_fmt(pix_fmt)) {
        GST_ERROR_OBJECT(self, "does not support pix-fmt: %s", gst_es_mpp_format_to_string(pix_fmt));
        return FALSE;
    }
    params->pix_fmt = pix_fmt;
    params->width = GST_VIDEO_INFO_WIDTH(info);
    params->height = GST_VIDEO_INFO_HEIGHT(info);
    params->fps_n = GST_VIDEO_INFO_FPS_N(info);
    params->fps_d = GST_VIDEO_INFO_FPS_D(info);
    hstride = GST_VIDEO_INFO_PLANE_STRIDE(info, 0);
    if ((hstride % params->stride_align) != 0) {
        GST_ERROR_OBJECT(self,
                         "Wrong stride setting, stride=%d, hstride=%d, width=%d",
                         params->stride_align,
                         hstride,
                         params->width);
        return FALSE;
    }
    gst_es_venc_cfg_codec(encoder, params);
    GST_DEBUG_OBJECT(self, "set format done");
    return TRUE;
}

#define VENC_SET_PROPERTY(src, dst) \
    if (src == dst) {               \
        return;                     \
    }                               \
    dst = src

static gint IsPower(gint n) {
    if (n <= 0) {
        return 0;
    }

    return ((n & (n - 1)) == 0) ? 1 : 0;
}

void gst_es_venc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstEsVencParam *params = &self->params;

    switch (prop_id) {
        case PROP_STRIDE_ALIGN: {
            gint align = g_value_get_int(value);
            if (!IsPower(align)) {
                GST_WARNING_OBJECT(encoder, "Should be aligned as integer multiples of 16.");
                return;
            }
            VENC_SET_PROPERTY(align, params->stride_align);
        } break;
        case PROP_RC_MODE: {
            MPP_ENC_RC_MODE rc_mode = g_value_get_enum(value);
            VENC_SET_PROPERTY(rc_mode, params->rc_mode);
        } break;
        case PROP_BITRATE: {
            gint bitrate = g_value_get_uint(value);
            VENC_SET_PROPERTY(bitrate, params->bitrate);
        } break;
        case PROP_MAX_BITRATE: {
            gint max_bitrate = g_value_get_uint(value);
            VENC_SET_PROPERTY(max_bitrate, params->max_bitrate);
        } break;
        case PROP_ROTATION: {
            if (self->input_state) {
                GST_WARNING_OBJECT(encoder, "unable to change rotation dynamically");
                return;
            } else {
                gint rotation = g_value_get_enum(value);
                VENC_SET_PROPERTY(rotation, params->rotation);
            }
        } break;
        case PROP_GOP: {
            gint gop = g_value_get_uint(value);
            VENC_SET_PROPERTY(gop, params->gop);
        } break;
        case PROP_CROP: {
            const gchar *crop_str = g_value_get_string(value);
            if (crop_str) {
                params->crop_str = g_strdup(crop_str);
            } else {
                g_free(params->crop_str);
                params->crop_str = NULL;
            }
        } break;
        case PROP_BIT_DEPTH: {
            gint bitdepth = g_value_get_enum(value);
            VENC_SET_PROPERTY(bitdepth, params->bitdepth);
        } break;
        case PROP_DBLK: {
            gint deblk = g_value_get_int(value);
            VENC_SET_PROPERTY(deblk, params->enable_deblocking);
        } break;
        case PROP_STAT_TIME: {
            gint stat_time = g_value_get_int(value);
            VENC_SET_PROPERTY(stat_time, params->stat_time);
        } break;
        case PROP_CPB_SIZE: {
            gint cpb_size = g_value_get_int(value);
            VENC_SET_PROPERTY(cpb_size, params->cpb_size);
        } break;
        case RC_IQP: {
            gint iqp = g_value_get_int(value);
            VENC_SET_PROPERTY(iqp, params->iqp);
        } break;
        case RC_PQP: {
            gint pqp = g_value_get_int(value);
            VENC_SET_PROPERTY(pqp, params->pqp);
        } break;
        case RC_BQP: {
            gint bqp = g_value_get_int(value);
            VENC_SET_PROPERTY(bqp, params->bqp);
        } break;
        case RC_QP_INIT: {
            gint qp_init = g_value_get_int(value);
            VENC_SET_PROPERTY(qp_init, params->qp_init);
        } break;
        case RC_QP_MAX: {
            gint qp_max = g_value_get_int(value);
            VENC_SET_PROPERTY(qp_max, params->qp_max);
        } break;
        case RC_QP_MIN: {
            gint qp_min = g_value_get_int(value);
            VENC_SET_PROPERTY(qp_min, params->qp_min);
        } break;
        case RC_QP_MAXI: {
            gint qp_maxi = g_value_get_int(value);
            VENC_SET_PROPERTY(qp_maxi, params->qp_max_i);
        } break;
        case RC_QP_MINI: {
            gint qp_mini = g_value_get_int(value);
            VENC_SET_PROPERTY(qp_mini, params->qp_min_i);
        } break;
        case GOP_MODE: {
            MPP_ENC_GOP_MODE gop_mode = g_value_get_enum(value);
            VENC_SET_PROPERTY(gop_mode, params->gop_mode);
        } break;
        case GOP_IP_QP_DELTA: {
            gint ip_qp_delta = g_value_get_int(value);
            VENC_SET_PROPERTY(ip_qp_delta, params->ip_qp_delta);
        } break;
        case GOP_BG_QP_DELTA: {
            gint bg_qp_delta = g_value_get_int(value);
            VENC_SET_PROPERTY(bg_qp_delta, params->bg_qp_delta);
        } break;
        case GOP_VI_QP_DELTA: {
            gint vi_qp_delta = g_value_get_int(value);
            VENC_SET_PROPERTY(vi_qp_delta, params->vi_qp_delta);
        } break;
        case GOP_B_QP_DELTA: {
            gint b_qp_delta = g_value_get_int(value);
            VENC_SET_PROPERTY(b_qp_delta, params->b_qp_delta);
        } break;
        case GOP_I_QP_DELTA: {
            gint i_qp_delta = g_value_get_int(value);
            VENC_SET_PROPERTY(i_qp_delta, params->i_qp_delta);
        } break;
        case GOP_SP_QP_DELTA: {
            gint sp_qp_delta = g_value_get_int(value);
            VENC_SET_PROPERTY(sp_qp_delta, params->sp_qp_delta);
        } break;
        case GOP_SB_INTERVAL: {
            gint sb_interval = g_value_get_int(value);
            VENC_SET_PROPERTY(sb_interval, params->sb_interval);
        } break;
        case GOP_BG_INTERVAL: {
            gint bg_interval = g_value_get_int(value);
            VENC_SET_PROPERTY(bg_interval, params->bg_interval);
        } break;
        case GOP_B_FRM_NUM: {
            gint b_frm_num = g_value_get_int(value);
            VENC_SET_PROPERTY(b_frm_num, params->b_frm_num);
        } break;
        case VUI_COLOR_SPACE:
            MppFrameColorSpace color_space = g_value_get_enum(value);
            VENC_SET_PROPERTY(color_space, params->color_space);
        case VUI_COLOR_PRIMARIES:
            MppFrameColorPrimaries color_primaries = g_value_get_enum(value);
            VENC_SET_PROPERTY(color_primaries, params->color_primaries);
        case VUI_COLOR_TRC:
            MppFrameColorTransferCharacteristic color_trc = g_value_get_enum(value);
            VENC_SET_PROPERTY(color_trc, params->color_trc);
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    self->prop_dirty = TRUE;
}

void gst_es_venc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstEsVencParam *params = &self->params;

    switch (prop_id) {
        case PROP_STRIDE_ALIGN:
            g_value_set_int(value, params->stride_align);
            break;
        case PROP_RC_MODE:
            g_value_set_enum(value, params->rc_mode);
            break;
        case PROP_BITRATE:
            g_value_set_uint(value, params->bitrate);
            break;
        case PROP_MAX_BITRATE:
            g_value_set_uint(value, params->max_bitrate);
            break;
        case PROP_ROTATION:
            g_value_set_enum(value, params->rotation);
            break;
        case PROP_GOP:
            g_value_set_uint(value, params->gop);
            break;
        case PROP_CROP:
            g_value_set_string(value, params->crop_str);
            break;
        case PROP_BIT_DEPTH:
            g_value_set_enum(value, params->bitdepth);
            break;
        case PROP_DBLK:
            g_value_set_int(value, params->enable_deblocking);
            break;
        case PROP_STAT_TIME:
            g_value_set_int(value, params->stat_time);
            break;
        case PROP_CPB_SIZE:
            g_value_set_int(value, params->cpb_size);
            break;
        case RC_IQP:
            g_value_set_int(value, params->iqp);
            break;
        case RC_PQP:
            g_value_set_int(value, params->pqp);
            break;
        case RC_BQP:
            g_value_set_int(value, params->bqp);
            break;
        case RC_QP_INIT:
            g_value_set_int(value, params->qp_init);
            break;
        case RC_QP_MAX:
            g_value_set_int(value, params->qp_max);
            break;
        case RC_QP_MIN:
            g_value_set_int(value, params->qp_min);
            break;
        case RC_QP_MAXI:
            g_value_set_int(value, params->qp_max_i);
            break;
        case RC_QP_MINI:
            g_value_set_int(value, params->qp_min_i);
            break;
        case GOP_MODE:
            g_value_set_enum(value, params->gop_mode);
            break;
        case GOP_IP_QP_DELTA:
            g_value_set_int(value, params->ip_qp_delta);
            break;
        case GOP_BG_QP_DELTA:
            g_value_set_int(value, params->bg_qp_delta);
            break;
        case GOP_VI_QP_DELTA:
            g_value_set_int(value, params->vi_qp_delta);
            break;
        case GOP_B_QP_DELTA:
            g_value_set_int(value, params->b_qp_delta);
            break;
        case GOP_I_QP_DELTA:
            g_value_set_int(value, params->i_qp_delta);
            break;
        case GOP_SP_QP_DELTA:
            g_value_set_int(value, params->sp_qp_delta);
            break;
        case GOP_SB_INTERVAL:
            g_value_set_int(value, params->sb_interval);
            break;
        case GOP_BG_INTERVAL:
            g_value_set_int(value, params->bg_interval);
            break;
        case GOP_B_FRM_NUM:
            g_value_set_int(value, params->b_frm_num);
            break;
        case VUI_COLOR_SPACE:
            g_value_set_enum(value, params->color_space);
            break;
        case VUI_COLOR_PRIMARIES:
            g_value_set_enum(value, params->color_trc);
            break;
        case VUI_COLOR_TRC:
            g_value_set_enum(value, params->color_primaries);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

#define GST_TYPE_ES_VENC_RC_MODE (gst_es_venc_rc_mode_get_type())
static GType gst_es_venc_rc_mode_get_type(void) {
    static GType rc_mode = 0;

    if (!rc_mode) {
        static const GEnumValue rc_mode_type[] = {{MPP_ENC_RC_MODE_CBR, "Constant bitrate", "cbr"},
                                                  {MPP_ENC_RC_MODE_VBR, "Variable bitrate", "vbr"},
                                                  {MPP_ENC_RC_MODE_FIXQP, "Fixed QP", "cqp"},
                                                  {0, NULL, NULL}};
        rc_mode = g_enum_register_static("GstEsVencRcMode", rc_mode_type);
    }

    return rc_mode;
}

#define GST_TYPE_ES_VENC_ROTATION (gst_es_venc_rotation_get_type())
static GType gst_es_venc_rotation_get_type(void) {
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values[] = {{GST_ES_VENC_ROTATION_0, "Rotate 0", "0"},
                                            {GST_ES_VENC_ROTATION_90, "Rotate 90", "90"},
                                            {GST_ES_VENC_ROTATION_180, "Rotate 180", "180"},
                                            {GST_ES_VENC_ROTATION_270, "Rotate 270", "270"},
                                            {0, NULL, NULL}};
        etype = g_enum_register_static("GstEsVencRotation", values);
    }
    return etype;
}

#define GST_TYPE_ES_VENC_BITDETH (gst_es_venc_bitdepth_get_type())
static GType gst_es_venc_bitdepth_get_type(void) {
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values[] = {
            {BIT_DEPTH_8BIT, "8 bit", "8"}, {BIT_DEPTH_10BIT, "10 bit", "10"}, {0, NULL, NULL}};
        etype = g_enum_register_static("GstEsVencBitDepth", values);
    }
    return etype;
}

#define GST_TYPE_ES_VENC_GOP_MODE (gst_es_venc_gop_mode_get_type())
static GType gst_es_venc_gop_mode_get_type(void) {
    static GType gop_mode = 0;

    if (!gop_mode) {
        static const GEnumValue gop_mode_type[] = {{MPP_ENC_GOP_MODE_NORMALP, "NORMALP", "normalP"},
                                                   {MPP_ENC_GOP_MODE_DUALREF, "DUALREF", "dualRef"},
                                                   {MPP_ENC_GOP_MODE_SMARTREF, "SMARTREF", "smartRef"},
                                                   {MPP_ENC_GOP_MODE_ADVSMARTREF, "ADVSMARTREF", "advSmartRef"},
                                                   {MPP_ENC_GOP_MODE_BIPREDB, "BIPREDB", "BIPRefB"},
                                                   {MPP_ENC_GOP_MODE_LOWDELAYB, "LOWDELAYB", "lowDelayB"},
                                                   {0, NULL, NULL}};
        gop_mode = g_enum_register_static("GstEsVencGopMode", gop_mode_type);
    }

    return gop_mode;
}

#define GST_TYPE_ES_VENC_COLOR_SPACE (gst_es_venc_color_space_get_type())
static GType gst_es_venc_color_space_get_type(void) {
    static GType color_space = 0;

    if (!color_space) {
        static const GEnumValue color_space_type[] = {{0, "SPC-RGB", "SPC-RGB"},
                                                      {1, "SPC-BT709", "SPC-BT709"},
                                                      {2, "SPC-UNSPECIFIED", "SPC-UNSPECIFIED"},
                                                      {3, "SPC-RESERVED", "SPC-RESERVED"},
                                                      {4, "SPC-FCC", "SPC-FCC"},
                                                      {5, "SPC-BT470BG", "SPC-BT470BG"},
                                                      {6, "SPC-SMPTE170M", "SPC-SMPTE170M"},
                                                      {7, "SPC-SMPTE240M", "SPC-SMPTE240M"},
                                                      {8, "SPC-YCOCG", "SPC-YCOCG"},
                                                      {9, "SPC-BT2020-NCL", "SPC-BT2020-NCL"},
                                                      {10, "SPC-BT2020-CL", "SPC-BT2020-CL"},
                                                      {11, "SPC-SMPTE2085", "SPC-SMPTE2085"},
                                                      {12, "SPC-CHROMA-DERIVED-NCL", "SPC-CHROMA-DERIVED-NCL"},
                                                      {13, "SPC-CHROMA-DERIVED-CL", "SPC-CHROMA-DERIVED-CL"},
                                                      {14, "SPC-ICTCP", "SPC-ICTCP"},
                                                      {0, NULL, NULL}};
        color_space = g_enum_register_static("GstEsVencColorSpace", color_space_type);
    }

    return color_space;
}

#define GST_TYPE_ES_VENC_COLOR_TRC (gst_es_venc_color_trc_get_type())
static GType gst_es_venc_color_trc_get_type(void) {
    static GType color_trc = 0;

    if (!color_trc) {
        static const GEnumValue color_trc_type[] = {{0, "TRC-RESERVED0", "TRC-RESERVED0"},
                                                    {1, "TRC-BT709", "TRC-BT709"},
                                                    {2, "TRC-UNSPECIFIED", "TRC-UNSPECIFIED"},
                                                    {3, "TRC-RESERVED", "TRC-RESERVED"},
                                                    {4, "TRC-GAMMA22", "TRC-GAMMA22"},
                                                    {5, "TRC-GAMMA28", "TRC-GAMMA28"},
                                                    {6, "TRC-SMPTE170M", "TRC-SMPTE170M"},
                                                    {7, "TRC-SMPTE240M", "TRC-SMPTE240M"},
                                                    {8, "TRC-LINEAR", "TRC-LINEAR"},
                                                    {9, "TRC-LOG", "TRC-LOG"},
                                                    {10, "TRC-LOG-SQRT", "TRC-LOG-SQRT"},
                                                    {11, "TRC-IEC61966-2-4", "TRC-IEC61966-2-4"},
                                                    {12, "TRC-BT1361-ECG", "TRC-BT1361-ECG"},
                                                    {13, "TRC-IEC61966-2-1", "TRC-IEC61966-2-1"},
                                                    {14, "TRC-BT2020-10", "TRC-BT2020-10"},
                                                    {15, "TRC-BT2020-12", "TRC-BT2020-12"},
                                                    {16, "TRC-SMPTEST2084", "TRC-SMPTEST2084"},
                                                    {17, "TRC-SMPTEST428-1", "TRC-SMPTEST428-1"},
                                                    {18, "TRC-ARIB-STD-B67", "TRC-ARIB-STD-B67"},
                                                    {0, NULL, NULL}};
        color_trc = g_enum_register_static("GstEsVencColorTrc", color_trc_type);
    }

    return color_trc;
}

#define GST_TYPE_ES_VENC_COLOR_PRI (gst_es_venc_color_primaries_get_type())
static GType gst_es_venc_color_primaries_get_type(void) {
    static GType color_primaries = 0;

    if (!color_primaries) {
        static const GEnumValue color_primaries_type[] = {{0, "PRI-RESERVED0", "PRI-RESERVED0"},
                                                          {1, "PRI-BT709", "PRI-BT709"},
                                                          {2, "PRI-UNSPECIFIED", "PRI-UNSPECIFIED"},
                                                          {3, "PRI-RESERVED", "PRI-RESERVED"},
                                                          {4, "PRI-BT470M", "PRI-BT470M"},
                                                          {5, "PRI-BT470BG", "PRI-BT470BG"},
                                                          {6, "PRI-SMPTE170M", "PRI-SMPTE170M"},
                                                          {7, "PRI-SMPTE240M", "PRI-SMPTE240M"},
                                                          {8, "PRI-FILM", "PRI-FILM"},
                                                          {9, "PRI-BT2020", "PRI-BT2020"},
                                                          {10, "PRI-SMPTEST428-1", "PRI-SMPTEST428-1"},
                                                          {11, "PRI-SMPTE431", "PRI-SMPTE431"},
                                                          {12, "PRI-SMPTE432", "PRI-SMPTE432"},
                                                          {22, "PRI-JEDEC-P22", "PRI-JEDEC-P22"},
                                                          {0, NULL, NULL}};
        color_primaries = g_enum_register_static("GstEsVencColorPri", color_primaries_type);
    }

    return color_primaries;
}

gboolean gst_es_venc_video_info_align(GstVideoInfo *info) {
    gint vstride = 0;
    gint hstride = 0;

    /* Allow vstride aligning */
    if (!g_getenv("GST_ES_VENC_ALIGNED_VSTRIDE")) {
        vstride = GST_ES_VIDEO_INFO_VSTRIDE(info);
    }
    if (!g_getenv("GST_ES_VENC_ALIGNED_HSTRIDE")) {
        hstride = GST_ES_VIDEO_INFO_HSTRIDE(info);
    }
    return gst_es_video_info_align(info, hstride, vstride);
}

static gboolean gst_es_venc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstStructure *config, *params;
    GstVideoAlignment align;
    GstBufferPool *pool;
    GstVideoInfo info;
    GstCaps *caps;
    guint size;

    GST_DEBUG_OBJECT(self, "propose allocation");

    gst_query_parse_allocation(query, &caps, NULL);
    if (caps == NULL) {
        GST_DEBUG_OBJECT(self, "can not get caps");
        return FALSE;
    }

    if (!gst_video_info_from_caps(&info, caps)) {
        GST_ERROR_OBJECT(self, "gst_video_info_from_caps failed");
        return FALSE;
    }

    gst_es_venc_video_info_align(&info);
    size = GST_VIDEO_INFO_SIZE(&info);

    gst_video_alignment_reset(&align);
    align.padding_right = gst_es_get_pixel_stride(&info) - GST_VIDEO_INFO_WIDTH(&info);
    align.padding_bottom = GST_ES_VIDEO_INFO_VSTRIDE(&info) - GST_VIDEO_INFO_HEIGHT(&info);

    GST_DEBUG_OBJECT(self,
                     "propose allocation top:%d, b:%d, l:%d, r:%d\n",
                     align.padding_top,
                     align.padding_bottom,
                     align.padding_left,
                     align.padding_right);
    /* Expose alignment to video-meta */
    params = gst_structure_new("video-meta",
                               "padding-top",
                               G_TYPE_UINT,
                               align.padding_top,
                               "padding-bottom",
                               G_TYPE_UINT,
                               align.padding_bottom,
                               "padding-left",
                               G_TYPE_UINT,
                               align.padding_left,
                               "padding-right",
                               G_TYPE_UINT,
                               align.padding_right,
                               NULL);
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, params);
    gst_structure_free(params);

    pool = gst_video_buffer_pool_new();

    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, caps, size, 0, 0);
    gst_buffer_pool_config_set_allocator(config, self->allocator, NULL);

    /* Expose alignment to pool */
    gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment(config, &align);

    gst_buffer_pool_set_config(pool, config);

    gst_query_add_allocation_pool(query, pool, size, MPP_PENDING_MAX, 0);
    gst_query_add_allocation_param(query, self->allocator, NULL);

    gst_object_unref(pool);

    return GST_VIDEO_ENCODER_CLASS(parent_class)->propose_allocation(encoder, query);
}

static gboolean gst_es_venc_video_info_matched(GstVideoInfo *info, GstVideoInfo *other) {
    guint i;

    if (GST_VIDEO_INFO_FORMAT(info) != GST_VIDEO_INFO_FORMAT(other)) {
        return FALSE;
    }

    if (GST_VIDEO_INFO_SIZE(info) != GST_VIDEO_INFO_SIZE(other)) {
        return FALSE;
    }

    if (GST_VIDEO_INFO_WIDTH(info) != GST_VIDEO_INFO_WIDTH(other)) {
        return FALSE;
    }

    if (GST_VIDEO_INFO_HEIGHT(info) != GST_VIDEO_INFO_HEIGHT(other)) {
        return FALSE;
    }

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES(info); i++) {
        if (GST_VIDEO_INFO_PLANE_STRIDE(info, i) != GST_VIDEO_INFO_PLANE_STRIDE(other, i)) {
            return FALSE;
        }
        if (GST_VIDEO_INFO_PLANE_OFFSET(info, i) != GST_VIDEO_INFO_PLANE_OFFSET(other, i)) {
            return FALSE;
        }
    }

    return TRUE;
}

/** convert frame to hw dma buffer frame.
 *  1 hw dma buffer;
 *  2 alloc a hw dma and copy data from vir addr.
 */
static GstBuffer *gst_es_venc_convert(GstVideoEncoder *encoder, GstVideoCodecFrame *frame) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstVideoInfo src_info = self->input_state->info;
    GstVideoInfo *dst_info = &self->info;
    GstVideoFrame src_frame, dst_frame;
    GstBuffer *outbuf, *inbuf;
    GstMemory *in_mem, *out_mem;
    GstVideoMeta *meta;
    gsize size, maxsize, offset;
    guint i;

    inbuf = frame->input_buffer;

    meta = gst_buffer_get_video_meta(inbuf);
    if (meta) {
        for (i = 0; i < meta->n_planes; i++) {
            GST_VIDEO_INFO_PLANE_STRIDE(&src_info, i) = meta->stride[i];
            GST_VIDEO_INFO_PLANE_OFFSET(&src_info, i) = meta->offset[i];
            GST_DEBUG_OBJECT(self, "stride[%d]:%d, offset[%d]:0x%lx\n", i, meta->stride[i], i, meta->offset[i]);
        }
    }

    size = gst_buffer_get_sizes(inbuf, &offset, &maxsize);
    if (size < GST_VIDEO_INFO_SIZE(&src_info)) {
        GST_ERROR_OBJECT(self,
                         "input buffer too small (%" G_GSIZE_FORMAT " < %" G_GSIZE_FORMAT ")",
                         size,
                         GST_VIDEO_INFO_SIZE(&src_info));
        return NULL;
    }

    outbuf = gst_buffer_new();
    if (!outbuf) {
        goto err;
    }

    gst_buffer_copy_into(outbuf, inbuf, GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, 0);
    gst_buffer_add_video_meta_full(outbuf,
                                   GST_VIDEO_FRAME_FLAG_NONE,
                                   GST_VIDEO_INFO_FORMAT(dst_info),
                                   GST_VIDEO_INFO_WIDTH(dst_info),
                                   GST_VIDEO_INFO_HEIGHT(dst_info),
                                   GST_VIDEO_INFO_N_PLANES(dst_info),
                                   dst_info->offset,
                                   dst_info->stride);

    if (!gst_es_venc_video_info_matched(&src_info, dst_info)) {
        GST_WARNING_OBJECT(self, "output not matched\n");
        goto convert;
    }

    if (gst_buffer_n_memory(inbuf) != 1) {
        GST_ERROR_OBJECT(self, "inbuf num not = 1\n");
        goto convert;
    }

    // If input buffer is hw dma buffer, peek it to out_mem
    in_mem = gst_buffer_peek_memory(inbuf, 0);
    out_mem = gst_es_allocator_import_gst_memory(self->allocator, in_mem);
    if (!out_mem) {
        goto convert;
    }

    gst_buffer_append_memory(outbuf, out_mem);

    /* Keep a ref of the original memory */
    gst_buffer_append_memory(outbuf, gst_memory_ref(in_mem));

    GST_DEBUG_OBJECT(self, "using imported buffer");
    return outbuf;

convert:
    out_mem = gst_allocator_alloc(self->allocator, GST_VIDEO_INFO_SIZE(dst_info), NULL);
    if (!out_mem) {
        GST_ERROR_OBJECT(self, " failed gst_allocator_alloc \n");
        goto err;
    }

    GST_DEBUG_OBJECT(self, "alloc dst size:%ld \n", GST_VIDEO_INFO_SIZE(dst_info));

    gst_buffer_append_memory(outbuf, out_mem);

    if (GST_VIDEO_INFO_FORMAT(&src_info) != GST_VIDEO_INFO_FORMAT(dst_info)) {
        GST_ERROR_OBJECT(self, "dst_info invalid\n");
        goto err;
    }

    // copy src -> dst buffer.
    if (gst_video_frame_map(&src_frame, &src_info, inbuf, GST_MAP_READ)) {
        if (gst_video_frame_map(&dst_frame, dst_info, outbuf, GST_MAP_WRITE)) {
            if (!gst_video_frame_copy(&dst_frame, &src_frame)) {
                gst_video_frame_unmap(&dst_frame);
                gst_video_frame_unmap(&src_frame);
                GST_ERROR_OBJECT(self, " failed gst_video_frame_copy \n");
                goto err;
            }
            gst_video_frame_unmap(&dst_frame);
        }
        gst_video_frame_unmap(&src_frame);
    }

    GST_DEBUG_OBJECT(self, "using software converted buffer");
    return outbuf;
err:
    if (outbuf) {
        gst_buffer_unref(outbuf);
    }

    GST_ERROR_OBJECT(self, "failed to convert frame");
    return NULL;
}

static void flush_EOS_pkts(GstVideoEncoder *encoder) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    MppPacketPtr mpkt = NULL;
    gint eos = 0;
    gint frame_sys_number = 0;
    gint pkt_size = 0;
    MppFramePtr input_mpp_frame = NULL;
    MppBufferPtr out_mpp_buf = NULL;

    esmpp_get_packet(self->ctx, &mpkt, 0);
    if (mpkt) {
        eos = mpp_packet_get_eos(mpkt);
        if (eos) {
            GST_DEBUG_OBJECT(self, "encoder receive EOS packet");
            self->eos = eos ? TRUE : FALSE;
            mpp_packet_deinit(&mpkt);
            // TODO: Combine the last packet of data and send it to the next pad
            return;
        }

        if (mpp_packet_has_meta(mpkt)) {
            MppMetaPtr meta = mpp_packet_get_meta(mpkt);
            if (meta) {
                mpp_meta_get_frame(meta, KEY_INPUT_FRAME, &input_mpp_frame);
                MppMetaPtr frame_meta = mpp_frame_get_meta(input_mpp_frame);
                if (frame_meta) {
                    mpp_meta_get_s32(frame_meta, KEY_FRAME_NUMBER, &frame_sys_number);
                } else {
                    GST_ERROR_OBJECT(self, "frame's meta invalid\n");
                }
                GST_DEBUG_OBJECT(self, "input_mpp_frame :%p, index:%d \n", input_mpp_frame, frame_sys_number);
            }
        } else {
            GST_ERROR_OBJECT(self, "packet's meta invalid\n");
        }

        pkt_size = mpp_packet_get_length(mpkt);
        out_mpp_buf = mpp_packet_get_buffer(mpkt);

        if (out_mpp_buf) {
            GST_ERROR_OBJECT(
                self, "encoder has packets not flushed size:%d, fd:%d", pkt_size, mpp_buffer_get_fd(out_mpp_buf));
        }
        mpp_packet_deinit(&mpkt);
    }

    return;
}

static void gst_es_venc_loop(GstVideoEncoder *encoder) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstVideoCodecFrame *gst_frame = NULL;
    MppPacketPtr mpkt = NULL;
    MppFramePtr input_mpp_frame = NULL;
    gint ret = 0;
    gint eos = 0;

    GST_ES_VENC_WAIT(encoder, self->pending_frames || self->flushing || !self->eos);
    GST_DEBUG_OBJECT(
        self, "receive loop, pending_frames:%d flushing:%d, eos:%d\n", self->pending_frames, self->flushing, self->eos);

    GST_VIDEO_ENCODER_STREAM_LOCK(encoder);

    if (self->flushing && !self->pending_frames) {
        goto flushing;
    }
    GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder);

    ret = esmpp_get_packet(self->ctx, &mpkt, 0);
    if (ret == MPP_ERR_TIMEOUT) {
        g_usleep(10 * 1000);
    } else if (ret != MPP_OK) {
        GST_ERROR_OBJECT(self, "get packet failed! ret = %d\n", ret);
    } else if (ret == MPP_OK) {
        GstBuffer *buffer = NULL;
        GstMemory *output_gst_mem = NULL;
        MppBufferPtr out_mpp_buf = NULL;
        gint pkt_size = 0;
        gint frame_sys_number = 0;

        GST_VIDEO_ENCODER_STREAM_LOCK(encoder);
        if (!mpkt) {
            GST_ERROR_OBJECT(self, " packet is null!\n");
            goto out;
        }

        eos = mpp_packet_get_eos(mpkt);
        if (eos) {
            self->eos = eos ? TRUE : FALSE;
            GST_DEBUG_OBJECT(self, " got EOS !\n");
        }
        if (mpp_packet_has_meta(mpkt)) {
            MppMetaPtr meta = mpp_packet_get_meta(mpkt);
            if (meta) {
                mpp_meta_get_frame(meta, KEY_INPUT_FRAME, &input_mpp_frame);
                MppMetaPtr frame_meta = mpp_frame_get_meta(input_mpp_frame);
                if (frame_meta) {
                    mpp_meta_get_s32(frame_meta, KEY_FRAME_NUMBER, &frame_sys_number);
                } else {
                    GST_ERROR_OBJECT(self, "frame's meta invalid\n");
                    goto out;
                }
                GST_DEBUG_OBJECT(self, "input_mpp_frame :%p, index:%d \n", input_mpp_frame, frame_sys_number);
            }
        } else {
            GST_ERROR_OBJECT(self, "packet's meta invalid\n");
            goto out;
        }

        pkt_size = mpp_packet_get_length(mpkt);
        out_mpp_buf = mpp_packet_get_buffer(mpkt);

        gst_frame = gst_video_encoder_get_frame(encoder, frame_sys_number);
        if (!gst_frame) {
            GST_ERROR_OBJECT(self, "Failed to gst_video_encoder_get_oldest_frame ");
            goto out;
        }
        self->pending_frames--;
        if (gst_frame->output_buffer) {
            gst_buffer_unref(gst_frame->output_buffer);
        };
        GST_ES_VENC_BROADCAST(encoder);

        GST_DEBUG_OBJECT(self,
                         "pkt_size:%d, out_mpp_buf:%p gst_frame:%p, fd:%d\n",
                         pkt_size,
                         out_mpp_buf,
                         gst_frame,
                         mpp_buffer_get_fd(out_mpp_buf));

        // if out_mpp_buf == null ptr, the input frame is dropped by codec
        if (!out_mpp_buf) {
            goto drop;
        }

        if (self->zero_copy_pkt) {
            buffer = gst_buffer_new();
            if (!buffer) {
                goto error;
            }

            /* Allocated from the same DRM allocator in MPP */
            mpp_buffer_set_index(out_mpp_buf, gst_es_allocator_get_index(self->allocator));
            output_gst_mem = gst_es_allocator_import_mppbuf(self->allocator, out_mpp_buf);
            if (!output_gst_mem) {
                gst_buffer_unref(buffer);
                goto error;
            }

            gst_memory_resize(output_gst_mem, 0, pkt_size);
            gst_buffer_append_memory(buffer, output_gst_mem);
        } else {
            buffer = gst_video_encoder_allocate_output_buffer(encoder, pkt_size);
            if (!buffer) {
                goto error;
            }

            gst_buffer_fill(buffer, 0, mpp_buffer_get_ptr(out_mpp_buf), pkt_size);
        }

        // gst_buffer_replace(&gst_frame->output_buffer, buffer);
        gst_frame->output_buffer = buffer;

        if (self->flushing && !self->draining) {
            goto drop;
        }

        ret = gst_video_encoder_finish_frame(encoder, gst_frame);
        if (ret != GST_FLOW_OK) {
            GST_ERROR_OBJECT(self, "Failed to finish frame");
        }
        GST_DEBUG_OBJECT(self, "finish frame ts=%" GST_TIME_FORMAT, GST_TIME_ARGS(gst_frame->pts));
        if (mpkt) {
            mpp_packet_deinit(&mpkt);
            mpkt = NULL;
        }
    }

out:
    if (input_mpp_frame) {
        mpp_frame_deinit(&input_mpp_frame);
    }

    if (mpkt) {
        mpp_packet_deinit(&mpkt);
        mpkt = NULL;
    }

    if (gst_frame) {
        gst_video_codec_frame_unref(gst_frame);
    }

    if (self->task_ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT(self, "leaving output thread: %s", gst_flow_get_name(self->task_ret));
        gst_pad_pause_task(encoder->srcpad);
    }

    GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder);
    GST_DEBUG_OBJECT(self, "out");
    return;

flushing:
    GST_DEBUG_OBJECT(self, "flushing");
    flush_EOS_pkts(encoder);
    self->task_ret = GST_FLOW_FLUSHING;
    goto out;

error:
    GST_ERROR_OBJECT(self, "can't process this frame");
    goto drop;

drop:
    GST_DEBUG_OBJECT(self, "drop gst frame");
    gst_buffer_replace(&gst_frame->output_buffer, NULL);
    gst_video_encoder_finish_frame(encoder, gst_frame);
    goto out;
}

static GstFlowReturn gst_es_venc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstBuffer *buffer;
    GstMemory *input_gst_mem = NULL;
    GstVideoInfo *info = &self->info;
    GstEsVencParam *params = &self->params;
    MppFramePtr mpp_frame;
    MppBufferPtr in_mpp_buf = NULL;
    gboolean keyframe;
    GstFlowReturn ret = GST_FLOW_OK;
    gint dump_input = 0;
    GstVideoInfo *src_info = &self->input_state->info;
    guint stride[4] = {0}, offsets[4] = {0};

    GST_DEBUG_OBJECT(self, "handling frame[%d]", frame->system_frame_number);
    GST_ES_VENC_LOCK(encoder);
    if (G_UNLIKELY(self->flushing)) {
        goto flushing;
    }

    if (G_UNLIKELY(!GST_ES_VENC_TASK_STARTED(encoder))) {
        GST_DEBUG_OBJECT(self, "starting encoding thread");
        gst_pad_start_task(encoder->srcpad, (GstTaskFunction)gst_es_venc_loop, encoder, NULL);
    }

    if (dump_input) {
        GstMapInfo mapinfo = {0};
        gst_buffer_map(frame->input_buffer, &mapinfo, GST_MAP_READ);
        GST_INFO_OBJECT(self, "Dump buffer virtual address: %p size :%ld\n", mapinfo.data, mapinfo.size);
        FILE *file = fopen("dump_input.yuv", "wb");
        if (file == NULL) {
            GST_DEBUG_OBJECT(self, "Failed to open file for writing\n");
        }
        fwrite(mapinfo.data, 1, mapinfo.size, file);
        fclose(file);
        gst_buffer_unmap(frame->input_buffer, &mapinfo);
    }

    GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder);
    buffer = gst_es_venc_convert(encoder, frame);
    GST_VIDEO_ENCODER_STREAM_LOCK(encoder);
    if (G_UNLIKELY(!buffer)) {
        goto not_negotiated;
    }
    input_gst_mem = gst_buffer_peek_memory(buffer, 0);
    in_mpp_buf = get_mpp_buffer_from_gst_mem(input_gst_mem);
    if (!in_mpp_buf) {
        GST_ERROR_OBJECT(self, "get_mpp_buffer_from_gst_mem failed\n");
        goto drop;
    }
    for (gint i = 0; i < GST_VIDEO_INFO_N_PLANES(src_info); i++) {
        stride[i] = GST_VIDEO_INFO_PLANE_STRIDE(src_info, i);
        offsets[i] = GST_VIDEO_INFO_PLANE_OFFSET(src_info, i);
    }
    GST_DEBUG_OBJECT(self,"frame planes:%d, stride:%d,%d,%d, offset:%d,%d,%d\n",
           GST_VIDEO_INFO_N_PLANES(src_info),
           stride[0],
           stride[1],
           stride[2],
           offsets[0],
           offsets[1],
           offsets[2]);

    mpp_frame_init(&mpp_frame);
    mpp_frame_set_buffer(mpp_frame, in_mpp_buf);
    mpp_frame_set_width(mpp_frame, params->width);
    mpp_frame_set_height(mpp_frame, params->height);
    mpp_frame_set_fmt(mpp_frame, params->pix_fmt);
    // mpp_frame_set_eos(mpp_frame, 0);
    mpp_frame_set_pts(mpp_frame, frame->pts);
    mpp_frame_set_hor_stride(mpp_frame, GST_ES_VIDEO_INFO_HSTRIDE(info));
    mpp_frame_set_ver_stride(mpp_frame, GST_ES_VIDEO_INFO_VSTRIDE(info));
    mpp_frame_set_stride(mpp_frame, stride);
    mpp_frame_set_offset(mpp_frame, offsets);

    MppMetaPtr meta = mpp_frame_get_meta(mpp_frame);
    if (meta) {
        if (mpp_meta_set_s32(meta, KEY_FRAME_NUMBER, frame->system_frame_number)) {
            GST_ERROR_OBJECT(self, "mpp_meta_set_s32 failed\n ");
            goto drop;
        }
    } else {
        GST_ERROR_OBJECT(self, "No meta data from mpp_frame\n");
        goto drop;
    }

    GST_DEBUG_OBJECT(self,
                     "alloc frame:%p pix_fmt=%s, wxh:%dx%d, hor-stride:%d, framerate:%d/%d, frm_num:%d",
                     mpp_frame,
                     gst_es_mpp_format_to_string(params->pix_fmt),
                     params->width,
                     params->height,
                     GST_ES_VIDEO_INFO_HSTRIDE(info),
                     params->fps_n,
                     params->fps_d,
                     frame->system_frame_number);

    keyframe = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(frame);
    if (keyframe) {
        GST_DEBUG_OBJECT(self, "force key frame\n");
        esmpp_control(self->ctx, MPP_ENC_SET_IDR_FRAME, NULL);
    }

    /* Avoid holding too much frames */
    GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder);
    GST_ES_VENC_WAIT(encoder, self->pending_frames < MPP_PENDING_MAX || self->flushing);
    GST_VIDEO_ENCODER_STREAM_LOCK(encoder);

send_frame:
    gint val = esmpp_put_frame(self->ctx, mpp_frame);
    if (MPP_ERR_INPUT_FULL == val) {
        g_usleep(10 * 1000);
        goto send_frame;
    } else if (MPP_OK != val) {
        GST_ERROR_OBJECT(self, "esmpp_put_frame faled val:%d\n", val);
        // drop
        goto drop;
    }

    frame->output_buffer = buffer;
    self->pending_frames++;
    GST_ES_VENC_BROADCAST(encoder);
    GST_ES_VENC_UNLOCK(encoder);
    return self->task_ret;

flushing:
    GST_WARNING_OBJECT(self, "flushing");
    ret = GST_FLOW_FLUSHING;
    goto drop;
not_negotiated:
    GST_ERROR_OBJECT(self, "not negotiated");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto drop;
drop:
    GST_WARNING_OBJECT(self, "can't handle this frame:%p", frame);
    if (mpp_frame) {
        mpp_frame_deinit(&mpp_frame);
    }

    if (in_mpp_buf) {
        gst_buffer_unref(buffer);
    }

    gst_video_encoder_finish_frame(encoder, frame);

    GST_ES_VENC_UNLOCK(encoder);

    return ret;
}

static GstStateChangeReturn gst_es_venc_change_state(GstElement *element, GstStateChange transition) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(element);

    if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
        GST_VIDEO_ENCODER_STREAM_LOCK(encoder);
        gst_es_venc_reset(encoder, FALSE, TRUE);
        GST_VIDEO_ENCODER_STREAM_UNLOCK(encoder);
    }

    return GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
}

gboolean gst_es_enc_set_src_caps(GstVideoEncoder *encoder, GstCaps *caps) {
    GstEsVenc *self = GST_ES_VENC(encoder);
    GstVideoInfo *info = &self->info;
    GstVideoCodecState *output_state;

    gst_caps_set_simple(
        caps, "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH(info), "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT(info), NULL);

    GST_DEBUG_OBJECT(self, "output caps: %" GST_PTR_FORMAT, caps);

    output_state = gst_video_encoder_set_output_state(encoder, caps, self->input_state);

    GST_VIDEO_INFO_WIDTH(&output_state->info) = GST_VIDEO_INFO_WIDTH(info);
    GST_VIDEO_INFO_HEIGHT(&output_state->info) = GST_VIDEO_INFO_HEIGHT(info);
    gst_video_codec_state_unref(output_state);

    return gst_video_encoder_negotiate(encoder);
}

static void gst_es_venc_class_init(GstEsVencClass *klass) {
    GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esvenc", 0, "ES Video encoder");

    encoder_class->start = GST_DEBUG_FUNCPTR(gst_es_venc_start);
    encoder_class->stop = GST_DEBUG_FUNCPTR(gst_es_venc_stop);
    encoder_class->flush = GST_DEBUG_FUNCPTR(gst_es_venc_flush);
    encoder_class->finish = GST_DEBUG_FUNCPTR(gst_es_venc_finish);
    encoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_venc_set_format);
    encoder_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_es_venc_propose_allocation);
    encoder_class->handle_frame = GST_DEBUG_FUNCPTR(gst_es_venc_handle_frame);

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_es_venc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_es_venc_get_property);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_es_venc_change_state);

    g_object_class_install_property(gobject_class,
                                    PROP_STRIDE_ALIGN,
                                    g_param_spec_int("stride-align",
                                                     "stride-align",
                                                     "set the stride alignment of input frame, multiple of 16",
                                                     1,
                                                     4096,
                                                     DEFAULT_STRIDE_ALIGN,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_RC_MODE,
                                    g_param_spec_enum("rc-mode",
                                                      "RC mode",
                                                      "RC mode",
                                                      GST_TYPE_ES_VENC_RC_MODE,
                                                      DEFAULT_PROP_RC_MODE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_BITRATE,
                                    g_param_spec_uint("bitrate",
                                                      "bitrate",
                                                      "Encoding bitrate in unit kbps",
                                                      10,
                                                      800000,
                                                      DEFAULT_BITRATE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_MAX_BITRATE,
                                    g_param_spec_uint("max-bitrate",
                                                      "max-bitrate",
                                                      "Encoding max bitrate in unit kbps",
                                                      10,
                                                      800000,
                                                      DEFAULT_MAX_BITRATE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_ROTATION,
                                    g_param_spec_enum("rotation",
                                                      "Rotation",
                                                      "Video rotation angle",
                                                      GST_TYPE_ES_VENC_ROTATION,
                                                      DEFAULT_PROP_ROTATION,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_GOP,
                                    g_param_spec_uint("gop",
                                                      "Group of pictures",
                                                      "Group of pictures",
                                                      1,
                                                      65536,
                                                      DEFAULT_PROP_GOP,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        PROP_CROP,
        g_param_spec_string(
            "crop", "Crop Rectangle", "set the crop cx:cy:cw:ch", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_BIT_DEPTH,
                                    g_param_spec_enum("bitdepth",
                                                      "Bitdepth",
                                                      "set bitdepth",
                                                      GST_TYPE_ES_VENC_BITDETH,
                                                      BIT_DEPTH_8BIT,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_DBLK,
                                    g_param_spec_int("enable-deblock",
                                                     "enable-deblock",
                                                     "0:disable deblock, 1:enable deblock",
                                                     0,
                                                     1,
                                                     1,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_STAT_TIME,
                                    g_param_spec_int("stat-time",
                                                     "stat-time",
                                                     "rate statistics time, in seconds",
                                                     1,
                                                     60,
                                                     1,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_CPB_SIZE,
                                    g_param_spec_int("cpb-size",
                                                     "cpb-size",
                                                     "set cpb_size when rc-mode is set to CBR, suggest [1.25*bitRate]",
                                                     10,
                                                     800000,
                                                     250000,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(
        gobject_class,
        RC_IQP,
        g_param_spec_int("iqp", "iqp", "Set iqp in CQP", 0, 51, 30, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_PQP,
        g_param_spec_int("pqp", "pqp", "Set pqp in CQP", 0, 51, 32, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_BQP,
        g_param_spec_int("bqp", "bqp", "Set bqp in CQP", 0, 51, 32, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    RC_QP_INIT,
                                    g_param_spec_int("qp-init",
                                                     "qp-init",
                                                     "Set qp_init in CBR or VBR",
                                                     50,
                                                     100,
                                                     80,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_QP_MAX,
        g_param_spec_int(
            "qp-max", "qp-max", "Set qp_max in CBR or VBR", 0, 51, 51, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_QP_MIN,
        g_param_spec_int(
            "qp-min", "qp-min", "Set qp_min in CBR or VBR", 0, 51, 24, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_QP_MAXI,
        g_param_spec_int(
            "qp-maxi", "qp-maxi", "Set qp_max_i in CBR or VBR", 0, 51, 51, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_QP_MINI,
        g_param_spec_int(
            "qp-mini", "qp-mini", "Set qp_min_i in CBR or VBR", 0, 51, 24, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_MODE,
                                    g_param_spec_enum("gop-mode",
                                                      "GOP mode",
                                                      "GOP mode",
                                                      GST_TYPE_ES_VENC_GOP_MODE,
                                                      DEFAULT_PROP_GOP_MODE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_IP_QP_DELTA,
                                    g_param_spec_int("ip-qp-delta",
                                                     "ip-qp-delta",
                                                     "Set ip_qp_delta in gop mode",
                                                     -51,
                                                     51,
                                                     2,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_BG_QP_DELTA,
                                    g_param_spec_int("bg-qp-delta",
                                                     "bg-qp-delta",
                                                     "Set bg_qp_delta in gop mode",
                                                     -51,
                                                     51,
                                                     5,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_VI_QP_DELTA,
                                    g_param_spec_int("vi-qp-delta",
                                                     "vi-qp-delta",
                                                     "Set vi_qp_delta in gop mode",
                                                     -51,
                                                     51,
                                                     3,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_B_QP_DELTA,
                                    g_param_spec_int("b-qp-delta",
                                                     "b-qp-delta",
                                                     "Set b_qp_delta in gop mode",
                                                     -51,
                                                     51,
                                                     5,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_I_QP_DELTA,
                                    g_param_spec_int("i-qp-delta",
                                                     "i-qp-delta",
                                                     "Set i_qp_delta in gop mode",
                                                     -51,
                                                     51,
                                                     3,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_SP_QP_DELTA,
                                    g_param_spec_int("sp-qp-delta",
                                                     "sp-qp-delta",
                                                     "Set sp_qp_delta in gop mode",
                                                     -51,
                                                     51,
                                                     5,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_SB_INTERVAL,
                                    g_param_spec_int("sb-interval",
                                                     "sb-interval",
                                                     "Set sb_interval in gop mode",
                                                     0,
                                                     65536,
                                                     4,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_BG_INTERVAL,
                                    g_param_spec_int("bg-interval",
                                                     "bg-interval",
                                                     "Set bg_interval in gop mode",
                                                     0,
                                                     65536,
                                                     60,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    GOP_B_FRM_NUM,
                                    g_param_spec_int("b-frm-num",
                                                     "b-frm-num",
                                                     "Set b_frm_num in gop mode",
                                                     1,
                                                     3,
                                                     2,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    VUI_COLOR_SPACE,
                                    g_param_spec_enum("color-space",
                                                      "vui color space",
                                                      "vui color space",
                                                      GST_TYPE_ES_VENC_COLOR_SPACE,
                                                      MPP_FRAME_SPC_BT2020_NCL,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    VUI_COLOR_PRIMARIES,
                                    g_param_spec_enum("color-primaries",
                                                      "vui color primaries",
                                                      "vui color primaries",
                                                      GST_TYPE_ES_VENC_COLOR_PRI,
                                                      MPP_FRAME_PRI_BT2020,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    VUI_COLOR_TRC,
                                    g_param_spec_enum("color-trc",
                                                      "vui color trc",
                                                      "vui color trc",
                                                      GST_TYPE_ES_VENC_COLOR_TRC,
                                                      MPP_FRAME_TRC_SMPTE170M,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void gst_es_venc_init(GstEsVenc *self) {
    GstEsVencParam *params = &self->params;
    self->mpp_type = MPP_VIDEO_CodingUnused;

    gst_es_venc_cfg_set_default(params);
}
