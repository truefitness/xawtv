/*
 * vbi-gui  --  motif videotext browser
 *
 *   (c) 2002 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <iconv.h>
#include <langinfo.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/keysym.h>
#include <X11/Xmu/Editres.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/PushB.h>
#include <Xm/DrawingA.h>
#include <Xm/Protocols.h>
#include <Xm/Separator.h>
#include <Xm/SelectioB.h>
#include <Xm/TransferP.h>
#include <Xm/DragIcon.h>
#include <Xm/FileSB.h>

#include "atoms.h"
#include "list.h"
#include "vbi-data.h"
#include "vbi-x11.h"
#include "vbi-gui.h"

#include "grab-ng.h"
#include "channel.h"

static int tt_debug = 1;
static int tt_windows = 0;

struct vbi_selection {
    struct list_head  list;
    Atom              atom;
    struct vbi_page   pg;
    struct vbi_rect   rect;
    Pixmap            pix;
};

static void vbi_new_cb(Widget, XtPointer, XtPointer);
static void vbi_goto_cb(Widget, XtPointer, XtPointer);
static void vbi_subpage_menu(struct vbi_window *vw);
static void selection_pri(struct vbi_window *vw);
static void selection_dnd_start(struct vbi_window *vw, XEvent *event);

/* --------------------------------------------------------------------- */

static void
vbi_fix_head(struct vbi_window *vw, struct vbi_char *ch)
{
    int showno,showsub,red,i;

    showno  = vw->pg.pgno;
    showsub = vw->pg.subno;
    red     = 0;
    if (0 == showno) {
	showno  = vw->pgno;
	showsub = 0;
	red     = 1;
    }
    if (vw->newpage) {
	showno  = vw->newpage;
	showsub = 0;
	red     = 1;
    }

    for (i = 1; i <= 6; i++)
	ch[i].unicode = ' ';
    if (showno >= 0x100)
	ch[1].unicode = '0' + ((showno >> 8) & 0xf);
    if (showno >= 0x10)
	ch[2].unicode = '0' + ((showno >> 4) & 0xf);
    if (showno >= 0x1)
	ch[3].unicode = '0' + ((showno >> 0) & 0xf);
    if (showsub) {
	ch[4].unicode = '/';
	ch[5].unicode = '0' + ((showsub >> 4) & 0xf);
	ch[6].unicode = '0' + ((showsub >> 0) & 0xf);
    }
    if (red) {
	ch[1].foreground = VBI_RED;
	ch[2].foreground = VBI_RED;
	ch[3].foreground = VBI_RED;
    }
}

static void
vbi_check_rectangle(struct vbi_rect *rect)
{
    int h;

    if (rect->x1 > rect->x2)
	h = rect->x1, rect->x1 = rect->x2, rect->x2 = h;
    if (rect->y1 > rect->y2)
	h = rect->y1, rect->y1 = rect->y2, rect->y2 = h;

    if (rect->x1 < 0) rect->x1 = 0;
    if (rect->x2 < 0) rect->x2 = 0;
    if (rect->y1 < 0) rect->y1 = 0;
    if (rect->y2 < 0) rect->y2 = 0;

    if (rect->x1 > 41) rect->x1 = 41;
    if (rect->x2 > 41) rect->x2 = 41;
    if (rect->y1 > 25) rect->y1 = 25;
    if (rect->y2 > 25) rect->y2 = 25;
}

static void
vbi_mark_rectangle(struct vbi_window *vw)
{
    struct vbi_rect rect;
    XGCValues values;
    int x,y,w,h;

    rect = vw->s;
    vbi_check_rectangle(&rect);
    x = vw->w * (rect.x1);
    w = vw->w * (rect.x2 - rect.x1);
    y = vw->h * (rect.y1);
    h = vw->h * (rect.y2 - rect.y1);
    values.function   = GXxor;
    values.foreground = ~0;
    XChangeGC(XtDisplay(vw->tt),vw->gc,GCFunction|GCForeground,&values);
    XFillRectangle(XtDisplay(vw->tt),XtWindow(vw->tt),vw->gc,
		   x,y,w,h);
}

static void
vbi_render_page(struct vbi_window *vw)
{
    struct vbi_char *ch;
    int y;

    vbi_fix_head(vw,vw->pg.text);
    for (y = 0; y < 25; y++) {
	ch = vw->pg.text + 41*y;
	vbi_render_line(vw,XtWindow(vw->tt),ch,y,0,0,41);
    }
    if ((vw->s.x1 || vw->s.x2) &&
	(vw->s.y1 || vw->s.y2))
	vbi_mark_rectangle(vw);
}

