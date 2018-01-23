/*
 * main.c for xawtv -- a TV application
 *
 *   (c) 1997-2002 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#define _GNU_SOURCE

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/Xmd.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Dialog.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/extensions/XShm.h>

#include "grab-ng.h"
#include "writefile.h"

#include "sound.h"
#include "channel.h"
#include "commands.h"
#include "frequencies.h"
#include "xv.h"
#include "capture.h"
#include "atoms.h"
#include "xt.h"
#include "x11.h"
#include "toolbox.h"
#include "complete.h"
#include "wmhooks.h"
#include "conf.h"
#include "blit.h"
#include "vbi-data.h"
#include "vbi-x11.h"

#define LABEL_WIDTH         "16"
#define BOOL_WIDTH          "24"

/*--- public variables ----------------------------------------------------*/

static String fallback_ressources[] = {
#include "Xawtv.h"
    NULL
};

Widget            opt_shell, opt_paned, chan_shell, conf_shell, str_shell;
Widget            launch_shell, launch_paned;
Widget            c_freq,c_cap;
Widget            s_bright,s_color,s_hue,s_contrast,s_volume;
Widget            chan_viewport, chan_box;
Pixmap            tv_pix;
struct vbi_window *vtx;

int               have_config = 0;
XtIntervalId      audio_timer;
XtIntervalId      unmute_timer;
int               unmute_pending = 0;
int               debug = 0;
int               window_configured = 0;

char              modename[64];
char              *progname;

XtWorkProcId      rec_work_id;

/* movie params / setup */
Widget            w_movie_status;
Widget            w_movie_driver;

Widget            w_movie_fvideo;
Widget            w_movie_video;
Widget            w_movie_fps;
Widget            w_movie_size;

Widget            w_movie_flabel;
Widget            w_movie_faudio;
Widget            w_movie_audio;
Widget            w_movie_rate;

struct STRTAB     *m_movie_driver;
struct STRTAB     *m_movie_audio;
struct STRTAB     *m_movie_video;

struct ng_writer  *movie_driver  = NULL;
unsigned int      i_movie_driver = 0;
unsigned int      movie_audio    = 0;
unsigned int      movie_video    = 0;
unsigned int      movie_fps      = 12000;
unsigned int      movie_rate     = 44100;

static struct STRTAB m_movie_fps[] = {
    {  2000, " 2.0   fps" },
    {  3000, " 3.0   fps" },
    {  5000, " 5.0   fps" },
    {  8000, " 8.0   fps" },
    { 10000, "10.0   fps" },
    { 12000, "12.0   fps" },
    { 15000, "15.0   fps" },
    { 18000, "18.0   fps" },
    { 20000, "20.0   fps" },
    { 23976, "23.976 fps" },
    { 24000, "24.0   fps" },
    { 25000, "25.0   fps" },
    { 29970, "29.970 fps" },
    { 30000, "30.0   fps" },
    { -1, NULL },
};
static struct STRTAB m_movie_rate[] = {
    {   8000, " 8000" },
    {  11025, "11025" },
    {  22050, "22050" },
    {  44100, "44100" },
    {  48000, "48000" },
    { -1, NULL },
};

struct xaw_attribute {
    struct ng_attribute   *attr;
    Widget                cmd,scroll;
    struct xaw_attribute  *next;
};
static struct xaw_attribute *xaw_attrs;

#define MOVIE_DRIVER  "movie driver"
#define MOVIE_AUDIO   "audio format"
#define MOVIE_VIDEO   "video format"
#define MOVIE_FPS     "frames/sec"
#define MOVIE_RATE    "sample rate"
#define MOVIE_SIZE    "video size"

/* fwd decl */
void change_audio(int mode);
void watch_audio(XtPointer data, XtIntervalId *id);
void unmute_audio(XtPointer data, XtIntervalId *id);

/*-------------------------------------------------------------------------*/

static struct MY_TOPLEVELS {
    char        *name;
    Widget      *shell;
    int         require_tune;
    int         *check;
    int          first;
    int          mapped;
} my_toplevels [] = {
    { "options",  &opt_shell,    0,          },
    { "channels", &chan_shell,   1, &count   },
    { "config",   &conf_shell,   1,          },
    { "streamer", &str_shell,    0,          },
    { "launcher", &launch_shell, 0, &nlaunch }
};
#define TOPLEVELS (sizeof(my_toplevels)/sizeof(struct MY_TOPLEVELS))

struct STRTAB *cmenu = NULL;

struct DO_AC {
    int  argc;
    char *name;
    char *argv[8];
};

/*--- actions -------------------------------------------------------------*/

/* conf.c */
extern void create_confwin(void);
extern void conf_station_switched(void);
extern void conf_list_update(void);

void CloseMainAction(Widget, XEvent*, String*, Cardinal*);
void ScanAction(Widget, XEvent*, String*, Cardinal*);
void ChannelAction(Widget, XEvent*, String*, Cardinal*);
void StayOnTop(Widget, XEvent*, String*, Cardinal*);
void PopupAction(Widget, XEvent*, String*, Cardinal*);
void MuteAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction  },
    { "Scan",        ScanAction },
    { "Channel",     ChannelAction },
    { "Remote",      RemoteAction },
    { "Zap",         ZapAction },
    { "Complete",    CompleteAction },
    { "Help",        help_AC },
    { "StayOnTop",   StayOnTop },
    { "Launch",      LaunchAction },
    { "Popup",       PopupAction },
    { "Command",     CommandAction },
    { "Autoscroll",  offscreen_scroll_AC },
    { "Ratio",       RatioAction },
#ifdef HAVE_ZVBI
    { "Vtx",         VtxAction },
#endif
    { "Event",       EventAction },
    { "Mute",        MuteAction },
};

static struct STRTAB cap_list[] = {
    {  CAPTURE_OFF,         "off"         },
    {  CAPTURE_OVERLAY,     "overlay"     },
    {  CAPTURE_GRABDISPLAY, "grabdisplay" },
    {  -1, NULL,     },
};

/*--- exit ----------------------------------------------------------------*/

