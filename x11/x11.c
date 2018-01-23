/*
 * misc x11 functions:  pixmap handling (incl. MIT SHMEM), event
 *                      tracking for the TV widget.
 *
 *  (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/extensions/XShm.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab-ng.h"
#include "capture.h"
#include "channel.h"
#include "x11.h"
#include "xv.h"
#include "commands.h"
#include "blit.h"

#define DEL_TIMER(proc)     XtRemoveTimeOut(proc)
#define ADD_TIMER(proc)     XtAppAddTimeOut(app_context,200,proc,NULL)

extern XtAppContext    app_context;
extern XVisualInfo     vinfo;

/* ------------------------------------------------------------------------ */

Pixmap
x11_capture_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
		   unsigned int width, unsigned int height)
{
    struct ng_video_buf *buf;
    struct ng_video_fmt fmt;
    Pixmap pix = 0;

    if (!(f_drv & CAN_CAPTURE))
	return 0;

    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = x11_dpy_fmtid;
    fmt.width  = width  ? width  : cur_tv_width;
    fmt.height = height ? height : cur_tv_height;
    if (NULL == (buf = ng_grabber_get_image(&fmt)))
	return 0;
    buf = ng_filter_single(cur_filter,buf);
    pix = x11_create_pixmap(dpy,vinfo,buf);
    ng_release_video_buf(buf);
    return pix;
}

void
x11_label_pixmap(Display *dpy, Colormap colormap, Pixmap pixmap,
		 int height, char *label)
{
    static XFontStruct    *font;
    static XColor          color,dummy;
    XGCValues              values;
    GC                     gc;

    if (!font) {
	font = XLoadQueryFont(dpy,"fixed");
	XAllocNamedColor(dpy,colormap,"yellow",&color,&dummy);
    }
    values.font       = font->fid;
    values.foreground = color.pixel;
    gc = XCreateGC(dpy, pixmap, GCFont | GCForeground, &values);
    XDrawString(dpy,pixmap,gc,5,height-5,label,strlen(label));
    XFreeGC(dpy, gc);
}

static int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    return 0;
}

/* ------------------------------------------------------------------------ */
/* video grabdisplay stuff                                                  */

struct video_handle {
    Widget                win;
    Dimension             width,height;
    XtWorkProcId          work_id;
    int                   suspend;         /* temporarely disabled */
    int                   nw,nh;           /* new size (suspend)   */
    struct ng_video_fmt   best;
    struct blit_state     *blit;

    /* image filtering */
    struct ng_filter      *filter;
    void                  *fhandle;
    struct ng_video_fmt   ffmt;
};
struct video_handle vh;

void video_gd_init(Widget widget, int use_gl)
{
    struct video_handle *h = &vh;

    if (debug)
	fprintf(stderr,"gd: init\n");
    h->win  = widget;
    h->blit = blit_init(h->win,&vinfo, use_gl);
}

static struct ng_video_buf*
video_gd_filter(struct video_handle *h, struct ng_video_buf *buf)
{
    if (NULL != h->filter &&
	(cur_filter      != h->filter ||
	 buf->fmt.fmtid  != h->ffmt.fmtid ||
	 buf->fmt.width  != h->ffmt.width ||
	 buf->fmt.height != h->ffmt.height)) {
	h->filter->fini(h->fhandle);
	h->filter  = NULL;
	h->fhandle = NULL;
	memset(&h->ffmt,0,sizeof(h->ffmt));
    }
    if ((1 << buf->fmt.fmtid) & cur_filter->fmts) {
	if (NULL == h->filter) {
	    h->filter  = cur_filter;
	    h->fhandle = h->filter->init(&buf->fmt);
	    h->ffmt    = buf->fmt;
	}
	buf = cur_filter->frame(h->fhandle,buf);
    }
    return buf;
}

int
video_gd_blitframe(struct video_handle *h, struct ng_video_buf *buf)
{
    if (buf->fmt.width  > cur_tv_width ||
	buf->fmt.height > cur_tv_height) {
	ng_release_video_buf(buf);
	return -1;
    }

    if (cur_filter)
	buf = video_gd_filter(h,buf);
    blit_putframe(h->blit,buf);
    return 0;
}

