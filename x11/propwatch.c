/*
 * propwatch.c -- (c) 1997-2003 Gerd Knorr <kraxel@bytesex.org>
 *
 * A tool to monitor window properties of root and application windows.
 * Nice for debugging property-based IPC of X11 programs.
 *
 * see also:
 *   xhost(1), xauth(1), xprop(1), xwd(1)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/cursorfont.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Viewport.h>

#ifndef TRUE
#define TRUE   1
#define FALSE  0
#endif

/*-------------------------------------------------------------------------*/

struct WATCHLIST {
    Window                 win;
    int                    watch;
    struct WATCHLIST       *next;
    char                   *text;
};

/* WM */
static Atom wm_del_win;
static Atom wm_class;

/*-------------------------------------------------------------------------*/

static Widget              bl,vp;
static struct WATCHLIST    *watchlist = NULL;
static char                **watch_name;
static Atom                *watch_atom;
static int                 watch_count;

static char *watch_default[] = {
    "WM_COMMAND",
};

static String   *str_list;
static int      str_count;

static void AddWatch(Display *dpy, Window win, int i);
static void DeleteWatch(Window win);
static void CheckWindow(Display *dpy, Window win);
static void Update(Display *dpy, Window win, Atom prop);

/*-------------------------------------------------------------------------*/

struct ARGS {
    char  *watch;
    int   verbose;
    int   proplog;
    int   kbdlog;
} args;

XtResource args_desc[] = {
    /* name, class, type, size, offset, default_type, default_addr */
    {
	/* ----- Strings ----- */
	"watch",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,watch),
	XtRString, NULL,
    },{
	/* ----- Integer ----- */
	"verbose",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,verbose),
	XtRString, "0"
    },{
	"proplog",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,proplog),
	XtRString, "0"
    },{
	"kbdlog",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,kbdlog),
	XtRString, "0"
    }
};
const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-watch",      "watch",       XrmoptionSepArg, NULL },
    { "-verbose",    "verbose",     XrmoptionNoArg,  "1" },
    { "-proplog",    "proplog",     XrmoptionNoArg,  "1" },
    { "-kbdlog",     "kbdlog",      XrmoptionNoArg,  "1" },
};
const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));

/*-------------------------------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell;
Cursor            left_ptr;
Cursor            menu_ptr;

static void QuitAction(Widget, XEvent*, String*, Cardinal*);
static void HookAction(Widget, XEvent*, String*, Cardinal*);

static void ProcessPropertyChange(Display*,XEvent*);
static void ProcessKeyPress(Display*,XEvent*);
static void ProcessClientMessage(Display*,XEvent*);
static void ProcessCreateWindow(Display*,XEvent*);
static void ProcessEvent(Display *dpy, XEvent *event);

/* Actions */
static XtActionsRec actionTable[] = {
    { "Quit", QuitAction },
    { "Hook", HookAction },
};

/*-------------------------------------------------------------------------*/

static int
x11_error_dev_null(Display * dpy, XErrorEvent * event)
{
    if (args.verbose)
	printf("x11 error -- ignored (likely just a race as X11 is async)\n");
    return 0;
}

static void
spy_input(XtPointer client_data, int *src, XtInputId *id)
{
    Display *spy_dpy = client_data;
    XEvent   event;

    while (True == XCheckMaskEvent(spy_dpy, 0xffffffff, &event))
	ProcessEvent(spy_dpy,&event);
}

static void
add_window(Display *dpy, Window win)
{
    Window rroot,parent,*children = NULL;
    int i, n;

    if (NULL == args.watch && XtWindow(app_shell) == win)
	/* don't f*ck up ourself */
	return;

    XSelectInput(dpy, win,
		 (args.kbdlog ? KeyPressMask | KeyReleaseMask : 0) |
		 PropertyChangeMask |
		 SubstructureNotifyMask);

    if (0 != XQueryTree(dpy, win, &rroot, &parent, &children, &n)) {
	for (i = 0; i < n; i++)
	    add_window(dpy,children[i]);
	if (children)
	    XFree(children);
    }

    /* look for properties to show */
    CheckWindow(dpy, win);
}