void
PopupAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    Dimension h;
    unsigned int i;
    int mh;
    if (debug)
	fprintf(stderr,"PopupAction: %s\n",
	        (*num_params > 0) ? params[0] : "-");
    /* which window we are talking about ? */
    if (*num_params > 0) {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (0 == strcasecmp(my_toplevels[i].name,params[0]))
		break;
	}
    } else {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (*(my_toplevels[i].shell) == widget)
		break;
	}
    }
    if (i == TOPLEVELS) {
	fprintf(stderr,"PopupAction: oops: shell widget not found (%s)\n",
		(*num_params > 0) ? params[0] : "-");
	return;
    }

    if (!(f_drv & CAN_TUNE) && my_toplevels[i].require_tune)
	return;


    /* Message from WM ??? */
    if (NULL != event && event->type == ClientMessage) {
	if (debug)
	    fprintf(stderr,"%s: received %s message\n",
		    my_toplevels[i].name,
		    XGetAtomName(dpy,event->xclient.data.l[0]));
	if ((Atom)event->xclient.data.l[0] == WM_DELETE_WINDOW) {
	    /* fall throuth -- popdown window */
	} else {
	    /* whats this ?? */
	    return;
	}
    }

    /* check if window should be displayed */
    if (NULL != my_toplevels[i].check)
	if (0 == *(my_toplevels[i].check))
	    return;

    /* popup/down window */
    if (my_toplevels[i].mapped) {
	XtPopdown(*(my_toplevels[i].shell));
	my_toplevels[i].mapped = 0;
    } else {
	XtPopup(*(my_toplevels[i].shell), XtGrabNone);
	if (wm_stay_on_top && stay_on_top > 0)
	    wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),1);
	my_toplevels[i].mapped = 1;
	if (!my_toplevels[i].first) {
	    XSetWMProtocols(XtDisplay(*(my_toplevels[i].shell)),
			    XtWindow(*(my_toplevels[i].shell)),
			    &WM_DELETE_WINDOW, 1);
	    mh = h = 0;
	    XtVaGetValues(*(my_toplevels[i].shell),
			  XtNmaxHeight,&mh,
			  XtNheight,&h,
			  NULL);
	    if (mh > 0 && h > mh) {
		if (debug)
		    fprintf(stderr,"height fixup: %d => %d\n",h,mh);
		XtVaSetValues(*(my_toplevels[i].shell),XtNheight,mh,NULL);
	    }
	    my_toplevels[i].first = 1;
	}
    }
}

void MuteAction(Widget w, XEvent *e, String *s, Cardinal *c)
{
    /*
     * If we're muted because we're changing channels, stay muted. We still do
     * a "volume mute on" command to show the muted msg in the title bar.
     */
    if (unmute_pending) {
        do_va_cmd(3, "volume", "mute", "on");
        unmute_pending = 0;
        return;
    }

    do_va_cmd(2, "volume", "mute"); /* Toggle mute */
}

static void
action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct DO_AC *ca = clientdata;
    XtCallActionProc(widget,ca->name,NULL,ca->argv,ca->argc);
}

void toolkit_set_label(Widget widget, char *str)
{
    XtVaSetValues(widget,XtNlabel,str,NULL);
}

/*--- videotext ----------------------------------------------------------*/

static void create_vtx(void)
{
    Widget shell,label;

    shell = XtVaCreateWidget("vtx",transientShellWidgetClass,
			     app_shell,
			     XtNoverrideRedirect,True,
			     XtNvisual,vinfo.visual,
			     XtNcolormap,colormap,
			     XtNdepth,vinfo.depth,
			     NULL);
    label = XtVaCreateManagedWidget("label", labelWidgetClass, shell,
				    NULL);
#ifdef HAVE_ZVBI
    vtx = vbi_render_init(shell,label,NULL);
#endif
}

#if TT
static void
display_vtx(struct TEXTELEM *tt)
{
    static Pixel fg, bg;
    static XFontStruct *font;
    static Pixmap pix;
    static GC gc;
    static int first = 1;
    XColor color, dummy;
    XGCValues  values;
    Dimension x,y,w,h,sw,sh;
    int maxwidth,width,height,direction,ascent,descent,lastline,i;
    XCharStruct cs;

    if (NULL == tt) {
	XtPopdown(vtx_shell);
	return;
    }

    if (NULL == font) {
	XtVaGetValues(vtx_label,
		      XtNfont,&font,
		      XtNbackground,&bg,
		      XtNforeground,&fg,
		      NULL);
	values.font = font->fid;
	gc = XCreateGC(dpy, XtWindow(vtx_label), GCFont, &values);
    }

    /* calc size + positions */
    width = 0; height = 0; maxwidth = 0;
    lastline = -1;
    for (i = 0; tt[i].len; i++) {
	XTextExtents(font,tt[i].str,tt[i].len,
		     &direction,&ascent,&descent,&cs);
	if (lastline != tt[i].line) {
	    if (maxwidth < width)
		maxwidth = width;
	    width = 0;
	    height += ascent+descent;
	    lastline = tt[i].line;
	}
	tt[i].x = width;
	tt[i].y = height - descent;
	width += cs.width;
    }
    if (maxwidth < width)
	maxwidth = width;

    /* alloc pixmap + draw text */
    if (pix)
	XFreePixmap(dpy,pix);
    fprintf(stderr,"pix: %dx%d\n",maxwidth, height);
    pix = XCreatePixmap(dpy, RootWindowOfScreen(XtScreen(vtx_label)),
			maxwidth, height,
			DefaultDepthOfScreen(XtScreen(vtx_label)));
    values.foreground = bg;
    values.background = bg;
    XChangeGC(dpy, gc, GCForeground | GCBackground, &values);
    XFillRectangle(dpy,pix,gc,0,0,maxwidth,height);
    for (i = 0; tt[i].len; i++) {
	if (tt[i].fg) {
	    XAllocNamedColor(dpy,colormap,tt[i].fg,
			     &color,&dummy);
	    values.foreground = color.pixel;
	} else {
	    values.foreground = fg;
	}
	if (tt[i].bg) {
	    XAllocNamedColor(dpy,colormap,tt[i].bg,
			     &color,&dummy);
	    values.background = color.pixel;
	} else {
	    values.background = bg;
	}
	XChangeGC(dpy, gc, GCForeground | GCBackground, &values);
	XDrawImageString(dpy,pix,gc,tt[i].x,tt[i].y,tt[i].str,tt[i].len);
    }
    XtVaSetValues(vtx_label,XtNbitmap,pix,XtNlabel,NULL,NULL);

    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,NULL);
    XtVaGetValues(vtx_shell,XtNwidth,&sw,XtNheight,&sh,NULL);
    XtVaSetValues(vtx_shell,XtNx,x+(w-sw)/2,XtNy,y+h-10-sh,NULL);
    XtPopup(vtx_shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(vtx_shell),1);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(vtx_shell), left_ptr);
	XDefineCursor(dpy, XtWindow(vtx_label), left_ptr);
    }
}
#endif

#ifdef HAVE_ZVBI
static void
display_subtitle(struct vbi_page *pg, struct vbi_rect *rect)
{
    static int first = 1;
    static Pixmap pix;
    Dimension x,y,w,h,sw,sh;

    if (NULL == pg) {
	XtPopdown(vtx->shell);
	return;
    }

    if (pix)
	XFreePixmap(dpy,pix);
    pix = vbi_export_pixmap(vtx,pg,rect);
    XtVaSetValues(vtx->tt,XtNbitmap,pix,XtNlabel,NULL,NULL);

    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,NULL);
    XtVaGetValues(vtx->shell,XtNwidth,&sw,XtNheight,&sh,NULL);
    XtVaSetValues(vtx->shell,XtNx,x+(w-sw)/2,XtNy,y+h-10-sh,NULL);
    XtPopup(vtx->shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(vtx->shell),1);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(vtx->shell), left_ptr);
	XDefineCursor(dpy, XtWindow(vtx->tt), left_ptr);
    }
}
#endif

