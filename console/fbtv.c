/*
 * console TV application.  Uses a framebuffer device.
 *
 *   (c) 1998-2001 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <curses.h>
#include <math.h>
#include <pthread.h>

#include <linux/kd.h>
#include <linux/fb.h>

#include "grab-ng.h"
#include "writefile.h"
#include "sound.h"
#include "channel.h"
#include "frequencies.h"
#include "commands.h"
#include "capture.h"
#include "lirc.h"
#include "joystick.h"
#include "midictrl.h"
#include "event.h"

#include "fbtools.h"
#include "fs.h"
#include "matrox.h"

#define MAX(x,y)        ((x)>(y)?(x):(y))
#define MIN(x,y)        ((x)<(y)?(x):(y))

/* ---------------------------------------------------------------------- */
/* framebuffer                                                            */

static char  *fbdev    = NULL;
static char  *fontfile = NULL;
static char  *mode     = NULL;
static char  *joydev   = NULL;
static struct fs_font *f;

static unsigned short red[256],  green[256],  blue[256];
static struct fb_cmap cmap  = { 0, 256, red,  green,  blue };

static int switch_last,fb;
static int keep_dma_on = 0;

static int sig,quiet,matrox;
static int ww,hh;
static float fbgamma = 1.0;

static struct ng_video_buf   *buf;
static struct ng_video_fmt   fmt,gfmt;
static struct ng_video_conv  *conv;
static struct ng_convert_handle *ch;
static int dx,dy;

int have_config;
int x11_native_format,have_dga=1,debug;

/*--- channels ------------------------------------------------------------*/

struct event_entry kbd_events[] = {
    {
	.event  = "kbd-key-+",
	.action = "volume inc",
    },{
	.event  = "kbd-key--",
	.action = "volume dec",
    },{
	.event  = "kbd-key-enter",
	.action = "volume mute",
    },{
	.event  = "kbd-key-space",
	.action = "setstation next",
    },{
	.event  = "kbd-key-backspace",
	.action = "setstation back",
    },{
	.event  = "kbd-key-pgup",
	.action = "setstation next",
    },{
	.event  = "kbd-key-pgdown",
	.action = "setstation prev",
    },{
	.event  = "kbd-key-right",
	.action = "setchannel fine_up",
    },{
	.event  = "kbd-key-left",
	.action = "setchannel fine_down",
    },{
	.event  = "kbd-key-up",
	.action = "setchannel next",
    },{
	.event  = "kbd-key-down",
	.action = "setchannel prev",
    },{
	.event  = "kbd-key-g",
	.action = "snap ppm",
    },{
	.event  = "kbd-key-j",
	.action = "snap jpeg",
    },{
	.event  = "kbd-key-v",
	.action = "capture toggle",
    },{
	.event  = "kbd-key-f",
	.action = "fullscreen toggle",
    },{
	.event  = "kbd-key-0",
	.action = "keypad 0",
    },{
	.event  = "kbd-key-1",
	.action = "keypad 1",
    },{
	.event  = "kbd-key-2",
	.action = "keypad 2",
    },{
	.event  = "kbd-key-3",
	.action = "keypad 3",
    },{
	.event  = "kbd-key-4",
	.action = "keypad 4",
    },{
	.event  = "kbd-key-5",
	.action = "keypad 5",
    },{
	.event  = "kbd-key-6",
	.action = "keypad 6",
    },{
	.event  = "kbd-key-7",
	.action = "keypad 7",
    },{
	.event  = "kbd-key-8",
	.action = "keypad 8",
    },{
	.event  = "kbd-key-9",
	.action = "keypad 9",
    },{

	/* end of list */
    }
};
struct KEYTAB {
    int  key;
    char *name;
};

static struct KEYTAB keytab[] = {
    { 9,             "tab"       },
    { 10,            "enter"     },
    { 13,            "enter"     },
    { KEY_ENTER,     "enter"     },

    { ' ',           "space"     },
    { KEY_BACKSPACE, "backspace" },

