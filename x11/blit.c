/*
 * x11 helper functions -- blit frames to the screen
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/XShm.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#if HAVE_GL
# include <GL/gl.h>
# include <GL/glx.h>
#endif

#include "grab-ng.h"
#include "blit.h"

/* ------------------------------------------------------------------------ */

extern XtAppContext    app_context;
extern int             debug;

unsigned int           x11_dpy_fmtid;

static int             display_bits = 0;
static unsigned int    display_bytes = 0;
static unsigned int    pixmap_bytes = 0;
static int             x11_byteswap = 0;
static int             no_mitshm = 0;
static int             gl_error = 0;

#if HAVE_LIBXV
static int             ver, rel, req, ev, err;
static int             formats;
static int             adaptors;
static XvImageFormatValues  *fo;
static XvAdaptorInfo        *ai;
#endif

static unsigned int    im_adaptor,im_port = UNSET;
static unsigned int    im_formats[VIDEO_FMT_COUNT];

static struct SEARCHFORMAT {
    unsigned int   depth;
    int            order;
    unsigned long  red;
    unsigned long  green;
    unsigned long  blue;
    unsigned int   format;
} fmt[] = {
    { 2, MSBFirst, 0x7c00,     0x03e0,     0x001f,     VIDEO_RGB15_BE },
    { 2, MSBFirst, 0xf800,     0x07e0,     0x001f,     VIDEO_RGB16_BE },
    { 2, LSBFirst, 0x7c00,     0x03e0,     0x001f,     VIDEO_RGB15_LE },
    { 2, LSBFirst, 0xf800,     0x07e0,     0x001f,     VIDEO_RGB16_LE },

    { 3, LSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_BGR24    },
    { 3, LSBFirst, 0x000000ff, 0x0000ff00, 0x00ff0000, VIDEO_RGB24    },
    { 3, MSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_RGB24    },
    { 3, MSBFirst, 0x000000ff, 0x0000ff00, 0x00ff0000, VIDEO_BGR24    },

    { 4, LSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_BGR32    },
    { 4, LSBFirst, 0x0000ff00, 0x00ff0000, 0xff000000, VIDEO_RGB32    },
    { 4, MSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_RGB32    },
    { 4, MSBFirst, 0x0000ff00, 0x00ff0000, 0xff000000, VIDEO_BGR32    },

    { 2, -1,       0,          0,          0,          VIDEO_LUT2     },
    { 4, -1,       0,          0,          0,          VIDEO_LUT4     },
    { 0 /* END OF LIST */ },
};

static int
catch_no_mitshm(Display * dpy, XErrorEvent * event)
{
    no_mitshm++;
    return 0;
}

