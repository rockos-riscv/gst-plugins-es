
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstesallocator.h"
#include "gstesdec.h"

#define GST_CAT_DEFAULT es_dec_debug
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

#define parent_class gst_es_dec_parent_class
G_DEFINE_ABSTRACT_TYPE(GstEsDec, gst_es_dec, GST_TYPE_VIDEO_DECODER);

#define GST_ES_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_ES_DEC, GstEsDecClass))

#define OUT_TIMEOUT_MS (200)
#define IN_TIMEOUT_MS (2000)

#define DISPLAY_BUFFER_CNT (4)

#define MPP_TO_GST_PTS(pts) ((pts) * GST_MSECOND)

#define TASK_IS_STARTED(decoder) (gst_pad_get_task_state((decoder)->srcpad) == GST_TASK_STARTED)

#define GST_ES_DEC_MUTEX(decoder) (&GST_ES_DEC(decoder)->mutex)

#define GST_ES_DEC_LOCK(decoder)                  \
    do {                                          \
        GST_VIDEO_DECODER_STREAM_UNLOCK(decoder); \
        g_mutex_lock(GST_ES_DEC_MUTEX(decoder));  \
        GST_VIDEO_DECODER_STREAM_LOCK(decoder);   \
    } while (0)

#define GST_ES_DEC_UNLOCK(decoder)                 \
    do {                                           \
        g_mutex_unlock(GST_ES_DEC_MUTEX(decoder)); \
    } while (0)

typedef enum {
    PROP_0,
    PROP_OUT_WIDTH,
    PROP_OUT_HEIGHT,
    PROP_CROP_X,
    PROP_CROP_Y,
    PROP_CROP_W,
    PROP_CROP_H,
    PROP_STRIDE_ALIGN,
    PROP_EXTRA_HW_FRM,
    PROP_BUF_CACHE,
    PROP_TEST_MEMSET_OUTPUT,
} ES_DEC_PROP_E;

static void gst_es_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER(object);
    GstEsDec *self = GST_ES_DEC(decoder);
    if (!self->input_state) {
        GST_WARNING_OBJECT(decoder, "unable to set property.");
    }
    gint val = g_value_get_int(value);

    switch (prop_id) {
        case PROP_OUT_WIDTH: {
            if (val == -2 || val == -4 || val == -8 || val >= 0)
                self->out_width = val;
            else
                GST_WARNING_OBJECT(decoder, "unable to change scale width");
            break;
        }
        case PROP_OUT_HEIGHT: {
            if (val == -2 || val == -4 || val == -8 || val >= 0)
                self->out_height = val;
            else
                GST_WARNING_OBJECT(decoder, "unable to change scale height");
            break;
        }
        case PROP_CROP_X: {
            if (val < 0)
                GST_WARNING_OBJECT(decoder, "unable to change crop x");
            else
                self->crop_x = val;
            break;
        }
        case PROP_CROP_Y: {
            if (val < 0)
                GST_WARNING_OBJECT(decoder, "unable to change crop y");
            else
                self->crop_y = val;
            break;
        }
        case PROP_CROP_W: {
            if (val < 0)
                GST_WARNING_OBJECT(decoder, "unable to change crop w");
            else
                self->crop_w = val;
            break;
        }
        case PROP_CROP_H: {
            if (val < 0)
                GST_WARNING_OBJECT(decoder, "unable to change crop h");
            else
                self->crop_h = val;
            break;
        }
        case PROP_STRIDE_ALIGN: {
            if (val == 1 || val == 8 || val == 16 || val == 32 || val == 64 || val == 128 || val == 256 || val == 512
                || val == 1024 || val == 2048)
                self->stride_align = val;
            else
                GST_WARNING_OBJECT(decoder, "unable to change stride align");
            break;
        }
        case PROP_EXTRA_HW_FRM: {
            if (val < 0)
                GST_WARNING_OBJECT(decoder, "unable to change extra hw frame");
            else
                self->extra_hw_frames = val;
            break;
        }
        case PROP_BUF_CACHE: {
            if (val == 0 || val == 1)
                self->buf_cache = (gboolean)val;
            else
                GST_WARNING_OBJECT(decoder, "unable to change buffer cache mode");
            break;
        }
        case PROP_TEST_MEMSET_OUTPUT: {
            if (val == 0 || val == 1)
                self->memset_output = (gboolean)val;
            else
                GST_WARNING_OBJECT(decoder, "invalid value of memset output");
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gst_es_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER(object);
    GstEsDec *self = GST_ES_DEC(decoder);

    switch (prop_id) {
        case PROP_OUT_WIDTH:
            g_value_set_int(value, self->out_width);
            break;
        case PROP_OUT_HEIGHT:
            g_value_set_int(value, self->out_height);
            break;
        case PROP_CROP_X:
            g_value_set_int(value, self->crop_x);
            break;
        case PROP_CROP_Y:
            g_value_set_int(value, self->crop_y);
            break;
        case PROP_CROP_W:
            g_value_set_int(value, self->crop_w);
            break;
        case PROP_CROP_H:
            g_value_set_int(value, self->crop_h);
            break;
        case PROP_STRIDE_ALIGN:
            g_value_set_int(value, self->stride_align);
            break;
        case PROP_EXTRA_HW_FRM:
            g_value_set_int(value, self->extra_hw_frames);
            break;
        case PROP_BUF_CACHE:
            g_value_set_int(value, self->buf_cache);
            break;
        case PROP_TEST_MEMSET_OUTPUT:
            g_value_set_int(value, self->memset_output);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            return;
    }
}

static void shut_down(GstVideoDecoder *decoder, gboolean drain) {
    GstEsDecClass *klass = GST_ES_DEC_GET_CLASS(decoder);

    if (!TASK_IS_STARTED(decoder)) {
        GST_DEBUG_OBJECT(decoder, "Not start, no need to shut down");
        return;
    }

    GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);
    if (klass->shutdown && klass->shutdown(decoder, drain)) {
        GstTask *task = decoder->srcpad->task;
        if (task) {
            GST_OBJECT_LOCK(task);
            while (GST_TASK_STATE(task) == GST_TASK_STARTED) {
                GST_TASK_WAIT(task);
            }
            GST_OBJECT_UNLOCK(task);
        }
    }

    gst_pad_stop_task(decoder->srcpad);
    GST_VIDEO_DECODER_STREAM_LOCK(decoder);
}