    { KEY_RIGHT,     "right"     },
    { KEY_LEFT,      "left"      },
    { KEY_UP,        "up"        },
    { KEY_DOWN,      "down"      },
    { KEY_PPAGE,     "pgup"      },
    { KEY_NPAGE,     "pgdown"    },
    { KEY_HOME,      "home"      },
    { KEY_END,       "end"       },
};

#define NKEYTAB (sizeof(keytab)/sizeof(struct KEYTAB))

static char              default_title[128] = "???";
static char              message[128] = "";

/* ---------------------------------------------------------------------- */
/* framebuffer stuff                                                      */

static void
linear_palette(int r, int g, int b)
{
    int i, size;

    size = 256 >> (8 - r);
    for (i = 0; i < size; i++)
	red[i] = (unsigned short)(65535.0
		* pow(i/(size - 1.0), fbgamma));

    size = 256 >> (8 - g);
    for (i = 0; i < size; i++)
	green[i] = (unsigned short)(65535.0
		* pow(i/(size - 1.0), fbgamma));

    size = 256 >> (8 - b);
    for (i = 0; i < size; i++)
	blue[i] = (unsigned short)(65535.0
		* pow(i/(size - 1.0), fbgamma));
}

static void
dither_palette(int r, int g, int b)
{
    int             rs, gs, bs, i;

    rs = 256 / (r - 1);
    gs = 256 / (g - 1);
    bs = 256 / (b - 1);
    for (i = 0; i < r*g*b; i++) {
	green[i+16] = (gs * ((i / (r * b)) % g)) * 255;
	red[i+16]   = (rs * ((i / b) % r)) * 255;
	blue[i+16]  = (bs * ((i) % b)) * 255;
    }
}

static void
fb_initcolors(int fd, int gray)
{
    /* get colormap */
    if (fb_var.bits_per_pixel == 8 ||
	fb_fix.visual == FB_VISUAL_DIRECTCOLOR) {
	if (-1 == ioctl(fd,FBIOGETCMAP,&cmap))
	    perror("ioctl FBIOGETCMAP");
    }

    switch (fb_var.bits_per_pixel) {
    case 8:
	if (gray) {
	    linear_palette(8,8,8);
	    x11_native_format = VIDEO_GRAY;
	} else {
	    dither_palette(5,9,5);
	    x11_native_format = VIDEO_RGB08;
	}
	break;
    case 15:
    case 16:
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
	    linear_palette(fb_var.red.length,
			   fb_var.green.length,
			   fb_var.blue.length);
#if BYTE_ORDER == BIG_ENDIAN
	x11_native_format = (fb_var.green.length == 6) ?
	    VIDEO_RGB16_BE : VIDEO_RGB15_BE;
#else
	x11_native_format = (fb_var.green.length == 6) ?
	    VIDEO_RGB16_LE : VIDEO_RGB15_LE;
#endif
	break;
    case 24:
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
	    linear_palette(8,8,8);
#if BYTE_ORDER == BIG_ENDIAN
	x11_native_format = VIDEO_RGB24;
#else
	x11_native_format = VIDEO_BGR24;
#endif
	break;
    case 32:
	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
	    linear_palette(8,8,8);
#if BYTE_ORDER == BIG_ENDIAN
	x11_native_format = VIDEO_RGB32;
#else
	x11_native_format = VIDEO_BGR32;
#endif
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		fb_var.bits_per_pixel);
	exit(1);
    }

    /* set colormap */
    if (fb_var.bits_per_pixel == 8 ||
	fb_fix.visual == FB_VISUAL_DIRECTCOLOR) {
	if (-1 == ioctl(fd,FBIOPUTCMAP,&cmap))
	    perror("ioctl FBIOPUTCMAP");
    }
}

static void
tty_init(void)
{
    /* we use curses just for kbd input */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr,1);
}

static void
tty_cleanup(void)
{
    clear();
    refresh();
    endwin();
}


/* ---------------------------------------------------------------------- */

static void
text_init(char *font)
{
    char   *fonts[2] = { font, NULL };

    if (NULL == f)
	f = fs_consolefont(font ? fonts : NULL);
    if (NULL == f) {
	fprintf(stderr,"no font available\n");
	exit(1);
    }
}