static void
vbi_render_head(struct vbi_window *vw, int pgno, int subno)
{
    vbi_page pg;

    memset(&pg,0,sizeof(pg));
    vbi_fetch_vt_page(vw->vbi->dec,&pg,pgno,subno,
		      VBI_WST_LEVEL_1p5,1,0);
    vbi_fix_head(vw,pg.text);
    vbi_render_line(vw,XtWindow(vw->tt),pg.text,0,0,0,41);
}

static void
vbi_newdata(struct vbi_event *ev, void *user)
{
    struct vbi_window *vw = user;

    switch (ev->type) {
    case VBI_EVENT_TTX_PAGE:
	if (vw->pgno  == ev->ev.ttx_page.pgno) {
	    if (vw->subno == ev->ev.ttx_page.subno ||
		vw->subno == VBI_ANY_SUBNO) {
		vbi_fetch_vt_page(vw->vbi->dec,&vw->pg,vw->pgno,vw->subno,
				  VBI_WST_LEVEL_1p5,25,1);
		vbi_render_page(vw);
	    }
	    vbi_subpage_menu(vw);
	} else {
	    vbi_render_head(vw,
			    ev->ev.ttx_page.pgno,
			    ev->ev.ttx_page.subno);
	}
	break;
    case VBI_EVENT_NETWORK:
	XtVaSetValues(vw->shell,XtNtitle,ev->ev.network.name,NULL);
	break;
    }
}

/* --------------------------------------------------------------------- */
/* GUI handling                                                          */

static void
vbi_subpage_menu(struct vbi_window *vw)
{
    WidgetList children,list;
    Cardinal nchildren;
    Widget push;
    vbi_page pg;
    char page[8];
    unsigned int i;

    /* delete children */
    XtVaGetValues(vw->submenu,XtNchildren,&children,
		  XtNnumChildren,&nchildren,NULL);
    if (0 != nchildren) {
	list = malloc(sizeof(Widget*)*nchildren);
	memcpy(list,children,sizeof(Widget*)*nchildren);
	for (i = 0; i < nchildren; i++)
	    XtDestroyWidget(list[i]);
	free(list);
    }

    /* rebuild menu */
    push = XtVaCreateManagedWidget("s00",xmPushButtonWidgetClass,
				   vw->submenu,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,
			    vw->submenu,NULL);
    if (vw->pg.pgno && vw->pg.subno) {
	XtVaSetValues(vw->subbtn,XtNsensitive,True,NULL);
	for (i = 0; i < VBI_MAX_SUBPAGES; i++) {
	    if (!vbi_fetch_vt_page(vw->vbi->dec,&pg,vw->pg.pgno,i,
				   VBI_WST_LEVEL_1,0,0))
		continue;
	    sprintf(page,"s%02x",i);
	    push = XtVaCreateManagedWidget(page,xmPushButtonWidgetClass,
					   vw->submenu,NULL);
	    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);
	}
    } else {
	XtVaSetValues(vw->subbtn,XtNsensitive,False,NULL);
    }
}

static void
vbi_setpage(struct vbi_window *vw, int pgno, int subno)
{
    vw->pgno = pgno;
    vw->subno = subno;
    vw->newpage = 0;
    memset(&vw->pg,0,sizeof(struct vbi_page));
    vbi_fetch_vt_page(vw->vbi->dec,&vw->pg,vw->pgno,vw->subno,
		      VBI_WST_LEVEL_1p5,25,1);
    if (XtWindow(vw->tt))
	vbi_render_page(vw);
    vbi_subpage_menu(vw);
}

static void
vbi_expose_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmDrawingAreaCallbackStruct *cd = call_data;
    struct vbi_window *vw = clientdata;

    if (cd->event->xexpose.count > 0)
	return;
    vbi_render_page(vw);
}

static void
vbi_destroy_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;

    vbi_event_handler_unregister(vw->vbi->dec,vbi_newdata,vw);
    vbi_render_free_font(widget,vw);
    XFreeGC(XtDisplay(widget),vw->gc);
    free(vw);
    tt_windows--;
    if (0 == tt_windows)
	exit(0);
}

static void
vbi_close_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;
    XtDestroyWidget(vw->shell);
}

static void
vbi_new_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;
    Widget shell;

    shell = XtVaAppCreateShell("mtt","mtt",applicationShellWidgetClass,
			       XtDisplay(widget),NULL);
    vbi_create_widgets(shell,vw->vbi);
    XtRealizeWidget(shell);
}

