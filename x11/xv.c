/*
 * (most) Xvideo extention code is here.
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#ifdef HAVE_LIBXV

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "grab-ng.h"
#include "commands.h"    /* FIXME: global *drv vars */
#include "atoms.h"
#include "xv.h"

extern Display    *dpy;
int               have_xv;

const struct ng_vid_driver xv_driver;

static int              ver, rel, req, ev, err, grabbed;
static int              adaptors;
static int              attributes;
static XvAdaptorInfo        *ai;
static XvEncodingInfo       *ei;
static XvAttribute          *at;

static int
xv_overlay(void *handle, struct ng_video_fmt *fmt, int x, int y,
	   struct OVERLAY_CLIP *oc, int count, int aspect)
{
    if (debug)
	fprintf(stderr,"Ouch: xv_overlay called\n");
    return 0;
}

/* ********************************************************************* */

struct ENC_MAP {
    int norm;
    int input;
    int encoding;
};

struct xv_handle {
    /* port */
    int                  vi_adaptor;
    XvPortID             vi_port;
    GC                   vi_gc;

    /* attributes */
    int                  nattr;
    struct ng_attribute  *attr;
    Atom xv_encoding;
    Atom xv_freq;
    Atom xv_colorkey;

    /* encoding */
    struct ENC_MAP       *enc_map;
    int                  norm, input, enc;
    int                  encodings;
};

static const struct XVATTR {
    int   id;
    int   type;
    char  *atom;
} xvattr[] = {
    { ATTR_ID_COLOR,    ATTR_TYPE_INTEGER, "XV_COLOR"       },
    { ATTR_ID_COLOR,    ATTR_TYPE_INTEGER, "XV_SATURATION"  },
    { ATTR_ID_HUE,      ATTR_TYPE_INTEGER, "XV_HUE",        },
    { ATTR_ID_BRIGHT,   ATTR_TYPE_INTEGER, "XV_BRIGHTNESS", },
    { ATTR_ID_CONTRAST, ATTR_TYPE_INTEGER, "XV_CONTRAST",   },
    { -1,               ATTR_TYPE_BOOL,    "XV_CHROMA_AGC", },
    { -1,               ATTR_TYPE_BOOL,    "XV_COMBFILTER", },
    { -1,               ATTR_TYPE_BOOL,    "XV_AUTOMUTE",   },
    { -1,               ATTR_TYPE_BOOL,    "XV_LUMA_DECIMATION_FILTER", },
    { -1,               ATTR_TYPE_BOOL,    "XV_AGC_CRUSH",  },
    { -1,               ATTR_TYPE_BOOL,    "XV_VCR_HACK",   },
    { -1,               ATTR_TYPE_BOOL,    "XV_FULL_LUMA_RANGE", },
    { ATTR_ID_MUTE,     ATTR_TYPE_BOOL,    "XV_MUTE",       },
    { -1,               ATTR_TYPE_INTEGER, "XV_BALANCE",    },
    { -1,               ATTR_TYPE_INTEGER, "XV_BASS",       },
    { -1,               ATTR_TYPE_INTEGER, "XV_TREBLE",     },
    { ATTR_ID_VOLUME,   ATTR_TYPE_INTEGER, "XV_VOLUME",     },
    { -1,               -1,                "XV_COLORKEY",   },
    { -1,               -1,                "XV_AUTOPAINT_COLORKEY", },
    { -1,               -1,                "XV_FREQ",       },
    { -1,               -1,                "XV_ENCODING",   },
    { -1,               -1,                "XV_WHITECRUSH_UPPER", },
    { -1,               -1,                "XV_WHITECRUSH_LOWER", },
    { -1,               -1,                "XV_UV_RATIO",   },
    { -1,               -1,                "XV_CORING",     },
    { -1,               -1,                "XV_AUTOPAINT_COLORKEY", },
    { -1,               -1,                "XV_SET_DEFAULTS", },
    { -1,               -1,                "XV_ITURBT_709", },
    {}
};