static void reset(GstVideoDecoder *decoder, gboolean drain, gboolean final) {
    GstEsDec *self = GST_ES_DEC(decoder);

    GST_ES_DEC_LOCK(decoder);
    GST_DEBUG_OBJECT(self, "resetting");

    self->is_flushing = TRUE;
    self->is_draining = drain;
    shut_down(decoder, drain);
    self->is_flushing = final;
    self->is_draining = FALSE;
    if (self->mpp_ctx) {
        esmpp_reset(self->mpp_ctx);
    }
    self->return_code = GST_FLOW_OK;
    self->frame_cnt = 0;

    self->gst_state = 0;

    GST_DEBUG_OBJECT(self, "reseted");
    GST_ES_DEC_UNLOCK(decoder);
}

static gboolean gst_es_dec_start(GstVideoDecoder *decoder) {
    GstEsDec *self = GST_ES_DEC(decoder);

    GST_DEBUG_OBJECT(self, "starting");

    self->allocator = gst_es_allocator_new(self->buf_cache);
    if (!self->allocator) {
        GST_ERROR_OBJECT(self, "gst_es_allocator_new() failed.");
        return FALSE;
    }

    self->mpp_coding_type = MPP_VIDEO_CodingUnused;
    self->found_valid_pts = FALSE;
    self->input_state = NULL;
    self->return_code = GST_FLOW_OK;
    self->frame_cnt = 0;
    self->is_flushing = FALSE;

    g_mutex_init(&self->mutex);

    GST_DEBUG_OBJECT(self, "started");

    return TRUE;
}

static gboolean gst_es_dec_stop(GstVideoDecoder *decoder) {
    GstEsDec *self = GST_ES_DEC(decoder);

    GST_DEBUG_OBJECT(self, "stopping");

    GST_VIDEO_DECODER_STREAM_LOCK(decoder);
    reset(decoder, FALSE, TRUE);
    GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);

    g_mutex_clear(&self->mutex);

    if (self->mpp_dec_cfg) {
        mpp_dec_cfg_deinit(&self->mpp_dec_cfg);
    }
    if (self->mpp_ctx) {
        esmpp_close(self->mpp_ctx);
        esmpp_deinit(self->mpp_ctx);
        esmpp_destroy(self->mpp_ctx);
        self->mpp_ctx = NULL;
    }

    gst_object_unref(self->allocator);

    if (self->input_state) {
        gst_video_codec_state_unref(self->input_state);
        self->input_state = NULL;
    }

    GST_DEBUG_OBJECT(self, "stopped");
    return TRUE;
}