static Boolean
video_gd_idle(XtPointer data)
{
    struct video_handle *h = data;
    struct ng_video_buf *buf;

    if (!(f_drv & CAN_CAPTURE))
	goto oops;

    buf = ng_grabber_grab_image(0);
    if (NULL != buf) {
	video_gd_blitframe(h,buf);
    } else {
	goto oops;
    }

    if (debug) {
	static long count,lastsec;
	struct timeval  t;
	struct timezone tz;
	gettimeofday(&t,&tz);
	if (t.tv_sec != lastsec) {
	    if (lastsec == t.tv_sec-1)
		fprintf(stderr,"%5ld fps \r", count);
	    lastsec = t.tv_sec;
	    count = 0;
	}
	count++;
    }
    return FALSE;

 oops:
    h->work_id = 0;
    if (f_drv & CAN_CAPTURE)
	drv->stopvideo(h_drv);
    return TRUE;
}

void
video_gd_start(void)
{
    struct video_handle *h = &vh;

    if (debug)
	fprintf(stderr,"gd: start [%d]\n",h->best.fmtid);
    if (0 == h->best.fmtid)
	return;
    if (0 != ng_grabber_setformat(&h->best,0))
	return;
    drv->startvideo(h_drv,-1,2);
//    drv->startvideo(h_drv,-1,4);
    h->work_id = XtAppAddWorkProc(app_context, video_gd_idle, h);
}

void
video_gd_stop(void)
{
    struct video_handle *h = &vh;

    if (debug)
	fprintf(stderr,"gd: stop\n");
    if (h->work_id) {
	drv->stopvideo(h_drv);
	XtRemoveWorkProc(h->work_id);
	h->work_id = 0;
	blit_fini_frame(h->blit);
    }
}

void
video_gd_suspend(void)
{
    struct video_handle *h = &vh;

    h->suspend = 1;
    if (cur_capture != CAPTURE_GRABDISPLAY)
	return;
    do_va_cmd(3, "capture", "off", "temp");
}

void
video_gd_restart(void)
{
    struct video_handle *h = &vh;

    if (!h->suspend)
	return;
    h->suspend = 0;
    if (h->nw && h->nh) {
	video_gd_configure(h->nw,h->nh);
	h->nw = 0;
	h->nh = 0;
    }
    if (cur_capture != CAPTURE_OFF)
	return;
    do_va_cmd(3, "capture", "grab", "temp");
}

void
video_gd_configure(int width, int height)
{
    struct video_handle *h = &vh;
    unsigned int i,fmtids[2*VIDEO_FMT_COUNT];

    if (!(f_drv & CAN_CAPTURE))
	return;

    blit_resize(h->blit,width,height);

    if (h->suspend) {
	if (debug)
	    fprintf(stderr,"gd: delay configure\n");
	h->nw = width;
	h->nh = height;
	return;
    }

    if (debug)
	fprintf(stderr,"gd: config %dx%d win=%lx\n",
		width,height,XtWindow(h->win));

    if (!XtWindow(h->win))
	return;

    cur_tv_width  = width;
    cur_tv_height = height;
    h->best.width   = width;
    h->best.height  = height;
    h->best.bytesperline = 0;
    ng_ratio_fixup(&cur_tv_width,  &cur_tv_height,  NULL, NULL);
    ng_ratio_fixup(&h->best.width, &h->best.height, NULL, NULL);

    if (0 == h->best.fmtid) {
	blit_get_formats(h->blit,fmtids,sizeof(fmtids)/sizeof(int));
	for (i = 0; i < sizeof(fmtids)/sizeof(int); i++) {
	    h->best.fmtid = fmtids[i];
	    if (0 == ng_grabber_setformat(&h->best,0))
		goto done;
	}
	/* failed */
	h->best.fmtid = 0;
    }

 done:
    if (debug)
	fprintf(stderr,"grabdisplay: using \"%s\"\n",
		ng_vfmt_to_desc[h->best.fmtid]);
    if (cur_capture == CAPTURE_GRABDISPLAY) {
	do_va_cmd(2, "capture", "off");
	do_va_cmd(2, "capture", "grab");
    }
}

/* ------------------------------------------------------------------------ */
/* video overlay stuff                                                      */

unsigned int     swidth,sheight;           /* screen  */
static int       x11_overlay_fmtid;

/* window  */
static Widget    video,video_parent;
static int       wx, wy, wmap;
static struct ng_video_fmt wfmt;

static XtIntervalId          overlay_refresh;
static int                   did_refresh, oc_count;
static int                   visibility = VisibilityFullyObscured;
static int                   conf = 1, move = 1;
static int                   overlay_on = 0, overlay_enabled = 0;
static struct OVERLAY_CLIP   oc[256];
static XtWorkProcId          conf_id;