static int xv_read_attr(struct ng_attribute *attr)
{
    struct xv_handle *h   = attr->handle;
    const XvAttribute *at = attr->priv;
    Atom atom;
    int value = 0;

    if (NULL != at) {
	atom = XInternAtom(dpy, at->name, False);
	XvGetPortAttribute(dpy, h->vi_port,atom,&value);
	if (debug)
	    fprintf(stderr,"xv: get %s: %d\n",at->name,value);

    } else if (attr->id == ATTR_ID_NORM) {
	value = h->norm;

    } else if (attr->id == ATTR_ID_INPUT) {
	value = h->input;

    }
    return value;
}

static void xv_write_attr(struct ng_attribute *attr, int value)
{
    struct xv_handle *h   = attr->handle;
    const XvAttribute *at = attr->priv;
    Atom atom;
    int i;

    if (NULL != at) {
	atom = XInternAtom(dpy, at->name, False);
	XvSetPortAttribute(dpy, h->vi_port,atom,value);
	if (debug)
	    fprintf(stderr,"xv: set %s: %d\n",at->name,value);

    } else if (attr->id == ATTR_ID_NORM || attr->id == ATTR_ID_INPUT) {
	if (attr->id == ATTR_ID_NORM)
	    h->norm  = value;
	if (attr->id == ATTR_ID_INPUT)
	    h->input = value;
	for (i = 0; i < h->encodings; i++) {
	    if (h->enc_map[i].norm  == h->norm &&
		h->enc_map[i].input == h->input) {
		h->enc = i;
		XvSetPortAttribute(dpy,h->vi_port,h->xv_encoding,h->enc);
		break;
	    }
	}
    }
    /* needed for proper timing on the
       "mute - wait - switch - wait - unmute" channel switches */
    XSync(dpy,False);
}

static void
xv_add_attr(struct xv_handle *h, int id, int type,
	    int defval, struct STRTAB *choices, XvAttribute *at)
{
    int i;

    h->attr = realloc(h->attr,(h->nattr+2) * sizeof(struct ng_attribute));
    memset(h->attr+h->nattr,0,sizeof(struct ng_attribute)*2);
    if (at) {
	h->attr[h->nattr].priv    = at;
	for (i = 0; xvattr[i].atom != NULL; i++)
	    if (0 == strcmp(xvattr[i].atom,at->name))
		break;
	if (-1 == xvattr[i].type)
	    /* ignore this one */
	    return;
	if (NULL != xvattr[i].atom) {
	    h->attr[h->nattr].id      = xvattr[i].id;
	    h->attr[h->nattr].type    = xvattr[i].type;
	    h->attr[h->nattr].priv    = at;
	    if (ATTR_TYPE_INTEGER == h->attr[h->nattr].type) {
		h->attr[h->nattr].min = at->min_value;
		h->attr[h->nattr].max = at->max_value;
	    }
	} else {
	    /* unknown */
	    return;
	}
    }

    if (id)
	h->attr[h->nattr].id      = id;
    if (type)
	h->attr[h->nattr].type    = type;
    if (defval)
	h->attr[h->nattr].defval  = defval;
    if (choices)
	h->attr[h->nattr].choices = choices;
    if (h->attr[h->nattr].id < ATTR_ID_COUNT)
	h->attr[h->nattr].name    = ng_attr_to_desc[h->attr[h->nattr].id];

    h->attr[h->nattr].read    = xv_read_attr;
    h->attr[h->nattr].write   = xv_write_attr;
    h->attr[h->nattr].handle  = h;
    h->nattr++;
}

static unsigned long
xv_getfreq(void *handle)
{
    struct xv_handle *h = handle;
    unsigned int freq;

    XvGetPortAttribute(dpy,h->vi_port,h->xv_freq,&freq);
    return freq;
}

static void
xv_setfreq(void *handle, unsigned long freq)
{
    struct xv_handle *h = handle;

    XvSetPortAttribute(dpy,h->vi_port,h->xv_freq,freq);
    XSync(dpy,False);
}