static void
vbi_font_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;
    char *name = XtName(widget);
    vbi_render_set_font(widget, vw, name);
    XtVaSetValues(vw->tt, XmNwidth,vw->w*41, XmNheight,vw->h*25, NULL);
    XClearWindow(XtDisplay(vw->tt),XtWindow(vw->tt));
}

static void
vbi_goto_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;
    int pgno,subno;
    char *name;

    pgno  = vw->pg.pgno;
    subno = vw->pg.subno;
    name = XtName(widget);
    if (0 == strcmp(name,"prev")) {
	pgno  = vbi_calc_page(vw->pgno,-1);
	subno = VBI_ANY_SUBNO;
    } else if (0 == strcmp(name,"next")) {
	pgno  = vbi_calc_page(vw->pgno,+1);
	subno = VBI_ANY_SUBNO;
    } else if (1 == sscanf(name,"s%x",&subno)) {
	/* nothing */
    } else {
	sscanf(name,"%x",&pgno);
	subno = VBI_ANY_SUBNO;
    }
    if (0 == subno)
	subno = VBI_ANY_SUBNO;
    vbi_setpage(vw,pgno,subno);
}

static int
vbi_findpage(struct vbi_page *pg, int px, int py)
{
    int newpage = 0;
    int x;

    if (py == 24) {
	/* navigation line */
	int i = (pg->text[py*41+px].foreground & 7) -1;
	if (i >= 6)
	    i = 0;
	newpage = pg->nav_link[i].pgno;
    } else if (px <= 40 && py <= 23) {
	if (pg->text[py*41+px].unicode >= '0' &&
	    pg->text[py*41+px].unicode <= '9') {
	    /* look for a 3-digit string ... */
	    x = px; newpage = 0;
	    while (pg->text[py*41+x].unicode >= '0' &&
		   pg->text[py*41+x].unicode <= '9' &&
		   x > 0) {
		x--;
	    }
	    x++;
	    while (pg->text[py*41+x].unicode >= '0' &&
		   pg->text[py*41+x].unicode <= '9' &&
		   x < 40) {
		newpage = newpage*16 + pg->text[py*41+x].unicode - '0';
		x++;
	    }

	} else if (pg->text[py*41+px].unicode == '>') {
	    /* next page */
	    newpage = vbi_calc_page(pg->pgno,+1);

	} else if (pg->text[py*41+px].unicode == '<') {
	    /* prev page */
	    newpage = vbi_calc_page(pg->pgno,-1);
	}
    }

    if (newpage < 0x100 || newpage >= 0x999)
	return 0;
    return newpage;
}

static void
vbi_kbd_eh(Widget widget, XtPointer clientdata, XEvent *event, Boolean *cont)
{
    struct vbi_window *vw = clientdata;
    KeySym sym;
    int digit,subno;

    switch (event->type) {
    case KeyPress:
	sym = XKeycodeToKeysym(XtDisplay(widget),event->xkey.keycode,0);
	digit = -1;
	switch (sym) {
	case XK_0:
	case XK_KP_Insert:
	    digit = 0;
	    break;
	case XK_1:
	case XK_KP_End:
	    digit = 1;
	    break;
	case XK_2:
	case XK_KP_Down:
	    digit = 2;
	    break;
	case XK_3:
	case XK_KP_Next:
	    digit = 3;
	    break;
	case XK_4:
	case XK_KP_Left:
	    digit = 4;
	    break;
	case XK_5:
	case XK_KP_Begin:
	    digit = 5;
	    break;
	case XK_6:
	case XK_KP_Right:
	    digit = 6;
	    break;
	case XK_7:
	case XK_KP_Home:
	    digit = 7;
	    break;
	case XK_8:
	case XK_KP_Up:
	    digit = 8;
	    break;
	case XK_9:
	case XK_KP_Prior:
	    digit = 9;
	    break;
	case XK_space:
	case XK_l:
	    vbi_setpage(vw,vbi_calc_page(vw->pgno,+1),VBI_ANY_SUBNO);
	    break;
	case XK_BackSpace:
	case XK_h:
	    vbi_setpage(vw,vbi_calc_page(vw->pgno,-1),VBI_ANY_SUBNO);
	    break;
	case XK_k:
	    subno = (vw->subno != VBI_ANY_SUBNO) ? vw->subno : vw->pg.subno;
	    subno = vbi_calc_subpage(vw->vbi->dec,vw->pgno,subno,+1);
	    vbi_setpage(vw,vw->pgno,subno);
	    break;
	case XK_j:
	    subno = (vw->subno != VBI_ANY_SUBNO) ? vw->subno : vw->pg.subno;
	    subno = vbi_calc_subpage(vw->vbi->dec,vw->pgno,subno,-1);
	    vbi_setpage(vw,vw->pgno,subno);
	    break;
	}
	if (-1 != digit) {
	    vw->newpage *= 16;
	    vw->newpage += digit;
	    if (vw->newpage >= 0x100)
		vbi_setpage(vw,vw->newpage,VBI_ANY_SUBNO);
	}
	break;
    }
}

