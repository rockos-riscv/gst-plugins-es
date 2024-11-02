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
#include "gstesh265enc.h"

#define GST_ES_H265_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_H265_ENC, GstEsH265Enc))

#define GST_CAT_DEFAULT es_h265_enc_debug
GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

typedef struct _GstEsH265Enc {
    GstEsVenc parent;
    PROFILE_H265_E profile;
    gint level;
    gint tier;
    gint sent_frm_cnt;
} GstEsH265Enc;

#define parent_class gst_es_h265_enc_parent_class
G_DEFINE_TYPE(GstEsH265Enc, gst_es_h265_enc, GST_TYPE_ES_VENC);

#define ES_H265_ENC_SIZE_CAPS "width  = (int) [ 136, 8192 ], height = (int) [ 128, 8192 ]"

static GstStaticPadTemplate enc_h265_raw_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
                            GST_PAD_SRC,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-h264, " ES_H265_ENC_SIZE_CAPS ","
                                            "stream-format = (string) { byte-stream }, "));

static GstStaticPadTemplate enc_h265_sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw,"
                    "format = (string) { " ES_VENC_SUPPORT_FORMATS " }, " ES_H265_ENC_SIZE_CAPS));

#define H265_SET_PROPERTY(src, dst) \
    if (src == dst) {               \
        return;                     \
    }                               \
    dst = src

enum {
    PROP_0,
    PROFILE,
    LEVEL,
    TIER,
};

static gboolean gst_es_h265_enc_set_src_caps(GstVideoEncoder *encoder) {
    GstStructure *structure;
    GstCaps *caps;

    caps = gst_caps_new_empty_simple("video/x-h265");

    structure = gst_caps_get_structure(caps, 0);
    gst_structure_set(structure, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
    return gst_es_enc_set_src_caps(encoder, caps);
}

#define GST_TYPE_ES_H265_ENC_PROFILE (gst_es_h265_enc_profile_get_type())
static GType gst_es_h265_enc_profile_get_type(void) {
    static GType profile = 0;

    if (!profile) {
        static const GEnumValue profiles[] = {
            {PROFILE_H265_MAIN, "Main", "main"},
            {PROFILE_H265_MAIN10, "Main10", "main10"},
            {PROFILE_H265_MAIN_STILL_PICTURE, "MainPic", "mainPic"},
            {0, NULL, NULL},
        };
        profile = g_enum_register_static("GstEsH265Profile", profiles);
    }
    return profile;
}

#define GST_TYPE_ES_H265_ENC_LEVEL (gst_es_h265_enc_level_get_type())
static GType gst_es_h265_enc_level_get_type(void) {
    static GType level = 0;

    if (!level) {
        static const GEnumValue levels[] = {
            {1, "1", "1"},
            {2, "2", "2"},
            {3, "2.1", "2.1"},
            {4, "3", "3"},
            {5, "3.1", "3.1"},
            {6, "4", "4"},
            {7, "4.1", "4.1"},
            {8, "5", "5"},
            {9, "5.1", "5.1"},
            {10, "5.2", "5.2"},
            {11, "6", "6"},
            {12, "6.1", "6.1"},
            {13, "6.2", "6.2"},
            {0, NULL, NULL},
        };
        level = g_enum_register_static("GstEsH265Level", levels);
    }
    return level;
}
static gboolean gst_mpp_h265_enc_apply_properties(GstVideoEncoder *encoder) {
    //_apply_properties
    return gst_es_h265_enc_set_src_caps(encoder);
}

static gboolean gst_es_h265_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state) {
    GstVideoEncoderClass *pclass = GST_VIDEO_ENCODER_CLASS(parent_class);

    if (!pclass->set_format(encoder, state)) return FALSE;

    return gst_mpp_h265_enc_apply_properties(encoder);
}

static void gst_es_h265_enc_init(GstEsH265Enc *self) {
    self->parent.mpp_type = MPP_VIDEO_CodingHEVC;
}

static void gst_es_h265_enc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsH265Enc *self = GST_ES_H265_ENC(encoder);
    GstEsVenc *enc = GST_ES_VENC(encoder);
    GstEsVencParam *params = &enc->params;

    switch (prop_id) {
        case PROFILE: {
            params->profile = g_value_get_enum(value);
            H265_SET_PROPERTY(params->profile, self->profile);
            break;
        }
        case LEVEL: {
            params->level = g_value_get_enum(value);
            H265_SET_PROPERTY(params->level, self->level);
            break;
        }
        case TIER: {
            params->tier = g_value_get_int(value);
            H265_SET_PROPERTY(params->tier, self->tier);
            break;
        }
        default:
            break;
    }

    enc->prop_dirty = TRUE;
}

static void gst_es_h265_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER(object);
    GstEsH265Enc *self = GST_ES_H265_ENC(encoder);

    switch (prop_id) {
        case PROFILE:
            g_value_set_enum(value, self->profile);
            break;
        case LEVEL:
            g_value_set_enum(value, self->level);
            break;
        case TIER:
            g_value_set_int(value, self->tier);
            break;
        default:
            break;
    }
}

static GstFlowReturn gst_es_h265_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame) {
    GstVideoEncoderClass *pclass = GST_VIDEO_ENCODER_CLASS(parent_class);
    GstEsH265Enc *self = GST_ES_H265_ENC(encoder);
    GstEsVenc *enc = GST_ES_VENC(encoder);
    GstEsVencParam *params = &enc->params;

    if (params->profile == PROFILE_H265_MAIN_STILL_PICTURE && self->sent_frm_cnt >= 1) {
        GST_DEBUG_OBJECT(self, "drop gst frame");
        gst_video_encoder_finish_frame(encoder, frame);
        return GST_FLOW_EOS;
    }
    self->sent_frm_cnt++;

    return pclass->handle_frame(encoder, frame);
}

static void gst_es_h265_enc_class_init(GstEsH265EncClass *klass) {
    GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esh265enc", 0, "ES H265 encoder");

    // Set function pointers for class methods, if any
    encoder_class->set_format = GST_DEBUG_FUNCPTR(gst_es_h265_enc_set_format);
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_es_h265_enc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_es_h265_enc_get_property);
    encoder_class->handle_frame = GST_DEBUG_FUNCPTR(gst_es_h265_handle_frame);

    // setup_default_format();

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&enc_h265_raw_src_template));

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&enc_h265_sink_template));

    gst_element_class_set_static_metadata(element_class,
                                          "ESWIN H265 encoder",
                                          "Codec/Encoder/Video",
                                          "H265 hardware encoder",
                                          "<lilijun@eswincomputing.com>");
    g_object_class_install_property(gobject_class,
                                    PROFILE,
                                    g_param_spec_enum("profile",
                                                      "hevc profile",
                                                      "hevc profile",
                                                      GST_TYPE_ES_H265_ENC_PROFILE,
                                                      PROFILE_H265_MAIN,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class,
                                    LEVEL,
                                    g_param_spec_enum("level",
                                                      "hevc level",
                                                      "hevc level",
                                                      GST_TYPE_ES_H265_ENC_LEVEL,
                                                      ES_HEVC_LEVEL_5,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(
        gobject_class,
        TIER,
        g_param_spec_int("tier", "tier", "set tier", 0, 1, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

gboolean gst_es_h265_enc_register(GstPlugin *plugin, guint rank) {
    if (!gst_es_venc_supported(MPP_VIDEO_CodingHEVC)) {
        return FALSE;
    }

    return gst_element_register(plugin, "esh265enc", rank, gst_es_h265_enc_get_type());
}
