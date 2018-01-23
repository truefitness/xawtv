/*
 * vbi-x11  --  render videotext into X11 drawables
 *
 *   (c) 2002 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#ifdef HAVE_ZVBI

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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include "atoms.h"
#include "list.h"
#include "vbi-data.h"
#include "vbi-x11.h"

/* --------------------------------------------------------------------- */

struct vbi_font vbi_fonts[] = {
    {
	.label = "Teletext 20",
	.xlfd1 = "-*-teletext-medium-r-normal--20-*-*-*-*-*-iso10646-1",
	.xlfd2 = "-*-teletext-medium-r-normal--40-*-*-*-*-*-iso10646-1",
    },{
	.label = "Teletext 10",
	.xlfd1 = "-*-teletext-medium-r-normal--10-*-*-*-*-*-iso10646-1",
	.xlfd2 = "-*-teletext-medium-r-normal--20-*-*-*-*-*-iso10646-1",
    },{
	.label = "Fixed 18",
	.xlfd1 = "-*-fixed-medium-r-normal--18-*-*-*-*-*-iso10646-1",
    },{
	.label = "Fixed 13",
	.xlfd1 = "-misc-fixed-medium-r-semicondensed--13-*-*-*-*-*-iso10646-1",
    },{
	/* end of list */
    }
};

/* --------------------------------------------------------------------- */
/* render teletext pages                                                 */

static int vbi_render_try_font(Widget shell, struct vbi_window *vw,
				struct vbi_font *fnt)
{
    if (NULL == vw->xft_font  &&  NULL != fnt->xlfd1) {
	vw->font1 = XLoadQueryFont(XtDisplay(shell), fnt->xlfd1);
	if (NULL != fnt->xlfd2)
	    vw->font2 = XLoadQueryFont(XtDisplay(shell), fnt->xlfd2);
	if (NULL != vw->font1) {
	    vw->a      = vw->font1->max_bounds.ascent;
	    vw->d      = vw->font1->max_bounds.descent;
	    vw->w      = vw->font1->max_bounds.width;
	    vw->h      = vw->a + vw->d;
	    return 0;
	}
    }
    return 1;
}

void vbi_render_free_font(Widget shell, struct vbi_window *vw)
{
#ifdef HAVE_XFT
    if (NULL != vw->xft_font) {
	XftFontClose(XtDisplay(shell),vw->xft_font);
	vw->xft_font = NULL;
    }
#endif
    if (NULL != vw->font1) {
	XFreeFont(XtDisplay(shell),vw->font1);
	vw->font1 = NULL;
    }
    if (NULL != vw->font2) {
	XFreeFont(XtDisplay(shell),vw->font2);
	vw->font2 = NULL;
    }
}

void vbi_render_set_font(Widget shell, struct vbi_window *vw, char *label)
{
#ifdef HAVE_XFT
    FcPattern  *pattern;
    FcResult   rc;
#endif
    int        i;

    /* free old stuff */
    vbi_render_free_font(shell,vw);

    if (NULL != label) {
	/* try core font */
	for (i = 0; vbi_fonts[i].label != NULL; i++) {
	    if (0 != strcasecmp(label,vbi_fonts[i].label))
		continue;
	    if (0 == vbi_render_try_font(shell, vw, &vbi_fonts[i]))
		return;
	}

#ifdef HAVE_XFT
	/* try xft */
	pattern = FcNameParse(label);
	pattern = XftFontMatch(XtDisplay(shell),
			       XScreenNumberOfScreen(XtScreen(shell)),
			       pattern,&rc);
	vw->xft_font = XftFontOpenPattern(XtDisplay(shell), pattern);
	if (vw->xft_font) {
	    vw->a = vw->xft_font->ascent;
	    vw->d = vw->xft_font->descent;
	    vw->w = vw->xft_font->max_advance_width;
	    vw->h = vw->xft_font->height;
	    return;
	}
#endif
    }

    /* walk through the whole list as fallback */
    for (i = 0; vbi_fonts[i].label != NULL; i++) {
	if (0 == vbi_render_try_font(shell, vw, &vbi_fonts[i]))
	    return;
    }
    fprintf(stderr,"Oops: can't load any font\n");
    exit(1);
}

