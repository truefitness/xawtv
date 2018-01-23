/*
 * Set the framebuffer parameters for bttv.
 *   tries to ask the X-Server if $DISPLAY is set,
 *   otherwise it checks /dev/fb0
 *
 *  (c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 *  Security checks by okir@caldera.de
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/vt.h>
#include <linux/fb.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif

#ifndef X_DISPLAY_MISSING
# include <X11/Xlib.h>
# include <X11/Xutil.h>
# ifdef HAVE_LIBXXF86DGA
#  include <X11/extensions/Xxf86dga.h>
# endif
#endif

#include <linux/types.h>
#include "videodev2.h"

struct DISPLAYINFO {
    int   width;             /* visible display width  (pixels) */
    int   height;            /* visible display height (pixels) */
    int   depth;             /* color depth                     */
    int   bpp;               /* bit per pixel                   */
    int   bpl;               /* bytes per scanline              */
    unsigned char *base;
};

int    verbose    = 1;
int    yuv        = 0;
int    user_bpp   = 0;
int    user_shift = 0;
int    user_bpl   = 0;
void   *user_base = NULL;
char   *display   = NULL;
char   *fbdev     = NULL;
char   *videodev  = "/dev/video0";

/* ---------------------------------------------------------------- */
/* this is required for MkLinux                                     */

#ifdef __powerpc__
struct vc_mode {
    int     height;
    int     width;
    int     depth;
    int     pitch;
    int     mode;
    char    name[32];
    unsigned long fb_address;
    unsigned long cmap_adr_address;
    unsigned long cmap_data_address;
    unsigned long disp_reg_address;
};

#define VC_GETMODE      0x7667
#define VC_SETMODE      0x7668
#define VC_INQMODE      0x7669

#define VC_SETCMAP      0x766a
#define VC_GETCMAP      0x766b

static int
is_mklinux(void)
{
    int fd;
    if(-1 == (fd = open("/proc/osfmach3", O_RDONLY)))
	return 0;
    close(fd);
    return 1;
}

static void
displayinfo_mklinux(struct DISPLAYINFO *d)
{
    struct vc_mode mode;
    int fd;

    if (verbose)
	fprintf(stderr,"v4l-conf: using mklinux console driver\n");

    if (-1 == (fd = open("/dev/console",O_RDWR|O_NDELAY))) {
	fprintf(stderr,"open console: %s\n",strerror(errno));
	exit(1);
    }
    if (-1 == ioctl(fd, VC_GETMODE, (unsigned long)&mode)) {
	perror("ioctl VC_GETMODE");
	exit(1);
    }
    close(fd);
    d->width  = mode.width;
    d->height = mode.height;
    d->bpp    = mode.depth;
    d->bpl    = mode.pitch;
    d->base   = (void*)mode.fb_address;
}
#endif

/* ---------------------------------------------------------------- */

#ifndef major
# define major(dev)  (((dev) >> 8) & 0xff)
#endif

static int
dev_open(const char *device, int major)
{
    struct stat stb;
    int	fd;

    if (strncmp(device, "/dev/", 5)) {
	fprintf(stderr, "error: %s is not a /dev file\n", device);
	exit(1);
    }

    /* open & check v4l device */
    if (-1 == (fd = open(device,O_RDWR))) {
	fprintf(stderr, "can't open %s: %s\n", device, strerror(errno));
	exit(1);
    }

    if (-1 == fstat(fd,&stb)) {
	fprintf(stderr, "fstat(%s): %s\n", device, strerror(errno));
	exit(1);
    }
    if (!S_ISCHR(stb.st_mode) || (major(stb.st_rdev) != major)) {
	fprintf(stderr, "%s: wrong device\n", device);
	exit(1);
    }
    return fd;
}

static void real_user(void)
{
    if (-1 == seteuid(getuid())) {
	perror("seteuid(user)");
	exit(1);
    }
}

static void root_user(void)
{
    if (-1 == seteuid(0)) {
	perror("seteuid(root)");
	exit(1);
    }
}

/* ---------------------------------------------------------------- */
/* get mode info                                                    */