static int
catch_gl_error(Display * dpy, XErrorEvent * event)
{
    fprintf(stderr,"WARNING: Your OpenGL setup is broken.\n");
    gl_error++;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* plain X11 stuff                                                          */

Visual*
x11_find_visual(Display *dpy)
{
    XVisualInfo  *info, template;
    Visual*      vi = CopyFromParent;
    int          found,i;
    char         *class;

    template.screen = XDefaultScreen(dpy);
    info = XGetVisualInfo(dpy, VisualScreenMask,&template,&found);
    for (i = 0; i < found; i++) {
	switch (info[i].class) {
	case StaticGray:   class = "StaticGray";  break;
	case GrayScale:    class = "GrayScale";   break;
	case StaticColor:  class = "StaticColor"; break;
	case PseudoColor:  class = "PseudoColor"; break;
	case TrueColor:    class = "TrueColor";   break;
	case DirectColor:  class = "DirectColor"; break;
	default:           class = "UNKNOWN";     break;
	}
	if (debug)
	    fprintf(stderr,"visual: id=0x%lx class=%d (%s), depth=%d\n",
		    info[i].visualid,info[i].class,class,info[i].depth);
    }
    for (i = 0; vi == CopyFromParent && i < found; i++)
	if (info[i].class == TrueColor && info[i].depth >= 15)
	    vi = info[i].visual;
    for (i = 0; vi == CopyFromParent && i < found; i++)
	if (info[i].class == StaticGray && info[i].depth == 8)
	    vi = info[i].visual;
    return vi;
}

void
x11_init_visual(Display *dpy, XVisualInfo *vinfo)
{
    XPixmapFormatValues *pf;
    int                  i,n;
    int                  format = 0;

    if (!XShmQueryExtension(dpy))
	no_mitshm = 1;

    display_bits = vinfo->depth;
    display_bytes = (display_bits+7)/8;

    pf = XListPixmapFormats(dpy,&n);
    for (i = 0; i < n; i++)
	if (pf[i].depth == display_bits)
	    pixmap_bytes = pf[i].bits_per_pixel/8;

    if (debug) {
	fprintf(stderr,"x11: color depth: "
		"%d bits, %d bytes - pixmap: %d bytes\n",
		display_bits,display_bytes,pixmap_bytes);
	if (vinfo->class == TrueColor || vinfo->class == DirectColor)
	    fprintf(stderr, "x11: color masks: "
		    "red=0x%08lx green=0x%08lx blue=0x%08lx\n",
		    vinfo->red_mask, vinfo->green_mask, vinfo->blue_mask);
	fprintf(stderr,"x11: server byte order: %s\n",
		ImageByteOrder(dpy)==LSBFirst ? "little endian":"big endian");
	fprintf(stderr,"x11: client byte order: %s\n",
		BYTE_ORDER==LITTLE_ENDIAN ? "little endian":"big endian");
    }
    if (ImageByteOrder(dpy)==LSBFirst && BYTE_ORDER!=LITTLE_ENDIAN)
	x11_byteswap=1;
    if (ImageByteOrder(dpy)==MSBFirst && BYTE_ORDER!=BIG_ENDIAN)
	x11_byteswap=1;
    if (vinfo->class == TrueColor /* || vinfo->class == DirectColor */) {
	/* pixmap format */
	for (i = 0; fmt[i].depth > 0; i++) {
	    if (fmt[i].depth  == pixmap_bytes                               &&
		(fmt[i].order == ImageByteOrder(dpy) || fmt[i].order == -1) &&
		(fmt[i].red   == vinfo->red_mask     || fmt[i].red   == 0)  &&
		(fmt[i].green == vinfo->green_mask   || fmt[i].green == 0)  &&
		(fmt[i].blue  == vinfo->blue_mask    || fmt[i].blue  == 0)) {
		x11_dpy_fmtid = fmt[i].format;
		break;
	    }
	}
	if (fmt[i].depth == 0) {
	    fprintf(stderr, "Huh?\n");
	    exit(1);
	}
	ng_lut_init(vinfo->red_mask, vinfo->green_mask, vinfo->blue_mask,
		    x11_dpy_fmtid,x11_byteswap);
	/* guess physical screen format */
	if (ImageByteOrder(dpy) == MSBFirst) {
	    switch (pixmap_bytes) {
	    case 2: format = (display_bits==15) ?
			VIDEO_RGB15_BE : VIDEO_RGB16_BE; break;
	    case 3: format = VIDEO_RGB24; break;
	    case 4: format = VIDEO_RGB32; break;
	    }
	} else {
	    switch (pixmap_bytes) {
	    case 2: format = (display_bits==15) ?
			VIDEO_RGB15_LE : VIDEO_RGB16_LE; break;
	    case 3: format = VIDEO_BGR24; break;
	    case 4: format = VIDEO_BGR32; break;
	    }
	}
    }
    if (vinfo->class == StaticGray && vinfo->depth == 8) {
	format = VIDEO_GRAY;
    }
    if (0 == format) {
	if (vinfo->class == PseudoColor && vinfo->depth == 8) {
	    fprintf(stderr,
"\n"
"8-bit Pseudocolor Visual (256 colors) is *not* supported.\n"
"You can startup X11 either with 15 bpp (or more)...\n"
"	xinit -- -bpp 16\n"
"... or with StaticGray visual:\n"
"	xinit -- -cc StaticGray\n"
	    );
	} else {
	    fprintf(stderr, "Sorry, I can't handle your strange display\n");
	}
	exit(1);
    }
    x11_dpy_fmtid = format;
}

XImage*
x11_create_ximage(Display *dpy, XVisualInfo *vinfo,
		  int width, int height, XShmSegmentInfo **shm)
{
    XImage          *ximage = NULL;
    unsigned char   *ximage_data;
    XShmSegmentInfo *shminfo = NULL;
    void            *old_handler;

    if (no_mitshm)
	goto no_mitshm;

    assert(width > 0 && height > 0);

    old_handler = XSetErrorHandler(catch_no_mitshm);
    shminfo = malloc(sizeof(XShmSegmentInfo));
    memset(shminfo, 0, sizeof(XShmSegmentInfo));
    ximage = XShmCreateImage(dpy,vinfo->visual,vinfo->depth,
			     ZPixmap, NULL,
			     shminfo, width, height);
    if (NULL == ximage)
	goto shm_error;
    shminfo->shmid = shmget(IPC_PRIVATE,
			    ximage->bytes_per_line * ximage->height,
			    IPC_CREAT | 0777);
    if (-1 == shminfo->shmid) {
	perror("shmget [x11]");
	goto shm_error;
    }
    shminfo->shmaddr = (char *) shmat(shminfo->shmid, 0, 0);
    if ((void *)-1 == shminfo->shmaddr) {
	perror("shmat");
	goto shm_error;
    }
    ximage->data = shminfo->shmaddr;
    shminfo->readOnly = False;

    XShmAttach(dpy, shminfo);
    XSync(dpy, False);
    if (no_mitshm)
	goto shm_error;
    shmctl(shminfo->shmid, IPC_RMID, 0);
    XSetErrorHandler(old_handler);
    *shm = shminfo;
    return ximage;

 shm_error:
    if (ximage) {
	XDestroyImage(ximage);
	ximage = NULL;
    }
    if ((void *)-1 != shminfo->shmaddr  &&  NULL != shminfo->shmaddr)
	shmdt(shminfo->shmaddr);
    free(shminfo);
    XSetErrorHandler(old_handler);
    no_mitshm = 1;

 no_mitshm:
    *shm = NULL;
    if (NULL == (ximage_data = malloc(width * height * pixmap_bytes))) {
	fprintf(stderr,"out of memory\n");
	exit(1);
    }
    ximage = XCreateImage(dpy, vinfo->visual, vinfo->depth,
			  ZPixmap, 0, ximage_data,
			  width, height,
			  8, 0);
    memset(ximage->data, 0, ximage->bytes_per_line * ximage->height);
    return ximage;
}

void
x11_destroy_ximage(Display *dpy, XImage *ximage, XShmSegmentInfo *shm)
{
    if (shm && !no_mitshm) {
	XShmDetach(dpy, shm);
	XDestroyImage(ximage);
	shmdt(shm->shmaddr);
	free(shm);
    } else
	XDestroyImage(ximage);
}

void x11_blit(Display *dpy, Drawable dr, GC gc, XImage *xi,
	      int a, int b, int c, int d, int w, int h)
{
    if (no_mitshm)
	XPutImage(dpy,dr,gc,xi,a,b,c,d,w,h);
    else
	XShmPutImage(dpy,dr,gc,xi,a,b,c,d,w,h,True);
}

Pixmap
x11_create_pixmap(Display *dpy, XVisualInfo *vinfo, struct ng_video_buf *buf)
{
    Pixmap          pixmap;
    XImage          *ximage;
    GC              gc;
    XShmSegmentInfo *shm;
    Screen          *scr = DefaultScreenOfDisplay(dpy);

    pixmap = XCreatePixmap(dpy,RootWindowOfScreen(scr),
			   buf->fmt.width, buf->fmt.height, vinfo->depth);

    gc = XCreateGC(dpy, pixmap, 0, NULL);

    if (NULL == (ximage = x11_create_ximage(dpy, vinfo, buf->fmt.width,
					    buf->fmt.height, &shm))) {
	XFreePixmap(dpy, pixmap);
	XFreeGC(dpy, gc);
	return 0;
    }
    memcpy(ximage->data,buf->data,buf->size);
    x11_blit(dpy, pixmap, gc, ximage, 0, 0, 0, 0,
	     buf->fmt.width, buf->fmt.height);
    x11_destroy_ximage(dpy, ximage, shm);
    XFreeGC(dpy, gc);
    return pixmap;
}

/* ------------------------------------------------------------------------ */
/* XVideo extention code                                                    */

#ifdef HAVE_LIBXV
void xv_image_init(Display *dpy)
{
    int i;

    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	if (debug)
	    fprintf(stderr,"Xvideo: Server has no Xvideo extention support\n");
	return;
    }
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	fprintf(stderr,"Xvideo: XvQueryAdaptors failed");
	return;
    }
    for (i = 0; i < adaptors; i++) {
	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvImageMask) &&
	    (im_port == UNSET)) {
	    im_port = ai[i].base_id;
	    im_adaptor = i;
	}
    }
    if (UNSET == im_port)
	return;

    fo = XvListImageFormats(dpy, im_port, &formats);
    for(i = 0; i < formats; i++) {
	if (debug)
	    fprintf(stderr, "blit: xv: 0x%x (%c%c%c%c) %s",
		    fo[i].id,
		    (fo[i].id)       & 0xff,
		    (fo[i].id >>  8) & 0xff,
		    (fo[i].id >> 16) & 0xff,
		    (fo[i].id >> 24) & 0xff,
		    (fo[i].format == XvPacked) ? "packed" : "planar");
	if (0x32595559 == fo[i].id) {
	    im_formats[VIDEO_YUYV] = fo[i].id;
	    if (debug)
		fprintf(stderr," [ok: %s]",ng_vfmt_to_desc[VIDEO_YUYV]);
	}
	if (0x59565955 == fo[i].id) {
	    im_formats[VIDEO_UYVY] = fo[i].id;
	    if (debug)
		fprintf(stderr," [ok: %s]",ng_vfmt_to_desc[VIDEO_UYVY]);
	}
	if (0x30323449 == fo[i].id) {
	    im_formats[VIDEO_YUV420P] = fo[i].id;
	    if (debug)
		fprintf(stderr," [ok: %s]",ng_vfmt_to_desc[VIDEO_YUV420P]);
	}
	if (debug)
	    fprintf(stderr,"\n");
    }
}

