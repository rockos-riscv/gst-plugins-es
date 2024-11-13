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

#include "gstesdec_comm.h"

struct _FmtInfo {
    GstVideoFormat fmt;
    gboolean jpeg_support;
    gboolean video_support;
    const gchar *fmt_name;
};

static struct _FmtInfo support_fmt_list[] = {{GST_VIDEO_FORMAT_NV12, TRUE, TRUE, "NV12"},
                                             {GST_VIDEO_FORMAT_NV21, TRUE, TRUE, "NV21"},
                                             {GST_VIDEO_FORMAT_I420, TRUE, TRUE, "I420"},
                                             {GST_VIDEO_FORMAT_GRAY8, TRUE, TRUE, "GRAY8"},
                                             {GST_VIDEO_FORMAT_BGR, TRUE, TRUE, "BGR"},
                                             {GST_VIDEO_FORMAT_RGB, TRUE, TRUE, "RGB"},
                                             {GST_VIDEO_FORMAT_BGRA, TRUE, TRUE, "BGRA"},
                                             {GST_VIDEO_FORMAT_RGBA, TRUE, TRUE, "RGBA"},
                                             {GST_VIDEO_FORMAT_BGRx, TRUE, TRUE, "BGRx"},
                                             {GST_VIDEO_FORMAT_RGBx, TRUE, TRUE, "RGBx"},
                                             {GST_VIDEO_FORMAT_P010_10LE, FALSE, TRUE, "P010_10LE"}};

static gint support_fmt_cnt = sizeof(support_fmt_list) / sizeof(support_fmt_list[0]);

static gboolean check_support_by_code_type(GstVideoFormat fmt, MppCodingType mpp_coding_type) {
    for (gint i = 0; i < support_fmt_cnt; i++) {
        if (support_fmt_list[i].fmt == fmt) {
            return mpp_coding_type == MPP_VIDEO_CodingMJPEG ? support_fmt_list[i].jpeg_support
                                                            : support_fmt_list[i].video_support;
        }
    }
    return FALSE;
}

static GstVideoFormat convert_name_to_gst_video_format(const char *name) {
    for (gint i = 0; i < support_fmt_cnt; i++) {
        if (!strcmp(support_fmt_list[i].fmt_name, name)) {
            return support_fmt_list[i].fmt;
        }
    }
    GST_WARNING_OBJECT(name, "format name: %s is not support.", name);
    return GST_VIDEO_FORMAT_UNKNOWN;
}

const gchar *gst_es_comm_dec_get_name_by_gst_video_format(const GstVideoFormat fmt) {
    for (gint i = 0; i < support_fmt_cnt; i++) {
        if (support_fmt_list[i].fmt == fmt) {
            return support_fmt_list[i].fmt_name;
        }
    }
    GST_WARNING_OBJECT(support_fmt_list, "gst format: %s is not support", gst_video_format_to_string(fmt));
    return NULL;
}

gboolean gst_es_comm_dec_set_extra_data(GstEsDec *esdec) {
    if (!esdec) {
        GST_DEBUG_OBJECT(esdec, "esdec is NULL.");
        return FALSE;
    }
    GstVideoCodecState *state = esdec->input_state;
    GstBuffer *codec_data = state->codec_data;
    GstMapInfo mapinfo = {0};
    MppPacketPtr mpp_packet = NULL;

    if (!codec_data) return TRUE;

    GST_DEBUG_OBJECT(codec_data, "codec_data exist, set as extra data.");
    gst_buffer_ref(codec_data);
    gst_buffer_map(codec_data, &mapinfo, GST_MAP_READ);

    mpp_packet_init(&mpp_packet, mapinfo.data, mapinfo.size);
    mpp_packet_set_extra_data(mpp_packet);

    if (esmpp_put_packet(esdec->mpp_ctx, mpp_packet) != MPP_OK) {
        GST_ERROR_OBJECT(esdec, "failed to put packet");
        return FALSE;
    }

    mpp_packet_deinit(&mpp_packet);
    gst_buffer_unmap(codec_data, &mapinfo);
    gst_buffer_unref(codec_data);

    return TRUE;
}

