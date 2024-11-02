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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <gst/allocators/gstdmabuf.h>
#include "gstesallocator.h"
#include "mpp_buffer.h"

#define GST_TYPE_ES_ALLOCATOR (gst_es_allocator_get_type())
G_DECLARE_FINAL_TYPE(GstEsAllocator, gst_es_allocator, GST, ES_ALLOCATOR, GstDmaBufAllocator);

#define GST_ES_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ES_ALLOCATOR, GstEsAllocator))

#define GST_CAT_DEFAULT esallocator_debug
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);

struct _GstEsAllocator {
    GstDmaBufAllocator parent;
    MppBufferGroupPtr group;
    MppBufferGroupPtr ext_group;
    gint index;
};

#define gst_es_allocator_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstEsAllocator,
                        gst_es_allocator,
                        GST_TYPE_DMABUF_ALLOCATOR,
                        GST_DEBUG_CATEGORY_INIT(esallocator_debug, "es-codec-allocator", 0, "ESWIN Codecs Allocator"));

static GQuark get_buffer_quark(void) {
    static GQuark quark = 0;
    if (quark == 0) {
        quark = g_quark_from_string("es-buf");
    }
    return quark;
}

static GQuark get_ext_buffer_quark(void) {
    static GQuark quark = 0;
    if (quark == 0) {
        quark = g_quark_from_string("es-ext-buf");
    }
    return quark;
}

gint gst_es_allocator_get_index(GstAllocator *allocator) {
    GstEsAllocator *self = GST_ES_ALLOCATOR(allocator);
    return self->index;
}

MppBufferGroupPtr gst_es_allocator_get_mpp_group(GstAllocator *allocator) {
    GstEsAllocator *self = GST_ES_ALLOCATOR(allocator);
    return self->group;
}

MppBufferPtr get_mpp_buffer_from_gst_mem(GstMemory *gst_mem) {
    if (gst_mem->parent) {
        return get_mpp_buffer_from_gst_mem(gst_mem->parent);
    }
    return gst_mini_object_get_qdata(GST_MINI_OBJECT(gst_mem), get_buffer_quark());
}

static void destroy_mpp_buffer(gpointer ptr) {
    MppBufferPtr mpp_buffer = ptr;
    mpp_buffer_put(mpp_buffer);
}

static GstMemory *import_dmafd(GstAllocator *allocator, gint fd, guint size) {
    GstEsAllocator *self = GST_ES_ALLOCATOR(allocator);
    GstMemory *gst_mem;
    MppBufferInfo mpp_buf_info;
    MppBufferPtr mpp_buffer = NULL;

    GST_DEBUG_OBJECT(self, "import dmafd: fd: %d(0x%X) size: %d", fd, fd, size);

    memset(&mpp_buf_info, 0, sizeof(MppBufferInfo));
    mpp_buf_info.type = MPP_BUFFER_TYPE_DMA_HEAP;
    mpp_buf_info.fd = fd;
    mpp_buf_info.size = size;
    mpp_buffer_group_clear(self->ext_group);
    mpp_buffer_import_with_tag(self->ext_group, &mpp_buf_info, &mpp_buffer, NULL, __func__);
    if (!mpp_buffer) {
        return NULL;
    }

    mpp_buffer_set_index(mpp_buffer, self->index);
    gst_mem = gst_es_allocator_import_mppbuf(allocator, mpp_buffer);
    mpp_buffer_put(mpp_buffer);
    return gst_mem;
}

GstMemory *gst_es_allocator_import_mppbuf(GstAllocator *allocator, MppBufferPtr mpp_buf) {
    GstEsAllocator *self = GST_ES_ALLOCATOR(allocator);
    GstMemory *gst_mem;
    GQuark quark;
    guint size;
    gint fd;

    fd = mpp_buffer_get_fd(mpp_buf);
    if (fd < 0) {
        GST_ERROR_OBJECT(self, "Don't get valid fd from mpp buffer.");
        return NULL;
    }

    size = GST_ROUND_UP_N(mpp_buffer_get_size(mpp_buf), 4096);
    if (mpp_buffer_get_index(mpp_buf) != self->index) {
        gst_mem = import_dmafd(allocator, fd, size);
        quark = get_ext_buffer_quark();
    } else {
        gst_mem = gst_fd_allocator_alloc(allocator, dup(fd), size, GST_FD_MEMORY_FLAG_KEEP_MAPPED);
        quark = get_buffer_quark();
    }

    mpp_buffer_inc_ref(mpp_buf);
    gst_mini_object_set_qdata(GST_MINI_OBJECT(gst_mem), quark, mpp_buf, destroy_mpp_buffer);
    return gst_mem;
}