XvImage*
xv_create_ximage(Display *dpy, int width, int height, int format,
		 XShmSegmentInfo **shm)
{
    XvImage         *xvimage = NULL;
    unsigned char   *ximage_data;
    XShmSegmentInfo *shminfo = NULL;
    void            *old_handler;

    if (no_mitshm)
	goto no_mitshm;

    old_handler = XSetErrorHandler(catch_no_mitshm);
    shminfo = malloc(sizeof(XShmSegmentInfo));
    memset(shminfo, 0, sizeof(XShmSegmentInfo));
    xvimage = XvShmCreateImage(dpy, im_port, format, 0,
			       width, height, shminfo);
    if (NULL == xvimage)
	goto shm_error;
    shminfo->shmid = shmget(IPC_PRIVATE, xvimage->data_size,
			    IPC_CREAT | 0777);
    if (-1 == shminfo->shmid) {
	perror("shmget [xv]");
	goto shm_error;
    }
    shminfo->shmaddr = (char *) shmat(shminfo->shmid, 0, 0);
    if ((void *)-1 == shminfo->shmaddr) {
	perror("shmat");
	goto shm_error;
    }
    xvimage->data = shminfo->shmaddr;
    shminfo->readOnly = False;

    XShmAttach(dpy, shminfo);
    XSync(dpy, False);
    if (no_mitshm)
	goto shm_error;
    shmctl(shminfo->shmid, IPC_RMID, 0);
    XSetErrorHandler(old_handler);
    *shm = shminfo;
    return xvimage;

shm_error:
    if (xvimage) {
	XFree(xvimage);
	xvimage = NULL;
    }
    if ((void *)-1 != shminfo->shmaddr  &&  NULL != shminfo->shmaddr)
	shmdt(shminfo->shmaddr);
    free(shminfo);
    XSetErrorHandler(old_handler);
    no_mitshm = 1;

 no_mitshm:
    *shm = NULL;
    if (NULL == (ximage_data = malloc(width * height * 2))) {
	fprintf(stderr,"out of memory\n");
	exit(1);
    }
    xvimage = XvCreateImage(dpy, im_port, format, ximage_data,
			    width, height);
    return xvimage;
}