static void
text_out(int x, int y, char *str)
{
    y *= f->height;
    y -= f->fontHeader.max_bounds.descent;
    fs_puts(f,x,y,str);
}

static int
text_width(char *str)
{
    return fs_textwidth(f,str);
}

/* ---------------------------------------------------------------------- */

#ifdef HAVE_ALSA
static struct midi_handle fb_midi;
#endif

/* ---------------------------------------------------------------------- */

static void
ctrlc(int signal)
{
    sig=1;
}

#if 0
void
change_audio(int mode)
{
    if (grabber->grab_audio)
	grabber->grab_audio(-1,-1,&mode);
}
#endif

static void do_capture(int from, int to, int tmp_switch)
{
    /* off */
    switch (from) {
    case CAPTURE_GRABDISPLAY:
	if (f_drv & CAN_CAPTURE)
	    drv->stopvideo(h_drv);
	break;
    case CAPTURE_OVERLAY:
	if (f_drv & CAN_CAPTURE)
	    drv->overlay(h_drv,NULL,0,0,NULL,0,0);
	if (matrox && !tmp_switch)
	    gfx_scaler_off();
	break;
    }

    /* on */
    memset(&buf,0,sizeof(buf));
    switch (to) {
    case CAPTURE_GRABDISPLAY:
	if (ww && hh) {
	    dx  = fb_var.xres-fmt.width;
	    dy  = 0;
	    fmt.fmtid  = x11_native_format;
	    fmt.width  = ww;
	    fmt.height = hh;
	    fmt.bytesperline = fb_fix.line_length;
	} else {
	    if (quiet) {
		dx  = 0;
		dy  = 0;
	    } else {
		dx  = f->height*3/2;
		dy  = f->height;
	    }
	    fmt.fmtid  = x11_native_format;
	    fmt.width  = fb_var.xres-dx;
	    fmt.height = fb_var.yres-dy;
	    fmt.bytesperline = fb_fix.line_length;
	}
	if (0 != ng_grabber_setformat(&fmt,1)) {
	    gfmt = fmt;
	    if (NULL == (conv = ng_grabber_findconv(&gfmt,0))) {
		fprintf(stderr,"can't fint useful capture format\n");
		exit(1);
	    }
	    ch = ng_convert_alloc(conv,&gfmt,&fmt);
	    ng_convert_init(ch);
	}
	dx += (fb_var.xres-24-fmt.width)/2;
	dy += (fb_var.yres-16-fmt.height)/2;

	if (f_drv & CAN_CAPTURE)
	    drv->startvideo(h_drv,-1,2);
	break;
    case CAPTURE_OVERLAY:
	fmt.fmtid  = x11_native_format;
	if (ww && hh) {
	    fmt.width  = ww;
	    fmt.height = hh;
	    dx = fb_var.xres-fmt.width;
	    dy = 0;
	} else if (quiet) {
	    fmt.width  = fb_var.xres;
	    fmt.height = fb_var.yres;
	    dx = 0;
	    dy = 0;
	} else {
	    fmt.width  = fb_var.xres-24;
	    fmt.height = fb_var.yres-16;
	    dx = f->height*3/2;
	    dy = f->height;
	}
	if (matrox) {
	    struct ng_video_fmt off;
	    int starty;
#if 1
	    /* FIXME: need some kind of size negotiation */
	    /* hardcoded: PAL, half height (want no interleace) */
	    off.width  = 768;
	    off.height = 288;
	    starty = fb_var.yres;
#else
	    /* settings for debugging */
	    off.width  = 320;
	    off.height = 240;
	    starty = fb_var.yres-off.height;
#endif
	    off.bytesperline = fb_fix.line_length;
	    if (off.width*2 > off.bytesperline)
		off.width = off.bytesperline/2;
	    off.fmtid = VIDEO_YUYV;
	    drv->overlay(h_drv,&off,0,starty,NULL,0,0);
	    gfx_scaler_on(starty*off.bytesperline,off.bytesperline,
			  off.width,off.height,
			  dx,dx+fmt.width,
			  dy,dy+fmt.height);
	} else {
	    drv->overlay(h_drv,&fmt,dx,dy,NULL,0,1);
	}
	break;
    }
}

