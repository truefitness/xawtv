extern unsigned int  swidth,sheight;
extern char *event_names[];
extern const int nevent_names;

void x11_label_pixmap(Display *dpy, Colormap colormap, Pixmap pixmap,
		      int height, char *label);
Pixmap x11_capture_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
			  unsigned int width, unsigned int height);

struct video_handle;
extern struct video_handle vh;
int video_gd_blitframe(struct video_handle *h, struct ng_video_buf *buf);
void video_gd_init(Widget widget, int use_gl);
void video_gd_start(void);
void video_gd_stop(void);
void video_gd_suspend(void);
void video_gd_restart(void);
void video_gd_configure(int width, int height);

void video_new_size(void);
void video_overlay(int state);

Widget video_init(Widget parent, XVisualInfo *vinfo,
		  WidgetClass class, int args_bpp, int args_gl);
void video_close(void);
