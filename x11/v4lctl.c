/*
 *  (c) 1999 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>

#include "config.h"

#ifdef HAVE_LIBXV
# include <X11/Xlib.h>
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
# include "atoms.h"
# include "xv.h"
#endif

#include "grab-ng.h"
#include "channel.h"
#include "frequencies.h"
#include "commands.h"

int debug = 0;
int have_dga = 0;
#ifdef HAVE_LIBXV
Display *dpy;
#endif

/*--- main ---------------------------------------------------------------*/

static void
grabber_init(void)
{
    drv = ng_vid_open(&ng_dev.video,ng_dev.driver,NULL,0,&h_drv);
    if (NULL == drv) {
	fprintf(stderr,"no grabber device available\n");
	exit(1);
    }
    f_drv = drv->capabilities(h_drv);
    add_attrs(drv->list_attrs(h_drv));
}

static void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: v4lctl [ options ] command\n"
	    "options:\n"
	    "  -v, --debug=n      debug level n, n = [0..2]\n"
	    "  -c, --device=file  use <file> as video4linux device\n"
	    "  -h, --help         print this text\n"
	    "\n");
}

int main(int argc, char *argv[])
{
    int c;
    int xvideo = 1;

    ng_init();
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hv:c:D:")))
	    break;
	switch (c) {
	case 'v':
	    ng_debug = debug = atoi(optarg);
	    break;
	case 'c':
	    ng_dev.video = optarg;
	    xvideo = 0;
	    break;
	case 'D':
	    ng_dev.driver = optarg;
	    xvideo = 0;
	    break;
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }
    if (optind == argc) {
	usage();
	exit(1);
    }

#ifdef HAVE_LIBXV
    if (NULL != getenv("DISPLAY"))
	dpy = XOpenDisplay(NULL);
    if (dpy) {
	init_atoms(dpy);
	if (xvideo)
	    xv_video_init(-1,0);
    }
#endif
    if (NULL == drv)
	grabber_init();
    freq_init();
    read_config(NULL,NULL,NULL);

    attr_init();
    audio_init();
    audio_init();

    parse_config(1);

    do_command(argc-optind,argv+optind);
    drv->close(h_drv);
#ifdef HAVE_LIBXV
    if (dpy)
	XCloseDisplay(dpy);
#endif
    return 0;
}
