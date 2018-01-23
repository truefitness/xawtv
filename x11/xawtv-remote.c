
/* shameless stolen from netscape :-) */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmu/WinUtil.h>    /* for XmuClientWindow() */

unsigned int debug=0;

static int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    fprintf(stderr,"x11-error\n");
    return 0;
}

static Window
find_window(Display * dpy, Atom atom)
{
    int             n;
    unsigned int    i;
    Window          root = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));
    Window          root2, parent, *kids;
    unsigned int    nkids;
    Window          result = 0;

    if (!XQueryTree(dpy, root, &root2, &parent, &kids, &nkids)) {
	fprintf(stderr, "XQueryTree failed on display %s\n",
		DisplayString(dpy));
	exit(2);
    }

    if (!(kids && nkids)) {
	fprintf(stderr, "root window has no children on display %s\n",
		DisplayString(dpy));
	exit(2);
    }
    for (n = nkids - 1; n >= 0; n--) {
	Atom            type;
	int             format;
	unsigned long   nitems, bytesafter;
	unsigned char  *args = NULL;

	Window          w = XmuClientWindow(dpy, kids[n]);

	XGetWindowProperty(dpy, w, atom,
			   0, (65536 / sizeof(long)),
			   False, XA_STRING,
			   &type, &format, &nitems, &bytesafter,
			   &args);

	if (!args)
	    continue;
	if (debug) {
	    printf("query 0x%08lx: ",w);
	    for (i = 0; i < nitems; i += strlen(args + i) + 1)
		printf("%s ", args + i);
	    printf("\n");
	}
	XFree(args);

	result = w;
#if 0 /* there might be more than window */
	break;
#endif
    }
    return result;
}

static void
pass_cmd(Display *dpy, Atom atom, Window win, int argc, char **argv)
{
    int             i, len;
    char           *pass;

    if (debug)
	printf("ctrl  0x%08lx: ",win);
    for (len = 0, i = 0; i < argc; i++) {
	if (debug)
	    printf("%s ",argv[i]);
	len += strlen(argv[i]) + 1;
    }
    if (debug)
	printf("\n");
    pass = malloc(len);
    pass[0] = 0;
    for (len = 0, i = 0; i < argc; i++)
	strcpy(pass + len, argv[i]),
	    len += strlen(argv[i]) + 1;
    XChangeProperty(dpy, win,
		    atom, XA_STRING,
		    8, PropModeReplace,
		    pass, len);
    free(pass);
}

static void
usage(char *argv0)
{
    char *prog;

    if (NULL != (prog = strrchr(argv0,'/')))
	prog++;
    else
	prog = argv0;

    fprintf(stderr,
"This is a \"remote control\" for xawtv\n"
"usage: %s [ options ] [ command ]\n"
"\n"
"available options:\n"
"    -d display\n"
"        set X11 display\n"
"    -i window ID\n"
"        select xawtv window\n"
"    -v n\n"
"        Set debug level to n, default 0."
"\n"
"available commands (most frequently used ones):\n"
"    setstation <name> | <nr> | next | prev\n"
"    setchannel <name> | next | prev\n"
"    capture on | off\n"
"    volume mute | dec | inc | <number>\n"
"    snap [ <format> [ <size> [ <filename> ]]]\n"
"\n"
"Check the man-page for a full list and detailed descriptions.\n"
"\n"
	    ,prog);
}

int
main(int argc, char *argv[])
{
    Display  *dpy;
    char     *dpyname = NULL;
    Window   win,id = 0;
    Atom     station,remote;
    int      c;

    for (;;) {
	c = getopt(argc, argv, "hd:i:v:");
	if (c == -1)
	    break;
	switch (c) {
	case 'd':
	    dpyname = optarg;
	    break;
	case 'i':
	    sscanf(optarg,"%li",&id);
	    break;
	case 'v':
	    debug = atoi(optarg);
	    break;
	case 'h':
	default:
	    usage(argv[0]);
	    exit(1);
	}
    }

    if (NULL == (dpy = XOpenDisplay(dpyname))) {
	fprintf(stderr,"can't open display %s\n", dpyname?dpyname:"");
	exit(1);
    }
    XSetErrorHandler(x11_error_dev_null);
    station = XInternAtom(dpy, "_XAWTV_STATION", False);
    remote =  XInternAtom(dpy, "_XAWTV_REMOTE",  False);

    if (0 == (win = find_window(dpy,station)) &&
	0 == id) {
	fprintf(stderr,"xawtv not running\n");
	exit(2);
    }
    if (argc > optind)
	pass_cmd(dpy, remote, (id != 0) ? id : win, argc-optind, argv+optind);

    XCloseDisplay(dpy);
    return 0;
}
