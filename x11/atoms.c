
#include <X11/Xlib.h>
#include "atoms.h"

/* window manager */
Atom WM_PROTOCOLS;
Atom WM_DELETE_WINDOW;

Atom _NET_SUPPORTED;
Atom _NET_WM_STATE;
Atom _NET_WM_STATE_STAYS_ON_TOP;
Atom _NET_WM_STATE_ABOVE;
Atom _NET_WM_STATE_FULLSCREEN;
Atom _WIN_SUPPORTING_WM_CHECK;
Atom _WIN_PROTOCOLS;
Atom _WIN_LAYER;

/* ipc: xawtv, xscreensaver */
Atom _XAWTV_STATION;
Atom _XAWTV_REMOTE;

Atom XA_DEACTIVATE;

/* selections / dnd */
Atom _MOTIF_CLIPBOARD_TARGETS;
Atom _MOTIF_DEFERRED_CLIPBOARD_TARGETS;
Atom _MOTIF_SNAPSHOT;
Atom _MOTIF_DROP;
Atom _MOTIF_EXPORT_TARGETS;
Atom _MOTIF_LOSE_SELECTION;

Atom XA_TARGETS;
Atom XA_DONE;
Atom XA_CLIPBOARD;
Atom XA_UTF8_STRING;
Atom XA_FILE_NAME;
Atom XA_FILE;
Atom XA_PIXEL;
Atom XA_BACKGROUND;
Atom XA_FOREGROUND;

Atom MIME_TEXT_ISO8859_1;
Atom MIME_TEXT_UTF_8;

Atom MIME_IMAGE_PPM;
Atom MIME_IMAGE_JPEG;

Atom MIME_TEXT_URI_LIST;
Atom _NETSCAPE_URL;

/* Xvideo */
Atom XV_MUTE;
Atom XV_ENCODING;
Atom XV_FREQ;
Atom XV_COLORKEY;

#define INIT_ATOM(dpy,atom) atom = XInternAtom(dpy,#atom,False)

void init_atoms(Display *dpy)
{
    INIT_ATOM(dpy, WM_PROTOCOLS);
    INIT_ATOM(dpy, WM_DELETE_WINDOW);
    INIT_ATOM(dpy, _NET_SUPPORTED);
    INIT_ATOM(dpy, _NET_WM_STATE);
    INIT_ATOM(dpy, _NET_WM_STATE_STAYS_ON_TOP);
    INIT_ATOM(dpy, _NET_WM_STATE_ABOVE);
    INIT_ATOM(dpy, _NET_WM_STATE_FULLSCREEN);
    INIT_ATOM(dpy, _WIN_SUPPORTING_WM_CHECK);
    INIT_ATOM(dpy, _WIN_PROTOCOLS);
    INIT_ATOM(dpy, _WIN_LAYER);

    INIT_ATOM(dpy, _NETSCAPE_URL);

    INIT_ATOM(dpy, _XAWTV_STATION);
    INIT_ATOM(dpy, _XAWTV_REMOTE);

    INIT_ATOM(dpy, XV_MUTE);
    INIT_ATOM(dpy, XV_ENCODING);
    INIT_ATOM(dpy, XV_FREQ);
    INIT_ATOM(dpy, XV_COLORKEY);

    INIT_ATOM(dpy, _MOTIF_CLIPBOARD_TARGETS);
    INIT_ATOM(dpy, _MOTIF_DEFERRED_CLIPBOARD_TARGETS);
    INIT_ATOM(dpy, _MOTIF_SNAPSHOT);
    INIT_ATOM(dpy, _MOTIF_DROP);
    INIT_ATOM(dpy, _MOTIF_EXPORT_TARGETS);
    INIT_ATOM(dpy, _MOTIF_LOSE_SELECTION);

    XA_DEACTIVATE      = XInternAtom(dpy, "DEACTIVATE",       False);

    XA_TARGETS         = XInternAtom(dpy, "TARGETS",          False);
    XA_DONE            = XInternAtom(dpy, "DONE",             False);
    XA_CLIPBOARD       = XInternAtom(dpy, "CLIPBOARD",        False);
    XA_UTF8_STRING     = XInternAtom(dpy, "UTF8_STRING",      False);
    XA_FILE_NAME       = XInternAtom(dpy, "FILE_NAME",        False);
    XA_FILE            = XInternAtom(dpy, "FILE",             False);
    XA_BACKGROUND      = XInternAtom(dpy, "BACKGROUND",       False);
    XA_FOREGROUND      = XInternAtom(dpy, "FOREGROUND",       False);
    XA_PIXEL           = XInternAtom(dpy, "PIXEL",            False);

    MIME_TEXT_ISO8859_1 =
	XInternAtom(dpy, "text/plain;charset=ISO-8859-1", False);
    MIME_TEXT_UTF_8 =
	XInternAtom(dpy, "text/plain;charset=UTF-8", False);

    MIME_IMAGE_PPM     = XInternAtom(dpy, "image/ppm",        False);
    MIME_IMAGE_JPEG    = XInternAtom(dpy, "image/jpeg",       False);

    MIME_TEXT_URI_LIST = XInternAtom(dpy, "text/uri-list",    False);
}