static void
do_exit(void)
{
    sig = 1;
}

static void
new_title(char *txt)
{
    strcpy(default_title,txt);
}

static void
new_message(char *txt)
{
    strcpy(message,txt);
}

static void
channel_menu(void)
{
    char key[32],ctrl[16],event[64],action[128];
    int  i;

    for (i = 0; i < count; i++) {
	if (channels[i]->key) {
	    if (2 != sscanf(channels[i]->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key))
		strcpy(key,channels[i]->key);
	    sprintf(event,"kbd-key-%s",key);
	    sprintf(action,"setstation \"%s\"",channels[i]->name);
	    event_register(event,action);
	}
    }
}

static void
do_fullscreen(void)
{
    do_va_cmd(2,"capture","off");
    quiet = !quiet;
    fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
    do_va_cmd(2,"capture","on");
}

/*--- main ---------------------------------------------------------------*/

static void
grabber_init(void)
{
    drv = NULL;
    struct ng_video_fmt screen;

    memset(&screen,0,sizeof(screen));
    screen.fmtid        = x11_native_format,
    screen.width        = fb_var.xres_virtual;
    screen.height       = fb_var.yres_virtual;
    screen.bytesperline = fb_fix.line_length;

    drv = ng_vid_open(&ng_dev.video, ng_dev.driver, &screen, 0, &h_drv);
    if (NULL == drv) {
	fprintf(stderr,"no grabber device available\n");
	exit(1);
    }
    f_drv = drv->capabilities(h_drv);
    add_attrs(drv->list_attrs(h_drv));
}

static void
console_switch(void)
{
    switch (fb_switch_state) {
    case FB_REL_REQ:
	if (!keep_dma_on)
	    do_va_cmd(2,"capture","off");
	switch_last = fb_switch_state;
	fb_switch_release();
	break;
    case FB_ACQ_REQ:
	switch_last = fb_switch_state;
	fb_switch_acquire();
	fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
	ioctl(fb,FBIOPAN_DISPLAY,&fb_var);
	do_va_cmd(2,"capture","on");
	break;
    case FB_ACTIVE:
    case FB_INACTIVE:
    default:
	switch_last = fb_switch_state;
	break;
    }
}

#if 0
/* just a hook for some test code */
static void
scaler_test(int off)
{
    if (!matrox) {
	matrox=1;
	if (-1 == gfx_init(fb))
	    matrox = 0;
    }

    if (matrox) {
	gfx_scaler_on(0,fb_fix.line_length,320,240,
		      fb_var.xres-320,fb_var.xres,0,240);
	sleep(2);
    }
}
#endif

