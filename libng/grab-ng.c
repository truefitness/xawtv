/*
 * next generation[tm] xawtv capture interfaces
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#define NG_PRIVATE
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <fnmatch.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/types.h>

#include <dlfcn.h>
#ifndef RTLD_NOW
# define RTLD_NOW RTLD_LAZY
#endif

#include "get_media_devices.h"
#include "grab-ng.h"

int  ng_debug          = 0;
int  ng_chromakey      = 0x00ff00ff;
int  ng_jpeg_quality   = 75;
int  ng_ratio_x        = 4;
int  ng_ratio_y        = 3;

char ng_v4l_conf[256]  = "v4l-conf";

/* --------------------------------------------------------------------- */

const unsigned int ng_vfmt_to_depth[] = {
    0,               /* unused   */
    8,               /* RGB8     */
    8,               /* GRAY8    */
    16,              /* RGB15 LE */
    16,              /* RGB16 LE */
    16,              /* RGB15 BE */
    16,              /* RGB16 BE */
    24,              /* BGR24    */
    32,              /* BGR32    */
    24,              /* RGB24    */
    32,              /* RGB32    */
    16,              /* LUT2     */
    32,              /* LUT4     */
    16,		     /* YUYV     */
    16,		     /* YUV422P  */
    12,		     /* YUV420P  */
    0,		     /* MJPEG    */
    0,		     /* JPEG     */
    16,		     /* UYVY     */
};

const char* ng_vfmt_to_desc[] = {
    "none",
    "8 bit PseudoColor (dithering)",
    "8 bit StaticGray",
    "15 bit TrueColor (LE)",
    "16 bit TrueColor (LE)",
    "15 bit TrueColor (BE)",
    "16 bit TrueColor (BE)",
    "24 bit TrueColor (LE: bgr)",
    "32 bit TrueColor (LE: bgr-)",
    "24 bit TrueColor (BE: rgb)",
    "32 bit TrueColor (BE: -rgb)",
    "16 bit TrueColor (lut)",
    "32 bit TrueColor (lut)",
    "16 bit YUV 4:2:2 (packed, YUYV)",
    "16 bit YUV 4:2:2 (planar)",
    "12 bit YUV 4:2:0 (planar)",
    "MJPEG (AVI)",
    "JPEG (JFIF)",
    "16 bit YUV 4:2:2 (packed, UYVY)",
};

/* --------------------------------------------------------------------- */

const unsigned int   ng_afmt_to_channels[] = {
    0,  1,  2,  1,  2,  1,  2, 0
};
const unsigned int   ng_afmt_to_bits[] = {
    0,  8,  8, 16, 16, 16, 16, 0
};
const char* ng_afmt_to_desc[] = {
    "none",
    "8bit mono",
    "8bit stereo",
    "16bit mono (LE)",
    "16bit stereo (LE)",
    "16bit mono (BE)",
    "16bit stereo (BE)",
    "mp3 compressed audio",
};

/* --------------------------------------------------------------------- */

const char* ng_attr_to_desc[] = {
    "none",
    "norm",
    "input",
    "volume",
    "mute",
    "audio mode",
    "color",
    "bright",
    "hue",
    "contrast",
};

/* --------------------------------------------------------------------- */

void ng_init_video_buf(struct ng_video_buf *buf)
{
    memset(buf,0,sizeof(*buf));
    pthread_mutex_init(&buf->lock,NULL);
    pthread_cond_init(&buf->cond,NULL);
}

void ng_release_video_buf(struct ng_video_buf *buf)
{
    int release;

    pthread_mutex_lock(&buf->lock);
    buf->refcount--;
    release = (buf->refcount == 0);
    pthread_mutex_unlock(&buf->lock);
    if (release && NULL != buf->release)
	buf->release(buf);
}

void ng_wakeup_video_buf(struct ng_video_buf *buf)
{
    pthread_cond_signal(&buf->cond);
}

void ng_waiton_video_buf(struct ng_video_buf *buf)
{
    pthread_mutex_lock(&buf->lock);
    while (buf->refcount)
	pthread_cond_wait(&buf->cond, &buf->lock);
    pthread_mutex_unlock(&buf->lock);
}