static int
xv_tuned(void *handle)
{
    /* don't know ... */
    return 0;
}

void
xv_video(Window win, int dw, int dh, int on)
{
    struct xv_handle *h = h_drv; /* FIXME */
    int sx,sy,dx,dy;
    int sw,sh;

    if (on) {
	sx = sy = dx = dy = 0;
	sw = dw;
	sh = dh;
	if (-1 != h->enc) {
	    sw = ei[h->enc].width;
	    sh = ei[h->enc].height;
	}
	if (NULL == h->vi_gc)
	    h->vi_gc = XCreateGC(dpy, win, 0, NULL);
	ng_ratio_fixup(&dw,&dh,&dx,&dy);
	if (0 == grabbed)
	    if (Success == XvGrabPort(dpy,h->vi_port,CurrentTime))
		grabbed = 1;
	if (1 == grabbed) {
	    XvPutVideo(dpy,h->vi_port,win,h->vi_gc,
		       sx,sy,sw,sh, dx,dy,dw,dh);
	    if (debug)
		fprintf(stderr,"Xvideo: video: win=0x%lx, "
			"src=%dx%d+%d+%d dst=%dx%d+%d+%d\n",
			win, sw,sh,sx,sy, dw,dh,dx,dy);
	} else {
	    fprintf(stderr,"Xvideo: port %ld busy\n",h->vi_port);
	}
    } else {
	if (grabbed) {
	    XClearArea(dpy,win,0,0,0,0,False);
	    XvStopVideo(dpy,h->vi_port,win);
	    XvUngrabPort(dpy,h->vi_port,CurrentTime);
	    grabbed = 0;
	    if (debug)
		fprintf(stderr,"Xvideo: video off\n");
	}
    }
}

static int
xv_strlist_add(struct STRTAB **tab, char *str)
{
    int i;

    if (NULL == *tab) {
	*tab = malloc(sizeof(struct STRTAB)*2);
	i = 0;
    } else {
	for (i = 0; (*tab)[i].str != NULL; i++)
	    if (0 == strcasecmp((*tab)[i].str,str))
		return (*tab)[i].nr;
	*tab = realloc(*tab,sizeof(struct STRTAB)*(i+2));
    }
    (*tab)[i].nr  = i;
    (*tab)[i].str = strdup(str);
    (*tab)[i+1].nr  = -1;
    (*tab)[i+1].str = NULL;
    return i;
}

static int xv_close(void *handle) { return 0; }

static int xv_flags(void *handle)
{
    struct xv_handle *h = handle;
    int ret = 0;

    ret |= CAN_OVERLAY;
    if (h->xv_freq != None)
	ret |= CAN_TUNE;
    return ret;
}

static struct ng_attribute* xv_attrs(void *handle)
{
    struct xv_handle *h  = handle;
    return h->attr;
}

/* ********************************************************************* */