static gboolean gst_es_dec_flush(GstVideoDecoder *decoder) {
    if (!TASK_IS_STARTED(decoder)) {
        return TRUE;
    }
    GST_DEBUG_OBJECT(decoder, "flushing");
    reset(decoder, FALSE, FALSE);
    GST_DEBUG_OBJECT(decoder, "flushed");
    return TRUE;
}

static GstFlowReturn gst_es_dec_drain(GstVideoDecoder *decoder) {
    if (!TASK_IS_STARTED(decoder)) {
        return GST_FLOW_OK;
    }
    GST_DEBUG_OBJECT(decoder, "draining");
    reset(decoder, TRUE, FALSE);
    GST_DEBUG_OBJECT(decoder, "drained");
    return GST_FLOW_OK;
}

static GstFlowReturn gst_es_dec_finish(GstVideoDecoder *decoder) {
    if (!TASK_IS_STARTED(decoder)) {
        return GST_FLOW_OK;
    }
    GST_DEBUG_OBJECT(decoder, "finishing");
    reset(decoder, TRUE, FALSE);
    GST_DEBUG_OBJECT(decoder, "finished");
    return GST_FLOW_OK;
}

static gboolean gst_es_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state) {
    GstEsDec *self = GST_ES_DEC(decoder);

    GST_DEBUG_OBJECT(self, "setting format: %" GST_PTR_FORMAT, state->caps);

    if (self->input_state) {
        if (gst_caps_is_strictly_equal(self->input_state->caps, state->caps)) {
            GST_DEBUG_OBJECT(self, "set the same caps.");
            return TRUE;
        }
        GST_DEBUG_OBJECT(self, "get new format, reset decoder");
        reset(decoder, TRUE, FALSE);
        gst_video_codec_state_unref(self->input_state);
        self->input_state = NULL;
    } else {
        MppFrameFormat mpp_fmt;
        if (self->mpp_coding_type != MPP_VIDEO_CodingAVC && self->mpp_coding_type != MPP_VIDEO_CodingHEVC
            && self->mpp_coding_type != MPP_VIDEO_CodingMJPEG) {
            GST_ERROR_OBJECT(self, "unsupported coding type %d.", self->mpp_coding_type);
            return FALSE;
        }
        if (esmpp_create(&self->mpp_ctx, MPP_CTX_DEC, self->mpp_coding_type) != MPP_OK) {
            GST_ERROR_OBJECT(self, "failed to create mpp context.");
            return FALSE;
        }
        if (esmpp_init(self->mpp_ctx) != MPP_OK) {
            GST_ERROR_OBJECT(self, "failed to init mpp ctx");
            goto error1;
        }
        self->buf_grp = gst_es_allocator_get_mpp_group(self->allocator);
        if (!self->buf_grp) {
            GST_ERROR_OBJECT(self, "failed to get buffer group");
            goto error2;
        }
        if (mpp_dec_cfg_init(&self->mpp_dec_cfg) != MPP_OK) {
            GST_ERROR_OBJECT(self, "failed to init mpp_dec_cfg");
            goto error2;
        }
        if (esmpp_control(self->mpp_ctx, MPP_DEC_GET_CFG, self->mpp_dec_cfg) != MPP_OK) {
            GST_ERROR_OBJECT(self, "failed to get dec cfg");
            goto error3;
        }
        GST_DEBUG_OBJECT(self, "format is %s", gst_video_format_to_string(self->out_format));
        mpp_fmt = gst_es_gst_format_to_mpp_format(self->out_format);
        if (mpp_fmt == MPP_FMT_BUTT) {
            GST_ERROR_OBJECT(self, "gst %s not support", gst_video_format_to_string(self->out_format));
            goto error3;
        }
        mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "output_fmt", mpp_fmt);
        if (self->stride_align) {
            GST_DEBUG_OBJECT(self, "set stride to %u", self->stride_align);
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "stride_align", self->stride_align);
        } else {
            // If user not set stide align, save it get from mpp
            mpp_dec_cfg_get_u32(self->mpp_dec_cfg, "stride_align", &self->stride_align);
            GST_DEBUG_OBJECT(self, "self->stride_align is %u", self->stride_align);
        }
        if (self->extra_hw_frames) {
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "extra_hw_frames", self->extra_hw_frames);
        }
        if (self->out_width && self->out_height) {
            if ((self->out_width * self->out_height) < 0) {
                GST_ERROR_OBJECT(self, "width %d height %d not support", self->out_width, self->out_height);
                goto error3;
            }
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "scale_width", self->out_width);
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "scale_height", self->out_height);
        }
        if (self->crop_w && self->crop_h) {
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "crop_xoffset", self->crop_x);
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "crop_yoffset", self->crop_y);
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "crop_width", self->crop_w);
            mpp_dec_cfg_set_s32(self->mpp_dec_cfg, "crop_height", self->crop_h);
        }
        if (esmpp_control(self->mpp_ctx, MPP_DEC_SET_CFG, self->mpp_dec_cfg) != MPP_OK) {
            GST_ERROR_OBJECT(self, "failed to set dec cfg");
            goto error3;
        }
        if (esmpp_open(self->mpp_ctx) != MPP_OK) {
            GST_ERROR_OBJECT(self, "failed to open esmpp");
            goto error3;
        }
    }
    self->input_state = gst_video_codec_state_ref(state);
    return TRUE;

