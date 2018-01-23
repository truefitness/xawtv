#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/xpm.h>
#include <Xm/Xm.h>

#include "icons.h"
#include "xpm/home.xpm"
#include "xpm/prev.xpm"
#include "xpm/next.xpm"
#include "xpm/movie.xpm"
#include "xpm/snap.xpm"
#include "xpm/mute.xpm"
#include "xpm/exit.xpm"
#include "xpm/tv.xpm"

static void
add_pixmap(Display *dpy, unsigned long bg,
	   char *imgname, char *maskname, char **data)
{
    XImage *image,*shape;
    XpmAttributes attr;
    unsigned int x,y;

    memset(&attr,0,sizeof(attr));
    XpmCreateImageFromData(dpy,data,&image,&shape,&attr);

    if (maskname) {
	XmInstallImage(image,imgname);
	if (shape)
	    XmInstallImage(shape,maskname);
	return;
    }

    if (shape) {
	for (y = 0; y < attr.height; y++)
	    for (x = 0; x < attr.width; x++)
		if (!XGetPixel(shape, x, y))
		    XPutPixel(image, x, y, bg);
    }
    XmInstallImage(image,imgname);
}

void
x11_icons_init(Display *dpy, unsigned long bg)
{
    add_pixmap(dpy, bg,  "home",   NULL,      home_xpm);
    add_pixmap(dpy, bg,  "prev",   NULL,      prev_xpm);
    add_pixmap(dpy, bg,  "next",   NULL,      next_xpm);
    add_pixmap(dpy, bg,  "movie",  NULL,      movie_xpm);
    add_pixmap(dpy, bg,  "snap",   NULL,      snap_xpm);
    add_pixmap(dpy, bg,  "mute",   NULL,      mute_xpm);
    add_pixmap(dpy, bg,  "exit",   NULL,      exit_xpm);
    add_pixmap(dpy, bg,  "TVimg",  "TVmask",  tv_xpm);
}
