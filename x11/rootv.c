#include "config.h"
#ifndef HAVE_LIBXV
#include "stdio.h"
#include "stdlib.h"
int main(void)
{puts("Compiled without Xvideo extention support, sorry.");exit(0);}
#else
/*
 * put a TV image to the root window - requires Xvideo
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "vroot.h"
#include "atoms.h"
#include "parseconfig.h"

#define SDIMOF(array)  ((signed int)(sizeof(array)/sizeof(array[0])))

int     port=-1,bye=0,termsig=0,verbose=0;
GC      gc;

XvAdaptorInfo        *ai;
XvEncodingInfo       *ei;
XvAttribute          *at;
XvImageFormatValues  *fo;

static char *reasons[] = {
    "XvStarted",
    "XvStopped",
    "XvBusy",
    "XvPreempted",
    "XvHardError",
};

static char *events[] = {
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

static void station_list(FILE *fp)
{
    char filename[100];
    char **list;

    sprintf(filename,"%.*s/%s",(int)sizeof(filename)-8,
	    getenv("HOME"),".xawtv");
    cfg_parse_file(CONFIGFILE);
    cfg_parse_file(filename);

    for (list = cfg_list_sections(); *list != NULL; list++) {
	if (0 == strcmp(*list,"defaults")) continue;
	if (0 == strcmp(*list,"global"))   continue;
	if (0 == strcmp(*list,"launch"))   continue;
	if (0 == strcmp(*list,"eventmap")) continue;
	fprintf(fp,"\t\"%s\" EXEC v4lctl setstation \"%s\"\n",
		*list,*list);
    }
}

static void wm_menu(FILE *fp)
{
    fprintf(fp,"\"TV stations\" MENU\n");
    station_list(fp);
    fprintf(fp,"\"TV stations\" END\n");
}

static void video_blit(Display *dpy, Window win)
{
    XWindowAttributes wts;

    XGetWindowAttributes(dpy, win, &wts);
#if 0
    XClearArea(dpy,win,0,0,0,0,False);
    XvStopVideo(dpy,port,win);
#endif
    XvPutVideo(dpy,port,win,gc,
	       0,0,wts.width,wts.height,
	       0,0,wts.width,wts.height);
    if (verbose)
	fprintf(stderr,"XvPutVideo(win=0x%lx,w=%d,h=%d)\n",
		win,wts.width,wts.height);
}

static void
catch_sig(int signal)
{
    termsig = signal;
    if (verbose)
	fprintf(stderr,"received signal %d [%s]\n",
		termsig,sys_siglist[termsig]);
}

static void usage(FILE *fp)
{
    fprintf(fp,
	    "rootv is a simple TV application.  Uses and requires Xvideo.\n"
	    "Most settings must be tweaked done using some other tool,\n"
	    "v4lctl for example.\n"
	    "\n"
	    "options:\n"
	    "  -help          print this text\n"
	    "  -verbose       be verbose\n"
	    "  -root          put video onto the root window instead of\n"
	    "                 creating a new window.\n"
	    "  -id <win>      put video into the window <win> instead of\n"
	    "                 creating a new window.\n"
	    "  -station <st>  tune station <st> (just calls v4lctl)\n"
	    "  -no-mute       don't toggle mute on start/exit.\n"
	    "  -port <n>      use Xvideo port <n>.\n"
	    "  -bg            fork into background.\n"
	    "  -wm            print WindowMaker menu, to set all stations\n"
	    "                 listed in ~/.xawtv using v4lctl.\n"
	    "\n");
}

int
main(int argc, char *argv[])
{
    struct sigaction act,old;
    Display *dpy;
    Screen  *scr;
    Window win = -1;
    XWindowAttributes wts;
    KeySym keysym;
    char c;

    int ver, rel, req, ev, err, dummy;
    int adaptors,attributes;
    int i,bg,newwin,do_mute,have_mute,grab;

    dpy = XOpenDisplay(NULL);
    scr = DefaultScreenOfDisplay(dpy);
    init_atoms(dpy);

    bg = 0;
    do_mute = 1;
    have_mute = 0;
    newwin = 1;

    while (argc > 1) {
	if (0 == strcmp(argv[1],"-wm")) {
	    /* windowmaker menu */
	    wm_menu(stdout);
	    exit(0);
	}
	if (0 == strcmp(argv[1],"-h") ||
	    0 == strcmp(argv[1],"-help") ||
	    0 == strcmp(argv[1],"--help")) {
	    usage(stdout);
	    exit(0);
	}
	if (argc > 2  &&  (0 == strcmp(argv[1],"-id") ||
			   0 == strcmp(argv[1],"-window-id"))) {
	    sscanf(argv[2],"%li",&win);
	    newwin = 0;
	    if (verbose)
		fprintf(stderr,"using window id 0x%lx\n",win);
	    argc-=2;
	    argv+=2;
	} else if (argc > 2 && 0 == strcmp(argv[1],"-port")) {
	    sscanf(argv[2],"%i",&port);
	    argc-=2;
	    argv+=2;
	} else if (argc > 2 && 0 == strcmp(argv[1],"-station")) {
	    if (0 == fork()) {
		execlp("v4lctl","v4lctl","setstation",argv[2],NULL);
		exit(1);
	    } else {
		wait(&dummy);
	    }
	    argc-=2;
	    argv+=2;
	} else if (0 == strcmp(argv[1],"-root")) {
	    win = RootWindowOfScreen(scr);
	    newwin = 0;
	    if (verbose)
		fprintf(stderr,"using root window [0x%lx]\n",win);
	    argc--;
	    argv++;
	} else if (0 == strcmp(argv[1],"-bg")) {
	    bg = 1;
	    argc--;
	    argv++;
	} else if (0 == strcmp(argv[1],"-verbose")) {
	    verbose = 1;
	    argc--;
	    argv++;
	} else if (0 == strcmp(argv[1],"-no-mute")) {
	    do_mute = 0;
	    argc--;
	    argv++;
	} else {
	    fprintf(stderr,"unknown arg: \"%s\"\n",argv[1]);
	    exit(1);
	}
    }

    /* init X11 */
    if (newwin) {
	win = XCreateSimpleWindow(dpy,RootWindowOfScreen(scr),
				  0,0,640,480,1,
				  BlackPixelOfScreen(scr),
				  BlackPixelOfScreen(scr));
	XChangeProperty(dpy, win, WM_PROTOCOLS, XA_ATOM, 32,
			PropModeReplace,
			(unsigned char *) &WM_DELETE_WINDOW, 1);
    }
    XGetWindowAttributes (dpy, win, &wts);
    XSelectInput(dpy, win, wts.your_event_mask |
		 KeyPressMask | StructureNotifyMask | ExposureMask);

    /* query+print Xvideo properties */
    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	puts("Server does'nt support Xvideo");
	exit(1);
    }
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	puts("Oops: XvQueryAdaptors failed");
	exit(1);
    }
    if (verbose)
	fprintf(stderr,"%d adaptors available.\n",adaptors);
    for (i = 0; i < adaptors; i++) {
	if (verbose)
	    fprintf(stderr,"  port=%ld name=\"%s\"\n",ai[i].base_id,ai[i].name);

	/* video adaptor ? */
	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvVideoMask) &&
	    (port == -1)) {
	    port = ai[i].base_id;
	}
    }
    if (adaptors > 0)
	XvFreeAdaptorInfo(ai);
    if (-1 == port) {
	fprintf(stderr,"no Xvideo port found\n");
	exit(1);
    }

    /* grab Xvideo port */
    grab = 0;
    for (i = 0; !grab && i < 3; i++) {
	switch (XvGrabPort(dpy,port,CurrentTime)) {
	case Success:
	    if (verbose)
		fprintf(stderr,"grabbed Xv port\n");
	    grab=1;
	    break;
	case XvAlreadyGrabbed:
	    if (verbose)
		fprintf(stderr,"Xv port already grabbed\n");
	    sleep(1);
	    break;
	default:
	    fprintf(stderr,"Xv port grab: Huh?\n");
	    exit(1);
	}
    }
    if (!grab) {
	fprintf(stderr,"can't grab Xv port\n");
	exit(1);
    }

    at = XvQueryPortAttributes(dpy,port,&attributes);
    for (i = 0; i < attributes; i++) {
	if (0 == strcmp("XV_MUTE",at[i].name))
	    have_mute = 1;
    }
    gc = XCreateGC(dpy,win,0,NULL);

    /* fork into background, but keep tty */
    if (bg)
	if (fork())
	    exit(0);

    /* catch signals */
    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = catch_sig;
    sigaction(SIGINT,&act,&old);
    sigaction(SIGHUP,&act,&old);
    sigaction(SIGTERM,&act,&old);

    /* put video to the window */
    if (newwin)
	XMapWindow(dpy,win);
    if (verbose)
	fprintf(stderr,"starting video\n");
    video_blit(dpy,win);
    if (do_mute && have_mute)
	XvSetPortAttribute(dpy,port,XV_MUTE,0);

    /* receive events */
    XvSelectPortNotify(dpy, port, 1);
    XvSelectVideoNotify(dpy, win, 1);

    /* main loop */
    for (;!bye && !termsig;) {
	int rc;
	fd_set set;
	XEvent event;

	if (False == XCheckMaskEvent(dpy, ~0, &event)) {
	    /* wait for x11 events, make sure that signals are catched */
	    XFlush(dpy);
	    FD_ZERO(&set);
	    FD_SET(ConnectionNumber(dpy),&set);
	    rc = select(ConnectionNumber(dpy)+1,&set,NULL,NULL,NULL);
	    if (-1 == rc)
		if (verbose)
		    perror("... select");
	    continue;
	}

	if (event.type > ev) {
	    /* Xvideo extention event */
	    switch (event.type-ev) {
	    case XvVideoNotify:
	    {
		XvVideoNotifyEvent *xve = (XvVideoNotifyEvent*)&event;
		if (verbose)
		    fprintf(stderr,"XvVideoNotify, reason=%s, exiting\n",
			    reasons[xve->reason]);
#if 0
		bye=1;
#endif
		break;
	    }
	    case XvPortNotify:
	    {
		XvPortNotifyEvent *xpe = (XvPortNotifyEvent*)&event;
		if (verbose)
		    fprintf(stderr,"XvPortNotify: %s=%ld\n",
			    XGetAtomName(dpy,xpe->attribute),xpe->value);
		break;
	    }
	    }
	} else {
	    /* other event */
	    switch (event.type) {
	    case KeyPress:
		c = 0;
		XLookupString (&event.xkey, &c, 1, &keysym, 0);
		if (verbose)
		    fprintf(stderr,"key: %c\n",c);
		switch (c) {
		case 'q':
		case 'Q':
		case 3:   /* ^C  */
		case 27:  /* ESC */
		    bye = 1;
		}
		break;
	    case ClientMessage:
		if (event.xclient.message_type    == WM_PROTOCOLS &&
		    (Atom)event.xclient.data.l[0] == WM_DELETE_WINDOW)
		    bye = 1;
		break;
	    case Expose:
		if (event.xexpose.count)
		    break;
		/* fall througth */
	    case ConfigureNotify:
		video_blit(dpy,win);
		break;
	    default:
		if (verbose) {
		    if (event.type < SDIMOF(events))
			fprintf(stderr,"ev: %s\n",events[event.type]);
		    else
			fprintf(stderr,"ev: #%d\n",event.type);
		}
	    }
	}
    }
    if (verbose && termsig)
	fprintf(stderr,"exiting on signal %d [%s]\n",
		termsig,sys_siglist[termsig]);
    if (do_mute && have_mute)
	XvSetPortAttribute(dpy,port,XV_MUTE,1);
    XvStopVideo(dpy,port,win);
    XvUngrabPort(dpy,port,CurrentTime);
    XClearArea(dpy,win,0,0,0,0,True);
    XCloseDisplay(dpy);
    if (verbose)
	fprintf(stderr,"bye\n");

    /* keep compiler happy */
    exit(0);
}

#endif
