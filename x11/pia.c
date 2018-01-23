/*
 * simple movie player
 *
 *  (c) 2002 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Simple.h>
#include <X11/extensions/XShm.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab-ng.h"
#include "blit.h"

/* ------------------------------------------------------------------------ */

int debug = 0;

/* X11 */
static XtAppContext app_context;
static Widget       app_shell;
static Display      *dpy;
static Colormap     colormap;
static Visual       *visual;
static XVisualInfo  vinfo,*vinfo_list;

static Widget simple;
static Dimension swidth,sheight;

static struct blit_state *blit;

/* libng */
static const struct ng_reader      *reader;
static void                        *rhandle;
static const struct ng_dsp_driver  *snd;
static void                        *shandle;
static int                         snd_fd;

static struct ng_video_fmt         cfmt;
static struct ng_video_conv        *conv;
static void                        *chandle;

static struct ng_video_fmt         *vfmt;
static struct ng_audio_fmt         *afmt;
static struct ng_video_buf         *vbuf;
static struct ng_audio_buf         *abuf;

/* ------------------------------------------------------------------------ */

struct ARGS {
    char  *dsp;
    int   slow;
    int   help;
    int   verbose;
    int   debug;
    int   xv;
    int   gl;
    int   max;
    int   audio;
    int   video;
} args;

XtResource args_desc[] = {
    /* name, class, type, size, offset, default_type, default_addr */
    {
	/* Strings */
	"dsp",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,dsp),
	XtRString, NULL,
    },{
	/* Integer */
	"verbose",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,verbose),
	XtRString, "0"
    },{
	"debug",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,debug),
	XtRString, "0"
    },{
	"help",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,help),
	XtRString, "0"
    },{
	"slow",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,slow),
	XtRString, "1"
    },{
	"xv",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,xv),
	XtRString, "1"
    },{
	"gl",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,gl),
	XtRString, "1"
    },{
	"max",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,max),
	XtRString, "0"
    },{
	"audio",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,audio),
	XtRString, "1"
    },{
	"video",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,video),
	XtRString, "1"
    }
};
const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-dsp",        "dsp",         XrmoptionSepArg, NULL },
    { "-slow",       "slow",        XrmoptionSepArg, NULL },

    { "-h",          "help",        XrmoptionNoArg,  "1" },
    { "-help",       "help",        XrmoptionNoArg,  "1" },
    { "--help",      "help",        XrmoptionNoArg,  "1" },

    { "-v",          "verbose",     XrmoptionNoArg,  "1" },
    { "-verbose",    "verbose",     XrmoptionNoArg,  "1" },
    { "-debug",      "debug",       XrmoptionNoArg,  "1" },
    { "-max",        "max",         XrmoptionNoArg,  "1" },

    { "-xv",         "xv",          XrmoptionNoArg,  "1" },
    { "-noxv",       "xv",          XrmoptionNoArg,  "0" },
    { "-gl",         "gl",          XrmoptionNoArg,  "1" },
    { "-nogl",       "gl",          XrmoptionNoArg,  "0" },
    { "-audio",      "audio",       XrmoptionNoArg,  "1" },
    { "-noaudio",    "audio",       XrmoptionNoArg,  "0" },
    { "-video",      "video",       XrmoptionNoArg,  "1" },
    { "-novideo",    "video",       XrmoptionNoArg,  "0" },
};
const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));

/* ------------------------------------------------------------------------ */

static void quit_ac(Widget widget, XEvent *event,
		    String *params, Cardinal *num_params)
{
    exit(0);
}

static void resize_ev(Widget widget, XtPointer client_data,
		      XEvent *event, Boolean *d)
{
    static int first = 1;

    switch(event->type) {
    case MapNotify:
    case ConfigureNotify:
	if (first) {
	    blit = blit_init(simple,&vinfo,args.gl);
	    first = 0;
	}
	XtVaGetValues(widget,XtNheight,&sheight,XtNwidth,&swidth,NULL);
	if (vfmt)
	    blit_resize(blit,swidth,sheight);
	break;
    }
}

static XtActionsRec action_table[] = {
    { "Quit",  quit_ac },
};

static String res[] = {
    "pia.winGravity:		 Static",
    "pia.playback.translations:  #override \\n"
    "       <Key>Q:              Quit()    \\n"
    "       <Key>Escape:         Quit()",
    "pia.playback.background:    black",
    NULL
};

static void usage(FILE *out, char *prog)
{
    char *h;

    if (NULL != (h = strrchr(prog,'/')))
	prog = h+1;
    fprintf(out,
	    "%s is simple movie player\n"
	    "\n"
	    "usage: %s [ options ] movie\n"
	    "options:\n"
	    "  -h, -help       this text\n"
	    "  -v, -verbose    be verbose\n"
	    "      -debug      enable debug messages\n"
	    "      -dsp <dev>  use sound device <dev>\n"
#ifdef HAVE_LIBXV
	    "      -noxv       disable Xvideo extention\n"
#endif
#ifdef HAVE_GL
	    "      -nogl       disable OpenGL\n"
#endif
	    "\n",
	    prog,prog);
}

