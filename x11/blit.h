/* plain X11 */
extern unsigned int x11_dpy_fmtid;

Visual* x11_find_visual(Display *dpy);
void x11_init_visual(Display *dpy, XVisualInfo *vinfo);

XImage *x11_create_ximage(Display *dpy,  XVisualInfo *vinfo,
			  int width, int height, XShmSegmentInfo **shm);
void x11_destroy_ximage(Display *dpy, XImage * ximage, XShmSegmentInfo *shm);
Pixmap x11_create_pixmap(Display *dpy, XVisualInfo *vinfo,
			 struct ng_video_buf *buf);
void x11_blit(Display *dpy, Drawable dr, GC gc, XImage *xi,
	      int a, int b, int c, int d, int w, int h);

/* xvideo extention */
#ifdef HAVE_LIBXV
void xv_image_init(Display *dpy);
XvImage* xv_create_ximage(Display *dpy, int width, int height,
			  int format, XShmSegmentInfo **shm);
void xv_destroy_ximage(Display *dpy, XvImage * xvimage, XShmSegmentInfo *shm);
void xv_blit(Display *dpy, Drawable dr, GC gc, XvImage *xi,
	     int a, int b, int c, int d, int x, int y, int w, int h);
#endif

/* video frame blitter */
struct blit_state;
struct blit_state* blit_init(Widget widget, XVisualInfo *vinfo, int use_gl);
void blit_get_formats(struct blit_state *st, int *fmtids, int max);
void blit_resize(struct blit_state *st, Dimension width, Dimension height);
void blit_init_frame(struct blit_state *st, struct ng_video_fmt *fmt);
void blit_fini_frame(struct blit_state *st);
void blit_fini(struct blit_state *st);
void blit_putframe(struct blit_state *st, struct ng_video_buf *buf);