static void ng_free_video_buf(struct ng_video_buf *buf)
{
    free(buf->data);
    free(buf);
}

struct ng_video_buf*
ng_malloc_video_buf(struct ng_video_fmt *fmt, int size)
{
    struct ng_video_buf *buf;

    buf = malloc(sizeof(*buf));
    if (NULL == buf)
	return NULL;
    ng_init_video_buf(buf);
    buf->fmt  = *fmt;
    buf->size = size;
    buf->data = malloc(size);
    if (NULL == buf->data) {
	free(buf);
	return NULL;
    }
    buf->refcount = 1;
    buf->release  = ng_free_video_buf;
    return buf;
}

/* --------------------------------------------------------------------- */

struct ng_audio_buf*
ng_malloc_audio_buf(struct ng_audio_fmt *fmt, int size)
{
    struct ng_audio_buf *buf;

    buf = malloc(sizeof(*buf)+size);
    memset(buf,0,sizeof(*buf));
    buf->fmt  = *fmt;
    buf->size = size;
    buf->data = (char*)buf + sizeof(*buf);
    return buf;
}

/* --------------------------------------------------------------------- */

struct ng_attribute*
ng_attr_byid(struct ng_attribute *attrs, int id)
{
    if (NULL == attrs)
	return NULL;
    for (;;) {
	if (NULL == attrs->name)
	    return NULL;
	if (attrs->id == id)
	    return attrs;
	attrs++;
    }
}

struct ng_attribute*
ng_attr_byname(struct ng_attribute *attrs, char *name)
{
    if (NULL == attrs)
	return NULL;
    for (;;) {
	if (NULL == attrs->name)
	    return NULL;
	if (0 == strcasecmp(attrs->name,name))
	    return attrs;
	attrs++;
    }
}

const char*
ng_attr_getstr(struct ng_attribute *attr, int value)
{
    int i;

    if (NULL == attr)
	return NULL;
    if (attr->type != ATTR_TYPE_CHOICE)
	return NULL;

    for (i = 0; attr->choices[i].str != NULL; i++)
	if (attr->choices[i].nr == value)
	    return attr->choices[i].str;
    return NULL;
}

int
ng_attr_getint(struct ng_attribute *attr, char *value)
{
    int i,val;

    if (NULL == attr)
	return -1;
    if (attr->type != ATTR_TYPE_CHOICE)
	return -1;

    for (i = 0; attr->choices[i].str != NULL; i++) {
	if (0 == strcasecmp(attr->choices[i].str,value))
	    return attr->choices[i].nr;
    }

    if (isdigit(value[0])) {
	/* Hmm.  String not found, but starts with a digit.
	   Check if this is a valid number ... */
	val = atoi(value);
	for (i = 0; attr->choices[i].str != NULL; i++)
	    if (val == attr->choices[i].nr)
		return attr->choices[i].nr;

    }
    return -1;
}

void
ng_attr_listchoices(struct ng_attribute *attr)
{
    int i;

    fprintf(stderr,"valid choices for \"%s\": ",attr->name);
    for (i = 0; attr->choices[i].str != NULL; i++)
	fprintf(stderr,"%s\"%s\"",
		i ? ", " : "",
		attr->choices[i].str);
    fprintf(stderr,"\n");
}

int
ng_attr_int2percent(struct ng_attribute *attr, int value)
{
    int range,percent;

    range   = attr->max - attr->min;
    percent = (value - attr->min) * 100 / range;
    if (percent < 0)
	percent = 0;
    if (percent > 100)
	percent = 100;
    return percent;
}

int
ng_attr_percent2int(struct ng_attribute *attr, int percent)
{
    int range,value;

    range = attr->max - attr->min;
    value = percent * range / 100 + attr->min;
    if (value < attr->min)
	value = attr->min;
    if (value > attr->max)
	value = attr->max;
    return value;
}

int
ng_attr_parse_int(struct ng_attribute *attr, char *str)
{
    int value,n;

    if (0 == sscanf(str,"%d%n",&value,&n))
	/* parse error */
	return attr->defval;
    if (str[n] == '%')
	value = ng_attr_percent2int(attr,value);
    if (value < attr->min)
	value = attr->min;
    if (value > attr->max)
	value = attr->max;
    return value;
}