#ifndef X_DISPLAY_MISSING
static void
displayinfo_x11(Display *dpy, struct DISPLAYINFO *d)
{
    Window                   root;
    XVisualInfo              *info, template;
    XPixmapFormatValues      *pf;
    XWindowAttributes        wts;
    int                      found,v,i,n;

    if (verbose)
	fprintf(stderr,"v4l-conf: using X11 display %s\n",display);

    /* take size from root window */
    root = DefaultRootWindow(dpy);
    XGetWindowAttributes(dpy, root, &wts);
    d->width  = wts.width;
    d->height = wts.height;

    /* look for a usable visual */
    template.screen = XDefaultScreen(dpy);
    info = XGetVisualInfo(dpy, VisualScreenMask,&template,&found);
    v = -1;
    for (i = 0; v == -1 && i < found; i++)
	if (info[i].class == TrueColor && info[i].depth >= 15)
	    v = i;
    for (i = 0; v == -1 && i < found; i++)
	if (info[i].class == StaticGray && info[i].depth == 8)
	    v = i;
    if (-1 == v) {
	fprintf(stderr,"x11: no approximate visual available\n");
	exit(1);
    }

    /* get depth + bpp (heuristic) */
    pf = XListPixmapFormats(dpy,&n);
    for (i = 0; i < n; i++) {
	if (pf[i].depth == info[v].depth) {
	    d->depth = pf[i].depth;
	    d->bpp   = pf[i].bits_per_pixel;
	    d->bpl   = d->bpp * d->width / 8;
	    break;
	}
    }
    if (0 == d->bpp) {
	fprintf(stderr,"x11: can't detect framebuffer depth\n");
	exit(1);
    }
}

#ifdef HAVE_LIBXXF86DGA
static int dga_error = 0;
static int dga_error_base;
static int (*orig_xfree_error_handler)(Display *, XErrorEvent *);

static int dga_error_handler(Display *d, XErrorEvent *e)
{
  if (e->error_code == (dga_error_base + XF86DGANoDirectVideoMode)) {
    dga_error = 1;
    return 0;
  }
  return orig_xfree_error_handler(d, e);
}
#endif

static void
displayinfo_dga(Display *dpy, struct DISPLAYINFO *d)
{
#ifdef HAVE_LIBXXF86DGA
    int                      width,bar,foo,major,minor,flags=0;
    void                     *base = NULL;

    if (!XF86DGAQueryExtension(dpy,&foo,&dga_error_base)) {
	fprintf(stderr,"WARNING: Your X-Server has no DGA support.\n");
	return;
    }
    XF86DGAQueryVersion(dpy,&major,&minor);
    if (verbose)
	fprintf(stderr,"dga: version %d.%d\n",major,minor);
    XF86DGAQueryDirectVideo(dpy,XDefaultScreen(dpy),&flags);
    if (!(flags & XF86DGADirectPresent)) {
	fprintf(stderr,"WARNING: No DGA support available for this display.\n");
	return;
    }
    orig_xfree_error_handler = XSetErrorHandler(dga_error_handler);
    if (!XF86DGAGetVideoLL(dpy,XDefaultScreen(dpy),(void*)&base,&width,&foo,&bar))
	dga_error = 1;
    XSync(dpy, 0);
    XSetErrorHandler(orig_xfree_error_handler);
    if (dga_error) {
	fprintf(stderr,"WARNING: No DGA direct video mode for this display.\n");
	return;
    }
    d->bpl  = width * d->bpp/8;
    d->base = base;
#else
    fprintf(stderr,"WARNING: v4l-conf is compiled without DGA support.\n");
#endif
}
#endif

static void
displayinfo_fbdev(struct DISPLAYINFO *d)
{
    struct fb_fix_screeninfo   fix;
    struct fb_var_screeninfo   var;
    struct fb_con2fbmap        c2m;
    struct vt_stat             vstat;
    int fd;

    if (NULL == fbdev) {
	if (-1 == (fd = open("/dev/tty",O_RDWR,0))) {
	    fprintf(stderr,"open /dev/tty: %s\n",strerror(errno));
	    exit(1);
	}
	if (-1 == ioctl(fd, VT_GETSTATE, &vstat)) {
	    perror("VT_GETSTATE is not supported");
	    return;
	}
	close(fd);
	c2m.console = vstat.v_active;
	if (-1 == (fd = open("/dev/fb0",O_RDWR,0))) {
	    fprintf(stderr,"open /dev/fb0: %s\n",strerror(errno));
	    exit(1);
	}
	if (-1 == ioctl(fd, FBIOGET_CON2FBMAP, &c2m)) {
	    perror("ioctl FBIOGET_CON2FBMAP");
	    c2m.framebuffer = 0;
	}
	close(fd);
	fprintf(stderr,"map: vt%02d => fb%d\n",c2m.console,c2m.framebuffer);
	sprintf(fbdev=malloc(16),"/dev/fb%d",c2m.framebuffer);
    }
    if (verbose)
	fprintf(stderr,"v4l-conf: using framebuffer device %s\n",fbdev);

    /* Open frame buffer device, with security checks */
    fd = dev_open(fbdev, 29 /* VIDEO_MAJOR */);
    if (-1 == ioctl(fd,FBIOGET_FSCREENINFO,&fix)) {
	perror("ioctl FBIOGET_FSCREENINFO");
	exit(1);
    }
    if (-1 == ioctl(fd,FBIOGET_VSCREENINFO,&var)) {
	perror("ioctl FBIOGET_VSCREENINFO");
	exit(1);
    }
    if (fix.type != FB_TYPE_PACKED_PIXELS) {
	fprintf(stderr,"can handle only packed pixel frame buffers\n");
	exit(1);
    }
    close(fd);

    d->width  = var.xres_virtual;
    d->height = var.yres_virtual;
    d->bpp    = var.bits_per_pixel;
    d->bpl    = fix.line_length;
    d->base   = (unsigned char*)fix.smem_start;

    d->depth  = d->bpp;
    if (var.green.length == 5)
	d->depth = 15;
}

