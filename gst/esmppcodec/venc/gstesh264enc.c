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
#include "gstesh264enc.h"

#define GST_ES_H264_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_H264_ENC, GstEsH264Enc))

#define GST_CAT_DEFAULT es_h264_enc_debug
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

struct _GstEsH264Enc {
    GstEsVenc parent;
    PROFILE_H264_E profile;
    gint level;
    gint enable_cabac;
};

#define parent_class gst_es_h264_enc_parent_class
G_DEFINE_TYPE(GstEsH264Enc, gst_es_h264_enc, GST_TYPE_ES_VENC);

#define H264_SET_PROPERTY(src, dst) \
    if (src == dst) {               \
        return;                     \
    }                               \
    dst = src

enum {
    PROP_0,
    PROFILE,
    LEVEL,
    CABAC,
};

#define GST_ES_H264_ENC_SIZE_CAPS "width  = (int) [ 144, 8192 ], height = (int) [ 128, 8192 ]"

static GstStaticPadTemplate gst_es_h264_enc_sink_template =
    GST_STATIC_PAD_TEMPLATE("src",
                            GST_PAD_SRC,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-h264, " GST_ES_H264_ENC_SIZE_CAPS ","
                                            "stream-format = (string) { byte-stream }, "));

static GstStaticPadTemplate gst_es_h264_enc_src_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw,"
                    "format = (string) { " ES_VENC_SUPPORT_FORMATS " }, " GST_ES_H264_ENC_SIZE_CAPS));

#define GST_TYPE_ES_H264_ENC_PROFILE (gst_es_h264_enc_profile_get_type())
static GType gst_es_h264_enc_profile_get_type(void) {
    static GType profile = 0;

    if (!profile) {
        static const GEnumValue profiles[] = {
            {PROFILE_H264_BASELINE, "Baseline", "baseline"},
            {PROFILE_H264_MAIN, "Main", "main"},
            {PROFILE_H264_HIGH, "High", "high"},
            {PROFILE_H264_HIGH10, "High10", "high10"},
            {0, NULL, NULL},
        };
        profile = g_enum_register_static("GstEsH264Profile", profiles);
    }
    return profile;
}

#define GST_TYPE_ES_H264_ENC_LEVEL (gst_es_h264_enc_level_get_type())
static GType gst_es_h264_enc_level_get_type(void) {
    static GType level = 0;

    if (!level) {
        static const GEnumValue levels[] = {
            {100, "1", "1"},     {101, "1b", "1b"},   {102, "1.1", "1.1"}, {103, "1.2", "1.2"}, {104, "1.3", "1.3"},
            {105, "2", "2"},     {106, "2.1", "2.1"}, {107, "2.2", "2.2"}, {108, "3", "3"},     {109, "3.1", "3.1"},
            {110, "3.2", "3.2"}, {111, "4", "4"},     {112, "4.1", "4.1"}, {113, "4.2", "4.2"}, {114, "5", "5"},
            {115, "5.1", "5.1"}, {116, "5.2", "5.2"}, {117, "6", "6"},     {118, "6.1", "6.1"}, {119, "6.2", "6.2"},
            {0, NULL, NULL},
        };
        level = g_enum_register_static("GstEsH264Level", levels);
    }
    return level;
}

static gboolean gst_es_h264_enc_set_src_caps(GstVideoEncoder *encoder) {
    GstStructure *structure;
    GstCaps *caps;

    caps = gst_caps_new_empty_simple("video/x-h264");

    structure = gst_caps_get_structure(caps, 0);
    gst_structure_set(structure, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
    return gst_es_enc_set_src_caps(encoder, caps);
}

static gboolean gst_es_h264_enc_apply_properties(GstVideoEncoder *encoder) {
    return gst_es_h264_enc_set_src_caps(encoder);
}

static gboolean gst_es_h264_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state) {
    GstVideoEncoderClass *pclass = GST_VIDEO_ENCODER_CLASS(parent_class);
    if (!pclass->set_format(encoder, state)) return FALSE;
    return gst_es_h264_enc_apply_properties(encoder);
}

static void gst_es_h264_enc_init(GstEsH264Enc *self) {
    self->parent.mpp_type = MPP_VIDEO_CodingAVC;
}

static void gst_es_h264_enc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsH264Enc *self = GST_ES_H264_ENC(encoder);
    GstEsVenc *enc = GST_ES_VENC(encoder);
    GstEsVencParam *params = &enc->params;

    switch (prop_id) {
        case PROFILE: {
            params->profile = g_value_get_enum(value);
            H264_SET_PROPERTY(params->profile, self->profile);
            break;
        }
        case LEVEL: {
            params->level = g_value_get_enum(value);
            H264_SET_PROPERTY(params->level, self->level);
            break;
        }
        case CABAC: {
            params->enable_cabac = g_value_get_int(value);
            H264_SET_PROPERTY(params->enable_cabac, self->enable_cabac);
            break;
        }
        default:
            break;
    }

    enc->prop_dirty = TRUE;
}

static void gst_es_h264_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsH264Enc *self = GST_ES_H264_ENC(encoder);

    switch (prop_id) {
        case PROFILE:
            g_value_set_enum(value, self->profile);
            break;
        case LEVEL:
            g_value_set_enum(value, self->level);
            break;
        case CABAC:
            g_value_set_int(value, self->enable_cabac);
            break;

        default:
            break;
    }
}

static void gst_es_h264_enc_class_init(GstEsH264EncClass *klass) {
    GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esh264enc", 0, "ES H264 encoder");

    // Set function pointers for class methods, if any
    encoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_h264_enc_set_format);
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_es_h264_enc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_es_h264_enc_get_property);

    g_object_class_install_property(gobject_class,
                                    PROFILE,
                                    g_param_spec_enum("profile",
                                                      "H264 profile",
                                                      "H264 profile",
                                                      GST_TYPE_ES_H264_ENC_PROFILE,
                                                      PROFILE_H264_MAIN,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    LEVEL,
                                    g_param_spec_enum("level",
                                                      "H264 level",
                                                      "H264 level",
                                                      GST_TYPE_ES_H264_ENC_LEVEL,
                                                      ES_H264_LEVEL_5,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    CABAC,
                                    g_param_spec_int("enable-cabac",
                                                     "H264 enable-cabac",
                                                     "0:enable cavlc, 1: enable cabac",
                                                     0,
                                                     1,
                                                     0,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_es_h264_enc_src_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_es_h264_enc_sink_template));

    gst_element_class_set_static_metadata(element_class,
                                          "ESWIN H264 encoder",
                                          "Codec/Encoder/Video",
                                          "H264 hardware encoder",
                                          "Lilijun <lilijun@eswincomputing.com>");
}

gboolean gst_es_h264_enc_register(GstPlugin *plugin, guint rank) {
    if (!gst_es_venc_supported(MPP_VIDEO_CodingAVC)) {
        return FALSE;
    }

    return gst_element_register(plugin, "esh264enc", rank, gst_es_h264_enc_get_type());
}
