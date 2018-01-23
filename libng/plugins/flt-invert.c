/*
 * simple libng filter -- just invert the image
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "grab-ng.h"

/* ------------------------------------------------------------------- */

static void inline
invert_bytes(unsigned char *dst, unsigned char *src, int bytes)
{
    while (bytes--)
	*(dst++) = 0xff - *(src++);
}

static void inline
invert_native_rgb15(void *d, void *s, int pixels)
{
    unsigned short *dst = d;
    unsigned short *src = s;
    unsigned short r,g,b;

    while (pixels--) {
	r = 0x1f - ((*src >> 10)  &  0x1f);
	g = 0x1f - ((*src >>  5)  &  0x1f);
	b = 0x1f - ( *src         &  0x1f);
	*dst = (r << 10) | (g << 5) | b;
	src++; dst++;
    }
}

static void inline
invert_native_rgb16(void *d, void *s, int pixels)
{
    unsigned short *dst = d;
    unsigned short *src = s;
    unsigned short r,g,b;

    while (pixels--) {
	r = 0x1f - ((*src >> 11)  &  0x1f);
	g = 0x3f - ((*src >>  5)  &  0x3f);
	b = 0x1f - ( *src         &  0x1f);
	*dst = (r << 11) | (g << 5) | b;
	src++; dst++;
    }
}

/* ------------------------------------------------------------------- */

static void *init(struct ng_video_fmt *out)
{
    /* don't have to carry around status info */
    static int dummy;
    return &dummy;
}

static struct ng_video_buf*
frame(void *handle, struct ng_video_buf *in)
{
    struct ng_video_buf *out;
    unsigned char *dst;
    unsigned char *src;
    unsigned int y,cnt;

    out = ng_malloc_video_buf(&in->fmt, in->fmt.height * in->fmt.bytesperline);
    out->info = in->info;

    dst = out->data;
    src = in->data;
    cnt = in->fmt.width * ng_vfmt_to_depth[in->fmt.fmtid] / 8;
    for (y = 0; y < in->fmt.height; y++) {
	switch (in->fmt.fmtid) {
	case VIDEO_GRAY:
	case VIDEO_BGR24:
	case VIDEO_RGB24:
	case VIDEO_BGR32:
	case VIDEO_RGB32:
	case VIDEO_YUYV:
	case VIDEO_UYVY:
	    invert_bytes(dst,src,cnt);
	    break;
	case VIDEO_RGB15_NATIVE:
	    invert_native_rgb15(dst,src,in->fmt.width);
	    break;
	case VIDEO_RGB16_NATIVE:
	    invert_native_rgb16(dst,src,in->fmt.width);
	    break;
	}
	dst += out->fmt.bytesperline;
	src += in->fmt.bytesperline;
    }

    ng_release_video_buf(in);
    return out;
}

static void fini(void *handle)
{
    /* nothing to clean up */
}

/* ------------------------------------------------------------------- */

static struct ng_filter filter = {
    name:    "invert",
    fmts:
    (1 << VIDEO_GRAY)         |
    (1 << VIDEO_RGB15_NATIVE) |
    (1 << VIDEO_RGB16_NATIVE) |
    (1 << VIDEO_BGR24)        |
    (1 << VIDEO_RGB24)        |
    (1 << VIDEO_BGR32)        |
    (1 << VIDEO_RGB32)        |
    (1 << VIDEO_YUYV)         |
    (1 << VIDEO_UYVY),
    init:    init,
    frame:   frame,
    fini:    fini,
};

extern void ng_plugin_init(void);
void ng_plugin_init(void)
{
    ng_filter_register(NG_PLUGIN_MAGIC,__FILE__,&filter);
}