/*--- tv -----------------------------------------------------------------*/

static void
resize_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    static int width = 0, height = 0;
    char label[64];

    switch(event->type) {
    case ConfigureNotify:
	if (!window_configured) {
	    video_gd_init(tv,args.gl);
	    window_configured = 1;
	}
	if (width  != event->xconfigure.width ||
	    height != event->xconfigure.height) {
	    width  = event->xconfigure.width;
	    height = event->xconfigure.height;
	    video_gd_configure(width, height);
	    XClearWindow(XtDisplay(tv),XtWindow(tv));
	    sprintf(label,"%-" LABEL_WIDTH "s: %dx%d",MOVIE_SIZE,width,height);
	    if (w_movie_size)
		XtVaSetValues(w_movie_size,XtNlabel,label,NULL);
	}
	break;
    }
}

/*------------------------------------------------------------------------*/

/* the RightWay[tm] to set float resources (copyed from Xaw specs) */
static void
set_float(Widget widget, char *name, float value)
{
    Arg   args[1];

    if (sizeof(float) > sizeof(XtArgVal)) {
	/*
	 * If a float is larger than an XtArgVal then pass this
	 * resource value by reference.
	 */
	XtSetArg(args[0], name, &value);
    } else {
	/*
	 * Convince C not to perform an automatic conversion, which
	 * would truncate 0.5 to 0.
	 *
	 * switched from pointer tricks to the union to fix alignment
	 * problems on ia64 (Stephane Eranian <eranian@cello.hpl.hp.com>)
	 */
	union {
	    XtArgVal xt;
	    float   fp;
	} foo;
	foo.fp = value;
	XtSetArg(args[0], name, foo.xt);
    }
    XtSetValues(widget,args,1);
}

static void
new_freqtab(void)
{
    char label[64];

    if (c_freq) {
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Frequency table",
		chanlists[chantab].name);
	XtVaSetValues(c_freq,XtNlabel,label,NULL);
    }
}

static void
new_attr(struct ng_attribute *attr, int val)
{
    struct xaw_attribute *a;
    char label[64],*olabel;
    const char *valstr;

    for (a = xaw_attrs; NULL != a; a = a->next) {
	if (a->attr->id == attr->id)
	    break;
    }
    if (NULL != a) {
	switch (attr->type) {
	case ATTR_TYPE_CHOICE:
	    XtVaGetValues(a->cmd,XtNlabel,&olabel,NULL);
	    valstr = ng_attr_getstr(attr,val);
	    sprintf(label,"%-" LABEL_WIDTH "." LABEL_WIDTH "s: %s",
		    olabel,valstr ? valstr : "unknown");
	    XtVaSetValues(a->cmd,XtNlabel,label,NULL);
	    break;
	case ATTR_TYPE_BOOL:
	    XtVaGetValues(a->cmd,XtNlabel,&olabel,NULL);
	    sprintf(label,"%-" BOOL_WIDTH "." BOOL_WIDTH "s  %s",
		    olabel,val ? "on" : "off");
	    XtVaSetValues(a->cmd,XtNlabel,label,NULL);
	    break;
	case ATTR_TYPE_INTEGER:
	    set_float(a->scroll,XtNtopOfThumb,
		      (float)(val-attr->min) / (attr->max - attr->min));
	    break;
	}
	return;
    }
}

static void
new_volume(void)
{
    struct ng_attribute *attr;

    attr = ng_attr_byid(attrs,ATTR_ID_VOLUME);
    if (NULL != attr)
	new_attr(attr,cur_attrs[ATTR_ID_VOLUME]);
}

static void
new_channel(void)
{
    set_property(cur_freq,
		 (cur_channel == -1) ? NULL : chanlist[cur_channel].name,
		 (cur_sender == -1)  ? NULL : channels[cur_sender]->name);
    conf_station_switched();

    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
    }
    if (scan_timer) {
	XtRemoveTimeOut(scan_timer);
	scan_timer = 0;
    }
    if (audio_timer) {
	XtRemoveTimeOut(audio_timer);
	audio_timer = 0;
    }
    if (unmute_timer) {
	XtRemoveTimeOut(unmute_timer);
	unmute_timer = 0;
    }
    audio_timer = XtAppAddTimeOut(app_context, 5000, watch_audio, NULL);
    if (unmute_pending)
        unmute_timer = XtAppAddTimeOut(app_context, 800, unmute_audio, NULL);
}

void
watch_audio(XtPointer data, XtIntervalId *id)
{
    if (-1 != cur_sender)
	change_audio(channels[cur_sender]->audio);
    audio_timer = 0;
}

void unmute_audio(XtPointer data, XtIntervalId *id)
{
    if (unmute_pending) {
        audio_on();
        unmute_pending = 0;
    }
    unmute_timer = 0;
}

/*------------------------------------------------------------------------*/

static void
do_capture(int from, int to, int tmp_switch)
{
    static int niced = 0;
    char label[64];

    /* off */
    switch (from) {
    case CAPTURE_OFF:
	XtVaSetValues(tv,XtNbackgroundPixmap,XtUnspecifiedPixmap,NULL);
	if (tv_pix)
	    XFreePixmap(dpy,tv_pix);
	tv_pix = 0;
	break;
    case CAPTURE_GRABDISPLAY:
	video_gd_stop();
	if (!tmp_switch)
	    XClearArea(XtDisplay(tv), XtWindow(tv), 0,0,0,0, True);
	break;
    case CAPTURE_OVERLAY:
	video_overlay(0);
	break;
    }

    /* on */
    switch (to) {
    case CAPTURE_OFF:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","off");
	if (!tmp_switch) {
	    tv_pix = x11_capture_pixmap(dpy, &vinfo, colormap, 0, 0);
	    if (tv_pix)
		XtVaSetValues(tv,XtNbackgroundPixmap,tv_pix,NULL);
	}
	break;
    case CAPTURE_GRABDISPLAY:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","grabdisplay");
	if (!niced)
	    nice(niced = 10);
	video_gd_start();
	break;
    case CAPTURE_OVERLAY:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","overlay");
	video_overlay(1);
	break;
    }
    if (c_cap)
	XtVaSetValues(c_cap,XtNlabel,label,NULL);
}