struct vbi_window*
vbi_render_init(Widget shell, Widget tt, struct vbi_state *vbi)
{
    struct vbi_window *vw;
    XColor color,dummy;
    int i;

    vw = malloc(sizeof(*vw));
    memset(vw,0,sizeof(*vw));

    vw->shell = shell;
    vw->tt    = tt;
    vw->vbi   = vbi;
    vw->gc    = XCreateGC(XtDisplay(shell),
			  RootWindowOfScreen(XtScreen(shell)),
			  0, NULL);

    XtVaGetValues(tt, XtNcolormap, &vw->cmap, NULL);
    for (i = 0; i < 8; i++) {
#ifdef HAVE_XFT
	XftColorAllocName(XtDisplay(shell),
			  DefaultVisualOfScreen(XtScreen(shell)),
			  vw->cmap, vbi_colors[i], &vw->xft_color[i]);
#endif
	XAllocNamedColor(XtDisplay(shell), vw->cmap, vbi_colors[i],
			 &color, &dummy);
	vw->colors[i] = color.pixel;
    }
    vbi_render_set_font(shell,vw,NULL);

    INIT_LIST_HEAD(&vw->selections);
    return vw;
}

void
vbi_render_line(struct vbi_window *vw, Drawable d, struct vbi_char *ch,
		int y, int top, int left, int right)
{
    XGCValues values;
    XChar2b line[42];
    int x1,x2,i,code,sy;
#ifdef HAVE_XFT
    FcChar32 wline[42];
    XftDraw *xft_draw = NULL;
#endif

    for (x1 = left; x1 < right; x1 = x2) {
	for (x2 = x1; x2 < right; x2++) {
	    if (ch[x1].foreground != ch[x2].foreground)
		break;
	    if (ch[x1].background != ch[x2].background)
		break;
	    if (ch[x1].size != ch[x2].size)
		break;
	}
	sy = 1;
	if (vw->font2) {
	    if (ch[x1].size == VBI_DOUBLE_HEIGHT ||
		ch[x1].size == VBI_DOUBLE_SIZE)
		sy = 2;
	    if (ch[x1].size == VBI_DOUBLE_HEIGHT2 ||
		ch[x1].size == VBI_DOUBLE_SIZE2)
		continue;
	}

	for (i = x1; i < x2; i++) {
	    code = ch[i].unicode;
	    if (ch[i].conceal)
		code = ' ';
	    if (ch[i].size == VBI_OVER_TOP       ||
		ch[i].size == VBI_OVER_BOTTOM    ||
		ch[i].size == VBI_DOUBLE_HEIGHT2 ||
		ch[i].size == VBI_DOUBLE_SIZE2)
		code = ' ';
	    line[i-x1].byte1 = (code >> 8) & 0xff;
	    line[i-x1].byte2 =  code       & 0xff;
#ifdef HAVE_XFT
	    wline[i-x1] = code;
#endif
	}

	values.function   = GXcopy;
	values.foreground = vw->colors[ch[x1].background & 7];
	XChangeGC(XtDisplay(vw->tt), vw->gc, GCForeground|GCFunction, &values);
	XFillRectangle(XtDisplay(vw->tt), d,
		       vw->gc, (x1-left)*vw->w, (y-top)*vw->h,
		       vw->w * (x2-x1), vw->h * sy);

	if (vw->xft_font) {
#ifdef HAVE_XFT
	    if (NULL == xft_draw)
		xft_draw = XftDrawCreate(XtDisplay(vw->tt), d,
					 DefaultVisualOfScreen(XtScreen(vw->tt)),
					 vw->cmap);
	    XftDrawString32(xft_draw, &vw->xft_color[ch[x1].foreground & 7],
			    vw->xft_font,
			    (x1-left)*vw->w, vw->a + (y-top+sy-1)*vw->h,
			    wline, x2-x1);
#endif
	} else {
	    values.foreground = vw->colors[ch[x1].foreground & 7];
	    values.font = (1 == sy) ? vw->font1->fid : vw->font2->fid;
	    XChangeGC(XtDisplay(vw->tt), vw->gc, GCForeground|GCFont, &values);
	    XDrawString16(XtDisplay(vw->tt), d, vw->gc,
			  (x1-left)*vw->w, vw->a + (y-top+sy-1)*vw->h,
			  line, x2-x1);
	}
    }
#ifdef HAVE_XFT
    if (NULL != xft_draw)
	XftDrawDestroy(xft_draw);
#endif
}

Pixmap
vbi_export_pixmap(struct vbi_window *vw,
		  struct vbi_page *pg, struct vbi_rect *rect)
{
    Pixmap pix;
    vbi_char *ch;
    int y;

    pix = XCreatePixmap(XtDisplay(vw->tt), XtWindow(vw->tt),
			vw->w * (rect->x2 - rect->x1),
			vw->h * (rect->y2 - rect->y1),
			DefaultDepthOfScreen(XtScreen(vw->tt)));
    for (y = rect->y1; y < rect->y2; y++) {
	ch = vw->pg.text + 41*y;
	vbi_render_line(vw,pix,ch,y,rect->y1,rect->x1,rect->x2);
    }
    return pix;
}

#endif /* HAVE_ZVBI */