int
main(int argc, char *argv[])
{
    int             i,key,c,gray=0,rc,vt=0,fps=0,t1,t2,lirc,js,err,mute=1,fdmax;
    unsigned int    ui;
    unsigned long   freq;
    struct timeval  tv;
    time_t          t;
    char            text[80],event[64],*env,*dst;
    fd_set          set;
    struct sigaction act,old;


    if (0 == geteuid() && 0 != getuid()) {
	fprintf(stderr,"fbtv /must not/ be installed suid root\n");
	exit(1);
    }

    if (NULL != (env = getenv("FBFONT")))
	fontfile = env;
    ng_init();
    for (;;) {
	double val;
	c = getopt(argc, argv, "Mgvqxk2d:o:s:c:f:m:z:t:j:D:");
	if (c == -1)
	    break;
	switch (c) {
	case 'z':
	    if(sscanf(optarg, "%lf", &val) == 1) {
		if(val < 0.1 || val > 10)
		    fprintf(stderr, "gamma value is out of range.  must be "
			    "0.1 < value < 10.0\n");
		else
		    fbgamma = 1.0 / val;
	    }
	    break;
	case 'f':
	    fontfile = optarg;
	    break;
	case 'm':
	    mode = optarg;
	    break;
	case 'g':
	    gray = 1;
	    break;
	case 'M':
	    matrox = 1;
	    break;
	case 'k':
	    keep_dma_on = 1;
	    break;
	case 'v':
	    debug++;
	    ng_debug++;
	    break;
	case 'q':
	    quiet = 1;
	    break;
	case 'd':
	    fbdev = optarg;
	    break;
	case 'o':
	    snapbase = strdup(optarg);
	    break;
	case 's':
	    sscanf(optarg,"%dx%d",&ww,&hh);
	    break;
	case 'c':
	    ng_dev.video = optarg;
	    break;
	case 'D':
	    ng_dev.driver = optarg;
	    break;
	case 't':
	    if (optarg)
		vt = strtoul(optarg, 0, 0);
	    else
		vt = 0;
	    break;
	case 'j':
	    joydev = optarg;
	    break;
	default:
	    exit(1);
	}
    }

    do_overlay = 1;
    text_init(fontfile);
    fb = fb_init(fbdev,mode,vt);
    fb_catch_exit_signals();
    fb_initcolors(fb,gray);
    fb_switch_init();
    switch_last = fb_switch_state;
    fs_init_fb(15);

    if (matrox)
	if (-1 == gfx_init(fb))
	    matrox = 0;
    if (matrox)
	strcat(ng_v4l_conf," -y ");

    grabber_init();
    freq_init();
    read_config(NULL,NULL,NULL);
    if (0 != strlen(mixerdev)) {
	struct ng_attribute *attr;
	if (NULL != (attr = ng_mix_init(mixerdev,mixerctl)))
	    add_attrs(attr);
    }

    /* set hooks (command.c) */
    update_title      = new_title;
    display_message   = new_message;
    set_capture_hook  = do_capture;
    exit_hook         = do_exit;
    fullscreen_hook   = do_fullscreen;

    tty_init();
    memset(&act,0,sizeof(act));
    act.sa_handler = ctrlc;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT,&act,&old);

    /* init hardware */
    attr_init();
    audio_on();
    audio_init();

    /* build channel list */
    parse_config(1);
    channel_menu();

    init_overlay();
    if (optind+1 == argc) {
	do_va_cmd(2,"setstation",argv[optind]);
    } else {
	if ((f_drv & CAN_TUNE) && 0 != (freq = drv->getfreq(h_drv))) {
	    for (i = 0; i < chancount; i++)
		if (chanlist[i].freq == freq*1000/16) {
		    do_va_cmd(2,"setchannel",chanlist[i].name);
		    break;
		}
	}
	if (-1 == cur_channel) {
	    if (count > 0)
		do_va_cmd(2,"setstation","0");
	    else
		set_defaults();
	}
    }

    /* keyboard, lirc + midi + joystick input support */
    event_register_list(kbd_events);
    lirc = lirc_tv_init();
    js = joystick_tv_init(joydev);
#ifdef HAVE_ALSA
    fb_midi.fd = -1;
    if (midi) {
	if (-1 != midi_open(&fb_midi, "fbtv"))
	    midi_connect(&fb_midi,midi);
    }