GstMemory *gst_es_allocator_import_gst_memory(GstAllocator *allocator, GstMemory *gst_mem) {
    MppBufferPtr mpp_buffer;
    gsize offset;
    guint size;
    gint fd;

    if (!gst_is_dmabuf_memory(gst_mem)) {
        return NULL;
    }
    mpp_buffer = get_mpp_buffer_from_gst_mem(gst_mem);
    if (mpp_buffer) {
        return gst_es_allocator_import_mppbuf(allocator, mpp_buffer);
    }
    fd = gst_dmabuf_memory_get_fd(gst_mem);
    if (fd < 0) {
        return NULL;
    }
    size = gst_memory_get_sizes(gst_mem, &offset, NULL);
    if (offset) {
        return NULL;
    }
    return import_dmafd(allocator, fd, size);
}

static MppBufferPtr alloc_mpp_buffer(GstAllocator *allocator, gsize size) {
    GstEsAllocator *self = GST_ES_ALLOCATOR(allocator);
    MppBufferPtr mpp_buffer = NULL;
    mpp_buffer_get(self->group, &mpp_buffer, size);
    mpp_buffer_set_index(mpp_buffer, self->index);
    return mpp_buffer;
}

static GstMemory *gst_es_allocator_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params) {
    GstMemory *gst_mem;
    MppBufferPtr mpp_buffer;
    (void)params;

    mpp_buffer = alloc_mpp_buffer(allocator, size);
    if (!mpp_buffer) {
        return NULL;
    }
    gst_mem = gst_es_allocator_import_mppbuf(allocator, mpp_buffer);
    mpp_buffer_put(mpp_buffer);
    gst_memory_resize(gst_mem, 0, size);
    return gst_mem;
}

static gpointer gst_es_allocator_mem_map_full(GstMemory *gst_mem, GstMapInfo *gst_map_info, gsize size) {
    if (gst_mem->parent) {
        return gst_es_allocator_mem_map_full(gst_mem->parent, gst_map_info, size);
    }
    if (GST_MEMORY_IS_NOT_MAPPABLE(gst_mem)) {
        return NULL;
    }
    return gst_mem->allocator->mem_map(gst_mem, size, gst_map_info->flags);
}

GstAllocator *gst_es_allocator_new(void) {
    GstEsAllocator *alloc;
    MppBufferGroupPtr group, ext_group;

    static gint num_mpp_alloc = 0;
    if (mpp_buffer_group_get_internal(&group, MPP_BUFFER_TYPE_DMA_HEAP)) {
        return FALSE;
    }
    if (mpp_buffer_group_get_external(&ext_group, MPP_BUFFER_TYPE_DMA_HEAP)) {
        mpp_buffer_group_put(group);
        return FALSE;
    }

    alloc = g_object_new(GST_TYPE_ES_ALLOCATOR, NULL);
    gst_object_ref_sink(alloc);
    alloc->group = group;
    alloc->ext_group = ext_group;
    alloc->index = num_mpp_alloc++;
    return GST_ALLOCATOR_CAST(alloc);
}

static void gst_es_allocator_finalize(GObject *obj) {
    GstEsAllocator *self = GST_ES_ALLOCATOR(obj);
    if (self->group) mpp_buffer_group_put(self->group);
    if (self->ext_group) mpp_buffer_group_put(self->ext_group);
    G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void gst_es_allocator_class_init(GstEsAllocatorClass *klass) {
    GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, "esallocator", 0, "ESWIN allocator");

    allocator_class->alloc = GST_DEBUG_FUNCPTR(gst_es_allocator_alloc);
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_es_allocator_finalize);
}

static void gst_es_allocator_init(GstEsAllocator *allocator) {
    GstAllocator *alloc = GST_ALLOCATOR_CAST(allocator);
    alloc->mem_type = "esallocator";
    alloc->mem_map_full = GST_DEBUG_FUNCPTR(gst_es_allocator_mem_map_full);
    GST_OBJECT_FLAG_SET(allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}
