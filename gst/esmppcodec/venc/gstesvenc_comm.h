#ifndef __GST_ES_VENC_COMM_H__
#define __GST_ES_VENC_COMM_H__

#include <gst/gst.h>
#include <mpp_frame.h>

#define FFALIGN(x, a) (((x) + (a)-1) & ~((a)-1))

guint64 gst_es_venc_get_picbufinfo(MppFrameFormat pix_fmt,
                                   guint32 width,
                                   guint32 height,
                                   guint32 align,
                                   guint32 alignHeight,
                                   guint32 *pStride,
                                   guint32 *pOffset,
                                   guint32 *pPlane);

#endif