#endif

    fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
    for (;!sig;) {
	if ((fb_switch_state == FB_ACTIVE || keep_dma_on) && !quiet) {
	    /* clear first lines */
	    fb_memset(fb_mem+fb_mem_offset,0,f->height*fb_fix.line_length);
	    if (message[0] != '\0') {
		strcpy(text,message);
	    } else {
		sprintf(text,"Framebuffer TV - %s",default_title);
	    }
	    /* debugging + preformance monitoring */
	    switch (cur_capture) {
	    case CAPTURE_GRABDISPLAY:
		sprintf(text+strlen(text), " - grab %d.%d fps",fps/5,(fps*2)%10);
		break;
	    }
	    text_out(0,0,text);

	    if (dy > 0) {
		/* display time */
		time(&t);
		strftime(text,16,"%H:%M",localtime(&t));
		text_out(fb_var.xres - text_width(text) - f->width, 0, text);
	    }
	}
	if (switch_last != fb_switch_state)
	    console_switch();

	t1 = time(NULL);
	fps = 0;
	message[0] = '\0';
	for (;!sig;) {
	    FD_ZERO(&set);
	    FD_SET(0,&set);
	    fdmax = 1;
	    if (lirc != -1) {
		FD_SET(lirc,&set);
		fdmax = MAX(fdmax,lirc+1);
	    }
	    if (js != -1) {
		FD_SET(js,&set);
		fdmax = MAX(fdmax,js+1);
	    }
#ifdef HAVE_ALSA
	    if (fb_midi.fd != -1) {
		FD_SET(fb_midi.fd,&set);
		fdmax = MAX(fdmax,fb_midi.fd+1);
	    }
#endif
	    if (cur_capture == CAPTURE_GRABDISPLAY &&
		(fb_switch_state == FB_ACTIVE || keep_dma_on)) {
		fps++;
		/* grab + convert frame */
		if (NULL == (buf = ng_grabber_grab_image(0))) {
		    fprintf(stderr,"capturing image failed\n");
		    exit(1);
		}
		if (ch)
		    buf = ng_convert_frame(ch,NULL,buf);
		/* blit frame */
		dst = fb_mem +
		    dy * fb_fix.line_length +
		    dx * ((fb_var.bits_per_pixel+7)/8);
		for (ui = 0; ui < buf->fmt.height; ui++) {
		    memcpy(dst, buf->data + ui*buf->fmt.bytesperline,
			   buf->fmt.bytesperline);
		    dst += fb_fix.line_length;
		}
		ng_release_video_buf(buf);
		tv.tv_sec  = 0;
		tv.tv_usec = 0;
		rc = select(fdmax,&set,NULL,NULL,&tv);
	    } else {
		tv.tv_sec  = 6;
		tv.tv_usec = 0;
		rc = select(fdmax,&set,NULL,NULL,&tv);
	    }
	    err = errno;
	    if (switch_last != fb_switch_state)
		console_switch();
	    if (-1 == rc  &&  EINTR == err) {
		FD_ZERO(&set);
		continue;
	    }
	    if (rc > 0)
		break;
	    t2 = time(NULL);
	    if (t2 - t1 >= 5) {
		keypad_timeout();
		break;
	    }
	}

	if (FD_ISSET(0,&set)) {
	    /* keyboard input */
	    switch (key = getch()) {
	    case 27: /* ESC */
	    case 'q':
	    case 'Q':
		sig=1;
		break;
	    case 'x':
	    case 'X':
		sig=1;
		mute=0;
		break;
	    case -1:
		break;

#if 0 /* debug */
	    case 'y':
		/* scaler_test(1); */
		do_va_cmd(2,"capture","off");
		do_va_cmd(2,"capture","grab");
		break;
#endif

	    default:
		event[0] = 0;
		if (key > ' ' && key < 127) {
		    /* as is */
		    sprintf(event,"kbd-key-%c",key);
		} else if (key >= KEY_F(0) && key <= KEY_F(12)) {
		    /* function keys */
		    sprintf(event,"kbd-key-f%d",key - KEY_F(0));
		} else {
		    /* other special keys */
		    for (ui = 0; ui < NKEYTAB; ui++) {
			if (keytab[ui].key == key)
			    break;
		    }
		    if (ui != NKEYTAB)
			sprintf(event,"kbd-key-%s",keytab[ui].name);
		}
		if (0 != event[0]) {
		    event_dispatch(event);
		} else {
		    sprintf(message,"unknown key: %d 0x%x ",key,key);
		}

	    }
	}  /* if (FD_ISSET(0,&set)) */

	if (lirc != -1 && FD_ISSET(lirc,&set)) {
	    /* lirc input */
	    if (-1 == lirc_tv_havedata()) {
		fprintf(stderr,"lirc: connection lost\n");
		close(lirc);
		lirc = -1;
	    }
	}

	if (js != -1 && FD_ISSET(js,&set)) {
	    /* joystick input */
	    joystick_tv_havedata(js);
	}

#ifdef HAVE_ALSA
	if (fb_midi.fd != -1 && FD_ISSET(fb_midi.fd,&set)) {
	    /* midi input */
	    midi_read(&fb_midi);
	    midi_translate(&fb_midi);
	}
#endif
    }
    do_va_cmd(2,"capture","off");
    if (mute)
	audio_off();
    drv->close(h_drv);
    if (fb_switch_state == FB_ACTIVE)
	fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
    tty_cleanup();
    fb_cleanup();
    exit(0);
}