/* gets called before switching away from a channel */
static void leaving_channel(void)
{
    Pixmap pix;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;

    if (!cur_attrs[ATTR_ID_MUTE]) {
        audio_off();
        unmute_pending = 1;
    }

    if (cur_sender == -1)
	return;

    /* save picture settings */
    channels[cur_sender]->color    = cur_attrs[ATTR_ID_COLOR];
    channels[cur_sender]->bright   = cur_attrs[ATTR_ID_BRIGHT];
    channels[cur_sender]->hue      = cur_attrs[ATTR_ID_HUE];
    channels[cur_sender]->contrast = cur_attrs[ATTR_ID_CONTRAST];
    channels[cur_sender]->input    = cur_attrs[ATTR_ID_INPUT];
    channels[cur_sender]->norm     = cur_attrs[ATTR_ID_NORM];

    if (0 == pix_width || 0 == pix_height)
	return;

    /* capture mini picture */
    if (!(f_drv & CAN_CAPTURE))
	return;

    video_gd_suspend();
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = x11_dpy_fmtid;
    fmt.width  = pix_width;
    fmt.height = pix_height;
    if (NULL == (buf = ng_grabber_get_image(&fmt)))
	goto done1;
    buf = ng_filter_single(cur_filter,buf);
    if (0 == (pix = x11_create_pixmap(dpy,&vinfo,buf)))
	goto done2;
    x11_label_pixmap(dpy,colormap,pix,buf->fmt.height,
		     channels[cur_sender]->name);
    XtVaSetValues(channels[cur_sender]->button,
		  XtNbackgroundPixmap,pix,
		  XtNlabel,"",
		  XtNwidth,pix_width,
		  XtNheight,pix_height,
		  NULL);
    if (channels[cur_sender]->pixmap)
	XFreePixmap(dpy,channels[cur_sender]->pixmap);
    channels[cur_sender]->pixmap = pix;

 done2:
    ng_release_video_buf(buf);
 done1:
    video_gd_restart();
}

static void
set_menu_val(Widget widget, char *name, struct STRTAB *tab, int val)
{
    char label[64];
    int i;

    for (i = 0; tab[i].str != NULL; i++) {
	if (tab[i].nr == val)
	    break;
    }
    sprintf(label,"%-15s : %s",name,
	    (tab[i].str != NULL) ? tab[i].str : "invalid");
    XtVaSetValues(widget,XtNlabel,label,NULL);
}

void
ChannelAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    int i;

    if (!(f_drv & CAN_TUNE))
	return;

    if (0 == count)
	return;
    i = popup_menu(widget,"Stations",cmenu);

    if (i != -1)
	do_va_cmd(2,"setstation",channels[i]->name);
}

static void create_chanwin(void)
{
    if (!(f_drv & CAN_TUNE))
	return;

    chan_shell = XtVaAppCreateShell("Channels", "Xawtv",
				    topLevelShellWidgetClass,
				    dpy,
				    XtNclientLeader,app_shell,
				    XtNvisual,vinfo.visual,
				    XtNcolormap,colormap,
				    XtNdepth,vinfo.depth,
		      XtNheight,XtScreen(app_shell)->height/2,
		      XtNmaxHeight,XtScreen(app_shell)->height-50,
				    NULL);
    XtOverrideTranslations(chan_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    chan_viewport = XtVaCreateManagedWidget("viewport",
					    viewportWidgetClass, chan_shell,
					    XtNallowHoriz, False,
					    XtNallowVert, True,
					    NULL);
    chan_box = XtVaCreateManagedWidget("channelbox",
				       boxWidgetClass, chan_viewport,
				       XtNsensitive, True,
				       NULL);
}

void channel_menu(void); /* FIXME */
void channel_menu(void)
{
    int  i,max,len;
    char str[100];

    if (!(f_drv & CAN_TUNE))
	return;

    if (cmenu)
	free(cmenu);
    cmenu = malloc((count+1)*sizeof(struct STRTAB));
    memset(cmenu,0,(count+1)*sizeof(struct STRTAB));
    for (i = 0, max = 0; i < count; i++) {
	len = strlen(channels[i]->name);
	if (max < len)
	    max = len;
    }
    for (i = 0; i < count; i++) {
	cmenu[i].nr      = i;
	cmenu[i].str     = channels[i]->name;
	if (channels[i]->key) {
	    sprintf(str,"%2d  %-*s  %s",i+1,
		    max+2,channels[i]->name,channels[i]->key);
	} else {
	    sprintf(str,"%2d  %-*s",i+1,max+2,channels[i]->name);
	}
	cmenu[i].str=strdup(str);
    }
    conf_list_update();
    calc_frequencies();
}

void
StayOnTop(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    unsigned int i;

    if (!wm_stay_on_top)
	return;

    stay_on_top = stay_on_top ? 0 : 1;
    if (debug)
	fprintf(stderr,"stay_on_top: %d\n",stay_on_top);

    wm_stay_on_top(dpy,XtWindow(app_shell),stay_on_top);
    wm_stay_on_top(dpy,XtWindow(on_shell),stay_on_top);
    for (i = 0; i < TOPLEVELS; i++)
	wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),
		       (stay_on_top == -1) ? 0 : stay_on_top);
}

/*--- option window ------------------------------------------------------*/

static void
update_movie_menus(void)
{
    struct list_head *item;
    struct ng_writer *writer;
    Boolean sensitive;
    unsigned int i;

    /* drivers  */
    if (NULL == m_movie_driver) {
	i = 0;
	list_for_each(item,&ng_writers)
	    i++;
	m_movie_driver = malloc(sizeof(struct STRTAB)*(i+1));
	memset(m_movie_driver,0,sizeof(struct STRTAB)*(i+1));
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    m_movie_driver[i].nr  = i;
	    m_movie_driver[i].str = writer->desc;
	    if (NULL == movie_driver ||
		(NULL != mov_driver && 0 == strcasecmp(mov_driver,writer->name))) {
		movie_driver = writer;
		i_movie_driver = i;
	    }
	    i++;
	}
	m_movie_driver[i].nr  = i;
	m_movie_driver[i].str = NULL;
    }

    /* audio formats */
    for (i = 0; NULL != movie_driver->audio[i].name; i++)
	;
    if (m_movie_audio)
	free(m_movie_audio);
    movie_audio = 0;
    m_movie_audio = malloc(sizeof(struct STRTAB)*(i+2));
    memset(m_movie_audio,0,sizeof(struct STRTAB)*(i+2));
    for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	m_movie_audio[i].nr  = i;
	m_movie_audio[i].str = movie_driver->audio[i].desc ?
	    movie_driver->audio[i].desc :
	    ng_afmt_to_desc[movie_driver->audio[i].fmtid];
	if (NULL != mov_audio)
	    if (0 == strcasecmp(mov_audio,movie_driver->audio[i].name))
		movie_audio = i;
    }
    m_movie_audio[i].nr  = i;
    m_movie_audio[i].str = "no sound";

    /* video formats */
    for (i = 0; NULL != movie_driver->video[i].name; i++)
	;
    if (m_movie_video)
	free(m_movie_video);
    movie_video = 0;
    m_movie_video = malloc(sizeof(struct STRTAB)*(i+2));
    memset(m_movie_video,0,sizeof(struct STRTAB)*(i+2));
    for (i = 0; NULL != movie_driver->video[i].name; i++) {
	m_movie_video[i].nr  = i;
	m_movie_video[i].str = movie_driver->video[i].desc ?
	    movie_driver->video[i].desc :
	    ng_vfmt_to_desc[movie_driver->video[i].fmtid];
	if (NULL != mov_video)
	    if (0 == strcasecmp(mov_video,movie_driver->video[i].name))
		movie_video = i;
    }

    /* need audio filename? */
    sensitive = movie_driver->combined ? False : True;
    XtVaSetValues(w_movie_flabel,
		  XtNsensitive,sensitive,
		  NULL);
    XtVaSetValues(w_movie_faudio,
		  XtNsensitive,sensitive,
		  NULL);
}

