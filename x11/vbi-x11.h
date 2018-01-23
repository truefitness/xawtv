#ifndef _VBI_X11_H
#define _VBI_X11_H 1

#ifdef HAVE_ZVBI

#ifdef HAVE_XFT
# define _XFT_NO_COMPAT_ 1
# include <X11/Xft/Xft.h>
#endif

#include "list.h"

struct vbi_font {
    char *label;
    char *xlfd1;
    char *xlfd2;
};

struct vbi_window {
    Widget            shell,tt,subbtn,submenu;
    Widget            savebox;
    Colormap          cmap;
    GC                gc;
    XFontStruct       *font1,*font2;
    int               w,a,d,h;
    unsigned long     colors[8];

#ifdef HAVE_XFT
    XftFont           *xft_font;
    XftColor          xft_color[8];
#else
    void              *xft_font;
#endif

    struct vbi_state  *vbi;
    struct vbi_page   pg;
    int               pgno,subno;
    char              *charset;

    int               newpage;
    Time              down;
    struct vbi_rect   s;

    struct list_head  selections;
};

extern struct vbi_font vbi_fonts[];

struct vbi_window* vbi_render_init(Widget shell, Widget tt,
				   struct vbi_state *vbi);
void vbi_render_free_font(Widget shell, struct vbi_window *vw);
void vbi_render_set_font(Widget shell, struct vbi_window *vw, char *label);
void vbi_render_line(struct vbi_window *vw, Drawable d, struct vbi_char *ch,
		     int y, int top, int left, int right);
Pixmap vbi_export_pixmap(struct vbi_window *vw,
			 struct vbi_page *pg, struct vbi_rect *rect);

#endif /* HAVE_ZVBI */
#endif /* _VBI_X11_H */
