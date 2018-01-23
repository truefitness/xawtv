/*
 *   (c) 1997-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/wait.h>

#include "grab-ng.h"
#include "writefile.h"
#include "channel.h"
#include "sound.h"
#include "capture.h"
#include "commands.h"

/* ---------------------------------------------------------------------- */

static int       bufcount = 16;
static int       parallel = 1;
static char*     tvnorm = NULL;
static char*     input  = NULL;
static char*     moviename = NULL;
static char*     audioname = NULL;
static char*     vfmt_name;
static char*     afmt_name;

static const struct ng_writer     *writer;
static const void                 *video_priv;
static const void                 *audio_priv;
static struct ng_video_fmt        video = {
    width: 320,
    height: 240,
};
static struct ng_audio_fmt        audio = {
    rate: 44100,
};
static void *movie_state;

static int  absframes = 1;
static int  quiet = 0, fps = 10000;

static int  signaled = 0, wait_seconds = 0;

int debug = 0, have_dga = 0;

/* ---------------------------------------------------------------------- */

static void
list_formats(FILE *out)
{
    struct list_head *item;
    const struct ng_writer *wr;
    int j;

    fprintf(out,"\nmovie writers:\n");
    list_for_each(item,&ng_writers) {
	wr = list_entry(item, struct ng_writer, list);
	fprintf(out,"  %s - %s\n",wr->name,
		wr->desc ? wr->desc : "-");
	if (NULL != wr->video) {
	    fprintf(out,"    video formats:\n");
	    for (j = 0; NULL != wr->video[j].name; j++) {
		fprintf(out,"      %-7s %-28s [%s]\n",wr->video[j].name,
			wr->video[j].desc ? wr->video[j].desc :
			ng_vfmt_to_desc[wr->video[j].fmtid],
			wr->video[j].ext);
	    }
	}
	if (NULL != wr->audio) {
	    fprintf(out,"    audio formats:\n");
	    for (j = 0; NULL != wr->audio[j].name; j++) {
		fprintf(out,"      %-7s %-28s [%s]\n",wr->audio[j].name,
			wr->audio[j].desc ? wr->audio[j].desc :
			ng_afmt_to_desc[wr->audio[j].fmtid],
			wr->audio[j].ext ? wr->audio[j].ext : "-");
	    }
	}
	fprintf(out,"\n");
    }
}

static void
usage(FILE *out)
{
    fprintf(out,
	    "streamer grabs image(s), records movies and sound\n"
	    "\n"
	    "usage: streamer [ options ]\n"
	    "\n"
	    "general options:\n"
	    "  -h          print this help text\n"
	    "  -q          quiet operation\n"
	    "  -d          enable debug output\n"
	    "  -p n        use n compression threads    [%d]\n"
	    "  -w seconds  wait before grabbing         [%d]\n"
	    "\n"
	    "video options:\n"
	    "  -o file     video/movie file name\n"
	    "  -f format   specify video format\n"
	    "  -c device   specify video4linux device   [%s]\n"
	    "  -D driver   specify video4linux driver   [%s]\n"
	    "  -r fps      frame rate                   [%d.%03d]\n"
	    "  -s size     specify size                 [%dx%d]\n"
	    "\n"
	    "  -t times    number of frames or hh:mm:ss [%d]\n"
	    "  -b buffers  specify # of buffers         [%d]\n"
	    "  -j quality  quality for mjpeg or jpeg    [%d]\n"
	    "  -n tvnorm   set pal/ntsc/secam\n"
	    "  -i input    set video source\n"
	    "  -a          don't unmute/mute v4l device.\n"
	    "\n"
	    "audio options:\n"
	    "  -O file     wav file name\n"
	    "  -F format   specify audio format\n"
	    "  -C device   specify dsp device           [%s]\n"
	    "  -R rate     sample rate                  [%d]\n"
	    "\n",

	    parallel,wait_seconds,
	    ng_dev.video, ng_dev.driver, fps/1000, fps%1000,
	    video.width, video.height,
	    absframes, bufcount, ng_jpeg_quality,
	    ng_dev.dsp, audio.rate
	);

    list_formats(out);
    fprintf(out,
	    "If you want to capture to multiple image files you should include some\n"
	    "digits into the movie filename (foo0000.jpeg for example), streamer will\n"
	    "use the digit block to enumerate the image files.\n"
	    "\n"
	    "For file formats which can hold *both* audio and video (like AVI and\n"
	    "QuickTime) the -O option has no effect.\n"
	    "\n"
	    "streamer will use the file extention of the output file name to figure\n"
	    "which format to use.  You need the -f/-F options only if the extention\n"
	    "allows more than one format.\n"
	    "\n"
	    "Examples:\n"
	    "  capture a single frame:\n"
	    "    streamer -o foobar.ppm\n"
	    "\n"
	    "  capture ten frames, two per second:\n"
	    "    streamer -t 10 -r 2 -o foobar00.jpeg\n"
	    "\n"
	    "  record 30 seconds stereo sound:\n"
	    "    streamer -t 0:30 -O soundtrack.wav -F stereo\n"
	    "\n"
	    "  record a quicktime movie with sound:\n"
	    "    streamer -t 0:30 -o movie.mov -f jpeg -F mono16\n"
	    "\n"
	    "  build mpeg movies using mjpegtools + compressed avi file:\n"
	    "    streamer -t 0:30 -s 352x240 -r 24 -o movie.avi -f mjpeg -F stereo\n"
	    "    lav2wav +p movie.avi | mp2enc -o audio.mp2\n"
	    "    lav2yuv +p movie.avi | mpeg2enc -o video.m1v\n"
	    "    mplex audio.mp2 video.m1v -o movie.mpg\n"
	    "\n"
	    "  build mpeg movies using mjpegtools + raw, uncompressed video:\n"
	    "    streamer -t 0:30 -s 352x240 -r 24 -o video.yuv -O audio.wav -F stereo\n"
	    "    mp2enc -o audio.mp2 < audio.wav\n"
	    "    mpeg2enc -o video.m1v < video.yuv\n"
	    "    mplex audio.mp2 video.m1v -o movie.mpg\n"
	    "\n"
	    "-- \n"
	    "(c) 1998-2001 Gerd Knorr <kraxel@bytesex.org>\n");
}