gint gst_es_comm_dec_send_mpp_packet(GstEsDec *esdec, MppPacketPtr mpp_packet, gint timeout_ms) {
    if (!esdec || !mpp_packet) {
        GST_DEBUG_OBJECT(esdec, "params are invalid, esdec: %p, mpp_packet: %p.", esdec, mpp_packet);
        return FALSE;
    }
    gint interval_ms = 2;
    MPP_RET ret = MPP_OK;
    do {
        ret = esmpp_put_packet(esdec->mpp_ctx, mpp_packet);
        switch (ret) {
            case MPP_OK:
                mpp_packet_deinit(&mpp_packet);
                return GST_SEND_PACKET_SUCCESS;
            case MPP_ERR_STREAM:
                mpp_packet_deinit(&mpp_packet);
                return GST_SEND_PACKET_BAD;
            case MPP_ERR_TIMEOUT:
                g_usleep(interval_ms * 1000);
                timeout_ms -= interval_ms;
                break;
            default:
                GST_ERROR_OBJECT(esdec, "put packet failed %d", ret);
                return GST_SEND_PACKET_FAIL;
        }
    } while (timeout_ms > 0);
    return GST_SEND_PACKET_TIMEOUT;
}

gboolean gst_es_comm_dec_shutdown(GstEsDec *esdec, gboolean drain) {
    if (!drain) return FALSE;

    MppPacketPtr mpp_packet;
    MPP_RET ret = 0;

    mpp_packet_init(&mpp_packet, NULL, 0);
    mpp_packet_set_eos(mpp_packet);
    GST_DEBUG_OBJECT(esdec, "shutdown, send a packet with eos flag");

    while (1) {
        ret = esmpp_put_packet(esdec->mpp_ctx, mpp_packet);
        if (!ret) break;
        g_usleep(1000);
    }

    mpp_packet_deinit(&mpp_packet);
    return TRUE;
}

GType get_format_type(void) {
    static GType format_type = 0;
    if (format_type == 0) {
        static const GEnumValue formats[] = {/* value, value_name, value_nick */
                                             {GST_VIDEO_FORMAT_NV12, "NV12", "NV12"},
                                             {GST_VIDEO_FORMAT_NV21, "NV21", "NV21"},
                                             {GST_VIDEO_FORMAT_I420, "I420", "I420"},
                                             {GST_VIDEO_FORMAT_YV12, "YV12", "YV12"},
                                             {GST_VIDEO_FORMAT_NV16, "NV16", "NV16"},
                                             {GST_VIDEO_FORMAT_NV61, "NV61", "NV61"},
                                             {GST_VIDEO_FORMAT_BGR16, "BGR565", "BGR16"},
                                             {GST_VIDEO_FORMAT_RGB, "RGB", "RGB"},
                                             {GST_VIDEO_FORMAT_BGR, "BGR", "BGR"},
                                             {GST_VIDEO_FORMAT_RGBA, "RGBA8888", "RGBA"},
                                             {GST_VIDEO_FORMAT_BGRA, "BGRA8888", "BGRA"},
                                             {GST_VIDEO_FORMAT_RGBx, "RGBX8888", "RGBx"},
                                             {GST_VIDEO_FORMAT_BGRx, "BGRX8888", "BGRx"},
                                             {0, NULL, NULL}};
        format_type = g_enum_register_static("GstEsVideoDecFormat", formats);
    }
    return format_type;
}

void gst_es_comm_dec_set_default_fmt(GstEsDec *esdec, const char *fmt_env) {
    GEnumClass *class;
    GEnumValue *value;
    const gchar *env;
    esdec->out_format = GST_VIDEO_FORMAT_RGBA;
    env = g_getenv(fmt_env);
    if (env) {
        class = g_type_class_ref(get_format_type());
        if (class) {
            value = g_enum_get_value_by_nick(class, env);
            if (value) {
                esdec->out_format = value->value;
            }
            g_type_class_unref(class);
        }
    }
    GST_DEBUG_OBJECT(esdec, "Default output format is %s", gst_video_format_to_string(esdec->out_format));
}

void gst_es_comm_dec_set_property(GstEsDec *self, guint prop_id, const GValue *value, GParamSpec *pspec) {
    if (!self) return;

    switch (prop_id) {
        case PROP_OUT_FORMAT: {
            if (self->input_state)
                GST_WARNING_OBJECT(self, "unable to change output format");
            else {
                GstVideoFormat format = convert_name_to_gst_video_format(g_value_get_string(value));
                if (!check_support_by_code_type(format, self->mpp_coding_type)) {
                    GST_WARNING_OBJECT(self, "do not support output format: %s", g_value_get_string(value));
                } else {
                    self->out_format = format;
                }
                break;
            }
            default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
                break;
        }
    }
}
