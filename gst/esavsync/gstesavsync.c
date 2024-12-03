/*
 * GStreamer
 * Copyright (C) 2015 Vivia Nikolaidou <vivia@toolsonair.com>
 *
 * Based on gstlevel.c:
 * Copyright (C) 2000,2001,2002,2003,2005
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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

/* Copyright © 2023 ESWIN. All rights reserved.
*
* Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
* all intellectual property and proprietary rights in and to this software. Except as expressly
* authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
* modified, adapted, translated, or created derivative work of, in whole or in part.
 */

/**
 * SECTION:element-esavsync
 * @title: esavsync
 *
 * This element acts like a synchronized audio/video "level". It gathers
 * all audio buffers sent between two video frames, and then sends a message
 * that contains the RMS value of all samples for these buffers.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location="test.mp4" ! decodebin name=d ! "audio/x-raw" ! queue ! audioconvert !
 * esavsync name=l ! queue ! autoaudiosink d. ! "video/x-raw" ! videoconvert ! queue ! l. l. ! queue ! autovideosink ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* FIXME 2.0: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstesavsync.h"
#include <stdio.h>
#include "lip_sync_api.h"

#define DEBUG_DUMP_FILE 1
#if DEBUG_DUMP_FILE
#define DUMP_BEFORE_SYNC_FILE_NAME "/tmp/audio/esavsync_audio_before_sync.pcm"
#define DUMP_AFTER_SYNC_FILE_NAME "/tmp/audio/esavsync_audio_after_sync.pcm"
static void gst_es_avsync_dump_data(const char *path, guint8 *buf, gsize bytes)
{
    if (!path) {
      return;
    }
    FILE *fp = fopen(path, "a+");
    if (fp) {
      fwrite(buf, 1, bytes, fp);
      fclose(fp);
    }
    return;
}
#endif

#define LIP_SYNC 1
#ifdef LIP_SYNC
typedef struct {
    GstEsAvSync *obj;
    GstBuffer *buffer;
}Frame_Data;


static guint g_chn = 0;
static int32_t gst_es_avsync_handle_cb(CALLBACK_TYPE type, void *data);

static int32_t gst_es_avsync_handle_cb(CALLBACK_TYPE type, void *data)
{
    ES_AVSync_AudioFrame *audio_frame = NULL;
    ES_AVSync_VideoFrame *video_frame = NULL;
    Frame_Data *frame_data = NULL;

    switch (type) {
    case ES_AUDIO_PLAYBACK:
        audio_frame = (ES_AVSync_AudioFrame *)data;
        frame_data = audio_frame->frame_data;
        if (!frame_data->obj->audio_eos_received) {
            #if DEBUG_DUMP_FILE
                GstMapInfo map;
                gst_buffer_map (frame_data->buffer, &map, GST_MAP_READ);
                gst_es_avsync_dump_data(DUMP_AFTER_SYNC_FILE_NAME,map.data, map.size);
                gst_buffer_unmap (frame_data->buffer, &map);
            #endif
                GST_DEBUG_OBJECT (frame_data->obj, "%s ES_AUDIO_PLAYBACK\n", __func__);
                gst_pad_push(frame_data->obj->asrcpad, frame_data->buffer);
        }
        break;

    case ES_VIDEO_DISPLAY:
        video_frame = (ES_AVSync_VideoFrame *)data;
        frame_data = video_frame->frame_data;
        if (!frame_data->obj->video_eos_received) {
            GST_DEBUG_OBJECT (frame_data->obj, "%s ES_VIDEO_DISPLAY\n", __func__);
            gst_pad_push(frame_data->obj->vsrcpad, frame_data->buffer);
        }
        break;

    case ES_AUDIO_RELEASE_BUFF:
        audio_frame = (ES_AVSync_AudioFrame *)data;
        frame_data = audio_frame->frame_data;
        GST_DEBUG_OBJECT (frame_data->obj, "%s ES_AUDIO_RELEASE_BUFF\n", __func__);
        gst_buffer_unref(frame_data->buffer);
        g_slice_free(Frame_Data, frame_data);
        break;

    case ES_VIDEO_RELEASE_BUFF:
        video_frame = (ES_AVSync_VideoFrame *)data;
        frame_data = video_frame->frame_data;
        GST_DEBUG_OBJECT (frame_data->obj, "%s ES_VIDEO_RELEASE_BUFF\n", __func__);
        gst_buffer_unref(frame_data->buffer);
        g_slice_free(Frame_Data, frame_data);
        break;

    default:
        break;
    }
    return 0;
}
#endif

#define GST_CAT_DEFAULT gst_es_avsync_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static GstStaticPadTemplate audio_sink_template =
GST_STATIC_PAD_TEMPLATE ("asink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw")
    );

static GstStaticPadTemplate audio_src_template =
GST_STATIC_PAD_TEMPLATE ("asrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw")
    );

static GstStaticPadTemplate video_sink_template =
GST_STATIC_PAD_TEMPLATE ("vsink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("vsrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

#define parent_class gst_es_avsync_parent_class
G_DEFINE_TYPE (GstEsAvSync, gst_es_avsync,
    GST_TYPE_ELEMENT);

static GstFlowReturn gst_es_avsync_asink_chain(GstPad * pad,
    GstObject * parent, GstBuffer * inbuf);
static GstFlowReturn gst_es_avsync_vsink_chain(GstPad * pad,
    GstObject * parent, GstBuffer * inbuf);

static GstIterator *gst_es_avsync_iterate_internal_links (GstPad *
    pad, GstObject * parent);

static void gst_avsync_finalize (GObject * object);

static void gst_es_avsync_class_init(GstEsAvSyncClass * klass)
{
    GstElementClass *gstelement_class;
    GObjectClass *gobject_class = (GObjectClass *) klass;

    GST_DEBUG_CATEGORY_INIT (gst_es_avsync_debug,
        "esavsync", 0, "Synchronized audio/video");

    gstelement_class = (GstElementClass *) klass;

    gst_element_class_set_static_metadata (gstelement_class,
        "esavsync", "Filter/Audio",
        "ES av sync",
        "http://eswin.com/");

    gobject_class->finalize = gst_avsync_finalize;
    gst_element_class_add_static_pad_template (gstelement_class,
        &audio_src_template);
    gst_element_class_add_static_pad_template (gstelement_class,
        &audio_sink_template);

    gst_element_class_add_static_pad_template (gstelement_class,
        &video_src_template);
    gst_element_class_add_static_pad_template (gstelement_class,
        &video_sink_template);
}

static void gst_avsync_finalize(GObject * object)
{
#ifdef LIP_SYNC
    GstEsAvSync *self = GST_ES_AVSYNC(object);

    GST_DEBUG_OBJECT(self, "%s ES_AVSync_Stop\n", __func__);
    ES_AVSync_Stop(self->chanId);
#endif

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean gst_es_avsync_audio_sink_event(GstPad * pad, GstObject * parent, GstEvent * event)
{
    GstEsAvSync *self = GST_ES_AVSYNC (parent);

    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
        GST_DEBUG_OBJECT (self, "EOS received on pad %s", GST_PAD_NAME (pad));

        self->audio_eos_received = TRUE;
    }

    return gst_pad_event_default (pad, parent, event);
}

static gboolean gst_es_avsync_video_sink_event(GstPad * pad, GstObject * parent, GstEvent * event)
{
    GstEsAvSync *self = GST_ES_AVSYNC (parent);

    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
        GST_DEBUG_OBJECT (self, "EOS received on pad %s", GST_PAD_NAME (pad));

        self->video_eos_received = TRUE;
    }

    return gst_pad_event_default (pad, parent, event);
}

static void gst_es_avsync_init(GstEsAvSync * self)
{
    self->asinkpad =
        gst_pad_new_from_static_template (&audio_sink_template, "asink");
    gst_pad_set_chain_function (self->asinkpad,
        GST_DEBUG_FUNCPTR (gst_es_avsync_asink_chain));
    gst_pad_set_iterate_internal_links_function (self->asinkpad,
        GST_DEBUG_FUNCPTR (gst_es_avsync_iterate_internal_links));
    gst_element_add_pad (GST_ELEMENT (self), self->asinkpad);

    self->vsinkpad =
        gst_pad_new_from_static_template (&video_sink_template, "vsink");
    gst_pad_set_chain_function (self->vsinkpad,
        GST_DEBUG_FUNCPTR (gst_es_avsync_vsink_chain));
    gst_pad_set_iterate_internal_links_function (self->vsinkpad,
        GST_DEBUG_FUNCPTR (gst_es_avsync_iterate_internal_links));
    gst_element_add_pad (GST_ELEMENT (self), self->vsinkpad);

    self->asrcpad =
        gst_pad_new_from_static_template (&audio_src_template, "asrc");
    gst_pad_set_iterate_internal_links_function (self->asrcpad,
        GST_DEBUG_FUNCPTR (gst_es_avsync_iterate_internal_links));
    gst_element_add_pad (GST_ELEMENT (self), self->asrcpad);

    self->vsrcpad =
        gst_pad_new_from_static_template (&video_src_template, "vsrc");
    gst_pad_set_iterate_internal_links_function (self->vsrcpad,
        GST_DEBUG_FUNCPTR (gst_es_avsync_iterate_internal_links));
    gst_element_add_pad (GST_ELEMENT (self), self->vsrcpad);

    GST_PAD_SET_PROXY_CAPS (self->asinkpad);
    GST_PAD_SET_PROXY_ALLOCATION (self->asinkpad);

    GST_PAD_SET_PROXY_CAPS (self->asrcpad);
    GST_PAD_SET_PROXY_SCHEDULING (self->asrcpad);

    GST_PAD_SET_PROXY_CAPS (self->vsinkpad);
    GST_PAD_SET_PROXY_ALLOCATION (self->vsinkpad);

    GST_PAD_SET_PROXY_CAPS (self->vsrcpad);
    GST_PAD_SET_PROXY_SCHEDULING (self->vsrcpad);

    gst_pad_set_event_function(self->asinkpad, GST_DEBUG_FUNCPTR(gst_es_avsync_audio_sink_event));
    gst_pad_set_event_function(self->vsinkpad, GST_DEBUG_FUNCPTR(gst_es_avsync_video_sink_event));

#ifdef LIP_SYNC
    self->chanId = g_chn++;
    ES_AVSync_Info avsync_info = { .clock_type = AUDIO_CLOCK, .buffer_capacity = 16, .sample_rate = 48000, .channels = 2, .bitdepth = 32};
    ES_AVSync_Init(self->chanId, &avsync_info);
    ES_AVSync_Playback_Register(self->chanId, gst_es_avsync_handle_cb);
    ES_AVSync_Start(self->chanId);
#endif

    self->audio_eos_received = FALSE;
    self->video_eos_received = FALSE;
}

static GstFlowReturn gst_es_avsync_vsink_chain(GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
    GstEsAvSync *self = GST_ES_AVSYNC (parent);

#ifdef LIP_SYNC
    ES_AVSync_VideoFrame video_frame;
    video_frame.pts = GST_BUFFER_PTS(buf) / 1000;
    video_frame.end_flag = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_LAST);

    Frame_Data *data = g_slice_new(Frame_Data);
    data->obj = self;
    data->buffer = gst_buffer_ref(buf);
    video_frame.frame_data = data;

    GST_DEBUG_OBJECT (self, "%s ES_Push_VideoFrame: pts:%lu, end_flag:%d\n", __func__, video_frame.pts, video_frame.end_flag);
    ES_Push_VideoFrame(self->chanId, &video_frame);

    return GST_FLOW_OK;
#else
    return gst_pad_push (self->vsrcpad, buf);
#endif
}

static GstFlowReturn gst_es_avsync_asink_chain(GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
    GstEsAvSync *self = GST_ES_AVSYNC (parent);

#if DEBUG_DUMP_FILE
    GstMapInfo map;
    gst_buffer_map (buf, &map, GST_MAP_READ);
    gst_es_avsync_dump_data(DUMP_BEFORE_SYNC_FILE_NAME,map.data, map.size);
    gst_buffer_unmap (buf, &map);
#endif

#ifdef LIP_SYNC
    ES_AVSync_AudioFrame audio_frame;

    audio_frame.pts = GST_BUFFER_PTS(buf) / 1000;
    audio_frame.size = gst_buffer_get_size(buf);
    audio_frame.end_flag = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_LAST);

    Frame_Data *data = g_slice_new(Frame_Data);
    data->obj = self;
    data->buffer = gst_buffer_ref(buf);
    audio_frame.frame_data = data;

    GST_DEBUG_OBJECT (self, "%s ES_Push_AudioFrame: pts:%lu, size:%d, end_flag:%d\n", __func__,
                      audio_frame.pts, audio_frame.size, audio_frame.end_flag);
    ES_Push_AudioFrame(self->chanId, &audio_frame);
    return GST_FLOW_OK;
#else
    return gst_pad_push (self->asrcpad, buf);
#endif
}

static GstIterator * gst_es_avsync_iterate_internal_links (GstPad * pad,
    GstObject * parent)
{
    GstIterator *it = NULL;
    GstPad *opad;
    GValue val = { 0, };
    GstEsAvSync *self = GST_ES_AVSYNC (parent);

    if (self->asinkpad == pad) {
        opad = gst_object_ref (self->asrcpad);
    }
    else if (self->asrcpad == pad)
        opad = gst_object_ref (self->asinkpad);
    else if (self->vsinkpad == pad)
        opad = gst_object_ref (self->vsrcpad);
    else if (self->vsrcpad == pad)
        opad = gst_object_ref (self->vsinkpad);
    else
        goto out;

    g_value_init (&val, GST_TYPE_PAD);
    g_value_set_object (&val, opad);
    it = gst_iterator_new_single (GST_TYPE_PAD, &val);
    g_value_unset (&val);

    gst_object_unref (opad);

    out:
    return it;
}

static gboolean gst_es_avsync_plugin_init (GstPlugin * plugin)
{
    return gst_element_register (plugin, "esavsync",
        GST_RANK_NONE, GST_TYPE_ES_AVSYNC);
}

#ifndef VERSION
#define VERSION "0.1"
#endif

#ifndef PACKAGE
#define PACKAGE "esavsync"
#endif

#ifndef PACKAGE_NAME
#define PACKAGE_NAME "esavsync"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://eswin.com/"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    esavsync,
    "Es av sync",
    gst_es_avsync_plugin_init, VERSION, "LGPL",
    PACKAGE_NAME, GST_PACKAGE_ORIGIN);