int
main(int argc, char *argv[])
{
    Screen *scr;
    XColor white,red,dummy;
    int i;
    Window root;
    Display *dpy, *spy_dpy;
    char title[1024];
    XEvent  event;

    /* init X11 */
    app_shell = XtAppInitialize(&app_context, "Propwatch",
				opt_desc, opt_count,
				&argc, argv,
				NULL,
				NULL, 0);
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);

    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    XtOverrideTranslations
	(app_shell,XtParseTranslationTable("<Message>WM_PROTOCOLS: Quit()\n"));
    dpy = XtDisplay(app_shell);
    if (NULL != args.watch) {
	if (NULL == (spy_dpy = XOpenDisplay(args.watch))) {
	    fprintf(stderr,"can't open display: %s\n",args.watch);
	    exit(1);
	}
	sprintf(title,"watch on %s - ",args.watch);
    } else {
	spy_dpy = dpy;
	sprintf(title,"watch - ");
    }
    root = DefaultRootWindow(spy_dpy);

    XSetErrorHandler(x11_error_dev_null);

    /* args */
    if (argc > 1) {
	watch_count = argc-1;
	watch_name  = argv+1;
    } else {
	watch_count = sizeof(watch_default)/sizeof(char*);
	watch_name  = watch_default;
    }
    watch_atom  = malloc(sizeof(Atom)*watch_count);

    /* Atoms */
    wm_del_win   = XInternAtom(dpy,"WM_DELETE_WINDOW", FALSE);
    wm_class     = XInternAtom(dpy,"WM_CLASS",         FALSE);
    for (i = 0; i < watch_count; i++) {
	watch_atom[i] = XInternAtom(spy_dpy,watch_name[i],FALSE);
	strcat(title,watch_name[i]);
	if (i < watch_count-1)
	    strcat(title,", ");
    }
    XtVaSetValues(app_shell,XtNtitle,title,NULL);

    /* nice Cursors */
    left_ptr = XCreateFontCursor(dpy,XC_left_ptr);
    menu_ptr = XCreateFontCursor(dpy,XC_right_ptr);
    scr = DefaultScreenOfDisplay(dpy);
    if (DefaultDepthOfScreen(scr) > 1) {
	if (XAllocNamedColor(dpy,DefaultColormapOfScreen(scr),
			     "white",&white,&dummy) &&
	    XAllocNamedColor(dpy,DefaultColormapOfScreen(scr),
			     "red",&red,&dummy)) {
	    XRecolorCursor(dpy,left_ptr,&red,&white);
	    XRecolorCursor(dpy,menu_ptr,&red,&white);
	}
    }

    /* widgets*/
    vp = XtVaCreateManagedWidget("vp",viewportWidgetClass,app_shell,
				 XtNallowHoriz, False,
				 XtNallowVert,  True,
				 XtNwidth,      600,
				 XtNheight,     400,
				 NULL);
    bl = XtVaCreateManagedWidget("box",listWidgetClass,vp,
				 XtNdefaultColumns,1,
				 XtNforceColumns,True,
				 NULL);
    XtOverrideTranslations(bl,XtParseTranslationTable
			   ("<Key>Q: Quit()\n"
			    "<Key>P: Hook(xprop)\n"));

    /* display main window */
    XtRealizeWidget(app_shell);
    XDefineCursor(dpy,XtWindow(app_shell),left_ptr);
    XSetWMProtocols(dpy,XtWindow(app_shell),&wm_del_win,1);

    add_window(spy_dpy,root);

    /* enter main loop */
    if (spy_dpy != dpy) {
	XtAppAddInput(app_context,ConnectionNumber(spy_dpy),
		      (XtPointer)XtInputReadMask,
		      spy_input,spy_dpy);
    }
    while (TRUE) {
	XtAppNextEvent(app_context,&event);
	if (XtDispatchEvent(&event))
	    continue;
	ProcessEvent(spy_dpy,&event);
    }

    /* keep compiler happy */
    return 0;
}

/*-------------------------------------------------------------------------*/

static int
cmp(const void *a, const void *b)
{
    char **aa = (char**)a;
    char **bb = (char**)b;
    return strcmp(*aa,*bb);
}