error3:
    mpp_dec_cfg_deinit(&self->mpp_dec_cfg);
error2:
    esmpp_deinit(self->mpp_ctx);
error1:
    esmpp_destroy(self->mpp_ctx);
    self->mpp_ctx = NULL;
    return FALSE;
}

static gboolean update_video_info(GstVideoDecoder *decoder,
                                  GstVideoFormat gst_format,
                                  guint width,
                                  guint height,
                                  gint hstride,
                                  gint vstride,
                                  guint align) {
    GstEsDec *self = GST_ES_DEC(decoder);
    GstVideoInfo *gst_info = &self->gst_info;
    GstVideoCodecState *output_state;

    g_return_val_if_fail(gst_format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);

    output_state = gst_video_decoder_set_output_state(
        decoder, gst_format, GST_ROUND_UP_2(width), GST_ROUND_UP_2(height), self->input_state);
    output_state->caps = gst_video_info_to_caps(&output_state->info);

    *gst_info = output_state->info;
    gst_video_codec_state_unref(output_state);

    if (!gst_video_decoder_negotiate(decoder)) return FALSE;

    align = align ? align : 2;
    hstride = hstride ? hstride : GST_ES_VIDEO_INFO_HSTRIDE(gst_info);
    hstride = GST_ROUND_UP_N(hstride, align);
    vstride = vstride ? vstride : GST_ES_VIDEO_INFO_VSTRIDE(gst_info);
    vstride = GST_ROUND_UP_N(vstride, 2);

    return gst_es_video_info_align(gst_info, hstride, vstride);
}

static GstFlowReturn apply_info_change(GstVideoDecoder *decoder, MppFramePtr mpp_frame) {
    GstEsDec *self = GST_ES_DEC(decoder);
    GstVideoInfo *gst_info = &self->gst_info;

    gint width = mpp_frame_get_width(mpp_frame);
    gint height = mpp_frame_get_height(mpp_frame);
    gint hstride = mpp_frame_get_hor_stride(mpp_frame);
    gint vstride = mpp_frame_get_ver_stride(mpp_frame);

    if (hstride % 2 || vstride % 2) return GST_FLOW_NOT_NEGOTIATED;

    gst_video_info_set_format(gst_info, self->out_format, width, height);

    if (!update_video_info(decoder, self->out_format, width, height, hstride, vstride, self->stride_align))
        return GST_FLOW_NOT_NEGOTIATED;

    return GST_FLOW_OK;
}