static void
init_movie_menus(void)
{
    update_movie_menus();

    if (mov_rate)
	do_va_cmd(3,"movie","rate",mov_rate);
    if (mov_fps)
	do_va_cmd(3,"movie","fps",mov_fps);
    set_menu_val(w_movie_driver,MOVIE_DRIVER,m_movie_driver,i_movie_driver);
    set_menu_val(w_movie_audio,MOVIE_AUDIO,m_movie_audio,movie_audio);
    set_menu_val(w_movie_rate,MOVIE_RATE,m_movie_rate,movie_rate);
    set_menu_val(w_movie_video,MOVIE_VIDEO,m_movie_video,movie_video);
    set_menu_val(w_movie_fps,MOVIE_FPS,m_movie_fps,movie_fps);
}

#define PANED_FIX               \
	XtNallowResize, False,  \
	XtNshowGrip,    False,  \
	XtNskipAdjust,  True

struct DO_CMD cmd_fs   = { 1, { "fullscreen",        NULL }};
struct DO_CMD cmd_cap  = { 2, { "capture", "toggle", NULL }};
struct DO_CMD cmd_jpeg = { 2, { "snap",    "jpeg",   NULL }};
struct DO_CMD cmd_ppm  = { 2, { "snap",    "ppm",    NULL }};

struct DO_AC  ac_fs    = { 0, "FullScreen", { NULL }};
struct DO_AC  ac_top   = { 0, "StayOnTop",  { NULL }};

struct DO_AC  ac_avi   = { 1, "Popup",      { "streamer", NULL }};
struct DO_AC  ac_chan  = { 1, "Popup",      { "channels", NULL }};
struct DO_AC  ac_conf  = { 1, "Popup",      { "config",   NULL }};
struct DO_AC  ac_launch = { 1, "Popup",      { "launcher",  NULL }};
struct DO_AC  ac_zap   = { 0, "Zap",        { NULL }};
struct DO_AC  ac_mute  = { 0, "Mute",       { NULL }};

static void
menu_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct ng_attribute *attr;
    long  cd = (long)clientdata;
    int   j;

    switch (cd) {
#if 0
    case 10:
	attr = ng_attr_byid(a_drv,ATTR_ID_NORM);
	if (-1 != (j=popup_menu(widget,"TV Norm",attr->choices)))
	    do_va_cmd(2,"setnorm",ng_attr_getstr(attr,j));
	break;
    case 11:
	attr = ng_attr_byid(a_drv,ATTR_ID_INPUT);
	if (-1 != (j=popup_menu(widget,"Video Source",attr->choices)))
	    do_va_cmd(2,"setinput",ng_attr_getstr(attr,j));
	break;
#endif
    case 12:
	if (-1 != (j=popup_menu(widget,"Freq table",chanlist_names)))
	    do_va_cmd(2,"setfreqtab",chanlist_names[j].str);
	break;
    case 13:
	attr = ng_attr_byid(attrs,ATTR_ID_AUDIO_MODE);
	if (NULL != attr) {
	    int i,mode = attr->read(attr);
	    for (i = 1; attr->choices[i].str != NULL; i++) {
		attr->choices[i].nr =
		    (1 << (i-1)) & mode ? (1 << (i-1)) : -1;
	    }
	    if (-1 != (j=popup_menu(widget,"Audio",attr->choices)))
		change_audio(attr->choices[j].nr);
	}
	break;
    case 14:
	if (-1 != (j=popup_menu(widget,"Capture",cap_list)))
	    do_va_cmd(2,"capture",cap_list[j].str);
	break;

    case 20:
	if (-1 != (j=popup_menu(widget,MOVIE_DRIVER,m_movie_driver))) {
	    int i = 0;
	    struct list_head *item;
	    struct ng_writer *writer = NULL;

	    list_for_each(item,&ng_writers) {
		if (i++ == j)
		    writer = list_entry(item,struct ng_writer, list);
	    }
	    do_va_cmd(3,"movie","driver",writer->name);
	}
	break;
    case 21:
	if (-1 != (j=popup_menu(widget,MOVIE_AUDIO,m_movie_audio)))
	    do_va_cmd(3,"movie","audio",
		      movie_driver->audio[j].name ?
		      movie_driver->audio[j].name :
		      "none");
	break;
    case 22:
	if (-1 != (j=popup_menu(widget,MOVIE_RATE,m_movie_rate)))
	    do_va_cmd(3,"movie","rate",m_movie_rate[j].str);
	break;
    case 23:
	if (-1 != (j=popup_menu(widget,MOVIE_VIDEO,m_movie_video)))
	    do_va_cmd(3,"movie","video",movie_driver->video[j].name);
	break;
    case 24:
	if (-1 != (j=popup_menu(widget,MOVIE_FPS,m_movie_fps)))
	    do_va_cmd(3,"movie","fps",m_movie_fps[j].str);
	break;
    default:
	/* nothing */
	break;
    }
}

static void
jump_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct xaw_attribute *a = clientdata;
    struct ng_attribute *attr = a->attr;
    const char *name;
    char val[16];
    int  value,range;

    range = attr->max - attr->min;
    value = (int)(*(float*)call_data * range) + attr->min;
    if (debug)
	fprintf(stderr,"scroll: value is %d\n",value);
    if (value < attr->min)
	value = attr->min;
    if (value > attr->max)
	value = attr->max;
    sprintf(val,"%d",value);

    if (a) {
	name = a->attr->name;
	do_va_cmd(3,"setattr",name,val);
    } else {
	name = XtName(XtParent(widget));
	do_va_cmd(2,name,val);
    }
}

static void
scroll_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    long      move = (long)call_data;
    Dimension length;
    float     shown,top1,top2;

    XtVaGetValues(widget,
		  XtNlength,     &length,
		  XtNshown,      &shown,
		  XtNtopOfThumb, &top1,
		  NULL);

    top2 = top1 + (float)move/length/5;
    if (top2 < 0) top2 = 0;
    if (top2 > 1) top2 = 1;
#if 1
    fprintf(stderr,"scroll by %ld\tlength %d\tshown %f\ttop %f => %f\n",
	    move,length,shown,top1,top2);
#endif
    jump_scb(widget,clientdata,&top2);
}

static void
attr_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct xaw_attribute *a = clientdata;
    int j;

    switch (a->attr->type) {
    case ATTR_TYPE_CHOICE:
	j=popup_menu(widget,a->attr->name,a->attr->choices);
	if (-1 != j)
	    do_va_cmd(3,"setattr",a->attr->name,a->attr->choices[j].str);
	break;
    case ATTR_TYPE_BOOL:
	do_va_cmd(3,"setattr",a->attr->name,"toggle");
	break;
    }
}