static void
RebuildList(void)
{
    static char *empty = "empty";
    int i;
    struct WATCHLIST *this;

    if (str_list)
	free(str_list);
    str_list = malloc(str_count*sizeof(String));
    for (i=0, this=watchlist; this!=NULL; i++, this=this->next)
	str_list[i] = this->text;
    qsort(str_list,str_count,sizeof(char*),cmp);
    XawListChange(bl,str_count ? str_list : &empty,
		  str_count ? str_count : 1,1000,1);
}

void
AddWatch(Display *dpy, Window win, int i)
{
    struct WATCHLIST   *this;

    this = malloc(sizeof(struct WATCHLIST));
    memset(this,0,sizeof(struct WATCHLIST));
    if (watchlist)
	this->next = watchlist;
    watchlist = this;

    this->win   = win;
    this->watch = i;
    str_count++;
    Update(dpy,win,watch_atom[i]);
    RebuildList();
}

void
DeleteWatch(Window win)
{
    struct WATCHLIST *this,*prev = NULL;

    for (this = watchlist; this != NULL;) {
	if (this->win == win) {
	    if (prev)
		prev->next = this->next;
	    else
		watchlist = this->next;
	    this = this->next;
	    str_count--;
	} else {
	    prev = this;
	    this = this->next;
	}
    }
    RebuildList();
}

void
CheckWindow(Display *dpy, Window win)
{
    Atom               type;
    int                format,i;
    unsigned long      nitems,rest;
    unsigned char      *data;

    for (i = 0; i < watch_count; i++) {
	if (Success != XGetWindowProperty
	    (dpy,win,watch_atom[i],
	     0,64,False,AnyPropertyType,
	     &type,&format,&nitems,&rest,&data))
	    continue;
	if (None != type) {
	    AddWatch(dpy,win,i);
	    XFree(data);
	}
    }
}

/*-------------------------------------------------------------------------*/

static char* str_append(char *dest, char *sep, char *quot, char *str)
{
    int size, pos;

    pos   = dest ? strlen(dest)   : 0;
    size  = str  ? strlen(str)    : 0;
    size += sep  ? strlen(sep)    : 0;
    size += quot ? strlen(quot)*2 : 0;
    dest  = realloc(dest,pos+size+1);
    sprintf(dest+pos,"%s%s%s%s",
	    sep  ? sep  : "",
	    quot ? quot : "",
	    str  ? str  : "",
	    quot ? quot : "");
    return dest;
}

static char*
PropertyToString(Display *dpy, Window win, Atom prop)
{
    Atom               type;
    int                format;
    unsigned int       i;
    unsigned long      nitems,rest;
    unsigned char      *cdata;
    unsigned long      *ldata;
    char               window[12],*name;
    char               *buf = NULL;
    char               *sep = NULL;

    if (Success != XGetWindowProperty
	(dpy,win,prop,0,64,False,AnyPropertyType,
	 &type,&format,&nitems,&rest,&cdata))
	return NULL;
    ldata = (unsigned long*)cdata;
    switch (type) {
    case XA_STRING:
	for (i = 0; i < nitems; i += strlen(cdata+i)+1) {
	    buf = str_append(buf,sep,"\"",cdata+i);
	    sep = ", ";
	}
	break;
    case XA_ATOM:
	for (i = 0; i < nitems; i++) {
	    name = XGetAtomName(dpy,ldata[i]);
	    buf = str_append(buf,sep,NULL,name);
	    sep = ", ";
	    if (name)
		XFree(name);
	}
	break;
    default:
	if (32 == format) {
	    for (i = 0; i < nitems; i++) {
		sprintf(window,"0x%x",(unsigned int)ldata[i]);
		buf = str_append(buf,sep,NULL,window);
		sep = ", ";
	    }
	} else {
	    name = XGetAtomName(dpy,type);
	    buf = malloc(40 + (name ? strlen(name) : 4));
	    sprintf(buf,"can't handle: format=%d type=%s",
		    format, name ? name : "NULL");
	    if (name)
		XFree(name);
	}
	break;
    }
    XFree(cdata);
    return buf;
}

