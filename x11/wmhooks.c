/*
 * Some WindowManager specific stuff
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "wmhooks.h"
#include "atoms.h"

/* ------------------------------------------------------------------------ */

void (*wm_stay_on_top)(Display *dpy, Window win, int state) = NULL;
void (*wm_fullscreen)(Display *dpy, Window win, int state) = NULL;

/* ------------------------------------------------------------------------ */

extern int debug;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */

static void
netwm_set_state(Display *dpy, Window win, int operation, Atom state)
{
    XEvent e;

    memset(&e,0,sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.message_type = _NET_WM_STATE;
    e.xclient.display = dpy;
    e.xclient.window = win;
    e.xclient.format = 32;
    e.xclient.data.l[0] = operation;
    e.xclient.data.l[1] = state;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
	       SubstructureRedirectMask, &e);
}

static void
netwm_stay_on_top(Display *dpy, Window win, int state)
{
    int op = state ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    netwm_set_state(dpy,win,op,_NET_WM_STATE_ABOVE);
}

static void
netwm_old_stay_on_top(Display *dpy, Window win, int state)
{
    int op = state ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    netwm_set_state(dpy,win,op,_NET_WM_STATE_STAYS_ON_TOP);
}

static void
netwm_fullscreen(Display *dpy, Window win, int state)
{
    int op = state ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    netwm_set_state(dpy,win,op,_NET_WM_STATE_FULLSCREEN);
}

/* ------------------------------------------------------------------------ */

#define WIN_LAYER_NORMAL                 4
#define WIN_LAYER_ONTOP                  6

/* tested with icewm + WindowMaker */
static void
gnome_stay_on_top(Display *dpy, Window win, int state)
{
    XClientMessageEvent  xev;

    if (0 == win)
	return;

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = _WIN_LAYER;
    xev.format = 32;
    xev.data.l[0] = state ? WIN_LAYER_ONTOP : WIN_LAYER_NORMAL;
    XSendEvent(dpy,DefaultRootWindow(dpy),False,
	       SubstructureNotifyMask,(XEvent*)&xev);
    if (state)
	XRaiseWindow(dpy,win);
}

/* ------------------------------------------------------------------------ */

static int
wm_check_capability(Display *dpy, Window root, Atom list, Atom wanted)
{
    Atom            type;
    int             format;
    unsigned int    i;
    unsigned long   nitems, bytesafter;
    unsigned char   *args;
    unsigned long   *ldata;
    char            *name;
    int             retval = -1;

    if (Success != XGetWindowProperty
	(dpy, root, list, 0, (65536 / sizeof(long)), False,
	 AnyPropertyType, &type, &format, &nitems, &bytesafter, &args))
	return -1;
    if (type != XA_ATOM)
	return -1;
    ldata = (unsigned long*)args;
    for (i = 0; i < nitems; i++) {
	if (ldata[i] == wanted)
	    retval = 0;
	if (debug > 1) {
	    name = XGetAtomName(dpy,ldata[i]);
	    fprintf(stderr,"wm cap: %s\n",name);
	    XFree(name);
	}
    }
    XFree(ldata);
    return retval;
}

void
wm_detect(Display *dpy)
{
    Window root = DefaultRootWindow(dpy);

    /* netwm checks */
    if (NULL == wm_stay_on_top &&
	0 == wm_check_capability(dpy,root,_NET_SUPPORTED,
				 _NET_WM_STATE_ABOVE)) {
	if (debug)
	    fprintf(stderr,"wmhooks: netwm state above\n");
	wm_stay_on_top = netwm_stay_on_top;
    }
    if (NULL == wm_stay_on_top &&
	0 == wm_check_capability(dpy,root,_NET_SUPPORTED,
				 _NET_WM_STATE_STAYS_ON_TOP)) {
	if (debug)
	    fprintf(stderr,"wmhooks: netwm state stays_on_top\n");
	wm_stay_on_top = netwm_old_stay_on_top;
    }
    if (NULL == wm_fullscreen &&
	0 == wm_check_capability(dpy,root,_NET_SUPPORTED,
				 _NET_WM_STATE_FULLSCREEN)) {
	if (debug)
	    fprintf(stderr,"wmhooks: netwm state fullscreen\n");
	wm_fullscreen = netwm_fullscreen;
    }

    /* gnome checks */
    if (NULL == wm_stay_on_top &&
	0 == wm_check_capability(dpy,root,_WIN_PROTOCOLS,_WIN_LAYER)) {
	if (debug)
	    fprintf(stderr,"wmhooks: gnome layer\n");
	wm_stay_on_top = gnome_stay_on_top;
    }
}