static void
vbi_mouse_eh(Widget widget, XtPointer clientdata, XEvent *event, Boolean *cont)
{
    struct vbi_window *vw = clientdata;
    int px,py,newpage;

    switch (event->type) {
    case ButtonPress:
	switch (event->xbutton.button) {
	case 1: /* left mouse button */
	    px = event->xbutton.x / vw->w;
	    py = event->xbutton.y / vw->h;
	    vw->s.x1 = vw->s.x2 = px;
	    vw->s.y1 = vw->s.y2 = py;
	    vw->down = event->xbutton.time;
	    break;
	case 2: /* middle button */
	    selection_dnd_start(vw,event);
	    break;
	}
	break;
    case MotionNotify:
	if (event->xmotion.state & Button1Mask) {
	    vw->s.x2 = event->xbutton.x / vw->w +1;
	    vw->s.y2 = event->xbutton.y / vw->h +1;
	    vbi_render_page(vw);
	}
	break;
    case ButtonRelease:
	switch (event->xbutton.button) {
	case 1: /* left mouse button */
	    px = event->xbutton.x / vw->w;
	    py = event->xbutton.y / vw->h;
	    if (abs(vw->s.x1 - px) < 2  &&  abs(vw->s.y1 - py) < 2  &&
		event->xbutton.time - vw->down < 500) {
		/* mouse click */
		vw->s.x1 = vw->s.x2 = 0;
		vw->s.y1 = vw->s.y2 = 0;
		newpage = vbi_findpage(&vw->pg,px,py);
		if (0 != newpage)
		    vbi_setpage(vw,newpage,VBI_ANY_SUBNO);
		else
		    vbi_render_page(vw);
	    } else {
		/* marked region */
		vw->s.x2 = px +1;
		vw->s.y2 = py +1;
		vbi_render_page(vw);
		selection_pri(vw);
	    }
	    break;
	case 4: /* wheel up */
	    newpage = vbi_calc_page(vw->pgno,-1);
	    vbi_setpage(vw,newpage,VBI_ANY_SUBNO);
	    break;
	case 5: /* wheel down */
	    newpage = vbi_calc_page(vw->pgno,+1);
	    vbi_setpage(vw,newpage,VBI_ANY_SUBNO);
	    break;
	}
	break;
    }
}

/* --------------------------------------------------------------------- */
/* export data                                                           */

static void
export_do_save_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    static struct vbi_rect rect = {
	x1:  0,
	x2: 41,
	y1:  0,
	y2: 25,
    };
    XmFileSelectionBoxCallbackStruct *cb = call_data;
    struct vbi_window *vw = clientdata;
    char *filename, *data;
    int len,fh;

    if (cb->reason == XmCR_OK) {
	filename = XmStringUnparse(cb->value,NULL,
				   XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
				   NULL,0,0);
	data = malloc(25*41*8);
	len = vbi_export_txt(data,vw->charset,25*41*8,&vw->pg,&rect,
			     VBI_NOCOLOR);
	fh = open(filename,O_WRONLY | O_CREAT, 0666);
	if (-1 == fh) {
	    fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	} else {
	    ftruncate(fh,0);
	    write(fh,data,len);
	    close(fh);
	}
	free(data);
    }
    XtUnmanageChild(widget);
}

static void
export_charset_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;
    vw->charset = XtName(widget);
}

