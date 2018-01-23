#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <aalib.h>

#include "grab-ng.h"
#include "capture.h"
#include "channel.h"
#include "commands.h"
#include "frequencies.h"
#include "parseconfig.h"
#include "sound.h"

/* ---------------------------------------------------------------------- */
/* capture stuff                                                          */

static struct aa_hardware_params params;
static aa_renderparams render;
static aa_context *context;
char *framebuffer;
static int signaled;

static char title[256];
static char message[256];
static time_t mtime;
static struct ng_video_fmt   fmt,gfmt;
static struct ng_video_conv  *conv;
static struct ng_convert_handle *ch;
static int fast;

static void
grabber_init(void)
{
    drv = ng_vid_open(&ng_dev.video,ng_dev.driver,NULL,0,&h_drv);
    if (NULL == drv) {
	fprintf(stderr,"no grabber device available\n");
	exit(1);
    }
    f_drv = drv->capabilities(h_drv);
    add_attrs(drv->list_attrs(h_drv));
}

static void
new_title(char *txt)
{
    strcpy(title,txt);
}

static void
new_message(char *txt)
{
    mtime = time(NULL);
    strcpy(message,txt);
}

static void do_capture(int from, int to, int tmp_switch)
{
    /* off */
    switch (from) {
    case CAPTURE_GRABDISPLAY:
	if (f_drv & CAN_CAPTURE)
	    drv->stopvideo(h_drv);
	break;
    }

    /* on */
    switch (to) {
    case CAPTURE_GRABDISPLAY:
	/* try native */
	fmt.fmtid  = VIDEO_GRAY;
	fmt.width  = aa_imgwidth(context);
	fmt.height = aa_imgheight(context);
	if (0 != ng_grabber_setformat(&fmt,1)) {
	    gfmt = fmt;
	    if (NULL == (conv = ng_grabber_findconv(&gfmt,0))) {
		fprintf(stderr,"can't capture gray data\n");
		exit(1);
	    }
	    ch = ng_convert_alloc(conv,&gfmt,&fmt);
	    ng_convert_init(ch);
	}
	if (f_drv & CAN_CAPTURE)
	    drv->startvideo(h_drv,-1,2);
	break;
    }
}

static void blitframe(struct ng_video_buf *buf)
{
    int w,h,y;
    char *s,*d;

#if 0
    /* debug */
    fprintf(stdout,"P5\n%d %d\n255\n",buf->fmt.width,buf->fmt.height);
    fwrite(buf->data,buf->fmt.width,buf->fmt.height,stdout);
    exit(1);
#endif
    if (buf->fmt.width  == aa_imgwidth(context) &&
	buf->fmt.height == aa_imgheight(context)) {
	memcpy(framebuffer,buf->data,buf->fmt.width*buf->fmt.height);
    } else {
	s = buf->data;
	d = framebuffer;
	if (buf->fmt.height < aa_imgheight(context)) {
	    h  = buf->fmt.height;
	    d += (aa_imgheight(context)-buf->fmt.height)/2 *
		aa_imgwidth(context);
	} else {
	    h = aa_imgheight(context);
	    s += (buf->fmt.height-aa_imgheight(context))/2 *
		buf->fmt.width;
	}
	if (buf->fmt.width < aa_imgwidth(context)) {
	    w  = buf->fmt.width;
	    d += (aa_imgwidth(context)-buf->fmt.width)/2;
	} else {
	    w  = aa_imgwidth(context);
	    s += (buf->fmt.width-aa_imgwidth(context))/2;
	}
	for (y = 0; y < h; y++) {
	    memcpy(d,s,w);
	    s += buf->fmt.width;
	    d += aa_imgwidth(context);
	}
    }
}

/* ----------------------------------------------------------------------- */

static void aa_cleanup(void)
{
    aa_uninitkbd(context);
    aa_close(context);
}

static void
ctrlc(int signal)
{
    signaled++;
}

static void
usage(void)
{
    printf("ttv -- watch tv on a ascii terminal using aalib\n"
	   "\n"
	   "ttv options:\n"
	   "  -h             print this text\n"
	   "  -f             use fast aalib render function\n"
	   "  -c <device>    video device [%s]\n"
	   "  -D <driver>    video driver [%s]\n"
	   "\n"
	   "aalib options:\n"
	   "%s",
	   ng_dev.video,ng_dev.driver,aa_help);
    exit(1);
}