static void
add_attr_option(Widget paned, struct ng_attribute *attr)
{
    struct xaw_attribute *a;
    Widget p,l;

    a = malloc(sizeof(*a));
    memset(a,0,sizeof(*a));
    a->attr = attr;

    switch (attr->type) {
    case ATTR_TYPE_BOOL:
    case ATTR_TYPE_CHOICE:
	a->cmd = XtVaCreateManagedWidget(attr->name,
					 commandWidgetClass, paned,
					 PANED_FIX,
					 NULL);
	XtAddCallback(a->cmd,XtNcallback,attr_cb,a);
	break;
    case ATTR_TYPE_INTEGER:
	p = XtVaCreateManagedWidget(attr->name,
				    panedWidgetClass, paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX,
				    NULL);
	l = XtVaCreateManagedWidget("l",labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	a->scroll = XtVaCreateManagedWidget("s",scrollbarWidgetClass,p,
					    PANED_FIX,
					    NULL);
	XtAddCallback(a->scroll, XtNjumpProc,   jump_scb,   a);
	XtAddCallback(a->scroll, XtNscrollProc, scroll_scb, a);
	if (attr->id >= ATTR_ID_COUNT)
	    XtVaSetValues(l,XtNlabel,attr->name,NULL);
	break;
    }
    a->next = xaw_attrs;
    xaw_attrs = a;
}

static void
create_optwin(void)
{
    Widget c;

    opt_shell = XtVaAppCreateShell("Options", "Xawtv",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   NULL);
    XtOverrideTranslations(opt_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    opt_paned = XtVaCreateManagedWidget("paned", panedWidgetClass, opt_shell,
					NULL);

    c = XtVaCreateManagedWidget("mute", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c, XtNcallback, action_cb, (XtPointer)&ac_mute);

    c = XtVaCreateManagedWidget("fs", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_fs);

    c = XtVaCreateManagedWidget("grabppm", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_ppm);
    c = XtVaCreateManagedWidget("grabjpeg", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_jpeg);
    c = XtVaCreateManagedWidget("recavi", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_avi);
    if (f_drv & CAN_TUNE) {
	c = XtVaCreateManagedWidget("chanwin", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
	XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_chan);
	c = XtVaCreateManagedWidget("confwin", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
	XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_conf);
	c = XtVaCreateManagedWidget("zap", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
	XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_zap);
    }
    c = XtVaCreateManagedWidget("launchwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_launch);
    if (wm_stay_on_top) {
	c = XtVaCreateManagedWidget("top", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
	XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_top);
    }
}

static void
create_attr_widgets(void)
{
    struct ng_attribute *attr;
    Widget c;

    /* menus / multiple choice options */
    attr = ng_attr_byid(attrs,ATTR_ID_NORM);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_INPUT);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_AUDIO_MODE);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);

    if (f_drv & CAN_TUNE) {
	c_freq = XtVaCreateManagedWidget("freq", commandWidgetClass, opt_paned,
					 PANED_FIX, NULL);
	XtAddCallback(c_freq,XtNcallback,menu_cb,(XtPointer)12);
    }
    c_cap = XtVaCreateManagedWidget("cap", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
    XtAddCallback(c_cap,XtNcallback,menu_cb,(XtPointer)14);

    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_CHOICE)
	    continue;
	add_attr_option(opt_paned,attr);
    }

    /* integer options */
    attr = ng_attr_byid(attrs,ATTR_ID_BRIGHT);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_HUE);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_CONTRAST);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_COLOR);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_VOLUME);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);

    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_INTEGER)
	    continue;
	add_attr_option(opt_paned,attr);
    }

    /* boolean options */
    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_BOOL)
	    continue;
	add_attr_option(opt_paned,attr);
    }

    /* quit */
    c = XtVaCreateManagedWidget("quit", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,ExitCB,NULL);
}

/*--- avi recording ------------------------------------------------------*/

static void
exec_record(Widget widget, XtPointer client_data, XtPointer calldata)
{
    if (!(f_drv & CAN_CAPTURE)) {
	fprintf(stderr,"grabbing: not supported [try -noxv switch?]\n");
	return;
    }

    if (rec_work_id) {
	do_va_cmd(2,"movie","stop");
    } else {
	do_va_cmd(2,"movie","start");
    }
    return;
}

static void
exec_player_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    char *filename;

    XtVaGetValues(w_movie_fvideo,XtNstring,&filename,NULL);
    filename = tilde_expand(filename);
    exec_player(filename);
}