/* --------------------------------------------------------------------- */

void
ng_ratio_fixup(int *width, int *height, int *xoff, int *yoff)
{
    int h = *height;
    int w = *width;

    if (0 == ng_ratio_x || 0 == ng_ratio_y)
	return;
    if (w * ng_ratio_y < h * ng_ratio_x) {
	*height = *width * ng_ratio_y / ng_ratio_x;
	if (yoff)
	    *yoff  += (h-*height)/2;
    } else if (w * ng_ratio_y > h * ng_ratio_x) {
	*width  = *height * ng_ratio_x / ng_ratio_y;
	if (yoff)
	    *xoff  += (w-*width)/2;
    }
}

void
ng_ratio_fixup2(int *width, int *height, int *xoff, int *yoff,
		int ratio_x, int ratio_y, int up)
{
    int h = *height;
    int w = *width;

    if (0 == ratio_x || 0 == ratio_y)
	return;
    if ((!up  &&  w * ratio_y < h * ratio_x) ||
	(up   &&  w * ratio_y > h * ratio_x)) {
	*height = *width * ratio_y / ratio_x;
	if (yoff)
	    *yoff  += (h-*height)/2;
    } else if ((!up  &&  w * ratio_y > h * ratio_x) ||
	       (up   &&  w * ratio_y < h * ratio_x)) {
	*width  = *height * ratio_x / ratio_y;
	if (yoff)
	    *xoff  += (w-*width)/2;
    }
}

/* --------------------------------------------------------------------- */

LIST_HEAD(ng_conv);
LIST_HEAD(ng_aconv);
LIST_HEAD(ng_filters);
LIST_HEAD(ng_writers);
LIST_HEAD(ng_readers);
LIST_HEAD(ng_vid_drivers);
LIST_HEAD(ng_dsp_drivers);
LIST_HEAD(ng_mix_drivers);

static int ng_check_magic(int magic, char *plugname, char *type)
{
    if (magic != NG_PLUGIN_MAGIC) {
	fprintf(stderr, "ERROR: plugin magic mismatch [xawtv=%d,%s=%d]\n",
		NG_PLUGIN_MAGIC,plugname,magic);
	return -1;
    }
#if 0
    if (ng_debug)
	fprintf(stderr,"plugins: %s registered by %s\n",type,plugname);
#endif
    return 0;
}

int
ng_conv_register(int magic, char *plugname,
		 struct ng_video_conv *list, int count)
{
    int n;

    if (0 != ng_check_magic(magic,plugname,"video converters"))
	return -1;
    for (n = 0; n < count; n++)
	list_add_tail(&(list[n].list),&ng_conv);
    return 0;
}

int
ng_aconv_register(int magic, char *plugname,
		  struct ng_audio_conv *list, int count)
{
    int n;

    if (0 != ng_check_magic(magic,plugname,"audio converters"))
	return -1;
    for (n = 0; n < count; n++)
	list_add_tail(&(list[n].list),&ng_aconv);
    return 0;
}

int
ng_filter_register(int magic, char *plugname, struct ng_filter *filter)
{
    if (0 != ng_check_magic(magic,plugname,"filter"))
	return -1;
    list_add_tail(&filter->list,&ng_filters);
    return 0;
}

int
ng_writer_register(int magic, char *plugname, struct ng_writer *writer)
{
    if (0 != ng_check_magic(magic,plugname,"writer"))
	return -1;
    list_add_tail(&writer->list,&ng_writers);
    return 0;
}

int
ng_reader_register(int magic, char *plugname, struct ng_reader *reader)
{
    if (0 != ng_check_magic(magic,plugname,"reader"))
	return -1;
    list_add_tail(&reader->list,&ng_readers);
    return 0;
}

int
ng_vid_driver_register(int magic, char *plugname, struct ng_vid_driver *driver)
{
    if (0 != ng_check_magic(magic,plugname,"video drv"))
	return -1;
    list_add_tail(&driver->list,&ng_vid_drivers);
    return 0;
}

int
ng_dsp_driver_register(int magic, char *plugname, struct ng_dsp_driver *driver)
{
    if (0 != ng_check_magic(magic,plugname,"dsp drv"))
	return -1;
    list_add_tail(&driver->list,&ng_dsp_drivers);
    return 0;
}

