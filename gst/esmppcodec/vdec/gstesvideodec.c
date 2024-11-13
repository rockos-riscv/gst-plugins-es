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
#include "gstesdec_comm.h"

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
    if (gst_es_comm_dec_set_extra_data(esdec)) {
        self->poll_timeout = 0;
        return TRUE;
    }
    return FALSE;
}

static MppPacketPtr gst_es_video_dec_prepare_mpp_packet(GstVideoDecoder *decoder, GstMapInfo *mapinfo) {
    (void)(decoder);
    MppPacketPtr mpp_packet = NULL;
    mpp_packet_init(&mpp_packet, mapinfo->data, mapinfo->size);
    return mpp_packet;
}

static gint gst_es_video_dec_send_mpp_packet(GstVideoDecoder *decoder, MppPacketPtr mpp_packet, gint timeout_ms) {
    GstEsDec *esdec = GST_ES_DEC(decoder);
    return gst_es_comm_dec_send_mpp_packet(esdec, mpp_packet, timeout_ms);
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
    return gst_es_comm_dec_shutdown(esdec, drain);
}

static void gst_es_video_dec_init(GstEsVideoDec *self) {
    GstEsDec *esdec = GST_ES_DEC(self);
    gst_es_comm_dec_set_default_fmt(esdec, "GST_ES_VIDEO_DEC_DEF_FMT");
}

static void gst_es_video_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER(object);
    GstEsDec *self = GST_ES_DEC(decoder);
    gst_es_comm_dec_set_property(self, prop_id, value, pspec);
}

static void gst_es_video_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER(object);
    GstEsDec *self = GST_ES_DEC(decoder);

    switch (prop_id) {
        case PROP_OUT_FORMAT: {
            g_value_set_string(value, gst_es_comm_dec_get_name_by_gst_video_format(self->out_format));
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            return;
    }
}

static void gst_es_video_dec_class_init(GstEsVideoDecClass *klass) {
    GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS(klass);
    GstEsDecClass *pclass = GST_ES_DEC_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esvideodec", 0, "ESWIN video decoder");

    decoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_video_dec_set_format);
    pclass->set_extra_data = GST_DEBUG_FUNCPTR(gst_es_video_dec_set_extra_data);
    pclass->prepare_mpp_packet = GST_DEBUG_FUNCPTR(gst_es_video_dec_prepare_mpp_packet);
    pclass->send_mpp_packet = GST_DEBUG_FUNCPTR(gst_es_video_dec_send_mpp_packet);
    pclass->get_mpp_frame = GST_DEBUG_FUNCPTR(gst_es_video_dec_get_mpp_frame);
    pclass->shutdown = GST_DEBUG_FUNCPTR(gst_es_video_dec_shutdown);

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_es_video_dec_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_es_video_dec_get_property);

    g_object_class_install_property(gobject_class,
                                    PROP_OUT_FORMAT,
                                    g_param_spec_string("format",
                                                        "Set the output format",
                                                        "NV12 NV21 I420 GRAY8 BGR RGB BGRA RGBA BGRx RGBx P010_10LE",
                                                        "RGBA",
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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