/* ---------------------------------------------------------------------- */

static void
find_formats(void)
{
    struct list_head *item;
    const struct ng_writer *wr = NULL;
    char *mext = NULL;
    char *aext = NULL;
    int v=-1,a=-1;

    if (moviename) {
	mext = strrchr(moviename,'.');
	if (mext)
	    mext++;
    }
    if (audioname) {
	aext = strrchr(audioname,'.');
	if (aext)
	    aext++;
    }
    list_for_each(item,&ng_writers) {
	wr = list_entry(item, struct ng_writer, list);
	if (debug)
	    fprintf(stderr,"checking writer %s [%s] ...\n",wr->name,wr->desc);
	if ((/*!wr->combined && */mext) || NULL != vfmt_name) {
	    if (NULL == wr->video) {
		if (debug)
		    fprintf(stderr,"  no video, skipping\n");
		continue;
	    }
	    for (v = 0; NULL != wr->video[v].name; v++) {
		if (debug)
		    fprintf(stderr,"  video name=%s ext=%s: ",
			    wr->video[v].name,wr->video[v].ext);
		if (mext && 0 != strcasecmp(wr->video[v].ext,mext)) {
		    if (debug)
			fprintf(stderr,"ext mismatch [need %s]\n",mext);
		    continue;
		}
		if (vfmt_name && 0 != strcasecmp(wr->video[v].name,vfmt_name)) {
		    if (debug)
			fprintf(stderr,"name mismatch [need %s]\n",vfmt_name);
		    continue;
		}
		if (debug)
		    fprintf(stderr,"OK\n");
		break;
	    }
	    if (NULL == wr->video[v].name)
		continue;
	}
	if ((!wr->combined && aext) || NULL != afmt_name) {
	    if (NULL == wr->audio) {
		if (debug)
		    fprintf(stderr,"  no audio, skipping\n");
		continue;
	    }
	    for (a = 0; NULL != wr->audio[a].name; a++) {
		if (debug)
		    fprintf(stderr,"  audio name=%s ext=%s: ",
			    wr->audio[a].name,wr->audio[a].ext);
		if (!wr->combined &&
		    aext && 0 != strcasecmp(wr->audio[a].ext,aext)) {
		    if (debug)
			fprintf(stderr,"ext mismatch [need %s]\n",aext);
		    continue;
		}
		if (wr->combined &&
		    mext && 0 != strcasecmp(wr->audio[a].ext,mext)) {
		    if (debug)
			fprintf(stderr,"ext mismatch [need %s]\n",mext);
		    continue;
		}
		if (afmt_name && 0 != strcasecmp(wr->audio[a].name,afmt_name)) {
		    if (debug)
			fprintf(stderr,"name mismatch [need %s]\n",afmt_name);
		    continue;
		}
		if (debug)
		    fprintf(stderr,"OK\n");
		break;
	    }
	    if (NULL == wr->audio[a].name)
		continue;
	}
	break;
    }
    if (item != &ng_writers) {
	writer = wr;
	if (-1 != v) {
	    video.fmtid = wr->video[v].fmtid;
	    video_priv  = wr->video[v].priv;
	}
	if (-1 != a) {
	    audio.fmtid = wr->audio[a].fmtid;
	    audio_priv  = wr->audio[a].priv;
	}
    } else {
	if (debug)
	    fprintf(stderr,"no match found\n");
    }
}

static int
parse_time(char *time)
{
    int hours, minutes, seconds, total=0;

    if (3 == sscanf(time,"%d:%d:%d",&hours,&minutes,&seconds))
	total = hours * 60*60 + minutes * 60 + seconds;
    else if (2 == sscanf(time,"%d:%d",&minutes,&seconds))
	total = minutes * 60 + seconds;

    if (0 != total) {
	/* hh:mm:ss => framecount */
	return total * fps / 1000;
    }

    return atoi(time);
}


/* ---------------------------------------------------------------------- */

static void
do_rec_status(char *message)
{
    if (!quiet)
	fprintf(stderr,"%s  \r",message);
}