void
xv_destroy_ximage(Display *dpy, XvImage * xvimage, XShmSegmentInfo *shm)
{
    if (shm && !no_mitshm) {
	XShmDetach(dpy, shm);
	XFree(xvimage);
	shmdt(shm->shmaddr);
	free(shm);
    } else
	XFree(xvimage);
}

void xv_blit(Display *dpy, Drawable dr, GC gc, XvImage *xi,
	     int a, int b, int c, int d, int x, int y, int w, int h)
{
    if (no_mitshm)
	XvPutImage(dpy,im_port,dr,gc,xi,a,b,c,d,x,y,w,h);
    else
	XvShmPutImage(dpy,im_port,dr,gc,xi,a,b,c,d,x,y,w,h,True);
}
#endif

/* ------------------------------------------------------------------------ */
/* OpenGL code                                                              */

#if HAVE_GL
static int have_gl,max_gl;
static int gl_attrib[] = { GLX_RGBA,
			   GLX_RED_SIZE, 1,
			   GLX_GREEN_SIZE, 1,
			   GLX_BLUE_SIZE, 1,
			   GLX_DOUBLEBUFFER,
			   None };

struct {
    int  fmt;
    int  type;
    char *ext;
} gl_formats[VIDEO_FMT_COUNT] = {
    [ VIDEO_RGB24 ] = {
	fmt:  GL_RGB,
	type: GL_UNSIGNED_BYTE,
    },
#ifdef GL_EXT_bgra
    [ VIDEO_BGR24 ] = {
	fmt:  GL_BGR_EXT,
	type: GL_UNSIGNED_BYTE,
	ext:  "GL_EXT_bgra",
    },
    [ VIDEO_BGR32 ] = {
	fmt:  GL_BGRA_EXT,
	type: GL_UNSIGNED_BYTE,
	ext:  "GL_EXT_bgra",
    },
#endif
};