int
ng_mix_driver_register(int magic, char *plugname, struct ng_mix_driver *driver)
{
    if (0 != ng_check_magic(magic,plugname,"mixer drv"))
	return -1;
    list_add_tail(&driver->list,&ng_mix_drivers);
    return 0;
}

struct ng_video_conv*
ng_conv_find_to(unsigned int out, int *i)
{
    struct list_head *item;
    struct ng_video_conv *ret;
    int j = 0;

    list_for_each(item,&ng_conv) {
	if (j < *i) {
	    j++;
	    continue;
	}
	ret = list_entry(item, struct ng_video_conv, list);
#if 0
	fprintf(stderr,"\tconv to:  %-28s =>  %s\n",
		ng_vfmt_to_desc[ret->fmtid_in],
		ng_vfmt_to_desc[ret->fmtid_out]);
#endif
	if (ret->fmtid_out == out) {
	    (*i)++;
	    return ret;
	}
	(*i)++;
	j++;
    }
    return NULL;
}

struct ng_video_conv*
ng_conv_find_from(unsigned int in, int *i)
{
    struct list_head *item;
    struct ng_video_conv *ret;

    int j = 0;

    list_for_each(item,&ng_conv) {
	if (j < *i) {
	    j++;
	    continue;
	}
	ret = list_entry(item, struct ng_video_conv, list);
#if 0
	fprintf(stderr,"\tconv from:  %-28s =>  %s\n",
		ng_vfmt_to_desc[ret->fmtid_in],
		ng_vfmt_to_desc[ret->fmtid_out]);
#endif
	if (ret->fmtid_in == in) {
	    (*i)++;
	    return ret;
	}
    }
    return NULL;
}

struct ng_video_conv*
ng_conv_find_match(unsigned int in, unsigned int out)
{
    struct list_head *item;
    struct ng_video_conv *ret = NULL;

    list_for_each(item,&ng_conv) {
	ret = list_entry(item, struct ng_video_conv, list);
	if (ret->fmtid_in  == in && ret->fmtid_out == out)
	    return ret;
    }
    return NULL;
}

/* --------------------------------------------------------------------- */

#ifdef __linux__ /* Because this depends on get_media_devices.c */
static void *ng_vid_open_auto(struct ng_vid_driver *drv, char *devpath,
			      int allow_grabber)
{
    void *md, *handle = NULL;
    const char *device = NULL;
    const char *scan_type = "an analog TV";

    *devpath = 0;
    md = discover_media_devices();
    if (!md)
	goto error;

    /* Step 1: try TV cards first */
    while (1) {
	device = get_associated_device(md, device, MEDIA_V4L_VIDEO, NULL, NONE);
	if (!device)
	    break; /* No more video devices to try */
	snprintf(devpath, PATH_MAX, "/dev/%s", device);
	if (ng_debug)
	    fprintf(stderr,"vid-open-auto: trying: %s... \n", devpath);
	handle = (drv->open)(devpath, CAN_CAPTURE | CAN_TUNE);
	if (handle) {
		fprintf(stderr,"vid-open-auto: using analog TV device %s\n", devpath);
		break;
	}
    }

    /* Step 2: try grabber devices and webcams */
    if (!handle) {
	if (!allow_grabber)
	    goto error;
	scan_type = "a capture";
	device = NULL;
	while (1) {
	    device = get_associated_device(md, device, MEDIA_V4L_VIDEO, NULL, NONE);
	    if (!device)
		break; /* No more video devices to try */
	    snprintf(devpath, PATH_MAX, "/dev/%s", device);
	    if (ng_debug)
		fprintf(stderr,"vid-open-auto: trying: %s... \n", devpath);
	    handle = (drv->open)(devpath, CAN_CAPTURE);
	    if (handle) {
		fprintf(stderr,"vid-open-auto: using grabber/webcam device %s\n", devpath);
		break;
	    }
	}
    }
    free_media_devices(md);

error:
    if (!handle) {
	    fprintf(stderr, "vid-open-auto: failed to open %s device",
		    scan_type);
	    if (*devpath)
		    fprintf(stderr, " at %s\n", devpath);
	    else
		    fprintf(stderr, "\n");

	    return NULL;
    }

    if (handle && ng_debug)
	fprintf(stderr,"vid-open-auto: success, using: %s\n", devpath);

    return handle;
}
#endif