static void
export_save_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;
    Widget help,text,menu,option,push;
    Arg args[2];

    if (NULL == vw->savebox) {
	vw->savebox = XmCreateFileSelectionDialog(vw->shell,"save",NULL,0);
	help = XmFileSelectionBoxGetChild(vw->savebox,XmDIALOG_HELP_BUTTON);
	text = XmFileSelectionBoxGetChild(vw->savebox,XmDIALOG_TEXT);
	XtUnmanageChild(help);

	menu = XmCreatePulldownMenu(vw->savebox,"formatM",NULL,0);
	XtSetArg(args[0],XmNsubMenuId,menu);
	option = XmCreateOptionMenu(vw->savebox,"format",args,1);
	XtManageChild(option);

	vw->charset = nl_langinfo(CODESET);
	push = XtVaCreateManagedWidget(vw->charset,xmPushButtonWidgetClass,
				       menu, NULL);
	XtAddCallback(push,XmNactivateCallback,export_charset_cb,vw);
	if (0 != strcasecmp(vw->charset,"UTF-8")) {
	    push = XtVaCreateManagedWidget("UTF-8",
					   xmPushButtonWidgetClass,menu,NULL);
	    XtAddCallback(push,XmNactivateCallback,export_charset_cb,vw);
	}
	if (0 != strcasecmp(vw->charset,"ISO-8859-1")) {
	    push = XtVaCreateManagedWidget("ISO-8859-1",
					   xmPushButtonWidgetClass,menu,NULL);
	    XtAddCallback(push,XmNactivateCallback,export_charset_cb,vw);
	}
	if (0 != strcasecmp(vw->charset,"US-ASCII")) {
	    push = XtVaCreateManagedWidget("US-ASCII",
					   xmPushButtonWidgetClass,menu,NULL);
	    XtAddCallback(push,XmNactivateCallback,export_charset_cb,vw);
	}
	XtAddCallback(vw->savebox,XmNokCallback,export_do_save_cb,vw);
	XtAddCallback(vw->savebox,XmNcancelCallback,export_do_save_cb,vw);
    }
    XtManageChild(vw->savebox);
}

/* --------------------------------------------------------------------- */
/* selection handling (cut+paste, drag'n'drop)                           */

static struct vbi_selection*
selection_find(struct vbi_window *vw, Atom selection)
{
    struct list_head      *item;
    struct vbi_selection  *sel;

    list_for_each(item,&vw->selections) {
	sel = list_entry(item, struct vbi_selection, list);
	if (sel->atom == selection)
	    return sel;
    }
    return NULL;
}

static void
selection_fini(struct vbi_window *vw, Atom selection)
{
    struct vbi_selection  *sel;

    sel = selection_find(vw,selection);
    if (NULL == sel)
	return;
    if (sel->pix)
	XFreePixmap(XtDisplay(vw->tt),sel->pix);

    list_del(&sel->list);
    free(sel);
}

static void
selection_init(struct vbi_window *vw, Atom selection)
{
    struct vbi_selection  *sel;

    selection_fini(vw,selection);
    sel = malloc(sizeof(*sel));
    memset(sel,0,sizeof(*sel));
    list_add_tail(&sel->list,&vw->selections);
    sel->atom = selection;
    sel->pg   = vw->pg;
    sel->rect = vw->s;
    vbi_check_rectangle(&sel->rect);
    if (0 == sel->rect.x2  &&  0 == sel->rect.y2) {
	sel->rect.x2 = 41;
	sel->rect.y2 = 25;
    }
}

static Atom selection_unique_atom(struct vbi_window *vw)
{
    char id_name[32];
    Atom id;
    int i;

    for (i = 0;; i++) {
	sprintf(id_name,"_VBI_DATA_%lX_%d",XtWindow(vw->tt),i);
	id = XInternAtom(XtDisplay(vw->tt),id_name,False);
	if (NULL == selection_find(vw,id))
	    break;
    }
    return id;
}