static void sync_info(long long drift, int drops, int frames)
{
    int total = drops + frames;

    fprintf(stderr,"a/v sync: audio drift is %4d ms / "
	    "%d of %d frame(s) [%3.1f%%] dropped \r",
	    (int)((drift)/1000000),drops,total,
	    total ? (float)drops * 100 / total : 0);
}

int main(int argc, char *argv[])
{
    long long start, now, delay, latency = 0, drift = 0;
    struct timeval wait;
    int n, drop, droptotal, framecount, ww, wh;
    unsigned int fmtids[2*VIDEO_FMT_COUNT], i;
    XEvent event;

    app_shell = XtVaAppInitialize(&app_context, "pia",
				  opt_desc, opt_count,
				  &argc, argv,
				  res, NULL);
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    if (args.help) {
	usage(stdout,argv[0]);
	exit(1);
    }
    if (args.debug) {
	debug    = args.debug;
	ng_debug = args.debug;
    }

    if (argc < 2) {
	usage(stderr,argv[0]);
	exit(1);
    }
    ng_init();

    /* open file */
    reader = ng_find_reader(argv[1]);
    if (NULL == reader) {
	fprintf(stderr,"can't handle %s\n",argv[1]);
	exit(1);
    }
    rhandle = reader->rd_open(argv[1]);
    if (NULL == rhandle) {
	fprintf(stderr,"opening %s failed\n",argv[1]);
	exit(1);
    }
    vfmt = reader->rd_vfmt(rhandle,NULL,0);
    if (0 == vfmt->width || 0 == vfmt->height)
	vfmt = NULL;
    afmt = reader->rd_afmt(rhandle);

    if (0 == args.video)
	vfmt = NULL;
    if (0 == args.audio)
	afmt = NULL;
    if (1 != args.slow)
	/* no audio for slow motion, will not sync anyway ... */
	afmt = NULL;

    /* init x11 stuff */
    dpy = XtDisplay(app_shell);
    visual = x11_find_visual(XtDisplay(app_shell));
    vinfo.visualid = XVisualIDFromVisual(visual);
    vinfo_list = XGetVisualInfo(dpy, VisualIDMask, &vinfo, &n);
    vinfo = vinfo_list[0];
    XFree(vinfo_list);
    if (visual != DefaultVisualOfScreen(XtScreen(app_shell))) {
	fprintf(stderr,"switching visual (0x%lx)\n",vinfo.visualid);
	colormap = XCreateColormap(dpy,RootWindowOfScreen(XtScreen(app_shell)),
				   vinfo.visual,AllocNone);
	XtDestroyWidget(app_shell);
	app_shell = XtVaAppCreateShell("pia","pia",
				       applicationShellWidgetClass, dpy,
				       XtNvisual,vinfo.visual,
				       XtNcolormap,colormap,
				       XtNdepth, vinfo.depth,
				       NULL);
    } else {
	colormap = DefaultColormapOfScreen(XtScreen(app_shell));
    }
    x11_init_visual(XtDisplay(app_shell),&vinfo);
#if HAVE_LIBXV
    if (args.xv)
	xv_image_init(dpy);
#endif
    XtAppAddActions(app_context,action_table,
		    sizeof(action_table)/sizeof(XtActionsRec));
    XtVaSetValues(app_shell, XtNtitle,argv[1],NULL);

    /* show window */
    ww = 320;
    wh = 32;
    if (vfmt) {
	ww = vfmt->width;
	wh = vfmt->height;
	if (args.max) {
	    int sw = XtScreen(app_shell)->width;
	    int sh = XtScreen(app_shell)->height;
	    if (sw * wh > sh * ww) {
		ww = ww * sh / wh;
		wh = sh;
	    } else {
		wh = wh * sw / ww;
		ww =sw;
	    }
	}
    }
    simple = XtVaCreateManagedWidget("playback",simpleWidgetClass,app_shell,
				     XtNwidth,ww, XtNheight,wh, NULL);
    XtAddEventHandler(simple, StructureNotifyMask, True, resize_ev, NULL);
    XtRealizeWidget(app_shell);
    while (NULL == blit) {
	XFlush(dpy);
	while (True == XCheckMaskEvent(dpy, ~0, &event))
	    XtDispatchEvent(&event);
    }

    /* video setup */
    if (vfmt) {
	blit_get_formats(blit,fmtids,sizeof(fmtids)/sizeof(int));
	vfmt = reader->rd_vfmt(rhandle,fmtids,sizeof(fmtids)/sizeof(int));
	if (0 == vfmt->width || 0 == vfmt->height || VIDEO_NONE == vfmt->fmtid)
	    vfmt = NULL;
    }
    if (vfmt) {
	for (i = 0; i < sizeof(fmtids)/sizeof(int); i++)
	    if (fmtids[i] == vfmt->fmtid)
		break;
	if (i == sizeof(fmtids)/sizeof(int)) {
	    /* blit can't display directly -- have to convert somehow */
	    for (i = 0; i < sizeof(fmtids)/sizeof(int); i++)
		if (NULL != (conv = ng_conv_find_match(vfmt->fmtid,fmtids[i])))
		    break;
	    if (conv) {
		cfmt = *vfmt;
		cfmt.fmtid = conv->fmtid_out;
		cfmt.bytesperline = 0;
		chandle = ng_convert_alloc(conv,vfmt,&cfmt);
		ng_convert_init(chandle);
		if (debug)
		    fprintf(stderr,"pia: conv [%s] => [%s]\n",
			    ng_vfmt_to_desc[vfmt->fmtid],
			    ng_vfmt_to_desc[cfmt.fmtid]);
	    }
	}
    }

    /* audio setup */
    if (afmt) {
	struct ng_audio_fmt f = *afmt;
	snd = ng_dsp_open(args.dsp ? args.dsp : "/dev/dsp", &f,0,&shandle);
	if (NULL == snd)
	    afmt = NULL;
	else {
	    snd_fd  = snd->fd(shandle);
	    latency = snd->latency(shandle);
	    if (debug)
		fprintf(stderr,"a/v sync: audio latency is %lld ms\n",
			latency/1000000);
	}
    }

    /* enter main loop
     *
     * can't use XtAppMainLoop + Input + Timeout handlers here because
     * that doesn't give us usable event scheduling, thus we have our
     * own main loop here ...
     */
    drop = 0;
    droptotal = 0;
    framecount = 0;
    start = 0;
    drift = 0;
    if (afmt) {
	/* fill sound buffer */
	fd_set wr;
	int rc;

	for (;;) {
	    FD_ZERO(&wr);
	    FD_SET(snd_fd,&wr);
	    wait.tv_sec = 0;
	    wait.tv_usec = 0;
	    rc = select(snd_fd+1,NULL,&wr,NULL,&wait);
	    if (1 != rc)
		break;
	    abuf = reader->rd_adata(rhandle);
	    if (NULL == abuf)
		break;
	    if (0 == start)
		start = ng_get_timestamp();
	    drift = abuf->info.ts - (ng_get_timestamp() - start);
	    abuf = snd->write(shandle,abuf);
	    if (NULL != abuf)
		break;
	}
    }
    if (0 == start)
	start = ng_get_timestamp();

    if (debug)
	fprintf(stderr,"a/v sync: audio buffer filled\n");
    for (; vfmt || afmt;) {
	int rc,max;
	fd_set rd,wr;

	/* handle X11 events */
	if (True == XCheckMaskEvent(dpy, ~0, &event)) {
	    XtDispatchEvent(&event);
	    continue;
	}

	/* read media data */
	if (afmt && NULL == abuf) {
	    abuf = reader->rd_adata(rhandle);
	    if (NULL == abuf)
		afmt = NULL; /* EOF */
	    else
		drift = abuf->info.ts - (ng_get_timestamp() - start);
	}
	if (vfmt && NULL == vbuf) {
	    droptotal += drop;
	    vbuf = reader->rd_vdata(rhandle,drop);
	    if (conv)
		vbuf = ng_convert_frame(chandle,NULL,vbuf);
	    if (NULL != vbuf && 1 != args.slow)
		/* ts fixup for slow motion */
		vbuf->info.ts *= args.slow;
	    if (NULL == vbuf)
		vfmt = NULL; /* EOF */
	}

	/* wait for events */
	XFlush(dpy);
	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_SET(ConnectionNumber(dpy),&rd);
	max = ConnectionNumber(dpy);
	if (afmt) {
	    FD_SET(snd_fd,&wr);
	    if (snd_fd > max)
		max = snd_fd;
	}
	if (vfmt) {
	    /* check how long we have to wait until the next frame
	     * should be blitted to the screen */
	    now = ng_get_timestamp() - start;
	    if (afmt && latency) {
		/* sync video with audio track */
		now += drift;
		now -= latency;
		if (args.verbose)
		    sync_info(drift-latency,droptotal,framecount);
	    }
	    delay = vbuf->info.ts - now;
	    if (delay < 0) {
		drop = -delay / reader->frame_time(rhandle);
		if (drop) {
		    if (args.verbose)
			sync_info(drift-latency,droptotal,framecount);
		}
		wait.tv_sec  = 0;
		wait.tv_usec = 0;
	    } else {
		drop = 0;
		wait.tv_sec  = delay / 1000000000;
		wait.tv_usec = (delay / 1000) % 1000000;
	    }
	} else {
	    wait.tv_sec  = 1;
	    wait.tv_usec = 0;
	}
	rc = select(max+1,&rd,&wr,NULL,&wait);

	if (afmt && FD_ISSET(snd_fd,&wr)) {
	    /* write audio data */
	    abuf = snd->write(shandle,abuf);
	}
	if (vfmt && 0 == rc) {
	    /* blit video frame */
	    blit_putframe(blit,vbuf);
	    framecount++;
	    vbuf = NULL;
	}
    }
    return 0;
}
