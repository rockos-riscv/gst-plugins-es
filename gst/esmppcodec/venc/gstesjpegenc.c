/*
 * Copyright (C) <2024> Beijing ESWIN Computing Technology Co., Ltd.
 * Author: Tangdaoyong <tangdaoyong@eswincomputing.com>
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

#include "gstesjpegenc.h"

#define GST_ES_JPEG_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_JPEG_ENC, GstEsJpegEnc))

#define GST_CAT_DEFAULT es_jpeg_enc_debug
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

struct _GstEsJpegEnc {
    GstEsVenc parent;
    gint qfactor;
    gint qfactor_max;
    gint qfactor_min;
};

#define parent_class gst_es_jpeg_enc_parent_class
G_DEFINE_TYPE(GstEsJpegEnc, gst_es_jpeg_enc, GST_TYPE_ES_VENC);

#define JPEG_SET_PROPERTY(src, dst) \
    if (src == dst) {               \
        return;                     \
    }                               \
    dst = src

enum {
    PROP_0,
    RC_QFACTOR,
    RC_QFACTOR_MAX,
    RC_QFACTOR_MIN,
};

#define GST_ES_JPEG_ENC_SIZE_CAPS "width  = (int) [ 16, MAX ], height = (int) [ 16, MAX ]"

static GstStaticPadTemplate gst_es_jpeg_enc_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
                            GST_PAD_SRC,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("image/jpeg, " GST_ES_JPEG_ENC_SIZE_CAPS ","
                                            "sof-marker = { 0 }"));

static GstStaticPadTemplate gst_es_jpeg_enc_sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw,"
                    "format = (string) { " ES_VENC_SUPPORT_FORMATS " }, " GST_ES_JPEG_ENC_SIZE_CAPS));

static void gst_es_jpeg_enc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsJpegEnc *self = GST_ES_JPEG_ENC(encoder);
    GstEsVenc *jpeg_enc = GST_ES_VENC(encoder);
    GstEsVencParam *params = &jpeg_enc->params;

    switch (prop_id) {
        case RC_QFACTOR: {
            params->qfactor = g_value_get_int(value);
            JPEG_SET_PROPERTY(params->qfactor, self->qfactor);
            break;
        }
        case RC_QFACTOR_MAX: {
            params->qfactor_max = g_value_get_int(value);
            JPEG_SET_PROPERTY(params->qfactor_max, self->qfactor_max);
            break;
        }
        case RC_QFACTOR_MIN: {
            params->qfactor_min = g_value_get_int(value);
            JPEG_SET_PROPERTY(params->qfactor_min, self->qfactor_min);
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            return;
    }

    jpeg_enc->prop_dirty = TRUE;
}

static void gst_es_jpeg_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsJpegEnc *self = GST_ES_JPEG_ENC(encoder);

    switch (prop_id) {
        case RC_QFACTOR:
            g_value_set_int(value, self->qfactor);
            break;
        case RC_QFACTOR_MAX:
            g_value_set_int(value, self->qfactor_max);
            break;
        case RC_QFACTOR_MIN:
            g_value_set_int(value, self->qfactor_min);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

// static gboolean gst_mpp_jpeg_enc_apply_properties(GstVideoEncoder *encoder) {
//     GstMppJpegEnc *self = GST_MPP_JPEG_ENC(encoder);
//     GstMppEnc *mppenc = GST_MPP_ENC(encoder);

//     if (!mppenc->prop_dirty) return TRUE;

//     mpp_enc_cfg_set_s32(mppenc->mpp_cfg, "jpeg:quant", self->quant);

//     return gst_mpp_enc_apply_properties(encoder);
// }

static gboolean gst_es_jpeg_enc_set_src_caps(GstVideoEncoder *encoder) {
    //GstStructure *structure;
    GstCaps *caps;

    caps = gst_caps_new_empty_simple("image/jpeg");

    // structure = gst_caps_get_structure(caps, 0);
    // gst_structure_set(structure, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
    return gst_es_enc_set_src_caps(encoder, caps);
}

static gboolean gst_es_jpeg_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state) {
    GstVideoEncoderClass *pclass = GST_VIDEO_ENCODER_CLASS(parent_class);
    if (!pclass->set_format(encoder, state)) return FALSE;
    return gst_es_jpeg_enc_set_src_caps(encoder);
}

// static GstFlowReturn gst_mpp_jpeg_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame) {
//     GstVideoEncoderClass *pclass = GST_VIDEO_ENCODER_CLASS(parent_class);

//     if (G_UNLIKELY(!gst_mpp_jpeg_enc_apply_properties(encoder))) {
//         gst_video_codec_frame_unref(frame);
//         return GST_FLOW_NOT_NEGOTIATED;
//     }

//     return pclass->handle_frame(encoder, frame);
// }

static void gst_es_jpeg_enc_init(GstEsJpegEnc *self) {
    self->parent.mpp_type = MPP_VIDEO_CodingMJPEG;
}

static void gst_es_jpeg_enc_class_init(GstEsJpegEncClass *klass) {
    GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esjpegenc", 0, "ES JPEG encoder");

    encoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_jpeg_enc_set_format);
    // encoder_class->handle_frame = GST_DEBUG_FUNCPTR(gst_mpp_jpeg_enc_handle_frame);
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_es_jpeg_enc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_es_jpeg_enc_get_property);

    g_object_class_install_property(
        gobject_class,
        RC_QFACTOR,
        g_param_spec_int("qfactor", "Qfactor", "MJPEG qfactor", 1, 99, 90, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_QFACTOR_MAX,
        g_param_spec_int(
            "qfactor-max", "Max Qfactor", "MJPEG max qfactor", 1, 99, 99, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class,
        RC_QFACTOR_MIN,
        g_param_spec_int(
            "qfactor-min", "Min Qfactor", "MJPEG min qfactor", 1, 99, 20, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_es_jpeg_enc_src_template));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_es_jpeg_enc_sink_template));

    gst_element_class_set_static_metadata(element_class,
                                          "ESWIN JPEG encoder",
                                          "Codec/Encoder/Video",
                                          "JPEG hardware encoder",
                                          "Lilijun <lilijun@eswincomputing.com>");
}

gboolean gst_es_jpeg_enc_register(GstPlugin *plugin, guint rank) {
    if (!gst_es_venc_supported(MPP_VIDEO_CodingMJPEG)) return FALSE;

    return gst_element_register(plugin, "esjpegenc", rank, gst_es_jpeg_enc_get_type());
}