static int gl_init(Widget widget)
{
    void *old_handler;
    XVisualInfo *visinfo;
    GLXContext ctx;

    if (debug)
	fprintf(stderr,"blit: gl: init\n");
    visinfo = glXChooseVisual(XtDisplay(widget),
			      XScreenNumberOfScreen(XtScreen(widget)),
			      gl_attrib);
    if (!visinfo) {
	if (debug)
	    fprintf(stderr,"blit: gl: can't get visual (rgb,db)\n");
	return -1;
    }
    ctx = glXCreateContext(XtDisplay(widget), visinfo, NULL, True);
    if (!ctx) {
	if (debug)
	    fprintf(stderr,"blit: gl: can't create context\n");
	return -1;
    }

    /* there is no point in using OpenGL for image scaling if it
     * isn't hardware accelerated ... */
    if (debug)
	fprintf(stderr, "blit: gl: DRI=%s\n",
		glXIsDirect(XtDisplay(widget), ctx) ? "Yes" : "No");
    if (!glXIsDirect(XtDisplay(widget), ctx))
	return -1;

    old_handler = XSetErrorHandler(catch_gl_error);
    glXMakeCurrent(XtDisplay(widget),XtWindow(widget),ctx);
    XSync(XtDisplay(widget), False);
    XSetErrorHandler(old_handler);
    if (gl_error)
	return -1;

    have_gl = 1;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_gl);
    if (debug)
	fprintf(stderr,"blit: gl: texture max size: %d\n",max_gl);
    return 0;
}

static int gl_ext(GLubyte *find)
{
    int len = strlen(find);
    const GLubyte *ext;
    GLubyte *pos;

    ext = glGetString(GL_EXTENSIONS);
    if (NULL == ext)
	return 0;
    if (NULL == (pos = strstr(ext,find)))
	return 0;
    if (pos != ext && pos[-1] != ' ')
	return 0;
    if (pos[len] != ' ' && pos[len] != '\0')
	return 0;
    if (debug)
	fprintf(stderr,"blit: gl: extention %s is available\n",find);
    return 1;
}