/* ---------------------------------------------------------------- */
/* set mode info                                                    */

static int
displayinfo_v4l2(int fd, struct DISPLAYINFO *d)
{
    struct v4l2_capability	cap;
    struct v4l2_framebuffer     fb;

    if (-1 == ioctl(fd,VIDIOC_QUERYCAP,&cap)) {
	if (verbose)
	    fprintf(stderr,"%s [v4l2]: ioctl VIDIOC_QUERYCAP: %s\n",
		    videodev,strerror(errno));
	return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_OVERLAY)) {
	fprintf(stderr,"%s [v4l2]: no overlay support\n",videodev);
	exit(1);
    }

    /* read-modify-write v4l screen parameters */
    if (-1 == ioctl(fd,VIDIOC_G_FBUF,&fb)) {
	fprintf(stderr,"%s [v4l2]: ioctl VIDIOC_G_FBUF: %s\n",
		videodev,strerror(errno));
	exit(1);
    }

    /* set values */
    fb.fmt.width  = d->width;
    fb.fmt.height = d->height;
    switch (d->bpp) {
    case  8: fb.fmt.pixelformat = V4L2_PIX_FMT_HI240;   break;
#if BYTE_ORDER == BIG_ENDIAN
    case 15: fb.fmt.pixelformat = V4L2_PIX_FMT_RGB555X; break;
    case 16: fb.fmt.pixelformat = V4L2_PIX_FMT_RGB565X; break;
    case 24: fb.fmt.pixelformat = V4L2_PIX_FMT_RGB24;   break;
    case 32: fb.fmt.pixelformat = V4L2_PIX_FMT_RGB32;   break;
#else
    case 15: fb.fmt.pixelformat = V4L2_PIX_FMT_RGB555;  break;
    case 16: fb.fmt.pixelformat = V4L2_PIX_FMT_RGB565;  break;
    case 24: fb.fmt.pixelformat = V4L2_PIX_FMT_BGR24;   break;
    case 32: fb.fmt.pixelformat = V4L2_PIX_FMT_BGR32;   break;
#endif
    }
    if (yuv)
	fb.fmt.pixelformat = V4L2_PIX_FMT_YUYV;
    /* Prefer an already configured bpl (if it makes sense) over our found bpl
       if we did not find a base as our bpl is not very reliable when we did
       not find a base */
    if (user_bpl || d->base ||
	    fb.fmt.bytesperline < (fb.fmt.width * ((d->bpp + 7) / 8)))
    fb.fmt.bytesperline = d->bpl;
    else
	fprintf(stderr,"WARNING: keeping fbuf pitch at: %d, as no base addr was detected\n",
	    (int)fb.fmt.bytesperline);
    fb.fmt.sizeimage = fb.fmt.height * fb.fmt.bytesperline;
    if (NULL != d->base)
	fb.base   = d->base;
    if (NULL == fb.base)
	fprintf(stderr,
		"WARNING: couldn't find framebuffer base address, try manual\n"
		"         configuration (\"v4l-conf -a <addr>\")\n");

    if (-1 == ioctl(fd,VIDIOC_S_FBUF,&fb)) {
	fprintf(stderr,"%s [v4l2]: ioctl VIDIOC_S_FBUF: %s\n",
		videodev,strerror(errno));
	if (EPERM == errno  &&  0 != geteuid())
	    fprintf(stderr,
		    "v4l-conf: You should install me suid root, I need\n"
		    "          root priviliges for the VIDIOC_S_FBUF ioctl.\n");
	exit(1);
    }
    if (verbose)
	fprintf(stderr,"%s [v4l2]: configuration done\n",videodev);
    return 0;
}

/* ---------------------------------------------------------------- */