/* ------------------------------------------------------------------------ */

char *event_names[] = {
    "0", "1",
    "KeyPress",
    "KeyRelease",
    "ButtonPress",
    "ButtonRelease",
    "MotionNotify",
    "EnterNotify",
    "LeaveNotify",
    "FocusIn",
    "FocusOut",
    "KeymapNotify",
    "Expose",
    "GraphicsExpose",
    "NoExpose",
    "VisibilityNotify",
    "CreateNotify",
    "DestroyNotify",
    "UnmapNotify",
    "MapNotify",
    "MapRequest",
    "ReparentNotify",
    "ConfigureNotify",
    "ConfigureRequest",
    "GravityNotify",
    "ResizeRequest",
    "CirculateNotify",
    "CirculateRequest",
    "PropertyNotify",
    "SelectionClear",
    "SelectionRequest",
    "SelectionNotify",
    "ColormapNotify",
    "ClientMessage",
    "MappingNotify"
};
const int nevent_names = sizeof(event_names)/sizeof(event_names[0]);

/* ------------------------------------------------------------------------ */

static void
add_clip(int x1, int y1, int x2, int y2)
{
    if (oc[oc_count].x1 != x1 || oc[oc_count].y1 != y1 ||
	oc[oc_count].x2 != x2 || oc[oc_count].y2 != y2) {
	conf = 1;
    }
    oc[oc_count].x1 = x1;
    oc[oc_count].y1 = y1;
    oc[oc_count].x2 = x2;
    oc[oc_count].y2 = y2;
    oc_count++;
}

static void
get_clips(void)
{
    int x1,y1,x2,y2,lastcount;
    Display *dpy;
    XWindowAttributes wts;
    Window root, me, rroot, parent, *children;
    uint nchildren, i;
    void *old_handler = XSetErrorHandler(x11_error_dev_null);

    if (debug > 1)
	fprintf(stderr," getclips");
    lastcount = oc_count;
    oc_count = 0;
    dpy = XtDisplay(video);

    if (wx<0)
	add_clip(0, 0, (uint)(-wx), wfmt.height);
    if (wy<0)
	add_clip(0, 0, wfmt.width, (uint)(-wy));
    if ((wx+wfmt.width) > swidth)
	add_clip(swidth-wx, 0, wfmt.width, wfmt.height);
    if ((wy+wfmt.height) > sheight)
	add_clip(0, sheight-wy, wfmt.width, wfmt.height);

    root=DefaultRootWindow(dpy);
    me = XtWindow(video);
    for (;;) {
	XQueryTree(dpy, me, &rroot, &parent, &children, &nchildren);
	XFree((char *) children);
	/* fprintf(stderr,"me=0x%x, parent=0x%x\n",me,parent); */
	if (root == parent)
	    break;
	me = parent;
    }
    XQueryTree(dpy, root, &rroot, &parent, &children, &nchildren);

    for (i = 0; i < nchildren; i++)
	if (children[i]==me)
	    break;

    for (i++; i<nchildren; i++) {
	XGetWindowAttributes(dpy, children[i], &wts);
	if (!(wts.map_state & IsViewable))
	    continue;

	x1=wts.x-wx;
	y1=wts.y-wy;
	x2=x1+wts.width+2*wts.border_width;
	y2=y1+wts.height+2*wts.border_width;
	if ((x2 < 0) || (x1 > (int)wfmt.width) ||
	    (y2 < 0) || (y1 > (int)wfmt.height))
	    continue;

	if (x1<0)      		 x1=0;
	if (y1<0)                y1=0;
	if (x2>(int)wfmt.width)  x2=wfmt.width;
	if (y2>(int)wfmt.height) y2=wfmt.height;
	add_clip(x1, y1, x2, y2);
    }
    XFree((char *) children);

    if (lastcount != oc_count)
	conf = 1;
    XSetErrorHandler(old_handler);
}

static void
refresh_timer(XtPointer clientData, XtIntervalId *id)
{
    Window   win = RootWindowOfScreen(XtScreen(video));
    Display *dpy = XtDisplay(video);
    XSetWindowAttributes xswa;
    unsigned long mask;
    Window   tmp;

    if (!move && wmap && visibility == VisibilityUnobscured) {
	if (debug > 1)
	    fprintf(stderr,"video: refresh skipped\n");
	return;
    }

    if (debug > 1)
	fprintf(stderr,"video: refresh\n");
    overlay_refresh = 0;
    if (wmap && visibility != VisibilityFullyObscured)
	did_refresh = 1;

    xswa.override_redirect = True;
    xswa.backing_store = NotUseful;
    xswa.save_under = False;
    mask = (CWSaveUnder | CWBackingStore| CWOverrideRedirect );
    tmp = XCreateWindow(dpy,win, 0,0, swidth,sheight, 0,
			CopyFromParent, InputOutput, CopyFromParent,
			mask, &xswa);
    XMapWindow(dpy, tmp);
    XUnmapWindow(dpy, tmp);
    XDestroyWindow(dpy, tmp);
    move = 0;
}