static GstVideoCodecFrame *get_gst_frame(GstVideoDecoder *decoder, GstClockTime pts) {
    GstEsDec *self = GST_ES_DEC(decoder);
    GstVideoCodecFrame *gst_frame;
    GList *frame_list, *l;
    gboolean first_frame = !self->frame_cnt;
    gint i;

    self->frame_cnt++;

    frame_list = gst_video_decoder_get_frames(decoder);
    if (!frame_list) {
        return NULL;
    }

    if (first_frame) {
        gst_frame = frame_list->data;
        GST_DEBUG_OBJECT(self, "using original pts, using first frame (#%d)", gst_frame->system_frame_number);
        goto out;
    }

    if (!pts) {
        pts = GST_CLOCK_TIME_NONE;
    }

    GST_TRACE_OBJECT(self, "receiving pts=%" GST_TIME_FORMAT, GST_TIME_ARGS(pts));

    if (!self->found_valid_pts) {
        gst_frame = frame_list->data;
        GST_DEBUG_OBJECT(self, "using oldest frame (#%d)", gst_frame->system_frame_number);
        goto out;
    }

    for (gst_frame = NULL, l = frame_list, i = 0; l != NULL; l = l->next, i++) {
        GstVideoCodecFrame *f = l->data;
        if (GST_CLOCK_TIME_IS_VALID(f->pts)) {
            if (abs((gint)f->pts - (gint)pts) < 3 * GST_MSECOND) {
                gst_frame = f;
                GST_TRACE_OBJECT(self, "using matched frame (#%d)", gst_frame->system_frame_number);
                goto out;
            }
            if (GST_CLOCK_TIME_IS_VALID(pts) && f->pts > pts) {
                continue;
            }
        }
        if (!gst_frame || gst_frame->pts > f->pts) {
            gst_frame = f;
        }
    }

out:
    if (gst_frame) {
        gst_video_codec_frame_ref(gst_frame);
        if (GST_CLOCK_TIME_IS_VALID(pts)) {
            gst_frame->pts = pts;
        }
    }

    g_list_free_full(frame_list, (GDestroyNotify)gst_video_codec_frame_unref);
    return gst_frame;
}

static GstBuffer *get_gst_buffer(GstVideoDecoder *decoder, MppFramePtr mpp_frame) {
    GstEsDec *self = GST_ES_DEC(decoder);
    GstVideoInfo *gst_info = &self->gst_info;
    GstBuffer *gst_buffer;
    GstMemory *gst_mem;
    MppBufferPtr mpp_buffer;

    mpp_buffer = mpp_frame_get_buffer(mpp_frame);
    if (!mpp_buffer) {
        return NULL;
    }

    mpp_buffer_set_index(mpp_buffer, gst_es_allocator_get_index(self->allocator));
    gst_mem = gst_es_allocator_import_mppbuf(self->allocator, mpp_buffer);
    if (!gst_mem) {
        return NULL;
    }

    gst_buffer = gst_buffer_new();
    if (!gst_buffer) {
        gst_memory_unref(gst_mem);
        return NULL;
    }

    gst_buffer_append_memory(gst_buffer, gst_mem);
    gst_buffer_add_video_meta_full(gst_buffer,
                                   GST_VIDEO_FRAME_FLAG_NONE,
                                   GST_VIDEO_INFO_FORMAT(gst_info),
                                   GST_VIDEO_INFO_WIDTH(gst_info),
                                   GST_VIDEO_INFO_HEIGHT(gst_info),
                                   GST_VIDEO_INFO_N_PLANES(gst_info),
                                   gst_info->offset,
                                   gst_info->stride);
    return gst_buffer;
}

static void memset_padding_width(GstEsDec *self, MppFramePtr mpp_frame) {
    gint width = mpp_frame_get_width(mpp_frame);
    gint height = mpp_frame_get_height(mpp_frame);
    gint align_width = ((width + self->stride_align - 1) / self->stride_align) * self->stride_align;

    if (align_width != width) {
        void *addr = mpp_buffer_get_ptr(mpp_frame_get_buffer(mpp_frame));
        if (addr == NULL) {
            GST_ERROR_OBJECT(self, "Failed to get buffer pointer");
            return;
        }

        gint align_width_size, width_size, padding_width_size;
        GstVideoInfo info;

        gst_video_info_init(&info);
        gst_video_info_set_format(&info, self->out_format, align_width, 1);

        if (GST_VIDEO_INFO_N_PLANES(&info) > 1) {
            GST_WARNING_OBJECT(self, "Not support padding buffer memset");
            return;
        }
        align_width_size = GST_VIDEO_INFO_SIZE(&info);

        gst_video_info_set_format(&info, self->out_format, width, 1);
        width_size = GST_VIDEO_INFO_SIZE(&info);

        padding_width_size = align_width_size - width_size;

        for (gint i = 0; i < height; i++) {
            memset((char *)addr + width_size, 0, padding_width_size);
            addr = (char *)addr + align_width_size;
        }
    }
    return;
}

