#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "grab-ng.h"

/*-------------------------------------------------------------------------*/
/* color space conversion / compression helper functions                   */

struct ng_convert_handle*
ng_convert_alloc(struct ng_video_conv *conv,
		 struct ng_video_fmt *i,
		 struct ng_video_fmt *o)
{
    struct ng_convert_handle *h;

    h = malloc(sizeof(*h));
    if (NULL == h)
	return 0;
    memset(h,0,sizeof(*h));

    /* fixup output image size to match incoming */
    o->width  = i->width;
    o->height = i->height;
    if (0 == o->bytesperline)
	o->bytesperline = o->width * ng_vfmt_to_depth[o->fmtid] / 8;

    h->ifmt = *i;
    h->ofmt = *o;
    if (conv)
	h->conv = conv;
    return h;
}

void
ng_convert_init(struct ng_convert_handle *h)
{
    if (0 == h->ifmt.bytesperline)
	h->ifmt.bytesperline = h->ifmt.width *
	    ng_vfmt_to_depth[h->ifmt.fmtid] / 8;
    if (0 == h->ofmt.bytesperline)
	h->ofmt.bytesperline = h->ofmt.width *
	    ng_vfmt_to_depth[h->ofmt.fmtid] / 8;

    h->isize = h->ifmt.height * h->ifmt.bytesperline;
    if (0 == h->isize)
	h->isize = h->ifmt.width * h->ifmt.height * 3;
    h->osize = h->ofmt.height * h->ofmt.bytesperline;
    if (0 == h->osize)
	h->osize = h->ofmt.width * h->ofmt.height * 3;

    if (h->conv)
	h->chandle = h->conv->init(&h->ofmt,h->conv->priv);

    if (ng_debug) {
	fprintf(stderr,"convert-in : %dx%d %s (size=%d)\n",
		h->ifmt.width, h->ifmt.height,
		ng_vfmt_to_desc[h->ifmt.fmtid], h->isize);
	fprintf(stderr,"convert-out: %dx%d %s (size=%d)\n",
		h->ofmt.width, h->ofmt.height,
		ng_vfmt_to_desc[h->ofmt.fmtid], h->osize);
    }
}

static void
ng_convert_copyframe(struct ng_video_buf *dest,
		     struct ng_video_buf *src)
{
    unsigned int i,sw,dw;
    unsigned char *sp,*dp;

    dw = dest->fmt.width * ng_vfmt_to_depth[dest->fmt.fmtid] / 8;
    sw = src->fmt.width * ng_vfmt_to_depth[src->fmt.fmtid] / 8;
    if (src->fmt.bytesperline == sw && dest->fmt.bytesperline == dw) {
	/* can copy in one go */
	memcpy(dest->data, src->data,
	       src->fmt.bytesperline * src->fmt.height);
    } else {
	/* copy line by line */
	dp = dest->data;
	sp = src->data;
	for (i = 0; i < src->fmt.height; i++) {
	    memcpy(dp,sp,dw);
	    dp += dest->fmt.bytesperline;
	    sp += src->fmt.bytesperline;
	}
    }
}

struct ng_video_buf*
ng_convert_frame(struct ng_convert_handle *h,
		 struct ng_video_buf *dest,
		 struct ng_video_buf *buf)
{
    if (NULL == buf)
	return NULL;

    if (NULL == dest && NULL != h->conv)
	dest = ng_malloc_video_buf(&h->ofmt,h->osize);

    if (NULL != dest) {
	dest->fmt  = h->ofmt;
	dest->size = h->osize;
	if (NULL != h->conv) {
	    h->conv->frame(h->chandle,dest,buf);
	} else {
	    ng_convert_copyframe(dest,buf);
	}
	dest->info = buf->info;
	ng_release_video_buf(buf);
	buf = dest;
    }
    return buf;
}

void
ng_convert_fini(struct ng_convert_handle *h)
{
    if (h->conv)
	h->conv->fini(h->chandle);
    free(h);
}

struct ng_video_buf*
ng_convert_single(struct ng_convert_handle *h, struct ng_video_buf *in)
{
    struct ng_video_buf *out;

    ng_convert_init(h);
    out = ng_convert_frame(h,NULL,in);
    ng_convert_fini(h);
    return out;
}