int
main(int argc, char *argv[])
{
    struct DISPLAYINFO       d;
    int                      fd,c,i,n;
    char                     *h;
#ifndef X_DISPLAY_MISSING
    Display                  *dpy;
#endif

    /* we don't need root proviliges for now ... */
    real_user();

    /* Make sure fd's 0 1 2 are open, otherwise
     * we might end up sending perror() messages to
     * the `device' file */
    for (i = 0; i < 3; i++) {
	if (-1 == fcntl(i, F_GETFL, &n))
	    exit(1);
    }

    /* take defaults from environment */
    if (NULL != (h = getenv("DISPLAY")))
	display = h;
    if (NULL != (h = getenv("FRAMEBUFFER")))
	fbdev = h;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hyq12d:c:b:s:fa:p:")))
	    break;
	switch (c) {
	case 'q':
	    verbose = 0;
	    break;
	case 'y':
	    yuv = 1;
	    break;
	case 'd':
	    display = optarg;
	    break;
	case 'c':
	    videodev = optarg;
	    break;
	case 'b':
	    user_bpp = atoi(optarg);
	    break;
	case 's':
	    user_shift = atoi(optarg);
	    if (user_shift < 0 || user_shift > 8192)
		user_shift = 0;
	    break;
	case 'f':
	    display = NULL;
	    break;
	case 'a':
	    if (0 == getuid()) {
		/* only root is allowed to set this, and it will work only
		 * if v4l-conf can't figure out the correct address itself.
		 * Useful for "post-install bttv ..." */
		sscanf(optarg,"%p",&user_base);
	    } else {
		fprintf(stderr,"only root is allowed to use the -a option\n");
		exit(1);
	    }
	    break;
	case 'p':
	    if (0 == getuid()) {
		sscanf(optarg,"%d",&user_bpl);
	    } else {
		fprintf(stderr,"only root is allowed to use the -p option\n");
		exit(1);
	    }
	    break;
	case 'h':
	default:
	    fprintf(stderr,
		    "usage: %s  [ options ] \n"
		    "\n"
		    "options:\n"
		    "    -q        quiet\n"
#ifndef X_DISPLAY_MISSING
		    "    -d <dpy>  X11 Display     [%s]\n"
#endif
		    "    -c <dev>  video device    [%s]\n"
		    "    -b <n>    displays color depth is <n> bpp\n"
		    "    -s <n>    shift display by <n> bytes\n"
		    "    -f        query frame buffer device for info\n"
		    "    -a <addr> set framebuffer address to <addr>\n"
		    "              (in hex, root only, successful autodetect\n"
		    "               will overwrite this address)\n"
		    "    -p <pitch> set framebuffer pitch to <pitch> bytes\n"
		    "              (decimal, root only)\n"
		    "    -1        force v4l API\n"
		    "    -2        force v4l2 API\n",
		    argv[0],
#ifndef X_DISPLAY_MISSING
		    display ? display : "none",
#endif
		    videodev);
	    exit(1);
	}
    }

    /* figure out display parameters */
    memset(&d,0,sizeof(struct DISPLAYINFO));
#ifdef __powerpc__
    if (is_mklinux()) {
	displayinfo_mklinux(&d);
    } else
#endif
#ifndef X_DISPLAY_MISSING
    if (NULL != display) {
	/* using X11 */
	if (display[0] != ':') {
	    fprintf(stderr,"WARNING: remote display `%s' not allowed, ",
		    display);
	    display = strchr(display,':');
	    if (NULL == display) {
		fprintf(stderr,"exiting");
		exit(1);
	    } else {
		fprintf(stderr,"using `%s' instead\n",display);
	    }
	}
	if (NULL == (dpy = XOpenDisplay(display))) {
	    fprintf(stderr,"can't open x11 display %s\n",display);
	    exit(1);
	}
	displayinfo_x11(dpy,&d);
	displayinfo_dga(dpy,&d);
    } else
#endif
    {
	/* try framebuffer device */
	displayinfo_fbdev(&d);
    }

    /* fixup struct displayinfo according to the given command line options */
    if (user_base) {
	if (NULL == d.base) {
	    fprintf(stderr,"using user provided base address %p\n",user_base);
	    d.base = user_base;
	} else {
	    fprintf(stderr,"user provided base address %p ignored.\n",
		    user_base);
	}
    }
    if (d.base)
	d.base += user_shift;
    if ((user_bpp == 24 || user_bpp == 32) &&
	(d.bpp    == 24 || d.bpp    == 32)) {
	d.bpp = user_bpp;
	d.bpl = d.width * d.bpp/8;
    }
    if ((user_bpp == 15 || user_bpp == 16) &&
	(d.depth  == 15 || d.depth  == 16))
	d.depth = user_bpp;

    if (user_bpl)
	d.bpl = user_bpl;

    if (verbose) {
	fprintf(stderr,"mode: %dx%d, depth=%d, bpp=%d, bpl=%d, ",
		d.width,d.height,d.depth,d.bpp,d.bpl);
	if (NULL != d.base)
	    fprintf(stderr,"base=%p\n",d.base);
	else
	    fprintf(stderr,"base=unknown\n");
    }

    /* Set the parameters (needs root) */
    root_user();
    fd = dev_open(videodev, 81 /* VIDEO_MAJOR */);
    displayinfo_v4l2(fd,&d);
    close(fd);
    return 0;
}
