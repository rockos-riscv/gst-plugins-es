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

#ifndef __GST_ES_DEC_COMM_H__
#define __GST_ES_DEC_COMM_H__

#include "gstesvideodec.h"
#include "gst/video/video.h"
#include <es_mpp.h>
#include <mpp_frame.h>

typedef enum {
    PROP_0,
    PROP_OUT_FORMAT,
} ES_DEC_PROP_E;

GType get_format_type(void);

const gchar *gst_es_comm_dec_get_name_by_gst_video_format(const GstVideoFormat fmt);

gboolean gst_es_comm_dec_set_extra_data(GstEsDec *esdec);

gint gst_es_comm_dec_send_mpp_packet(GstEsDec *esdec, MppPacketPtr mpp_packet, gint timeout_ms);

gboolean gst_es_comm_dec_shutdown(GstEsDec *esdec, gboolean drain);

void gst_es_comm_dec_set_default_fmt(GstEsDec *esdec, const char *fmt_env);

void gst_es_comm_dec_set_property(GstEsDec *self, guint prop_id, const GValue *value, GParamSpec *pspec);

#endif