static void
selection_convert_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmConvertCallbackStruct *ccs = call_data;
    struct vbi_window *vw = clientdata;
    struct vbi_selection *sel;
    Display *dpy = XtDisplay(widget);
    unsigned long *ldata;
    unsigned char *cdata;
    Atom *targs;
    int n;

    if (tt_debug) {
	char *y = !ccs->type      ? NULL : XGetAtomName(dpy,ccs->type);
	char *t = !ccs->target    ? NULL : XGetAtomName(dpy,ccs->target);
	char *s = !ccs->selection ? NULL : XGetAtomName(dpy,ccs->selection);
	fprintf(stderr,"tt: target=%s type=%s selection=%s\n",t,y,s);
	if (y) XFree(y);
	if (t) XFree(t);
	if (s) XFree(s);
    }

    if ((ccs->target == XA_TARGETS) ||
	(ccs->target == _MOTIF_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_DEFERRED_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_EXPORT_TARGETS)) {
	/* query convert capabilities */
	n = 0;
	targs = (Atom*)XtMalloc(sizeof(Atom)*32);
	if (ccs->target != _MOTIF_CLIPBOARD_TARGETS) {
	    targs[n++]  = XA_TARGETS;
	    targs[n++]  = XA_PIXMAP;
	    targs[n++]  = XA_COLORMAP;
	    targs[n++]  = XA_FOREGROUND;
	    targs[n++]  = XA_BACKGROUND;
	    targs[n++]  = MIME_TEXT_UTF_8;
	    targs[n++]  = XA_UTF8_STRING;
	    targs[n++]  = MIME_TEXT_ISO8859_1;
	    targs[n++]  = XA_STRING;
	}
	if (ccs->target == _MOTIF_EXPORT_TARGETS) {
	    /* save away drag'n'drop data */
	    selection_init(vw,ccs->selection);
	}
	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;

    } else if (ccs->target == _MOTIF_SNAPSHOT) {
	/* save away clipboard data */
	n = 0;
	targs = (Atom*)XtMalloc(sizeof(Atom));
	targs[n++] = selection_unique_atom(vw);
	selection_init(vw,targs[0]);
	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    /* find data */
    sel = selection_find(vw,ccs->selection);
    if (NULL == sel) {
	/* shouldn't happen */
	fprintf(stderr,"tt: oops: selection data not found\n");
	ccs->status = XmCONVERT_REFUSE;
	return;
    }

    if (ccs->target == _MOTIF_LOSE_SELECTION ||
	ccs->target == XA_DONE) {
	/* cleanup */
	selection_fini(vw,ccs->selection);
	if (XA_PRIMARY == ccs->selection) {
	    /* unmark selection */
	    vw->s.x1 = vw->s.x2 = 0;
	    vw->s.y1 = vw->s.y2 = 0;
	    vbi_render_page(vw);
	}
	ccs->value  = NULL;
	ccs->length = 0;
	ccs->type   = XA_INTEGER;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    /* convert data */
    if (ccs->target == XA_STRING ||
	ccs->target == MIME_TEXT_ISO8859_1) {
	cdata = XtMalloc(25*41*8);
	n = vbi_export_txt(cdata,"ISO8859-1",25*41*8,&sel->pg,&sel->rect,
			   VBI_NOCOLOR);
	ccs->value  = cdata;
	ccs->length = n;
	ccs->type   = XA_STRING;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_UTF8_STRING ||
	       ccs->target == MIME_TEXT_UTF_8) {
	cdata = XtMalloc(25*41*8);
	n = vbi_export_txt(cdata,"UTF-8",25*41*8,&sel->pg,&sel->rect,
			   VBI_NOCOLOR);
	ccs->value  = cdata;
	ccs->length = n;
	ccs->type   = XA_STRING;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_BACKGROUND ||
	       ccs->target == XA_FOREGROUND ||
	       ccs->target == XA_COLORMAP) {
	n = 0;
	ldata = (Atom*)XtMalloc(sizeof(Atom)*8);
	if (ccs->target == XA_BACKGROUND) {
	    ldata[n++] = WhitePixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_FOREGROUND) {
	    ldata[n++] = BlackPixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_COLORMAP) {
	    ldata[n++] = DefaultColormapOfScreen(XtScreen(widget));
	    ccs->type  = XA_COLORMAP;
	}
	ccs->value  = ldata;
	ccs->length = n;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_PIXMAP) {
	/* xfer pixmap */
	if (!sel->pix)
	    sel->pix = vbi_export_pixmap(vw,&sel->pg,&sel->rect);
	if (tt_debug)
	    fprintf(stderr,"tt: pixmap id is 0x%lx\n",sel->pix);
	ldata = (Pixmap*)XtMalloc(sizeof(Pixmap));
	ldata[0] = sel->pix;
	ccs->value  = ldata;
	ccs->length = 1;
	ccs->type   = XA_DRAWABLE;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else {
	/* shouldn't happen */
	fprintf(stderr,"tt: oops: target not found\n");
	ccs->status = XmCONVERT_REFUSE;
    }
}

static void
selection_pri(struct vbi_window *vw)
{
    if (tt_debug)
	fprintf(stderr,"tt: primary\n");

    selection_init(vw,XA_PRIMARY);
    XmePrimarySource(vw->tt,XtLastTimestampProcessed(XtDisplay(vw->tt)));
}

static void
selection_clip_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;

    if (tt_debug)
	fprintf(stderr,"tt: clipboard [copy]\n");

    XmeClipboardSource(vw->tt,XmCOPY,
		       XtLastTimestampProcessed(XtDisplay(vw->tt)));
}

static void
selection_dnd_done(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct vbi_window *vw = clientdata;

    if (tt_debug)
	fprintf(stderr,"tt: dnd done\n");
    selection_fini(vw,_MOTIF_DROP);
}

static void
selection_dnd_start(struct vbi_window *vw, XEvent *event)
{
    Widget    drag;
    Arg       args[4];
    Cardinal  n=0;

    if (tt_debug)
	fprintf(stderr,"tt: dnd start\n");
    n = 0;
    XtSetArg(args[n], XmNdragOperations, XmDROP_COPY); n++;
    drag = XmeDragSource(vw->tt, NULL, event, args, n);
    XtAddCallback(drag, XmNdragDropFinishCallback, selection_dnd_done, vw);
}

/* --------------------------------------------------------------------- */

static void vbi_station_cb(Widget widget, XtPointer client, XtPointer call)
{
    struct vbi_state *vbi = client;
    char *name = XtName(widget);
    int i;

    for (i = 0; i < count; i++)
	if (0 == strcmp(channels[i]->name,name))
	    break;
    if (i == count)
	return;
#if 0
    fprintf(stderr,"tune: %.3f MHz [channel %s, station %s]\n",
	    channels[i]->freq / 16.0,
	    channels[i]->cname,
	    channels[i]->name);
#endif

#ifdef linux
#include <linux/types.h>
#include "videodev2.h"
    {
	struct v4l2_frequency frequency;

	memset (&frequency, 0, sizeof(frequency));
	frequency.type = V4L2_TUNER_RADIO;
	frequency.frequency = channels[i]->freq;
	if (-1 == ioctl(vbi->fd, VIDIOC_S_FREQUENCY, &frequency))
	    perror("ioctl VIDIOCSFREQ");
    }
#endif

    /* FIXME: should add some BSD code once libzvbi is ported ... */
}

static void vbi_station_menu(Widget menubar, struct vbi_state *vbi)
{
    struct {
	char *name;
	Widget menu;
    } *sub = NULL;
    int subs = 0;

    Widget m,menu,push;
    XmString label;
    int i,j;

    if (0 == count)
	return;

    menu = XmCreatePulldownMenu(menubar,"stationM",NULL,0);
    XtVaCreateManagedWidget("station",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);

    for (i = 0; i < count; i++) {
#if 0
	if (channels[i]->key) {
	    if (2 == sscanf(channels[i]->key,
			    "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key)) {
		sprintf(accel,"%s<Key>%s",ctrl,key);
	    } else {
		sprintf(accel,"<Key>%s",channels[i]->key);
	    }
	} else {
	    accel[0] = 0;
	}
#endif

	m = menu;
	if (0 != strcmp(channels[i]->group,"main")) {
	    for (j = 0; j < subs; j++)
		if (0 == strcmp(channels[i]->group,sub[j].name))
		    break;
	    if (j == subs) {
		subs++;
		sub = realloc(sub, subs * sizeof(*sub));
		sub[j].name = channels[i]->group;
		sub[j].menu = XmCreatePulldownMenu(menu,
						   channels[i]->group,
						   NULL,0);
		XtVaCreateManagedWidget(channels[i]->group,
					xmCascadeButtonWidgetClass, menu,
					XmNsubMenuId, sub[j].menu,
					NULL);
	    }
	    m = sub[j].menu;
	}

	label = XmStringGenerate(channels[i]->name, NULL, XmMULTIBYTE_TEXT, NULL);
	push = XtVaCreateManagedWidget(channels[i]->name,
				       xmPushButtonWidgetClass,m,
				       XmNlabelString,label,
				       NULL);
	XtAddCallback(push,XmNactivateCallback,vbi_station_cb,vbi);
	XmStringFree(label);
    }
}

static int fntcmp(const void *a, const void *b)
{
    char const * const *aa = a;
    char const * const *bb = b;

    return strcmp(*aa,*bb);
}

static void vbi_xft_font_menu(Widget menu, struct vbi_window *vw)
{
#ifdef HAVE_XFT
    FcPattern   *pattern;
    FcObjectSet *oset;
    FcFontSet   *fset;
    Widget      push;
    XmString    label;
    char        **fonts, *h;
    int         i;

    pattern = FcNameParse(":style=Regular:spacing=100:slant=0:weight=100");
    oset = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_SPACING, FC_SLANT,
			    FC_WEIGHT, NULL);
    fset = FcFontList(NULL, pattern, oset);
    FcPatternDestroy(pattern);
    if (fset) {
	XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
	fonts = malloc(sizeof(char*) * fset->nfont);
	for (i = 0; i < fset->nfont; i++)
	    fonts[i] = FcNameUnparse (fset->fonts[i]);
	qsort(fonts,fset->nfont,sizeof(char*),fntcmp);

	for (i = 0; i < fset->nfont; i++) {
	    push = XtVaCreateManagedWidget(fonts[i],xmPushButtonWidgetClass,menu,NULL);
	    h = strchr(fonts[i],':');
	    if (h)
		*h = 0;
	    label = XmStringGenerate(fonts[i], NULL, XmMULTIBYTE_TEXT, NULL);
	    XtVaSetValues(push, XmNlabelString, label, NULL);
	    XmStringFree(label);

	    XtAddCallback(push, XmNactivateCallback, vbi_font_cb, vw);
	}

	for (i = 0; i < fset->nfont; i++)
	    free(fonts[i]);
	free(fonts);
    }
#endif
}

/* --------------------------------------------------------------------- */

void vbi_create_widgets(Widget shell, struct vbi_state *vbi)
{
    Widget form,menubar,tool,menu,push,tt;
    struct vbi_window *vw;
    int i;

    /* form container */
    XtVaSetValues(shell, XmNallowShellResize, True, NULL);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, shell,
				   NULL);

    /* menu- & toolbar */
    menubar = XmCreateMenuBar(form,"bar",NULL,0);
    XtManageChild(menubar);
    tool = XtVaCreateManagedWidget("tool",xmRowColumnWidgetClass,form,NULL);

    /* main view */
    tt = XtVaCreateManagedWidget("tt", xmDrawingAreaWidgetClass,form, NULL);
    vw = vbi_render_init(shell,tt,vbi);
    XtVaSetValues(tt,XmNwidth,vw->w*41,XmNheight,vw->h*25,NULL);
    XtAddEventHandler(tt,KeyPressMask,
		      False,vbi_kbd_eh,vw);
    XtAddEventHandler(tt,ButtonPressMask|ButtonReleaseMask|Button1MotionMask,
		      False,vbi_mouse_eh,vw);
    XtAddCallback(tt,XmNexposeCallback,vbi_expose_cb,vw);
    XtAddCallback(tt,XmNdestroyCallback,vbi_destroy_cb,vw);
    XtAddCallback(tt,XmNconvertCallback,selection_convert_cb,vw);
    vbi_event_handler_register(vw->vbi->dec,~0,vbi_newdata,vw);

    /* menu -- file */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("new",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_new_cb,vw);
    push = XtVaCreateManagedWidget("save",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,export_save_cb,vw);
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("quit",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_close_cb,vw);

    /* menu -- edit */
    menu = XmCreatePulldownMenu(menubar,"editM",NULL,0);
    XtVaCreateManagedWidget("edit",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("copy",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,selection_clip_cb,vw);

    /* menu -- go (navigation) */
    menu = XmCreatePulldownMenu(menubar,"goM",NULL,0);
    XtVaCreateManagedWidget("go",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("100",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);
    push = XtVaCreateManagedWidget("prev",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);
    push = XtVaCreateManagedWidget("next",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);

    /* menu -- subpage */
    vw->submenu = XmCreatePulldownMenu(menubar,"subpageM",NULL,0);
    vw->subbtn  = XtVaCreateManagedWidget("subpage",xmCascadeButtonWidgetClass,
					  menubar,
					  XmNsubMenuId,vw->submenu,NULL);

    /* menu -- stations */
    vbi_station_menu(menubar,vbi);

    /* menu -- fonts */
    menu = XmCreatePulldownMenu(menubar,"fontM",NULL,0);
    XtVaCreateManagedWidget("font",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    for (i = 0; vbi_fonts[i].label != NULL; i++) {
	push = XtVaCreateManagedWidget(vbi_fonts[i].label,
				       xmPushButtonWidgetClass,menu,NULL);
	XtAddCallback(push,XmNactivateCallback,vbi_font_cb,vw);
    }
    vbi_xft_font_menu(menu,vw);

    /* toolbar */
    push = XtVaCreateManagedWidget("100",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);
    XtAddEventHandler(push,KeyPressMask,False,vbi_kbd_eh,vw);
    push = XtVaCreateManagedWidget("prev",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);
    XtAddEventHandler(push,KeyPressMask,False,vbi_kbd_eh,vw);
    push = XtVaCreateManagedWidget("next",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_goto_cb,vw);
    XtAddEventHandler(push,KeyPressMask,False,vbi_kbd_eh,vw);
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,tool,NULL);
    push = XtVaCreateManagedWidget("exit",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,vbi_close_cb,vw);
    XtAddEventHandler(push,KeyPressMask,False,vbi_kbd_eh,vw);

    /* shell stuff */
    XtAddEventHandler(shell, (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XmAddWMProtocolCallback(shell,WM_DELETE_WINDOW,vbi_close_cb,vw);

    /* set start page */
    vbi_setpage(vw,0x100,VBI_ANY_SUBNO);
    tt_windows++;
}