const struct ng_vid_driver*
ng_vid_open(char **device, char *driver, struct ng_video_fmt *screen,
	    void *base, void **handle)
{
    struct list_head *item;
    struct ng_vid_driver *drv;

    if (!driver) {
	fprintf (stderr, "Video4linux driver is not specified\n");
	return NULL;
    }

    /* check all grabber drivers */
    list_for_each(item,&ng_vid_drivers) {
	drv = list_entry(item, struct ng_vid_driver, list);
	if (strcasecmp(driver, drv->name) == 0)
	    break;
    }

    if (item == &ng_vid_drivers) {
	if (strcasecmp(driver, "help") != 0)
	    fprintf (stderr, "Cannot find %s video driver\n", driver);
	fprintf (stderr, "Available drivers:");
	list_for_each(item,&ng_vid_drivers) {
	    drv = list_entry(item, struct ng_vid_driver, list);
	    fprintf (stderr, " %s", drv->name);
	}
	fprintf (stderr, "\n");

	return NULL;
    }

#ifndef __linux__
    if (!strcmp(*device, "auto") || !strcmp(*device, "auto_tv"))
	*device = "/dev/bktr0";
#else
    if (!strcmp(*device, "auto") || !strcmp(*device, "auto_tv")) {
	char devpath[PATH_MAX];
	*handle = ng_vid_open_auto(drv, devpath,
				   !strcmp(*device, "auto_tv") ? 0 : 1);
	if (*handle == NULL) {
	    fprintf(stderr, "vid-open: could not find a suitable videodev\n");
	    return NULL;
	}
	*device = strdup(devpath);
    } else
#endif
    {
	if (ng_debug)
	    fprintf(stderr,"vid-open: trying: %s... \n", drv->name);
	if (!(*handle = (drv->open)(*device, 0))) {
	    fprintf(stderr,"vid-open: failed: %s\n", drv->name);
	    return NULL;
	}
	if (ng_debug)
	    fprintf(stderr,"vid-open: ok: %s\n", drv->name);
    }

    if (NULL != screen && drv->capabilities(*handle) & CAN_OVERLAY) {
#ifdef __linux__
	int l = strlen(ng_v4l_conf);

	snprintf(ng_v4l_conf + l, sizeof(ng_v4l_conf) - l, " -c %s", *device);

	if (ng_debug)
	    fprintf(stderr,"vid-open: closing dev to run v4lconf\n");
	drv->close(*handle);
	switch (system(ng_v4l_conf)) {
	case -1: /* can't run */
	    fprintf(stderr,"could'nt start v4l-conf\n");
	    break;
	case 0: /* ok */
	    break;
	default: /* non-zero return */
	    fprintf(stderr,"v4l-conf had some trouble, "
		    "trying to continue anyway\n");
	    break;
	}
	if (ng_debug)
	    fprintf(stderr,"vid-open: re-opening dev after v4lconf\n");
	if (!(*handle = (drv->open)(*device, 0))) {
	    fprintf(stderr,"vid-open: failed: %s\n", drv->name);
	    return NULL;
	}
	if (ng_debug)
	    fprintf(stderr,"vid-open: re-open ok\n");
#endif
	drv->setupfb(*handle,screen,base);
    }

    return drv;
}

const struct ng_dsp_driver*
ng_dsp_open(char *device, struct ng_audio_fmt *fmt, int record, void **handle)
{
    struct list_head *item;
    struct ng_dsp_driver *drv;

    /* check all dsp drivers */
    list_for_each(item,&ng_dsp_drivers) {
	drv = list_entry(item, struct ng_dsp_driver, list);
	if (NULL == drv->name)
	    continue;
	if (record && NULL == drv->read)
	    continue;
	if (!record && NULL == drv->write)
	    continue;
	if (ng_debug)
	    fprintf(stderr,"dsp-open: trying: %s... \n", drv->name);
	if (NULL != (*handle = (drv->open)(device,fmt,record)))
	    break;
	if (ng_debug)
	    fprintf(stderr,"dsp-open: failed: %s\n", drv->name);
    }
    if (item == &ng_dsp_drivers)
	return NULL;
    if (ng_debug)
	fprintf(stderr,"dsp-open: ok: %s\n",drv->name);
    return drv;
}