static void gst_es_dec_loop(GstVideoDecoder *decoder) {
    GstEsDecClass *klass = GST_ES_DEC_GET_CLASS(decoder);
    GstEsDec *self = GST_ES_DEC(decoder);
    GstVideoCodecFrame *gst_frame = NULL;
    GstBuffer *gst_buffer = NULL;
    MppFramePtr mpp_frame = NULL;

    mpp_frame = klass->get_mpp_frame(decoder, OUT_TIMEOUT_MS);
    if (!mpp_frame) {
        return;
    }

    GST_VIDEO_DECODER_STREAM_LOCK(decoder);

    if (mpp_frame_get_eos(mpp_frame)) {
        GST_DEBUG_OBJECT(self, "get an eos mpp frame");
        goto eos_frame;
    }

    if (mpp_frame_get_info_change(mpp_frame)) {
        ES_U32 width = mpp_frame_get_width(mpp_frame);
        ES_U32 height = mpp_frame_get_height(mpp_frame);
        ES_U32 hor_stride = mpp_frame_get_hor_stride(mpp_frame);
        ES_U32 ver_stride = mpp_frame_get_ver_stride(mpp_frame);
        ES_U32 buf_size = mpp_frame_get_buf_size(mpp_frame);
        // Reserve additional buffers for display
        ES_U32 group_buf_count = mpp_frame_get_group_buf_count(mpp_frame) + DISPLAY_BUFFER_CNT;
        if (self->extra_hw_frames) {
            group_buf_count += self->extra_hw_frames;
        }

        GST_DEBUG_OBJECT(self,
                         "info changed found. Require buffer w:h [%u:%u] stride [%u:%u] buf_size[%u] buf_cnt[%u]",
                         width,
                         height,
                         hor_stride,
                         ver_stride,
                         buf_size,
                         group_buf_count);
        mpp_buffer_group_limit_config(self->buf_grp, buf_size, group_buf_count);
        esmpp_control(self->mpp_ctx, MPP_DEC_SET_EXT_BUF_GROUP, self->buf_grp);
        esmpp_control(self->mpp_ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
        self->return_code = apply_info_change(decoder, mpp_frame);
        goto info_change_frame;
    }

    if (!self->gst_info.size) {
        GST_DEBUG_OBJECT(self, "info changed not found, set by itself.");
        self->return_code = apply_info_change(decoder, mpp_frame);
    }

    gst_frame = get_gst_frame(decoder, mpp_frame_get_pts(mpp_frame));
    if (!gst_frame) {
        goto no_frame;
    }

    if (mpp_frame_get_discard(mpp_frame) || mpp_frame_get_errinfo(mpp_frame)) {
        goto error;
    }

    gst_buffer = get_gst_buffer(decoder, mpp_frame);
    if (!gst_buffer) {
        goto error;
    }

    if (self->buf_cache) {
        mpp_buffer_sync_begin(mpp_frame_get_buffer(mpp_frame));
    }

    if (self->memset_output) {
        memset_padding_width(self, mpp_frame);
    }

    gst_buffer_resize(gst_buffer, 0, GST_VIDEO_INFO_SIZE(&self->gst_info));
    GST_MINI_OBJECT_FLAG_SET(gst_buffer, GST_MINI_OBJECT_FLAG_LOCKABLE);
    gst_frame->output_buffer = gst_buffer;

    if (self->is_flushing && !self->is_draining) {
        GST_DEBUG_OBJECT(self, "is flushing and not draining, drop frame");
        goto drop_frame;
    }

    GST_TRACE_OBJECT(self, "Call finish frame, pts=%" GST_TIME_FORMAT, GST_TIME_ARGS(gst_frame->pts));
    gst_video_decoder_finish_frame(decoder, gst_frame);

out:
    mpp_frame_deinit(&mpp_frame);

    if (self->return_code != GST_FLOW_OK) {
        GST_DEBUG_OBJECT(self, "leaving output thread: %s", gst_flow_get_name(self->return_code));

        gst_pad_pause_task(decoder->srcpad);
    }

    GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);
    return;
eos_frame:
    GST_DEBUG_OBJECT(self, "got frame with eos");
    self->return_code = GST_FLOW_EOS;
    goto out;
info_change_frame:
    GST_INFO_OBJECT(self, "got frame with video info changed");
    goto out;
no_frame:
    GST_WARNING_OBJECT(self, "no frame");
    goto out;
error:
    GST_WARNING_OBJECT(self, "got error, can not handle this frame");
    goto drop_frame;
drop_frame:
    GST_DEBUG_OBJECT(self, "drop this frame");
    gst_video_decoder_release_frame(decoder, gst_frame);
    goto out;
}

