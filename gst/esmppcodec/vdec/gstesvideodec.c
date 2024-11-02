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

#include "gstesvideodec.h"

#define GST_TYPE_ES_VIDEO_DEC (gst_es_video_dec_get_type())
#define GST_ES_VIDEO_DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_VIDEO_DEC, GstEsVideoDec))

#define GST_CAT_DEFAULT es_video_dec_debug
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

struct _GstEsVideoDec {
    GstEsDec parent;
    gint poll_timeout;
};

#define parent_class gst_es_video_dec_parent_class
G_DEFINE_TYPE(GstEsVideoDec, gst_es_video_dec, GST_TYPE_ES_DEC);

static GstStaticPadTemplate gst_es_video_dec_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
                            GST_PAD_SINK,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-h264,"
                                            "stream-format = (string) { avc, byte-stream }"
                                            ";"
                                            "video/x-h265,"
                                            "stream-format = (string) { hvc1, hev1, byte-stream }"
                                            ";"));

static GstStaticPadTemplate gst_es_video_dec_src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{" ES_DEC_FORMATS "}") ";"));

static MppCodingType get_mpp_coding_type(GstStructure *s) {
    if (gst_structure_has_name(s, "video/x-h264")) return MPP_VIDEO_CodingAVC;
    if (gst_structure_has_name(s, "video/x-h265")) return MPP_VIDEO_CodingHEVC;
    return MPP_VIDEO_CodingUnused;
}

static gboolean gst_es_video_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state) {
    GstVideoDecoderClass *pclass = GST_VIDEO_DECODER_CLASS(parent_class);
    GstEsDec *esdec = GST_ES_DEC(decoder);
    GstStructure *structure;

    structure = gst_caps_get_structure(state->caps, 0);
    esdec->mpp_coding_type = get_mpp_coding_type(structure);
    if (esdec->mpp_coding_type == MPP_VIDEO_CodingUnused) {
        GST_ERROR_OBJECT(esdec, "esvideodec only support AVC and HEVC");
        return FALSE;
    }

    // continue set format to esdec
    return pclass->set_format(decoder, state);
}

static gboolean gst_es_video_dec_set_extra_data(GstVideoDecoder *decoder) {
    GstEsVideoDec *self = GST_ES_VIDEO_DEC(decoder);
    GstEsDec *esdec = GST_ES_DEC(decoder);
    GstVideoCodecState *state = esdec->input_state;
    GstBuffer *codec_data = state->codec_data;
    GstMapInfo mapinfo = {0};
    MppPacketPtr mpp_packet = NULL;

    if (codec_data) {
        GST_DEBUG_OBJECT(decoder, "codec_data exist, set as extra data.");
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
    }

    self->poll_timeout = 0;

    return TRUE;
}

static MppPacketPtr gst_es_video_dec_prepare_mpp_packet(GstVideoDecoder *decoder, GstMapInfo *mapinfo) {
    (void)(decoder);
    MppPacketPtr mpp_packet = NULL;
    mpp_packet_init(&mpp_packet, mapinfo->data, mapinfo->size);
    return mpp_packet;
}

static gint gst_es_video_dec_send_mpp_packet(GstVideoDecoder *decoder, MppPacketPtr mpp_packet, gint timeout_ms) {
    GstEsDec *esdec = GST_ES_DEC(decoder);
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

static MppFramePtr gst_es_video_dec_get_mpp_frame(GstVideoDecoder *decoder, gint timeout_ms) {
    GstEsVideoDec *self = GST_ES_VIDEO_DEC(decoder);
    GstEsDec *esdec = GST_ES_DEC(decoder);
    MppFramePtr mpp_frame = NULL;
    if (self->poll_timeout != timeout_ms) {
        self->poll_timeout = timeout_ms;
    }
    esmpp_get_frame(esdec->mpp_ctx, &mpp_frame, self->poll_timeout);
    return mpp_frame;
}

static gboolean gst_es_video_dec_shutdown(GstVideoDecoder *decoder, gboolean drain) {
    GstEsDec *esdec = GST_ES_DEC(decoder);
    MppPacketPtr mpp_packet;
    MPP_RET ret = 0;

    if (!drain) return FALSE;

    mpp_packet_init(&mpp_packet, NULL, 0);
    mpp_packet_set_eos(mpp_packet);
    GST_DEBUG_OBJECT(decoder, "shutdown, send a packet with eos flag");

    while (1) {
        ret = esmpp_put_packet(esdec->mpp_ctx, mpp_packet);
        if (!ret) break;
        g_usleep(1000);
    }

    mpp_packet_deinit(&mpp_packet);
    return TRUE;
}

static GType get_format_type(void) {
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

static void gst_es_video_dec_init(GstEsVideoDec *self) {
    GstEsDec *esdec = GST_ES_DEC(self);
    GEnumClass *class;
    GEnumValue *value;
    const gchar *env;
    esdec->out_format = GST_VIDEO_FORMAT_RGBA;
    env = g_getenv("GST_ES_VIDEO_DEC_DEF_FMT");
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

static void gst_es_video_dec_class_init(GstEsVideoDecClass *klass) {
    GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS(klass);
    GstEsDecClass *pclass = GST_ES_DEC_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esvideodec", 0, "ESWIN video decoder");

    decoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_video_dec_set_format);
    pclass->set_extra_data = GST_DEBUG_FUNCPTR(gst_es_video_dec_set_extra_data);
    pclass->prepare_mpp_packet = GST_DEBUG_FUNCPTR(gst_es_video_dec_prepare_mpp_packet);
    pclass->send_mpp_packet = GST_DEBUG_FUNCPTR(gst_es_video_dec_send_mpp_packet);
    pclass->get_mpp_frame = GST_DEBUG_FUNCPTR(gst_es_video_dec_get_mpp_frame);
    pclass->shutdown = GST_DEBUG_FUNCPTR(gst_es_video_dec_shutdown);

    gst_element_class_add_static_pad_template(element_class, &gst_es_video_dec_src_template);
    gst_element_class_add_static_pad_template(element_class, &gst_es_video_dec_sink_template);

    gst_element_class_set_static_metadata(element_class,
                                          "ESWIN video decoder",
                                          "Codec/Decoder/Video",
                                          "Multicodec (HEVC / AVC) hardware decoder",
                                          "Liujie <liujie@eswincomputing.com>");
}

gboolean gst_es_video_dec_register(GstPlugin *plugin, guint rank) {
    return gst_element_register(plugin, "esvideodec", rank, gst_es_video_dec_get_type());
}