void xv_video_init(unsigned int port, int hwscan)
{
    struct xv_handle *handle;
    struct STRTAB *norms  = NULL;
    struct STRTAB *inputs = NULL;
    char *h;
    int n, i, vi_port = -1, vi_adaptor = -1;

    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	if (debug)
	    fprintf(stderr,"Xvideo: Server has no Xvideo extention support\n");
	return;
    }
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	fprintf(stderr,"Xvideo: XvQueryAdaptors failed");
	exit(1);
    }
    if (debug)
	fprintf(stderr,"Xvideo: %d adaptors available.\n",adaptors);
    for (i = 0; i < adaptors; i++) {
	if (debug)
	    fprintf(stderr,"Xvideo: %s:%s%s%s%s%s, ports %ld-%ld\n",
		    ai[i].name,
		    (ai[i].type & XvInputMask)  ? " input"  : "",
		    (ai[i].type & XvOutputMask) ? " output" : "",
		    (ai[i].type & XvVideoMask)  ? " video"  : "",
		    (ai[i].type & XvStillMask)  ? " still"  : "",
		    (ai[i].type & XvImageMask)  ? " image"  : "",
		    ai[i].base_id,
		    ai[i].base_id+ai[i].num_ports-1);
	if (hwscan) {
	    /* just print some info's about the Xvideo port */
	    n = fprintf(stderr,"port %ld-%ld",
			ai[i].base_id,ai[i].base_id+ai[i].num_ports-1);
	    if ((ai[i].type & XvInputMask) &&
		(ai[i].type & XvVideoMask))
		fprintf(stderr,"%*s[ -xvport %ld ]",40-n,"",ai[i].base_id);
	    fprintf(stderr,"\n");
	    if ((ai[i].type & XvInputMask) &&
		(ai[i].type & XvVideoMask))
		fprintf(stderr,"    type : Xvideo, video overlay\n");
	    if ((ai[i].type & XvInputMask) &&
		(ai[i].type & XvImageMask))
		fprintf(stderr,"    type : Xvideo, image scaler\n");
	    fprintf(stderr,"    name : %s\n",ai[i].name);
	    fprintf(stderr,"\n");
	    continue;
	}

	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvVideoMask) &&
	    (vi_port == -1)) {
	    if (0 == port) {
		vi_port = ai[i].base_id;
		vi_adaptor = i;
	    } else if (port >= ai[i].base_id  &&
		       port <  ai[i].base_id+ai[i].num_ports) {
		vi_port = port;
		vi_adaptor = i;
	    } else {
		if (debug)
		    fprintf(stderr,"Xvideo: skipping ports %ld-%ld (configured other: %d)\n",
			    ai[i].base_id, ai[i].base_id+ai[i].num_ports-1, port);
	    }
	}
    }
    if (hwscan)
	return;

    /* *** video port *** */
    if (vi_port == -1) {
	if (debug)
	    fprintf(stderr,"Xvideo: no usable video port found\n");
    } else {
	if (debug)
	    fprintf(stderr,"Xvideo: using port %d for video\n",vi_port);
	handle = malloc(sizeof(struct xv_handle));
	memset(handle,0,sizeof(struct xv_handle));
	handle->vi_port     = vi_port;
	handle->vi_adaptor  = vi_adaptor;
	handle->xv_encoding = None;
	handle->xv_freq     = None;
	handle->xv_colorkey = None;
	handle->enc         = -1;
	handle->norm        = -1;
	handle->input       = -1;

	/* query encoding list */
	if (Success != XvQueryEncodings(dpy, vi_port,
					&handle->encodings, &ei)) {
	    fprintf(stderr,"Oops: XvQueryEncodings failed\n");
	    exit(1);
	}
	handle->enc_map = malloc(sizeof(struct ENC_MAP)*handle->encodings);
	for (i = 0; i < handle->encodings; i++) {
	    if (NULL != (h = strrchr(ei[i].name,'-'))) {
		*(h++) = 0;
		handle->enc_map[i].input = xv_strlist_add(&inputs,h);
	    }
	    handle->enc_map[i].norm = xv_strlist_add(&norms,ei[i].name);
	    handle->enc_map[i].encoding = ei[i].encoding_id;
	}

	/* build atoms */
	at = XvQueryPortAttributes(dpy,vi_port,&attributes);
	for (i = 0; i < attributes; i++) {
	    if (debug)
		fprintf(stderr,"  %s%s%s, %i -> %i\n",
			at[i].name,
			(at[i].flags & XvGettable) ? " get" : "",
			(at[i].flags & XvSettable) ? " set" : "",
			at[i].min_value,at[i].max_value);
	    if (0 == strcmp("XV_ENCODING",at[i].name))
		handle->xv_encoding = XV_ENCODING;
	    if (0 == strcmp("XV_FREQ",at[i].name))
		handle->xv_freq     = XV_FREQ;
#if 0
	    if (0 == strcmp("XV_COLORKEY",at[i].name))
		handle->xv_colorkey = XV_COLORKEY;
#endif
	    xv_add_attr(handle, 0, 0, 0, NULL, at+i);
	}

	if (handle->xv_encoding != None) {
	    if (norms)
		xv_add_attr(handle, ATTR_ID_NORM, ATTR_TYPE_CHOICE,
			    0, norms, NULL);
	    if (inputs)
		xv_add_attr(handle, ATTR_ID_INPUT, ATTR_TYPE_CHOICE,
			    0, inputs, NULL);
	}
#if 0
	if (xv_colorkey != None) {
	    XvGetPortAttribute(dpy,vi_port,xv_colorkey,&xv.colorkey);
	    fprintf(stderr,"Xvideo: colorkey: %x\n",xv.colorkey);
	}
#endif
	have_xv = 1;
	drv   = &xv_driver;
	h_drv = handle;
	f_drv = xv_flags(h_drv);
	add_attrs(xv_attrs(h_drv));
    }
}

