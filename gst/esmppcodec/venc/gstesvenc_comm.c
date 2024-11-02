#include "gstesvenc_comm.h"

static guint32 gst_es_venc_get_bpp(MppFrameFormat pix_fmt) {
    uint8_t bpp[3] = {0, 0, 0};

    switch (pix_fmt) {
        case MPP_FMT_I420:
            bpp[0] = 8;
            bpp[1] = bpp[2] = 2;
            break;
        case MPP_FMT_NV12:
        case MPP_FMT_NV21:
            bpp[0] = 8;
            bpp[1] = 4;
            break;

        case MPP_FMT_UYVY:
        case MPP_FMT_YUY2:
            bpp[0] = 16;
            break;
        case MPP_FMT_P010:
            bpp[0] = 16;
            bpp[1] = bpp[2] = 4;
            break;
        case MPP_FMT_I010:
            bpp[0] = 16;
            bpp[1] = 8;
            break;
        default:
            return 0;
    }

    return (guint32)bpp[0] + bpp[1] + bpp[2];
}

#define ES_ALIGN_UP(x, a) ((((x) + ((a)-1)) / (a)) * (a))
guint64 gst_es_venc_get_picbufinfo(MppFrameFormat pix_fmt,
                                   guint32 width,
                                   guint32 height,
                                   guint32 align,
                                   guint32 alignHeight,
                                   guint32 *pStride,
                                   guint32 *pOffset,
                                   guint32 *pPlane) {
    guint32 bpp, plane, stride;
    guint32 uStride, vStride, uOffset, vOffset, alignWidth, strideAlign;

    bpp = gst_es_venc_get_bpp(pix_fmt);
    if (!bpp) return 0;
    alignWidth = (align > 0) ? ES_ALIGN_UP(width, align) : width;
    alignHeight = (alignHeight > 0) ? ES_ALIGN_UP(height, alignHeight) : height;
    strideAlign = (align < 2) ? 2 : ES_ALIGN_UP(align, 2);
    stride = ES_ALIGN_UP(alignWidth, strideAlign);

    switch (pix_fmt) {
        case MPP_FMT_NV12:
        case MPP_FMT_NV21:
            /*  WxH Y plane followed by (W)x(H/2) interleaved U/V plane. */
            stride = alignWidth;
            stride = ES_ALIGN_UP(stride, strideAlign);
            uStride = vStride = stride;
            uOffset = vOffset = stride * alignHeight;
            plane = 2;
            break;
        case MPP_FMT_I420:
            /*  WxH Y plane followed by (W/2)x(H/2) U and V planes. */
            uStride = vStride = (stride / 2);
            stride = ES_ALIGN_UP(stride, strideAlign);
            uStride = ES_ALIGN_UP(uStride, strideAlign / 2);
            vStride = ES_ALIGN_UP(vStride, strideAlign / 2);
            uOffset = stride * alignHeight;
            vOffset = uOffset + vStride * alignHeight / 2;
            plane = 3;
            break;
        case MPP_FMT_P010:
            /*  WxH Y plane followed by (W/2)x(H/2) U and V planes. */
            stride = alignWidth * 2;
            uStride = vStride = (stride / 2);
            stride = ES_ALIGN_UP(stride, strideAlign);
            uStride = ES_ALIGN_UP(uStride, strideAlign / 2);
            vStride = ES_ALIGN_UP(vStride, strideAlign / 2);
            uOffset = stride * alignHeight;
            vOffset = uOffset + uStride * alignHeight / 2;
            plane = 3;
            break;
        case MPP_FMT_I010:
            /*  WxH Y plane followed by (W)x(H/2) interleaved U/V plane. */
            stride = alignWidth * 2;
            stride = ES_ALIGN_UP(stride, strideAlign);
            uStride = vStride = stride;
            uOffset = vOffset = stride * alignHeight;
            plane = 2;
            break;
        default:
            stride = (alignWidth * bpp) / 8;
            uStride = vStride = 0;
            uOffset = vOffset = 0;
            plane = 1;
            break;
    }

    if (pStride) {
        pStride[0] = stride;
        if (plane > 1) pStride[1] = uStride;
        if (plane > 2) pStride[2] = vStride;
    }
    if (pOffset) {
        pOffset[0] = 0;
        if (plane > 1) pOffset[1] = uOffset;
        if (plane > 2) pOffset[2] = vOffset;
    }
    if (pPlane) {
        *pPlane = plane;
    }

    return (guint64)alignWidth * alignHeight * bpp / 8;
}