static Boolean
configure_delayed(XtPointer data)
{
    if (debug > 1)
	fprintf(stderr,"video: configure delayed");
    if (wmap && visibility != VisibilityFullyObscured) {
	if (visibility == VisibilityPartiallyObscured)
	    get_clips();
	else
	    oc_count = 0;

	if (debug > 1)
	    fprintf(stderr," %s\n",conf ? "yes" : "no");
	if (conf) {
	    overlay_on = 1;
	    if (f_drv & CAN_OVERLAY)
		drv->overlay(h_drv,&wfmt,wx,wy,oc,oc_count,1);
	    if (overlay_refresh)
		DEL_TIMER(overlay_refresh);
	    overlay_refresh = ADD_TIMER(refresh_timer);
	    conf = 0;
	}
    } else {
	if (debug > 1)
	    fprintf(stderr," off\n");
	if (conf && overlay_on) {
	    overlay_on = 0;
	    if (f_drv & CAN_OVERLAY)
		drv->overlay(h_drv,NULL,0,0,NULL,0,0);
	    if (overlay_refresh)
		DEL_TIMER(overlay_refresh);
	    overlay_refresh = ADD_TIMER(refresh_timer);
	    conf = 0;
	}
    }
    conf_id = 0;
    return TRUE;
}

static void
configure_overlay(void)
{
    if (!overlay_enabled)
	return;

#ifdef HAVE_LIBXV
    if (have_xv) {
	if (wfmt.width && wfmt.height)
	    xv_video(XtWindow(video),wfmt.width,wfmt.height,1);
	return;
    }
#endif

    if (0 == conf_id)
	conf_id = XtAppAddWorkProc(app_context,configure_delayed,NULL);
}

void
video_new_size()
{
    Dimension x,y,w,h;

    XtVaGetValues(video_parent, XtNx, &x, XtNy, &y,
		  XtNwidth, &w, XtNheight, &h, NULL);
    wx          = x; if (wx > 32768)          wx          -= 65536;
    wy          = y; if (wy > 32768)          wy          -= 65536;
    wfmt.width  = w; if (wfmt.width > 32768)  wfmt.width  -= 65536;
    wfmt.height = h; if (wfmt.height > 32768) wfmt.height -= 65536;
    wfmt.fmtid  = x11_overlay_fmtid;
    if (debug > 1)
	fprintf(stderr,"video: shell: size %dx%d+%d+%d\n",
		wfmt.width,wfmt.height,wx,wy);

    conf = 1;
    move = 1;
    configure_overlay();
}

/* ------------------------------------------------------------------------ */