static GstFlowReturn gst_es_dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *frame) {
    GstEsDecClass *klass = GST_ES_DEC_GET_CLASS(decoder);
    GstEsDec *self = GST_ES_DEC(decoder);
    GstMapInfo gst_map_info;
    GstBuffer *tmp = NULL;
    GstFlowReturn ret;
    gint ret_send;
    MppPacketPtr mpp_pkt = NULL;

    GST_ES_DEC_LOCK(decoder);

    memset(&gst_map_info, 0, sizeof(GstMapInfo));
    GST_TRACE_OBJECT(self, "handle frame %u", frame->system_frame_number);

    if (G_UNLIKELY(self->is_flushing)) {
        goto err_flushing;
    }

    if (G_UNLIKELY(!TASK_IS_STARTED(decoder))) {
        if (klass->set_extra_data && !klass->set_extra_data(decoder)) {
            goto err_extradata;
        }
        gst_pad_start_task(decoder->srcpad, (GstTaskFunction)gst_es_dec_loop, decoder, NULL);
    }

    GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);
    gst_buffer_map(frame->input_buffer, &gst_map_info, GST_MAP_READ);

    mpp_pkt = klass->prepare_mpp_packet(decoder, &gst_map_info);
    GST_VIDEO_DECODER_STREAM_LOCK(decoder);
    if (!mpp_pkt) {
        goto err_no_packet;
    }

    mpp_packet_set_pts(mpp_pkt, (ES_S64)frame->pts);
    if (GST_CLOCK_TIME_IS_VALID(frame->pts)) {
        self->found_valid_pts = TRUE;
    }
    GST_TRACE_OBJECT(self,
                     "get mpp packet success, pts = %lld, found_valid_pts = %d",
                     mpp_packet_get_pts(mpp_pkt),
                     self->found_valid_pts);

    while (1) {
        GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);
        ret_send = klass->send_mpp_packet(decoder, mpp_pkt, IN_TIMEOUT_MS);
        if (GST_SEND_PACKET_SUCCESS == ret_send || GST_SEND_PACKET_BAD == ret_send) {
            GST_VIDEO_DECODER_STREAM_LOCK(decoder);
            break;
        }
        GST_VIDEO_DECODER_STREAM_LOCK(decoder);
        if (GST_SEND_PACKET_TIMEOUT != ret_send) {
            goto err_send;
        }
    }
    GST_TRACE_OBJECT(self, "packet send to mpp queue success");

    mpp_pkt = NULL;
    gst_buffer_unmap(frame->input_buffer, &gst_map_info);

    tmp = frame->input_buffer;
    frame->input_buffer = gst_buffer_new();
    gst_buffer_copy_into(
        frame->input_buffer, tmp, GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META, 0, 0);
    gst_buffer_unref(tmp);

    gst_video_codec_frame_unref(frame);
    GST_ES_DEC_UNLOCK(decoder);
    return self->return_code;

err_flushing:
    GST_WARNING_OBJECT(self, "Drop this frame bacause we are flushing");
    ret = GST_FLOW_FLUSHING;
    goto drop;
err_extradata:
    GST_ERROR_OBJECT(self, "Drop this frame because set extradata failed");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto drop;
err_no_packet:
    GST_WARNING_OBJECT(self, "Drop this frame because we cannot get packet");
    ret = GST_FLOW_ERROR;
    goto drop;
err_send:
    GST_WARNING_OBJECT(self, "Drop this frame because we cannot send packet");
    ret = GST_FLOW_ERROR;
    goto drop;
drop:
    if (mpp_pkt) {
        mpp_packet_deinit(&mpp_pkt);
    }
    if (gst_map_info.size) {
        gst_buffer_unmap(frame->input_buffer, &gst_map_info);
    }
    gst_video_decoder_release_frame(decoder, frame);
    GST_ES_DEC_UNLOCK(decoder);
    return ret;
}