static int gl_resize(int iw, int ih, int ww, int wh,
		     GLint *tex, int *tw, int *th, int fmt, int type)
{
    char *dummy;
    int i;

    /* check against max size */
    if (iw > max_gl)
	return -1;
    if (ih > max_gl)
	return -1;

    /* textures have power-of-two x,y dimensions */
    for (i = 0; iw >= (1 << i); i++)
	;
    *tw = (1 << i);
    for (i = 0; ih >= (1 << i); i++)
	;
    *th = (1 << i);
    if (debug)
	fprintf(stderr,"blit: gl: frame=%dx%d, texture=%dx%d\n",
		iw,ih,*tw,*th);

    glClearColor (0.0, 0.0, 0.0, 0.0);
    glShadeModel(GL_FLAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, ww, 0.0, wh, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glGenTextures(1,tex);
    glBindTexture(GL_TEXTURE_2D,*tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    dummy = malloc((*tw)*(*th)*3);
    memset(dummy,128,(*tw)*(*th)*3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,*tw,*th,0,
		 fmt,type,dummy);
    free(dummy);

    return 0;
}

static void gl_cleanup(GLint tex)
{
    /* FIXME: del texture */
}

static void gl_blit(Widget widget, char *rgbbuf,
		    int iw, int ih, int ww, int wh,
		    GLint tex, int tw, int th, int fmt, int type)
{
    float x,y;

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0,0,iw,ih, fmt,type,rgbbuf);
    x = (float)iw/tw;
    y = (float)ih/th;

    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glBegin(GL_QUADS);
    glTexCoord2f(0,y);  glVertex3f(0,0,0);
    glTexCoord2f(0,0);  glVertex3f(0,wh,0);
    glTexCoord2f(x,0);  glVertex3f(ww,wh,0);
    glTexCoord2f(x,y);  glVertex3f(ww,0,0);
    glEnd();
    glXSwapBuffers(XtDisplay(widget), XtWindow(widget));
    glDisable(GL_TEXTURE_2D);
}
#endif

/* ------------------------------------------------------------------------ */
/* video frame blitter                                                      */

enum blit_status {
    STATUS_UNKNOWN = 0,
    STATUS_BROKEN  = 1,
    STATUS_CONVERT = 2,
    STATUS_XVIDEO  = 3,
    STATUS_OPENGL  = 4,
};

struct blit_state {
    enum blit_status          status;
    Widget                    widget;
    Dimension                 win_width, win_height;
    int                       wx,wy,ww,wh;
    GC                        gc;
    XVisualInfo               *vinfo;
    struct ng_video_fmt       fmt;
    struct ng_video_buf       buf;
    struct ng_video_conv      *conv;
    struct ng_convert_handle  *chandle;
    XShmSegmentInfo           *shm;
    XImage                    *ximage;
#ifdef HAVE_LIBXV
    XvImage                   *xvimage;
#endif
#if HAVE_GL
    GLint                     tex;
    int                       tw,th;
#endif
};

struct blit_state*
blit_init(Widget widget, XVisualInfo *vinfo, int use_gl)
{
    struct blit_state *st;

    if (debug)
	fprintf(stderr,"blit: init\n");
    BUG_ON(0 == XtWindow(widget), "no blit window");

    st = malloc(sizeof(*st));
    memset(st,0,sizeof(*st));

    st->widget = widget;
    st->vinfo  = vinfo;
    st->gc     = XCreateGC(XtDisplay(st->widget),XtWindow(st->widget),0,NULL);
#ifdef HAVE_GL
    if (use_gl)
	gl_init(st->widget);
#endif

    return st;
}

void blit_get_formats(struct blit_state *st, int *fmtids, int max)
{
    struct ng_video_conv *conv;
    int i, n=0;

    BUG_ON(NULL == st, "blit handle is NULL");

    /* Xvideo extention */
#ifdef HAVE_LIBXV
    for (i = 0; i < VIDEO_FMT_COUNT; i++) {
	if (0 != im_formats[i])
	    fmtids[n++] = i;
	if (n == max)
	    return;
    }
#endif

#if HAVE_GL
    /* OpenGL */
    if (have_gl) {
	for (i = 0; i < VIDEO_FMT_COUNT; i++) {
	    if (0 != gl_formats[i].fmt  &&
		(NULL == gl_formats[i].ext || gl_ext(gl_formats[i].ext)))
		fmtids[n++] = i;
	    if (n == max)
		return;
	}
    }
#endif

    /* plain X11 */
    fmtids[n++] = x11_dpy_fmtid;
    if (n == max)
	return;
    for (i = 0;;) {
	conv = ng_conv_find_to(x11_dpy_fmtid, &i);
	if (NULL == conv)
	    break;
	fmtids[n++] = conv->fmtid_in;
	if (n == max)
	    return;
    }
    for (; n < max; n++)
	fmtids[n] = 0;
}

void blit_resize(struct blit_state *st, Dimension width, Dimension height)
{
    if (debug)
	fprintf(stderr,"blit: resize %dx%d\n",width,height);
    st->win_width  = width;
    st->win_height = height;

    st->wx = 0;
    st->wy = 0;
    st->ww = st->win_width;
    st->wh = st->win_height;
    ng_ratio_fixup(&st->ww, &st->wh, &st->wx, &st->wy);

    blit_fini_frame(st);
}

void blit_init_frame(struct blit_state *st, struct ng_video_fmt *fmt)
{
    struct ng_video_conv *conv;
    int i;

    /* Xvideo extention */
#ifdef HAVE_LIBXV
    if (0 != im_formats[fmt->fmtid]) {
	st->xvimage = xv_create_ximage(XtDisplay(st->widget),
				       fmt->width, fmt->height,
				       im_formats[fmt->fmtid],
				       &st->shm);
	st->buf.fmt = *fmt;
	st->status  = STATUS_XVIDEO;
	if (debug)
	    fprintf(stderr,"blit: %dx%d/[%s] => Xvideo\n",
		    fmt->width, fmt->height, ng_vfmt_to_desc[fmt->fmtid]);
	return;
    }
#endif

#if HAVE_GL
    /* OpenGL */
    if (have_gl  &&  0 != gl_formats[fmt->fmtid].fmt  &&
	(NULL == gl_formats[fmt->fmtid].ext ||
	 gl_ext(gl_formats[fmt->fmtid].ext)) &&
	0 == gl_resize(fmt->width,fmt->height,
		       st->win_width,st->win_height,
		       &st->tex,&st->tw,&st->th,
		       gl_formats[fmt->fmtid].fmt,
		       gl_formats[fmt->fmtid].type)) {
	st->buf.fmt = *fmt;
	st->status  = STATUS_OPENGL;
	if (debug)
	    fprintf(stderr,"blit: %dx%d/[%s] => OpenGL\n",
		    fmt->width, fmt->height, ng_vfmt_to_desc[fmt->fmtid]);
	return;
    }
#endif

    /* plain X11 */
    st->ximage = x11_create_ximage(XtDisplay(st->widget), st->vinfo,
				   fmt->width, fmt->height,
				   &st->shm);
    st->buf.data = st->ximage->data;
    if (x11_dpy_fmtid == fmt->fmtid) {
	st->buf.fmt = *fmt;
	st->status  = STATUS_CONVERT;
	if (debug)
	    fprintf(stderr,"blit: %dx%d/[%s] => X11 direct\n",
		    fmt->width, fmt->height, ng_vfmt_to_desc[fmt->fmtid]);
	return;
    }
    for (i = 0;;) {
	conv = ng_conv_find_to(x11_dpy_fmtid, &i);
	if (NULL == conv) {
	    st->status = STATUS_BROKEN;
	    if (debug)
		fprintf(stderr,"blit: %dx%d/[%s] => can't display\n",
			fmt->width, fmt->height, ng_vfmt_to_desc[fmt->fmtid]);
	    return;
	}
	if (debug)
	    fprintf(stderr,"blit test: %s\n",ng_vfmt_to_desc[conv->fmtid_in]);
	if (conv->fmtid_in != fmt->fmtid)
	    continue;
	break;
    }
    st->buf.fmt = *fmt;
    st->status  = STATUS_CONVERT;
    st->conv    = conv;
    st->buf.fmt.fmtid  = x11_dpy_fmtid;
    st->buf.fmt.bytesperline = 0;
    st->chandle = ng_convert_alloc(st->conv,fmt,&st->buf.fmt);
    ng_convert_init(st->chandle);
    if (debug)
	fprintf(stderr,"blit: %dx%d/[%s] => X11 via [%s]\n",
		fmt->width, fmt->height, ng_vfmt_to_desc[fmt->fmtid],
		ng_vfmt_to_desc[st->buf.fmt.fmtid]);
    return;
}

void blit_fini_frame(struct blit_state *st)
{
    switch (st->status) {
    case STATUS_CONVERT:
	if (st->chandle) {
	    ng_convert_fini(st->chandle);
	    st->chandle = NULL;
	}
	if (st->ximage) {
	    x11_destroy_ximage(XtDisplay(st->widget),st->ximage,st->shm);
	    st->ximage = NULL;
	}
	break;

#if HAVE_LIBXV
    case STATUS_XVIDEO:
	if (st->xvimage) {
	    xv_destroy_ximage(XtDisplay(st->widget),st->xvimage,st->shm);
	    st->xvimage = NULL;
	}
	XvStopVideo(XtDisplay(st->widget), im_port, XtWindow(st->widget));
	break;
#endif

#if HAVE_GL
    case STATUS_OPENGL:
	gl_cleanup(st->tex);
	break;
#endif

    case STATUS_UNKNOWN:
    case STATUS_BROKEN:
	break;
    }
    memset(&st->fmt,0,sizeof(st->fmt));
    memset(&st->buf,0,sizeof(st->buf));
    st->status = STATUS_UNKNOWN;
}

void blit_fini(struct blit_state *st)
{
    free(st);
}

void blit_putframe(struct blit_state *st, struct ng_video_buf *buf)
{
    if (st->fmt.fmtid  != buf->fmt.fmtid &&
	st->fmt.width  != buf->fmt.width &&
	st->fmt.height != buf->fmt.height) {
	blit_fini_frame(st);
	blit_init_frame(st,&buf->fmt);
	st->fmt = buf->fmt;
    }

    if (debug > 1)
	fprintf(stderr,"blit: putframe\n");
    switch (st->status) {
    case STATUS_CONVERT:
	if (NULL == st->chandle) {
	    memcpy(st->ximage->data,buf->data,buf->size);
	    ng_release_video_buf(buf);
	} else {
	    buf = ng_convert_frame(st->chandle,&st->buf,buf);
	}
	x11_blit(XtDisplay(st->widget), XtWindow(st->widget),
		 st->gc,st->ximage,0,0,
		 (st->win_width  - st->buf.fmt.width)  >> 1,
		 (st->win_height - st->buf.fmt.height) >> 1,
		 st->buf.fmt.width, st->buf.fmt.height);
	break;

#ifdef HAVE_LIBXV
    case STATUS_XVIDEO:
	memcpy(st->xvimage->data,buf->data,buf->size);
	ng_release_video_buf(buf);
	xv_blit(XtDisplay(st->widget), XtWindow(st->widget),
		st->gc, st->xvimage,
		0, 0,  st->buf.fmt.width, st->buf.fmt.height,
		st->wx, st->wy, st->ww, st->wh);
	break;
#endif

#if HAVE_GL
    case STATUS_OPENGL:
	gl_blit(st->widget,buf->data,
		st->buf.fmt.width, st->buf.fmt.height,
		st->win_width, st->win_height,
		st->tex, st->tw, st->th,
		gl_formats[buf->fmt.fmtid].fmt,
		gl_formats[buf->fmt.fmtid].type);
	ng_release_video_buf(buf);
	break;
#endif

    case STATUS_UNKNOWN:
    case STATUS_BROKEN:
	if (debug > 1)
	    fprintf(stderr,"blit: putframe: oops: status = %d\n",st->status);
	ng_release_video_buf(buf);
	break;
    }
}