struct ng_attribute*
ng_mix_init(char *device, char *channel)
{
    struct list_head *item;
    struct ng_mix_driver *drv = NULL;
    struct ng_attribute *attrs = NULL;
    void *handle;

    /* check all mixer drivers */
    list_for_each(item, &ng_mix_drivers) {
	drv = list_entry(item, struct ng_mix_driver, list);
	if (ng_debug)
	    fprintf(stderr,"mix-init: trying: %s... \n", drv->name);
	if (NULL != (handle = (drv->open)(device))) {
	    if (NULL != (attrs = drv->volctl(handle,channel)))
		break;
	    drv->close(handle);
	}
	if (ng_debug)
	    fprintf(stderr,"mix-init: failed: %s\n",drv->name);
    }
    if (ng_debug && NULL != attrs)
	fprintf(stderr,"mix-init: ok: %s\n",drv->name);
    return attrs;
}

struct ng_reader* ng_find_reader(char *filename)
{
    struct list_head *item;
    struct ng_reader *reader;
    char blk[512];
    FILE *fp;
    int m;

    if (NULL == (fp = fopen(filename, "r"))) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	return NULL;
    }
    memset(blk,0,sizeof(blk));
    fread(blk,1,sizeof(blk),fp);
    fclose(fp);

    list_for_each(item,&ng_readers) {
	reader = list_entry(item, struct ng_reader, list);
	for (m = 0; m < 4 && reader->mlen[m] > 0; m++) {
	    if (0 == memcmp(blk+reader->moff[m],reader->magic[m],
			    reader->mlen[m]))
		return reader;
	}
    }
    if (ng_debug)
	fprintf(stderr,"%s: no reader found\n",filename);
    return NULL;
}

int64_t
ng_tofday_to_timestamp(struct timeval *tv)
{
    long long ts;

    ts  = tv->tv_sec;
    ts *= 1000000;
    ts += tv->tv_usec;
    ts *= 1000;
    return ts;
}

int64_t
ng_get_timestamp()
{
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return ng_tofday_to_timestamp(&tv);
}

struct ng_video_buf*
ng_filter_single(struct ng_filter *filter, struct ng_video_buf *in)
{
    struct ng_video_buf *out = in;
    void *handle;

    if (NULL != filter  &&  filter->fmts & (1 << in->fmt.fmtid)) {
	handle = filter->init(&in->fmt);
	out = filter->frame(handle,in);
	filter->fini(handle);
    }
    return out;
}

/* --------------------------------------------------------------------- */

static void clip_dump(char *state, struct OVERLAY_CLIP *oc, int count)
{
    int i;

    fprintf(stderr,"clip: %s - %d clips\n",state,count);
    for (i = 0; i < count; i++)
	fprintf(stderr,"clip:   %d: %dx%d+%d+%d\n",i,
		oc[i].x2 - oc[i].x1,
		oc[i].y2 - oc[i].y1,
		oc[i].x1, oc[i].y1);
}

static void clip_drop(struct OVERLAY_CLIP *oc, int n, int *count)
{
    (*count)--;
    memmove(oc+n, oc+n+1, sizeof(struct OVERLAY_CLIP) * (*count-n));
}

void ng_check_clipping(int width, int height, int xadjust, int yadjust,
		       struct OVERLAY_CLIP *oc, int *count)
{
    int i,j;

    if (ng_debug > 1) {
	fprintf(stderr,"clip: win=%dx%d xa=%d ya=%d\n",
		width,height,xadjust,yadjust);
	clip_dump("init",oc,*count);
    }
    for (i = 0; i < *count; i++) {
	/* fixup coordinates */
	oc[i].x1 += xadjust;
	oc[i].x2 += xadjust;
	oc[i].y1 += yadjust;
	oc[i].y2 += yadjust;
    }
    if (ng_debug > 1)
	clip_dump("fixup adjust",oc,*count);