static void
do_movie_record(int argc, char **argv)
{
    char *fvideo,*faudio;
    struct ng_video_fmt video;
    struct ng_audio_fmt audio;
    const struct ng_writer *wr;
    int i;

    /* set parameters */
    if (argc > 1 && 0 == strcasecmp(argv[0],"driver")) {
	struct list_head *item;
	struct ng_writer *writer;
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    if (0 == strcasecmp(argv[1],writer->name)) {
		movie_driver = writer;
		i_movie_driver = i;
	    }
	    i++;
	}

	set_menu_val(w_movie_driver,MOVIE_DRIVER,
		     m_movie_driver,i_movie_driver);
	update_movie_menus();
	set_menu_val(w_movie_audio,MOVIE_AUDIO,
		     m_movie_audio,movie_audio);
	set_menu_val(w_movie_video,MOVIE_VIDEO,
		     m_movie_video,movie_video);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fvideo")) {
	XtVaSetValues(w_movie_fvideo,XtNstring,argv[1],NULL);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"faudio")) {
	XtVaSetValues(w_movie_faudio,XtNstring,argv[1],NULL);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"audio")) {
	for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	    if (0 == strcasecmp(argv[1],movie_driver->audio[i].name))
		movie_audio = m_movie_audio[i].nr;
	}
	if (0 == strcmp(argv[1],"none"))
	    movie_audio = m_movie_audio[i].nr;
	set_menu_val(w_movie_audio,MOVIE_AUDIO,
		     m_movie_audio,movie_audio);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"rate")) {
	for (i = 0; m_movie_rate[i].str != NULL; i++)
	    if (atoi(argv[1]) == m_movie_rate[i].nr)
		movie_rate = m_movie_rate[i].nr;
	set_menu_val(w_movie_rate,MOVIE_RATE,
		     m_movie_rate,movie_rate);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"video")) {
	for (i = 0; NULL != movie_driver->video[i].name; i++)
	    if (0 == strcasecmp(argv[1],movie_driver->video[i].name))
		movie_video = m_movie_video[i].nr;
	set_menu_val(w_movie_video,MOVIE_VIDEO,
		     m_movie_video,movie_video);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fps")) {
	for (i = 0; m_movie_fps[i].str != NULL; i++) {
	    int fps = (int)(atof(argv[1]) * 1000 + .5);
	    if (fps == m_movie_fps[i].nr)
		movie_fps = m_movie_fps[i].nr;
	}
	set_menu_val(w_movie_fps,MOVIE_FPS,
		     m_movie_fps,movie_fps);
    }

    /* start */
    if (argc > 0 && 0 == strcasecmp(argv[0],"start")) {
	if (0 != cur_movie)
	    return; /* records already */
	cur_movie = 1;
	movie_blit = (cur_capture == CAPTURE_GRABDISPLAY);
	video_gd_suspend();

	XtVaGetValues(w_movie_fvideo,XtNstring,&fvideo,NULL);
	XtVaGetValues(w_movie_faudio,XtNstring,&faudio,NULL);
	fvideo = tilde_expand(fvideo);
	faudio = tilde_expand(faudio);

	memset(&video,0,sizeof(video));
	memset(&audio,0,sizeof(audio));

	wr = movie_driver;
	video.fmtid  = wr->video[movie_video].fmtid;
	video.width  = cur_tv_width;
	video.height = cur_tv_height;
	if (NULL != wr->audio[movie_audio].name) {
	    audio.fmtid  = wr->audio[movie_audio].fmtid;
	    audio.rate   = movie_rate;
	} else {
	    audio.fmtid  = AUDIO_NONE;
	}

	movie_state = movie_writer_init
	    (fvideo, faudio, wr,
	     &video, wr->video[movie_video].priv, movie_fps,
	     &audio, wr->audio[movie_audio].priv, args.dspdev,
	     args.bufcount,args.parallel);
	if (NULL == movie_state) {
	    /* init failed */
	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    XtVaSetValues(w_movie_status,XtNlabel,"error [init]",NULL);
	    return;
	}
	if (0 != movie_writer_start(movie_state)) {
	    /* start failed */
	    movie_writer_stop(movie_state);
	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    XtVaSetValues(w_movie_status,XtNlabel,"error [start]",NULL);
	    return;
	}
	rec_work_id  = XtAppAddWorkProc(app_context,rec_work,NULL);
	XtVaSetValues(w_movie_status,XtNlabel,"recording",NULL);
	return;
    }

    /* stop */
    if (argc > 0 && 0 == strcasecmp(argv[0],"stop")) {
	if (0 == cur_movie)
	    return; /* nothing to stop here */

	movie_writer_stop(movie_state);
	XtRemoveWorkProc(rec_work_id);
	rec_work_id = 0;
	video_gd_restart();
	cur_movie = 0;
	return;
    }
}

static void
do_rec_status(char *message)
{
    XtVaSetValues(w_movie_status,XtNlabel,message,NULL);
}

#define FIX_LEFT_TOP        \
    XtNleft,XawChainLeft,   \
    XtNright,XawChainRight, \
    XtNtop,XawChainTop,     \
    XtNbottom,XawChainTop