void
Update(Display *dpy, Window win, Atom prop)
{
    struct WATCHLIST   *this;
    char               *str;

    for (this = watchlist; this != NULL; this = this->next)
	if (this->win == win && watch_atom[this->watch] == prop)
	    break;
    if (this) {
	if (this->text)
	    free(this->text);
	str = PropertyToString(dpy,win,prop);
	this->text = malloc((str ? strlen(str) : 4) +
			    strlen(watch_name[this->watch]) + 20);
	sprintf(this->text,"0x%08lx: %s: %s",
		this->win, watch_name[this->watch], str ? str : "NULL");
	if (str)
	    free(str);
    }
}

void
ProcessPropertyChange(Display *dpy, XEvent* event)
{
    int                i;
    struct WATCHLIST   *this;
    char               *name = NULL;
    char               *prop = NULL;

    name = XGetAtomName(dpy,event->xproperty.atom);
    for (i = 0; i < watch_count; i++) {
	if (watch_atom[i] == event->xproperty.atom) {
	    for (this = watchlist; this != NULL; this = this->next)
		if (this->win == event->xproperty.window &&
		    watch_atom[this->watch] == event->xproperty.atom)
		    break;
	    if (!this)
		AddWatch(dpy,event->xproperty.window, i);
	    else {
		Update(dpy,event->xproperty.window,event->xproperty.atom);
		XawListChange(bl,str_list,str_count,1000,1);
	    }
	}
    }

    if (args.proplog) {
	prop = PropertyToString(dpy, event->xproperty.window,
				event->xproperty.atom);
	printf("0x%-8lx: PropertyChange: %s: %s\n",
	       event->xproperty.window,
	       name ? name : "NULL",
	       prop ? prop : "NULL");
	if (prop)
	    free(prop);
    }
    if (name)
	XFree(name);
}

void
ProcessClientMessage(Display *dpy, XEvent* event)
{
    fprintf(stderr,"0x%-8lx: ClientMessage\n",
	    event->xclient.window);
}

void
ProcessKeyPress(Display *dpy, XEvent* event)
{
    fprintf(stderr,"0x%-8lx: %s: code=%d sym=%s\n",
	    event->xkey.window,
	    event->type == KeyPress ? "KeyPress" : "KeyRelease",
	    event->xkey.keycode,
	    XKeysymToString(XKeycodeToKeysym(dpy,event->xkey.keycode,0)));
}

void
ProcessCreateWindow(Display *dpy, XEvent* event)
{
    add_window(dpy,event->xcreatewindow.window);
}

void
ProcessEvent(Display *dpy, XEvent *event)
{
    if (event->type == PropertyNotify) {
	ProcessPropertyChange(dpy,event);

    } else if (event->type == CreateNotify) {
	ProcessCreateWindow(dpy,event);

    } else if (event->type == KeyPress ||
	       event->type == KeyRelease) {
	ProcessKeyPress(dpy,event);

    } else if (event->type == ClientMessage) {
	ProcessClientMessage(dpy,event);

    } else if (event->type == DestroyNotify) {
	DeleteWatch(event->xdestroywindow.window);
    }
}

/*-------------------------------------------------------------------------*/

void
QuitAction(Widget widget, XEvent* event, String* arg, Cardinal* arg_count)
{
    exit(0);
}

void
HookAction(Widget widget, XEvent* event, String* arg, Cardinal* arg_count)
{
    XawListReturnStruct *ret;
    struct WATCHLIST *this;
    char *dname;
    char cmd[256];
    int i;

    /* find window */
    if (0 != strcmp(XtName(widget),"box"))
	return;
    ret = XawListShowCurrent(widget);
    for (i = 0, this = watchlist; this != NULL; i++, this=this->next)
	if (0 == strcmp(ret->string,this->text))
	    break;
    if (NULL == this)
	return;
    if (0 == arg_count)
	return;
    dname = DisplayString(XtDisplay(widget));

    if (0 == strcmp(arg[0],"xprop")) {
	/* dump window properties */
	sprintf(cmd,"xprop -display %s -id 0x%lx",
		args.watch ? args.watch : dname,
		this->win);
	printf("### %s\n",cmd);
	system(cmd);
	printf("### done\n");
    }
}