    for (i = 0; i < *count; i++) {
	/* fixup borders */
	if (oc[i].x1 < 0)
	    oc[i].x1 = 0;
	if (oc[i].x2 < 0)
	    oc[i].x2 = 0;
	if (oc[i].x1 > width)
	    oc[i].x1 = width;
	if (oc[i].x2 > width)
	    oc[i].x2 = width;
	if (oc[i].y1 < 0)
	    oc[i].y1 = 0;
	if (oc[i].y2 < 0)
	    oc[i].y2 = 0;
	if (oc[i].y1 > height)
	    oc[i].y1 = height;
	if (oc[i].y2 > height)
	    oc[i].y2 = height;
    }
    if (ng_debug > 1)
	clip_dump("fixup range",oc,*count);

    /* drop zero-sized clips */
    for (i = 0; i < *count;) {
	if (oc[i].x1 == oc[i].x2 || oc[i].y1 == oc[i].y2) {
	    clip_drop(oc,i,count);
	    continue;
	}
	i++;
    }
    if (ng_debug > 1)
	clip_dump("zerosize done",oc,*count);

    /* try to merge clips */
 restart_merge:
    for (j = *count - 1; j >= 0; j--) {
	for (i = 0; i < *count; i++) {
	    if (i == j)
		continue;
	    if (oc[i].x1 == oc[j].x1 &&
		oc[i].x2 == oc[j].x2 &&
		oc[i].y1 <= oc[j].y1 &&
		oc[i].y2 >= oc[j].y1) {
		if (ng_debug > 1)
		    fprintf(stderr,"clip: merge y %d,%d\n",i,j);
		if (oc[i].y2 < oc[j].y2)
		    oc[i].y2 = oc[j].y2;
		clip_drop(oc,j,count);
		if (ng_debug > 1)
		    clip_dump("merge y done",oc,*count);
		goto restart_merge;
	    }
	    if (oc[i].y1 == oc[j].y1 &&
		oc[i].y2 == oc[j].y2 &&
		oc[i].x1 <= oc[j].x1 &&
		oc[i].x2 >= oc[j].x1) {
		if (ng_debug > 1)
		    fprintf(stderr,"clip: merge x %d,%d\n",i,j);
		if (oc[i].x2 < oc[j].x2)
		    oc[i].x2 = oc[j].x2;
		clip_drop(oc,j,count);
		if (ng_debug > 1)
		    clip_dump("merge x done",oc,*count);
		goto restart_merge;
	    }
	}
    }
    if (ng_debug)
	clip_dump("final",oc,*count);
}

/* --------------------------------------------------------------------- */

static int ng_plugins(char *dirname)
{
    struct dirent **list;
    char filename[1024];
    void *plugin;
    void (*initcall)(void);
    int i,n = 0,l = 0;

    n = scandir(dirname,&list,NULL,alphasort);
    if (n <= 0)
	return 0;
    for (i = 0; i < n; i++) {
	if (0 != fnmatch("*.so",list[i]->d_name,0))
	    continue;
	sprintf(filename,"%s/%s",dirname,list[i]->d_name);
	if (NULL == (plugin = dlopen(filename,RTLD_NOW))) {
	    fprintf(stderr,"dlopen: %s\n",dlerror());
	    continue;
	}
	if (NULL == (initcall = dlsym(plugin,"ng_plugin_init"))) {
	    if (NULL == (initcall = dlsym(plugin,"_ng_plugin_init"))) {
		fprintf(stderr,"dlsym[%s]: %s\n",filename,dlerror());
		continue;
	    }
	}
	initcall();
	l--;
    }
    for (i = 0; i < n; i++)
	free(list[i]);
    free(list);
    return l;
}

void
ng_init(void)
{
    static int once=0;
    int count=0;

    if (once++) {
	fprintf(stderr,"panic: ng_init called twice\n");
	exit(1);
    }
    ng_device_init();
    ng_color_packed_init();
    ng_color_yuv2rgb_init();
    ng_writefile_init();

    count += ng_plugins(LIBDIR);
    if (0 == count) {
	/* nice for development */
	count += ng_plugins("../libng/plugins");
	count += ng_plugins("../libng/contrib-plugins");
    }
    if (0 == count)
	fprintf(stderr,"WARNING: no plugins found [%s]\n",LIBDIR);
}
