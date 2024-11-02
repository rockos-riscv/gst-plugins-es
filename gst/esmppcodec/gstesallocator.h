/*
 * Copyright (C) <2024> Beijing ESWIN Computing Technology Co., Ltd.
 *     Author: Tangdaoyong <tangdaoyong@eswincomputing.com>
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

#ifndef __GST_ES_ALLOCATOR_H__
#define __GST_ES_ALLOCATOR_H__

#include <gst/video/video.h>
#include "mpp_type.h"

gint gst_es_allocator_get_index(GstAllocator *allocator);
MppBufferGroupPtr gst_es_allocator_get_mpp_group(GstAllocator *allocator);
GstMemory *gst_es_allocator_import_mppbuf(GstAllocator *allocator, MppBufferPtr mpp_buf);
GstMemory *gst_es_allocator_import_gst_memory(GstAllocator *allocator, GstMemory *gst_mem);
GstAllocator *gst_es_allocator_new(void);

MppBufferPtr get_mpp_buffer_from_gst_mem(GstMemory *gst_mem);

#endif