static void
create_strwin(void)
{
    Widget form,label,button,text;

    str_shell = XtVaAppCreateShell("Streamer", "Xawtv",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   NULL);
    XtOverrideTranslations(str_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));

    form = XtVaCreateManagedWidget("form", formWidgetClass, str_shell,
				   NULL);

    /* driver */
    button = XtVaCreateManagedWidget("driver", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     NULL);
    w_movie_driver = button;

    /* movie filename */
    label = XtVaCreateManagedWidget("vlabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, button,
				    NULL);
    text = XtVaCreateManagedWidget("vname", asciiTextWidgetClass, form,
				   FIX_LEFT_TOP,
				   XtNfromVert, label,
				   NULL);
    w_movie_fvideo = text;

    /* audio filename */
    label = XtVaCreateManagedWidget("alabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, text,
				    NULL);
    w_movie_flabel = label;
    text= XtVaCreateManagedWidget("aname", asciiTextWidgetClass, form,
				  FIX_LEFT_TOP,
				  XtNfromVert, label,
				  NULL);
    w_movie_faudio = text;

    /* audio format */
    button = XtVaCreateManagedWidget("audio", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, text,
				     NULL);
    w_movie_audio = button;
    button = XtVaCreateManagedWidget("rate", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_rate = button;

    /* video format */
    button = XtVaCreateManagedWidget("video", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_video = button;
    button = XtVaCreateManagedWidget("fps", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_fps = button;
    label = XtVaCreateManagedWidget("size", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, button,
				    NULL);
    w_movie_size = label;

    /* status line */
    label = XtVaCreateManagedWidget("status", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, label,
				    XtNlabel,    "",
				    NULL);
    w_movie_status = label;

    /* cmd buttons */
    button = XtVaCreateManagedWidget("streamer", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, label,
				     NULL);
    XtAddCallback(button,XtNcallback,exec_record,NULL);

    button = XtVaCreateManagedWidget("xanim", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    XtAddCallback(button,XtNcallback,exec_player_cb,NULL);

#if 0
    label = XtVaCreateManagedWidget("olabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert,button,
				    NULL);
    str_text = XtVaCreateManagedWidget("output", asciiTextWidgetClass, form,
				       XtNleft,XawChainLeft,
				       XtNright,XawChainRight,
				       XtNtop,XawChainTop,
				       XtNbottom,XawChainBottom,
				       XtNfromVert,label,
				       NULL);
#endif

    XtAddCallback(w_movie_driver,XtNcallback,menu_cb,(XtPointer)20);
    XtAddCallback(w_movie_audio,XtNcallback,menu_cb,(XtPointer)21);
    XtAddCallback(w_movie_rate,XtNcallback,menu_cb,(XtPointer)22);
    XtAddCallback(w_movie_video,XtNcallback,menu_cb,(XtPointer)23);
    XtAddCallback(w_movie_fps,XtNcallback,menu_cb,(XtPointer)24);
}

/*--- launcher window -----------------------------------------------------*/

static void
create_launchwin(void)
{
    launch_shell = XtVaAppCreateShell("Launcher", "Xawtv",
				     topLevelShellWidgetClass,
				     dpy,
				     XtNclientLeader,app_shell,
				     XtNvisual,vinfo.visual,
				     XtNcolormap,colormap,
				     XtNdepth,vinfo.depth,
				     NULL);
    XtOverrideTranslations(launch_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    launch_paned = XtVaCreateManagedWidget("paned", panedWidgetClass,
					  launch_shell, NULL);
}

/*--- main ---------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
    int            i, min_width, min_height;
    unsigned long  freq;

    hello_world("xawtv");
    progname = strdup(argv[0]);

    /* toplevel */
    XtSetLanguageProc(NULL,NULL,NULL);
    app_shell = XtVaAppInitialize(&app_context, "Xawtv",
				  opt_desc, opt_count,
				  &argc, argv,
				  fallback_ressources,
				  NULL);
    dpy = XtDisplay(app_shell);
    init_atoms(dpy);

    /* command line args */
    ng_init();
    handle_cmdline_args();

    /* device scan */
    if (args.hwscan) {
	fprintf(stderr,"looking for available devices\n");
#ifdef HAVE_LIBXV
	xv_video_init(-1,1);
#endif
	grabber_scan();
    }

    /* look for a useful visual */
    visual_init("xawtv","Xawtv");

    /* remote display? */
    do_overlay = !args.remote;
    if (do_overlay)
	x11_check_remote();
    v4lconf_init();

    /* x11 stuff */
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    x11_misc_init(dpy);
    if (debug)
	fprintf(stderr,"main: dga extention...\n");
    xfree_dga_init(dpy);
    if (debug)
	fprintf(stderr,"main: xinerama extention...\n");
    xfree_xinerama_init(dpy);
#ifdef HAVE_LIBXV
    if (debug)
	fprintf(stderr,"main: xvideo extention [video]...\n");
    if (args.xv_video)
	xv_video_init(args.xv_port,0);
    if (debug)
	fprintf(stderr,"main: xvideo extention [image]...\n");
    if (args.xv_image)
	xv_image_init(dpy);
#endif

    /* set hooks (command.c) */
    update_title        = new_title;
    display_message     = new_message;
#if TT
    vtx_message         = display_vtx;
#endif
#ifdef HAVE_ZVBI
    vtx_subtitle        = display_subtitle;
#endif
    attr_notify         = new_attr;
    volume_notify       = new_volume;
    set_capture_hook    = do_capture;
    fullscreen_hook     = do_fullscreen;
    movie_hook          = do_movie_record;
    rec_status          = do_rec_status;
    exit_hook           = do_exit;
    capture_get_hook    = video_gd_suspend;
    capture_rel_hook    = video_gd_restart;

    if (debug)
	fprintf(stderr,"main: init main window...\n");
    tv = video_init(app_shell, &vinfo, simpleWidgetClass,
		    args.bpp, args.gl);
    XtAddEventHandler(XtParent(tv),StructureNotifyMask, True,
		      resize_event, NULL);
    if (debug)
	fprintf(stderr,"main: install signal handlers...\n");
    xt_siginit();
    if (NULL == drv) {
	if (debug)
	    fprintf(stderr,"main: open grabber device...\n");
	grabber_init();
    }

    if (f_drv & CAN_TUNE) {
	freqtab_notify      = new_freqtab;
	setfreqtab_notify   = new_freqtab;
	setstation_notify   = new_channel;
	channel_switch_hook = leaving_channel;
    } else {
	new_title("Capture");
    }

    /* create windows */
    XSetIOErrorHandler(x11_ctrl_alt_backspace);
    if (debug)
	fprintf(stderr,"main: checking wm...\n");
    wm_detect(dpy);
    if (debug)
	fprintf(stderr,"main: creating windows ...\n");
    create_optwin();
    create_onscreen(labelWidgetClass);
    create_vtx();
    create_chanwin();
    if ((f_drv & CAN_TUNE))
	create_confwin();
    create_strwin();
    create_launchwin();

    /* read config file + related settings */
    if (debug)
	fprintf(stderr,"main: init frequency tables ...\n");
    freq_init();
    if (args.readconfig) {
	if (debug)
	    fprintf(stderr,"main: read config file ...\n");
	read_config(args.conffile ? args.conffile : NULL, &argc, argv);
    }
    if (0 != strlen(mixerdev)) {
	struct ng_attribute *attr;
	if (debug)
	    fprintf(stderr,"main: open mixer device...\n");
	if (NULL != (attr = ng_mix_init(mixerdev,mixerctl)))
	    add_attrs(attr);
    }
    init_movie_menus();
    create_attr_widgets();

    xt_vm_randr_input_init(dpy);

    if (debug)
	fprintf(stderr,"main: mapping main window ...\n");
    XtRealizeWidget(app_shell);
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);
    XSetWMProtocols(XtDisplay(app_shell), XtWindow(app_shell),
		    &WM_DELETE_WINDOW, 1);

    drv->get_min_size(h_drv, &min_width, &min_height);
    ng_ratio_fixup2(&min_width, &min_height, NULL, NULL,
                    ng_ratio_x, ng_ratio_y, True);
    min_width  = ((min_width + (WIDTH_INC - 1)) / WIDTH_INC) * WIDTH_INC;
    min_height = ((min_height + (HEIGHT_INC - 1)) / HEIGHT_INC) * HEIGHT_INC;
    if (debug)
	fprintf(stderr,"main: window min size %dx%d\n", min_width, min_height);
    XtVaSetValues(app_shell,
		  XtNwidthInc,  WIDTH_INC,
		  XtNheightInc, HEIGHT_INC,
		  XtNminWidth,  min_width,
		  XtNminHeight, min_height,
		  NULL);
    if (f_drv & CAN_TUNE)
	XtVaSetValues(chan_shell,
		      XtNwidth,pix_width*pix_cols+30,
		      NULL);

    /* mouse pointer magic */
    XtAddEventHandler(tv, PointerMotionMask, True, mouse_event, NULL);
    mouse_event(tv,NULL,NULL,NULL);

    /* init hardware */
    if (debug)
	fprintf(stderr,"main: initialize hardware ...\n");
    attr_init();
    audio_on();
    audio_init();

    /* build channel list */
    if (args.readconfig) {
	if (debug)
	    fprintf(stderr,"main: parse config file ...\n");
	parse_config(f_drv & CAN_TUNE);
    }

    channel_menu();

    while (!window_configured)
        xt_handle_pending(dpy);
    init_overlay();

    set_property(0,NULL,NULL);
    if (optind+1 == argc && (f_drv & CAN_TUNE)) {
	do_va_cmd(2,"setstation",argv[optind]);
    } else if (f_drv & CAN_TUNE) {
	if (0 != (freq = drv->getfreq(h_drv))) {
	    for (i = 0; i < chancount; i++)
		if (chanlist[i].freq == freq*1000/16) {
		    do_va_cmd(2,"setchannel",chanlist[i].name);
		    break;
		}
	}
	if (-1 == cur_channel) {
	    if (count > 0) {
		if (debug)
		    fprintf(stderr,"main: tuning first station\n");
		do_va_cmd(2,"setstation","0");
	    } else {
		if (debug)
		    fprintf(stderr,"main: setting defaults\n");
		set_defaults();
	    }
	} else {
	    if (debug)
		fprintf(stderr,"main: known station tuned, not changing\n");
	}
    }
    XtAddEventHandler(tv,ExposureMask, True, tv_expose_event, NULL);

    if (args.fullscreen) {
	XSync(dpy,False);
	do_fullscreen();
    } else {
	XtAppAddWorkProc(app_context,MyResize,NULL);
    }

    sprintf(modename,"%dx%d, ",
	    XtScreen(app_shell)->width,XtScreen(app_shell)->height);
    strcat(modename,ng_vfmt_to_desc[x11_dpy_fmtid]);
    new_message(modename);
    if (!have_config)
	XtCallActionProc(tv,"Help",NULL,NULL,0);

    xt_main_loop();
    return 0;
}