/* ********************************************************************* */

#if 0
static Window icon_win;
static int icon_width,icon_height;

static void
icon_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    switch (event->type) {
    case Expose:
	if (debug)
	    fprintf(stderr,"icon expose\n");
	xv_video(icon_win, icon_width, icon_height, 1);
	break;
    case MapNotify:
	if (debug)
	    fprintf(stderr,"icon map\n");
	xv_video(icon_win, icon_width, icon_height, 1);
	break;
    case UnmapNotify:
	if (debug)
	    fprintf(stderr,"icon unmap\n");
	break;
    default:
	fprintf(stderr,"icon other\n");
	break;
    }
}

void
init_icon_window(Widget shell,WidgetClass class)
{
    Window root = RootWindowOfScreen(XtScreen(shell));
    Widget widget;
    XIconSize *is;
    int i,count;

    if (XGetIconSizes(XtDisplay(shell),root,&is,&count)) {
	for (i = 0; i < count; i++) {
	    fprintf(stderr,"icon size: min=%dx%d - max=%dx%d - inc=%dx%d\n",
		    is[i].min_width, is[i].min_height,
		    is[i].max_width, is[i].max_height,
		    is[i].width_inc, is[i].height_inc);
	}
	icon_width  = is[0].max_width;
	icon_height = is[0].max_height;
	if (icon_width * 3 > icon_height * 4) {
	    while (icon_width * 3 > icon_height * 4 &&
		   icon_width - is[0].width_inc > is[0].min_width)
		icon_width -= is[0].width_inc;
	} else {
	    while (icon_width * 3 < icon_height * 4 &&
		   icon_height - is[0].height_inc > is[0].min_height)
		icon_height -= is[0].height_inc;
	}
    } else {
	icon_width  = 64;
	icon_height = 48;
    }
    fprintf(stderr,"icon init %dx%d\n",icon_width,icon_height);

    icon_win = XCreateWindow(XtDisplay(shell),root,
			     0,0,icon_width,icon_height,1,
			     CopyFromParent,InputOutput,CopyFromParent,
			     0,NULL);
    widget = XtVaCreateWidget("icon",class,shell,NULL);
    XtRegisterDrawable(XtDisplay(shell),icon_win,widget);
    XtAddEventHandler(widget,StructureNotifyMask | ExposureMask,
		      False,icon_event,NULL);
    XSelectInput(XtDisplay(shell),icon_win,
		 StructureNotifyMask | ExposureMask);
    XtVaSetValues(shell,XtNiconWindow,icon_win,NULL);
}
#endif

/* ********************************************************************* */

const struct ng_vid_driver xv_driver = {
    name:          "Xvideo",
    close:         xv_close,

    capabilities:  xv_flags,
    list_attrs:    xv_attrs,

    overlay:       xv_overlay,

    getfreq:       xv_getfreq,
    setfreq:       xv_setfreq,
    is_tuned:      xv_tuned,
};

#else /* HAVE_LIBXV */

int               have_xv = 0;

#endif /* HAVE_LIBXV */