int
main(int argc, char **argv)
{
    struct ng_video_buf *buf;
    unsigned long freq;
    int c,i,key,frames,fps;
    time_t now,last;
    struct tm *t;

    ng_init();
    params = aa_defparams;
    render = aa_defrenderparams;
    if (!aa_parseoptions (&params, &render, &argc, argv))
	usage();
    for (;;) {
	c = getopt(argc, argv, "vfhc:D:");
	if (c == -1)
	    break;
	switch (c) {
	case 'v':
	    debug++;
	    ng_debug++;
	    break;
	case 'f':
	    fast++;
	    break;
	case 'c':
	    ng_dev.video = optarg;
	    break;
	case 'D':
	    ng_dev.driver = optarg;
	    break;
	case 'h':
	default:
	    usage();
	    break;
	}
    }

    /* init aalib */
    context = aa_autoinit(&params);
    if (context == NULL) {
	printf("Failed to initialize aalib\n");
	exit (1);
    }
    aa_autoinitkbd(context,0);
    atexit(aa_cleanup);
    framebuffer = aa_image(context);

    /* init v4l */
    grabber_init();
    freq_init();
    read_config(NULL,NULL,NULL);
    ng_ratio_x = 0;
    ng_ratio_y = 0;

    /* init mixer */
    if (0 != strlen(mixerdev)) {
	struct ng_attribute *attr;
	if (NULL != (attr = ng_mix_init(mixerdev,mixerctl)))
	    add_attrs(attr);
    }

    /* set hooks (command.c) */
    update_title      = new_title;
    display_message   = new_message;
    set_capture_hook  = do_capture;

    /* init hardware */
    attr_init();
    audio_on();
    audio_init();

    /* build channel list */
    parse_config(1);
    do_va_cmd(2,"setfreqtab",(-1 != chantab)
	      ? chanlist_names[chantab].str : "europe-west");
    cur_capture = 0;
    do_va_cmd(2,"capture","grabdisplay");
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

    /* catch ^C */
    signal(SIGINT,ctrlc);
    signal(SIGTERM,ctrlc);
    signal(SIGQUIT,ctrlc);

    /* main loop */
    last = now = time(NULL);
    frames = fps = 0;
    for (;!signaled;) {
	/* time + performance */
	now = time(NULL);
	t = localtime(&now);
	frames++;
	if (now - last >= 5) {
	    fps = frames * 10 / (now-last);
	    last = now;
	    frames = 0;
	}

	/* grab + convert frame */
	if (NULL == (buf = ng_grabber_grab_image(0))) {
	    fprintf(stderr,"capturing image failed\n");
	    exit(1);
	}
	if (ch)
	    buf = ng_convert_frame(ch,NULL,buf);

	/* blit frame */
	blitframe(buf);
	ng_release_video_buf(buf);
	if (fast)
	    aa_fastrender(context, 0, 0,
			  aa_scrwidth (context), aa_scrheight (context));
	else
	    aa_render(context, &render, 0, 0,
		      aa_scrwidth (context), aa_scrheight (context));
	if (now - mtime < 6) {
	    aa_printf(context,0,0,AA_NORMAL,"[ %s ] ",
		      message);
	} else {
	    aa_printf(context,0,0,AA_NORMAL,"[ %s - %d.%d fps - %02d:%02d ] ",
		      title,fps/10,fps%10,t->tm_hour,t->tm_min);
	}
	aa_flush(context);

	/* check for keys */
	key = aa_getkey(context,0);
	switch (key) {
	case 'q':
	case 'Q':
	case 'e':
	case 'E':
	case 'x':
	case 'X':
	    audio_off();
	    signaled++;
	    break;
	case 'd':
	case 'D':
	    mtime = time(NULL);
	    sprintf(message,"debug: scr=%dx%d / img=%dx%d / cap=%dx%d",
		    aa_scrwidth(context),aa_scrheight(context),
		    aa_imgwidth(context),aa_imgheight(context),
		    fmt.width,fmt.height);
	    break;
	case '+':
	    do_va_cmd(2,"volume","inc");
	    break;
	case '-':
	    do_va_cmd(2,"volume","dec");
	    break;
	case 10:
	case 13:
	    do_va_cmd(2,"volume","mute");
	    break;
	case AA_UP:
	    do_va_cmd(2,"setchannel","next");
	    break;
	case AA_DOWN:
	    do_va_cmd(2,"setchannel","prev");
	    break;
	case AA_RIGHT:
	    do_va_cmd(2,"setchannel","fine_up");
	    break;
	case AA_LEFT:
	    do_va_cmd(2,"setchannel","fine_down");
	    break;
	case ' ':
	    do_va_cmd(2,"setstation","next");
	    break;
	default:
	    /* nothing */
	    break;
	}
    }
    exit(0);
}