static GstStateChangeReturn gst_es_dec_change_state(GstElement *element, GstStateChange transition) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER(element);
    GstEsDec *self = GST_ES_DEC(decoder);
    self->gst_state = transition;
    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY: {
            GST_INFO_OBJECT(self, "State changed: Paused -> Ready");
            GST_VIDEO_DECODER_STREAM_LOCK(decoder);
            reset(decoder, FALSE, TRUE);
            GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);
            break;
        }
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED: {
            GST_INFO_OBJECT(self, "State changed: Playing -> Paused");
            break;
        }
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
            GST_INFO_OBJECT(self, "State changed: Paused -> Playing");
            break;
        }
        case GST_STATE_CHANGE_NULL_TO_READY: {
            GST_INFO_OBJECT(self, "State changed: NULL -> Ready");
            break;
        }
        case GST_STATE_CHANGE_READY_TO_PAUSED: {
            GST_INFO_OBJECT(self, "State changed: Ready -> Paused");
            break;
        }
        case GST_STATE_CHANGE_READY_TO_NULL: {
            GST_INFO_OBJECT(self, "State changed: Ready -> NULL");
            break;
        }
        case GST_STATE_CHANGE_NULL_TO_NULL: {
            GST_INFO_OBJECT(self, "State changed: NULL -> NULL");
            break;
        }
        case GST_STATE_CHANGE_READY_TO_READY: {
            GST_INFO_OBJECT(self, "State changed: Ready -> Ready");
            break;
        }
        case GST_STATE_CHANGE_PAUSED_TO_PAUSED: {
            GST_INFO_OBJECT(self, "State changed: Paused -> Paused");
            break;
        }
        case GST_STATE_CHANGE_PLAYING_TO_PLAYING: {
            GST_INFO_OBJECT(self, "State changed: Playing -> Playing");
            break;
        }
    }
    return GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
}

static void gst_es_dec_init(GstEsDec *self) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER(self);
    gst_video_decoder_set_packetized(decoder, TRUE);
}

static void gst_es_dec_class_init(GstEsDecClass *klass) {
    GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esdec", 0, "ESWIN decoder");

    decoder_class->start = GST_DEBUG_FUNCPTR(gst_es_dec_start);
    decoder_class->stop = GST_DEBUG_FUNCPTR(gst_es_dec_stop);
    decoder_class->flush = GST_DEBUG_FUNCPTR(gst_es_dec_flush);
    decoder_class->drain = GST_DEBUG_FUNCPTR(gst_es_dec_drain);
    decoder_class->finish = GST_DEBUG_FUNCPTR(gst_es_dec_finish);
    decoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_dec_set_format);
    decoder_class->handle_frame = GST_DEBUG_FUNCPTR(gst_es_dec_handle_frame);
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_es_dec_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_es_dec_get_property);

    g_object_class_install_property(gobject_class,
                                    PROP_CROP_X,
                                    g_param_spec_int("cx",
                                                     "Crop Rect left",
                                                     "Pixels to the crop rect at left",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_CROP_W,
                                    g_param_spec_int("cw",
                                                     "Crop Rect width",
                                                     "Pixels to the width of crop rect",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_CROP_Y,
                                    g_param_spec_int("cy",
                                                     "Crop Rect top",
                                                     "Pixels to the crop rect at top",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_CROP_H,
                                    g_param_spec_int("ch",
                                                     "Crop Rect height",
                                                     "Pixels to height of crop rect",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_STRIDE_ALIGN,
                                    g_param_spec_int("stride-align",
                                                     "Set the output stride align",
                                                     "set the stride alignment of output frame, multiple of 2",
                                                     1,
                                                     2048,
                                                     1,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_OUT_WIDTH,
                                    g_param_spec_int("sw",
                                                     "Downscale width",
                                                     "Pixels of video downscale width",
                                                     -8,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_OUT_HEIGHT,
                                    g_param_spec_int("sh",
                                                     "Downscale height",
                                                     "Pixels of video downscale height",
                                                     -8,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_EXTRA_HW_FRM,
                                    g_param_spec_int("extra-hw-frm",
                                                     "extra hardware frames count",
                                                     "Set the extra hardware frames count",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_BUF_CACHE,
                                    g_param_spec_int("buf-cache",
                                                     "buffer cache mode",
                                                     "Set the cache mode of output buffer, 0-Noncache, 1-Cache",
                                                     0,
                                                     1,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    PROP_TEST_MEMSET_OUTPUT,
                                    g_param_spec_int("test-memset-output",
                                                     "memset output buffer",
                                                     "Memset output buffer for test, 0-noset, 1-set",
                                                     0,
                                                     1,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_es_dec_change_state);
}