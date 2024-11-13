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

#include "gstesjpegdec.h"
#include "gstesdec_comm.h"

#define GST_ES_JPEG_DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_JPEG_DEC, GstEsJpegDec))

#define GST_CAT_DEFAULT es_jpeg_dec_debug
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);
struct _GstEsJpegDec {
    GstEsDec parent;
    gint poll_timeout;
};

#define parent_class gst_es_jpeg_dec_parent_class
G_DEFINE_TYPE(GstEsJpegDec, gst_es_jpeg_dec, GST_TYPE_ES_DEC);

/* GstVideoDecoder base class method */
static GstStaticPadTemplate gst_es_jpeg_dec_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
                            GST_PAD_SINK,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("image/jpeg,"
                                            "parsed = (boolean) true"
                                            ";"));

#define ES_JPEG_FORMATS     \
    "NV12, NV21, YUV420P, " \
    "BGR24, RGB24, GRAY8, " \
    "BGRA, RGBA, BGRx, RGBx"

static GstStaticPadTemplate gst_es_jpeg_dec_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
                            GST_PAD_SRC,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-raw, "
                                            "format = (string) {" ES_JPEG_FORMATS " }, "
                                            "width = (int) [ 48, 32768 ], height = (int) [ 48, 32768 ]"
                                            ";"));

static gboolean gst_es_jpeg_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state) {
    GstVideoDecoderClass *pclass = GST_VIDEO_DECODER_CLASS(parent_class);
    GstEsDec *esdec = GST_ES_DEC(decoder);

    esdec->mpp_coding_type = MPP_VIDEO_CodingMJPEG;
    return pclass->set_format(decoder, state);
}

static void gst_es_jpeg_dec_init(GstEsJpegDec *self) {
    GstEsDec *esdec = GST_ES_DEC(self);
    gst_es_comm_dec_set_default_fmt(esdec, "GST_ES_JPEG_DEC_DEF_FMT");
}

static gboolean gst_es_jpeg_dec_set_extra_data(GstVideoDecoder *decoder) {
    GstEsJpegDec *self = GST_ES_JPEG_DEC(decoder);
    GstEsDec *esdec = GST_ES_DEC(decoder);
    if (gst_es_comm_dec_set_extra_data(esdec)) {
        self->poll_timeout = 0;
        return TRUE;
    }
    return FALSE;
}

static MppPacketPtr gst_es_jpeg_dec_prepare_mpp_packet(GstVideoDecoder *decoder, GstMapInfo *mapinfo) {
    (void)(decoder);
    MppPacketPtr mpp_packet = NULL;
    mpp_packet_init(&mpp_packet, mapinfo->data, mapinfo->size);
    return mpp_packet;
}

static gint gst_es_jpeg_dec_send_mpp_packet(GstVideoDecoder *decoder, MppPacketPtr mpp_packet, gint timeout_ms) {
    GstEsDec *esdec = GST_ES_DEC(decoder);
    return gst_es_comm_dec_send_mpp_packet(esdec, mpp_packet, timeout_ms);
}

static MppFramePtr gst_es_jpeg_dec_get_mpp_frame(GstVideoDecoder *decoder, gint timeout_ms) {
    GstEsJpegDec *self = GST_ES_JPEG_DEC(decoder);
    GstEsDec *esdec = GST_ES_DEC(decoder);
    MppFramePtr mpp_frame = NULL;
    if (self->poll_timeout != timeout_ms) {
        self->poll_timeout = timeout_ms;
    }
    esmpp_get_frame(esdec->mpp_ctx, &mpp_frame, self->poll_timeout);
    return mpp_frame;
}

static gboolean gst_es_jpeg_dec_shutdown(GstVideoDecoder *decoder, gboolean drain) {
    GstEsDec *esdec = GST_ES_DEC(decoder);
    return gst_es_comm_dec_shutdown(esdec, drain);
}

static void gst_es_jpeg_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER(object);
    GstEsDec *self = GST_ES_DEC(decoder);
    gst_es_comm_dec_set_property(self, prop_id, value, pspec);
}

static void gst_es_jpeg_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
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

static void gst_es_jpeg_dec_class_init(GstEsJpegDecClass *klass) {
    GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS(klass);
    GstEsDecClass *pclass = GST_ES_DEC_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esjpegdec", 0, "ES JPEG decoder");

    decoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_set_format);

    pclass->set_extra_data = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_set_extra_data);
    pclass->prepare_mpp_packet = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_prepare_mpp_packet);
    pclass->send_mpp_packet = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_send_mpp_packet);
    pclass->get_mpp_frame = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_get_mpp_frame);
    pclass->shutdown = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_shutdown);

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_es_jpeg_dec_get_property);

    g_object_class_install_property(gobject_class,
                                    PROP_OUT_FORMAT,
                                    g_param_spec_string("format",
                                                        "Set the output format",
                                                        "NV12 NV21 I420 GRAY8 BGR RGB BGRA RGBA BGRx RGBx",
                                                        "RGBA",
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_es_jpeg_dec_src_template));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_es_jpeg_dec_sink_template));

    gst_element_class_set_static_metadata(element_class,
                                          "ESWIN video decoder",
                                          "Codec/Decoder/Video",
                                          "MJPEG hardware decoder",
                                          "Lijiangchuan <lijiangchuan@eswincomputing.com>");
}

gboolean gst_es_jpeg_dec_register(GstPlugin *plugin, guint rank) {
    return gst_element_register(plugin, "esjpegdec", rank, gst_es_jpeg_dec_get_type());
}