static void
ctrlc(int signal)
{
    static char text[] = "^C - one moment please\n";
    if (!quiet)
	write(2,text,strlen(text));
    signaled=1;
}

int
main(int argc, char **argv)
{
    int  c,queued=0,noaudio=0;
    char *raw_length=NULL;

    /* parse options */
    ng_init();
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "haqdp:w:"
			      "o:c:f:r:s:t:n:i:b:j:D:" "O:C:F:R:")))
	    break;
	switch (c) {
	    /* general options */
	case 'q':
	    quiet = 1;
	    break;
	case 'a':
	    noaudio = 1;
	    break;
	case 'd':
	    debug++;
	    ng_debug++;
	    break;
	case 'w':
	    wait_seconds = atoi(optarg);
	    break;
	case 'p':
	    parallel = atoi(optarg);
	    break;

	    /* video options */
	case 'o':
	    moviename = optarg;
	    break;
	case 'f':
	    vfmt_name = optarg;
	    break;
	case 'c':
	    ng_dev.video = optarg;
	    break;
	case 'D':
	    ng_dev.driver = optarg;
	    break;
	case 'r':
	    fps = (int)(atof(optarg) * 1000 + 0.5);
	    break;
	case 's':
	    if (2 != sscanf(optarg,"%dx%d",&video.width,&video.height))
		video.width = video.height = 0;
	    break;

	case 't':
	    raw_length = optarg;
	    break;
	case 'b':
	    bufcount = atoi(optarg);
	    break;
	case 'j':
	    ng_jpeg_quality = atoi(optarg);
	    break;
	case 'n':
	    tvnorm = optarg;
	    break;
	case 'i':
	    input = optarg;
	    break;

	    /* audio options */
	case 'O':
	    audioname = optarg;
	    break;
	case 'F':
	    afmt_name = optarg;
	    break;
	case 'C':
	    ng_dev.dsp = optarg;
	    break;
	case 'R':
	    audio.rate = atoi(optarg);
	    break;

	    /* errors / help */
	case 'h':
	    usage(stdout);
	    exit(0);
	default:
	    usage(stderr);
	    exit(1);
	}
    }
    if (raw_length)
	absframes = parse_time(raw_length);
    find_formats();

    /* sanity checks */
    if (video.fmtid == VIDEO_NONE && audio.fmtid == AUDIO_NONE) {
	fprintf(stderr,"neither audio nor video format specified/found\n");
	exit(1);
    }
    if (NULL == writer) {
	fprintf(stderr,"no output driver found\n");
	exit(1);
    }
    if (audio.fmtid != AUDIO_NONE && !writer->combined && NULL == audioname) {
	fprintf(stderr,"no audio file name specified\n");
	exit(1);
    }

    /* set hooks */
    rec_status = do_rec_status;

    /* open */
    if (writer && !quiet)
	fprintf(stderr,"%s / video: %s / audio: %s\n",writer->name,
		ng_vfmt_to_desc[video.fmtid],ng_afmt_to_desc[audio.fmtid]);

    if (video.fmtid != VIDEO_NONE) {
	drv = ng_vid_open(&ng_dev.video,ng_dev.driver,NULL,0,&h_drv);
	if (NULL == drv) {
	    fprintf(stderr,"no grabber device available\n");
	    exit(1);
	}
	f_drv = drv->capabilities(h_drv);
	add_attrs(drv->list_attrs(h_drv));
	if (!(f_drv & CAN_CAPTURE)) {
	    fprintf(stderr,"%s: capture not supported\n",drv->name);
	    exit(1);
	}
	if (!noaudio)
	    audio_on();
	audio_init();

	/* modify settings */
	if (input != NULL)
	    do_va_cmd(2,"setinput",input);
	if (tvnorm != NULL)
	    do_va_cmd(2,"setnorm",tvnorm);
    }

    /* init movie writer */
    ng_ratio_x = video.width;
    ng_ratio_y = video.height;
    movie_state = movie_writer_init
	(moviename, audioname, writer,
	 &video, video_priv, fps,
	 &audio, audio_priv, ng_dev.dsp,
	 bufcount, parallel);
    if (NULL == movie_state) {
	fprintf(stderr,"movie writer initialisation failed\n");
	if (video.fmtid != VIDEO_NONE) {
	    audio_off();
	    drv->close(h_drv);
	}
	exit(1);
    }

    /* catch ^C */
    signal(SIGINT,ctrlc);

    /* wait for some cameras to wake up and adjust light and all that */
    if (wait_seconds)
	sleep(wait_seconds);

    /* main loop */
    movie_writer_start(movie_state);
    for (;queued < absframes && !signaled;) {
	if (video.fmtid != VIDEO_NONE) {
	    /* video */
	    queued = movie_grab_put_video(movie_state,NULL);
	} else {
	    sleep(1);
	    queued += fps / 1000;
	}
    }
    movie_writer_stop(movie_state);

    /* done */
    if (video.fmtid != VIDEO_NONE) {
	if (!noaudio)
	    audio_off();
	drv->close(h_drv);
    }
    return 0;
}