static void
video_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    if (widget == video_parent) {
	/* shell widget */
	switch(event->type) {
	case ConfigureNotify:
#if 0
	    wx      = event->xconfigure.x;
	    wy      = event->xconfigure.y;
	    wwidth  = event->xconfigure.width;
	    wheight = event->xconfigure.height;
	    if (debug > 1)
		fprintf(stderr,"video: shell: cfg %dx%d+%d+%d\n",
			wwidth,wheight,wx,wy);
#endif
	    video_new_size();
	    break;
	case MapNotify:
	    if (debug > 1)
		fprintf(stderr,"video: shell: map\n");
	    wmap = 1;
	    conf = 1;
	    configure_overlay();
	    break;
	case UnmapNotify:
	    if (debug > 1)
		fprintf(stderr,"video: shell: unmap\n");
	    wmap = 0;
	    conf = 1;
	    configure_overlay();
	    break;
	default:
	    if (debug > 1)
		fprintf(stderr,"video: shell: %s\n",
			event_names[event->type]);
	}
	return;

    } else {
	/* TV widget (+root window) */
	switch(event->type) {
	case Expose:
	    if (event->xvisibility.window == XtWindow(video)) {
		/* tv */
		if (!event->xexpose.count) {
		    if (did_refresh) {
			did_refresh = 0;
			if (debug > 1)
			    fprintf(stderr,"video: tv: last refresh expose\n");
		    } else {
			if (debug > 1)
			    fprintf(stderr,"video: tv: expose\n");
			conf = 1;
			configure_overlay();
		    }
		}
	    }
	    break;
	case VisibilityNotify:
	    if (event->xvisibility.window == XtWindow(video)) {
		/* tv */
		visibility = event->xvisibility.state;
		if (debug > 1)
		    fprintf(stderr,"video: tv: visibility %d%s\n",
			    event->xvisibility.state,
			    did_refresh?" (ignored)":"");
		if (did_refresh) {
		    if (event->xvisibility.state != VisibilityFullyObscured)
			did_refresh = 0;
		} else {
		    conf = 1;
		    configure_overlay();
		}
	    } else {
		/* root */
		if (debug > 1)
		    fprintf(stderr,"video: root: visibility\n");
	    }
	    break;
	case MapNotify:
	case UnmapNotify:
	case ConfigureNotify:
	    if (event->xvisibility.window != XtWindow(video)) {
		if (debug > 1)
		    fprintf(stderr,"video: root: %s%s\n",
			    event_names[event->type],did_refresh?" (ignored)":"");
		if (!did_refresh)
		    configure_overlay();
	    }
	    break;
	default:
	    if (debug > 1)
		fprintf(stderr,"video: tv(+root): %s\n",
			event_names[event->type]);
	    break;
	}
    }
}

void
video_overlay(int state)
{
    if (state) {
	conf = 1;
	overlay_enabled = 1;
	configure_overlay();
    } else {
	if (1 == overlay_enabled) {
#ifdef HAVE_LIBXV
	    if (have_xv) {
		xv_video(XtWindow(video),0,0,0);
	    } else
#endif
	    {
		overlay_on = 0;
		if (f_drv & CAN_OVERLAY)
		    drv->overlay(h_drv,NULL,0,0,NULL,0,0);
		overlay_refresh = ADD_TIMER(refresh_timer);
	    }
	}
	overlay_enabled = 0;
    }
}

Widget
video_init(Widget parent, XVisualInfo *vinfo, WidgetClass class,
	   int args_bpp, int args_gl)
{
    Window root = DefaultRootWindow(XtDisplay(parent));

    swidth  = XtScreen(parent)->width;
    sheight = XtScreen(parent)->height;

    x11_overlay_fmtid = x11_dpy_fmtid;
    if (ImageByteOrder(XtDisplay(parent)) == MSBFirst) {
	/* X-Server is BE */
	switch(args_bpp) {
	case  8: x11_overlay_fmtid = VIDEO_RGB08;    break;
	case 15: x11_overlay_fmtid = VIDEO_RGB15_BE; break;
	case 16: x11_overlay_fmtid = VIDEO_RGB16_BE; break;
	case 24: x11_overlay_fmtid = VIDEO_BGR24;    break;
	case 32: x11_overlay_fmtid = VIDEO_BGR32;    break;
	}
    } else {
	/* X-Server is LE */
	switch(args_bpp) {
	case  8: x11_overlay_fmtid = VIDEO_RGB08;    break;
	case 15: x11_overlay_fmtid = VIDEO_RGB15_LE; break;
	case 16: x11_overlay_fmtid = VIDEO_RGB16_LE; break;
	case 24: x11_overlay_fmtid = VIDEO_BGR24;    break;
	case 32: x11_overlay_fmtid = VIDEO_BGR32;    break;
	}
    }

    video_parent = parent;
    video = XtVaCreateManagedWidget("tv",class,parent,NULL);

    /* Shell widget -- need map, unmap, configure */
    XtAddEventHandler(parent,
		      StructureNotifyMask,
		      True, video_event, NULL);

    if (!have_xv) {
	/* TV Widget -- need visibility, expose */
	XtAddEventHandler(video,
			  VisibilityChangeMask |
			  StructureNotifyMask,
			  False, video_event, NULL);

	/* root window -- need */
	XSelectInput(XtDisplay(video),root,
		     VisibilityChangeMask |
		     SubstructureNotifyMask |
		     StructureNotifyMask);

	XtRegisterDrawable(XtDisplay(video),root,video);
    }

    return video;
}

void
video_close(void)
{
    Window root = DefaultRootWindow(XtDisplay(video));

    if (overlay_refresh)
	DEL_TIMER(overlay_refresh);
    XSelectInput(XtDisplay(video),root,0);
    XtUnregisterDrawable(XtDisplay(video),root);
}
