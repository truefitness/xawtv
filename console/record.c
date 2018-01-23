#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#include <curses.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#ifdef HAVE_SOUNDCARD_H
# include <soundcard.h>
#endif
#ifdef HAVE_SYS_SOUNDCARD_H
# include <sys/soundcard.h>
#endif

/* -------------------------------------------------------------------- */

static void
tty_raw(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr,1);
    refresh();
}

static void
tty_restore(void)
{
    endwin();
}

/* -------------------------------------------------------------------- */

static int           sound_fd;
static int           sound_rcount;
static unsigned int  sound_blksize;
static int16_t       *sound_buffer;
static int           maxl,maxr;
static int           secl,secr;
static int           *histl,*histr,histn,histi;
static float         peak_seconds = 1.5;
static char          *audio_dev = "/dev/dsp";

static int
sound_open(int rate)
{
    int frag,afmt,channels,trigger,srate;

    if (-1 == (sound_fd = open(audio_dev, O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n",audio_dev,strerror(errno));
	exit(1);
    }

    frag = 0x7fff000d; /* 8k */
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_SETFRAGMENT, &frag))
	perror("ioctl SNDCTL_DSP_SETFRAGMENT");

    /* format */
    afmt = AFMT_S16_LE;
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_SETFMT, &afmt)) {
	perror("ioctl SNDCTL_DSP_SETFMT");
	exit(1);
    }
    if (afmt != AFMT_S16_LE) {
	fprintf(stderr,"can't set sound format to 16 bit (le)\n");
	exit(1);
    }

    /* channels */
    channels = 2;
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_CHANNELS, &channels)) {
	perror("ioctl SNDCTL_DSP_CHANNELS");
	exit(1);
    }
    if (channels != 2) {
	fprintf(stderr,"can't record in stereo\n");
	exit(1);
    }

    /* rate */
    srate = rate;
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_SPEED, &srate)) {
	perror("ioctl SNDCTL_DSP_SPEED");
	exit(1);
    }
    /* accept +/- 1% */
    if (srate < rate *  99 / 100 ||
	srate > rate * 101 / 100) {
	fprintf(stderr,"can't set sample rate to %d (got %d)\n",
		rate,srate);
	exit(1);
    }

    /* get block size */
    if (-1 == ioctl(sound_fd, SNDCTL_DSP_GETBLKSIZE,  &sound_blksize)) {
	perror("ioctl SNDCTL_DSP_GETBLKSIZE");
	exit(1);
    }
    if (0 == sound_blksize)
	sound_blksize = 4096;
    sound_buffer = malloc(sound_blksize);

    /* peak level history */
    histn = peak_seconds * rate * 4 / sound_blksize;
    histl = malloc(histn * sizeof(int));
    histr = malloc(histn * sizeof(int));
    memset(histl,0,histn * sizeof(int));
    memset(histr,0,histn * sizeof(int));

    /* trigger record */
    trigger = ~PCM_ENABLE_INPUT;
    ioctl(sound_fd,SNDCTL_DSP_SETTRIGGER,&trigger);
    trigger = PCM_ENABLE_INPUT;
    ioctl(sound_fd,SNDCTL_DSP_SETTRIGGER,&trigger);

    return sound_fd;
}

static int
sound_read(void)
{
    unsigned int have;
    int     i,rc;
    int16_t *v;

    /* read */
    for (have = 0;have < sound_blksize;) {
	rc = read(sound_fd,sound_buffer+have,sound_blksize-have);
	switch (rc) {
	case -1:
	    if (EINTR != errno) {
		perror("read sound");
		exit(1);
	    }
	    break;
	case 0:
	    fprintf(stderr,"Huh? got 0 bytes from sound device?\n");
	    exit(1);
	default:
	    have += rc;

	}
    }

    /* look for peaks */
    maxl = 0;
    maxr = 0;
    for (i = sound_blksize>>2, v=sound_buffer; i > 0; i--) {
	if (abs(*v) > maxl)
	    maxl = abs(*v);
	v++;
	if (abs(*v) > maxr)
	    maxr = abs(*v);
	v++;
    }

    /* max for the last second */
    histl[histi] = maxl;
    histr[histi] = maxr;
    histi++;
    if (histn == histi)
	histi = 0;

    for (secl = 0, secr = 0, i = 0; i < histn; i++) {
	if (secl < histl[i])
	    secl = histl[i];
	if (secr < histr[i])
	    secr = histr[i];
    }
    sound_rcount++;
    return 0;
}

/* -------------------------------------------------------------------- */

char *names[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;
char *config_names[SOUND_MIXER_NRDEVICES][4];

static int  mix;
static int  dev = -1;
static int  volume;
static char *mixer_dev = "/dev/mixer";

static int
mixer_open(char *filename, char *device)
{
    int i, devmask;

    if (-1 == (mix = open(filename,O_RDONLY))) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	exit(1);
    }
    if (-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask)) {
	perror("mixer read devmask");
	exit(1);
    }
    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask && strcasecmp(names[i],device) == 0) {
	    if (-1 == ioctl(mix,MIXER_READ(i),&volume)) {
		perror("mixer read volume");
		exit(1);
	    } else {
		dev = i;
	    }
	}
    }
    if (-1 == dev) {
	fprintf(stderr,"mixer: havn't found device '%s'\nmixer: available: ",device);
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
	    if ((1<<i) & devmask)
		fprintf(stderr," '%s'",names[i]);
	fprintf(stderr,"\n");
	exit(1);
    }
    return (-1 != dev) ? 0 : -1;
}

static void
mixer_close(void)
{
    close(mix);
    dev = -1;
}

static int
mixer_get_volume(void)
{
    return (-1 == dev) ? -1 : (volume & 0x7f);
}

static int
mixer_set_volume(int val)
{
    if (-1 == dev)
	return -1;
    val   &= 0x7f;
    volume = val | (val << 8);;
    if (-1 == ioctl(mix,MIXER_WRITE(dev),&volume)) {
	perror("mixer write volume");
	return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* *.wav I/O stolen from cdda2wav */

/* Copyright (C) by Heiko Eissfeldt */

typedef uint8_t   BYTE;
typedef uint16_t  WORD;
typedef uint32_t  DWORD;
typedef uint32_t  FOURCC;	/* a four character code */

/* flags for 'wFormatTag' field of WAVEFORMAT */
#define WAVE_FORMAT_PCM 1

/* MMIO macros */
#define mmioFOURCC(ch0, ch1, ch2, ch3) \
  ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
  ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))

#define FOURCC_RIFF	mmioFOURCC ('R', 'I', 'F', 'F')
#define FOURCC_LIST	mmioFOURCC ('L', 'I', 'S', 'T')
#define FOURCC_WAVE	mmioFOURCC ('W', 'A', 'V', 'E')
#define FOURCC_FMT	mmioFOURCC ('f', 'm', 't', ' ')
#define FOURCC_DATA	mmioFOURCC ('d', 'a', 't', 'a')

typedef struct CHUNKHDR {
    FOURCC ckid;		/* chunk ID */
    DWORD dwSize; 		/* chunk size */
} CHUNKHDR;

/* simplified Header for standard WAV files */
typedef struct WAVEHDR {
    CHUNKHDR chkRiff;
    FOURCC fccWave;
    CHUNKHDR chkFmt;
    WORD wFormatTag;	   /* format type */
    WORD nChannels;	   /* number of channels (i.e. mono, stereo, etc.) */
    DWORD nSamplesPerSec;  /* sample rate */
    DWORD nAvgBytesPerSec; /* for buffer estimation */
    WORD nBlockAlign;	   /* block size of data */
    WORD wBitsPerSample;
    CHUNKHDR chkData;
} WAVEHDR;

#define IS_STD_WAV_HEADER(waveHdr) ( \
  waveHdr.chkRiff.ckid == FOURCC_RIFF && \
  waveHdr.fccWave == FOURCC_WAVE && \
  waveHdr.chkFmt.ckid == FOURCC_FMT && \
  waveHdr.chkData.ckid == FOURCC_DATA && \
  waveHdr.wFormatTag == WAVE_FORMAT_PCM)

#define cpu_to_le32(x) (x)
#define cpu_to_le16(x) (x)
#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)

/* -------------------------------------------------------------------- */

static WAVEHDR  fileheader;
static size_t   wav_size;
static size_t   done_size;

static void
wav_init_header(int rate)
{
    /* stolen from cdda2wav */
    int nBitsPerSample = 16;
    int channels = 2;

    unsigned long nBlockAlign = channels * ((nBitsPerSample + 7) / 8);
    unsigned long nAvgBytesPerSec = nBlockAlign * rate;
    unsigned long temp = /* data length */ 0 +
	sizeof(WAVEHDR) - sizeof(CHUNKHDR);

    fileheader.chkRiff.ckid    = cpu_to_le32(FOURCC_RIFF);
    fileheader.fccWave         = cpu_to_le32(FOURCC_WAVE);
    fileheader.chkFmt.ckid     = cpu_to_le32(FOURCC_FMT);
    fileheader.chkFmt.dwSize   = cpu_to_le32(16);
    fileheader.wFormatTag      = cpu_to_le16(WAVE_FORMAT_PCM);
    fileheader.nChannels       = cpu_to_le16(channels);
    fileheader.nSamplesPerSec  = cpu_to_le32(rate);
    fileheader.nAvgBytesPerSec = cpu_to_le32(nAvgBytesPerSec);
    fileheader.nBlockAlign     = cpu_to_le16(nBlockAlign);
    fileheader.wBitsPerSample  = cpu_to_le16(nBitsPerSample);
    fileheader.chkData.ckid    = cpu_to_le32(FOURCC_DATA);
    fileheader.chkRiff.dwSize  = cpu_to_le32(temp);
    fileheader.chkData.dwSize  = cpu_to_le32(0 /* data length */);
}

static void
wav_start_write(int fd,int rate)
{
    wav_init_header(rate);
    lseek(fd,0,SEEK_SET);
    write(fd,&fileheader,sizeof(WAVEHDR));
    wav_size = 0;
}

static int
wav_write_audio(int fd, void *data, int len)
{
    int rc;

    rc = write(fd,data,len);
    if (len == rc) {
	wav_size += len;
	return 0;
    } else
	return -1;
}

static void
wav_stop_write(int fd)
{
    unsigned long temp = wav_size + sizeof(WAVEHDR) - sizeof(CHUNKHDR);

    fileheader.chkRiff.dwSize = cpu_to_le32(temp);
    fileheader.chkData.dwSize = cpu_to_le32(wav_size);
    lseek(fd,0,SEEK_SET);
    write(fd,&fileheader,sizeof(WAVEHDR));
    done_size += wav_size;
}

/* -------------------------------------------------------------------- */

static char full[] =
"##################################################"
"##################################################"
"##################################################"
"##################################################";

static char empty[] =
"--------------------------------------------------"
"--------------------------------------------------"
"--------------------------------------------------"
"--------------------------------------------------";

static char blank[] =
"                                                  "
"                                                  "
"                                                  "
"                                                  ";

static char alive[] = "-\\|/";
//static char alive[] = ".oOo";
#define ALIVE(count)  alive[count % (sizeof(alive)/sizeof(alive[0])-1)]

static void
print_bar(int line, char *name, int val1, int val2, int max)
{
    int total,len;

    total = COLS-16;
    len   = val1*total/max;

    mvprintw(line,0,"%-6s: %5d  ",name,(val2 != -1) ? val2 : val1);
    printw("%*.*s",len,len,full);
    printw("%*.*s",total-len,total-len,empty);
    if (val2 != -1)
	mvprintw(line,14+val2*total/max,"|");
}

/* -------------------------------------------------------------------- */

enum MODE {
    NCURSES = 1,
    CONSOLE = 2,
};
enum MODE mode = NCURSES;
int       stop,verbose;
char      *filename = "record";
int       rate = 44100;

static void
ctrlc(int signal)
{
    if (verbose)
	fprintf(stderr,"\n%s - exiting\n",
		sys_siglist[signal]);
    stop = 1;
}

static int
record_start(char *outfile, int *nr)
{
    int wav;

    do {
	sprintf(outfile,"%s%03d.wav",filename,(*nr)++);
	wav = open(outfile, O_WRONLY | O_EXCL | O_CREAT, 0666);
    } while ((-1 == wav) && (EEXIST == errno));
    if (-1 == wav) {
	perror("open");
	exit(1);
    }
    wav_start_write(wav,rate);
    return wav;
}

static void
record_stop(int fd)
{
    wav_stop_write(fd);
    close(fd);
    switch (mode) {
    case CONSOLE:
	if (verbose)
	    printf("\n");
	break;
    case NCURSES:
	mvprintw(3,0,"%*.*s",COLS-1,COLS-1,blank);
	break;
    }
}

static size_t
parse_size(const char *arg)
{
    int value;
    char mul[4];
    off_t retval = -1;

    if (2 != sscanf(arg,"%d%3s",&value,mul))
	return 0;
    if (0 == strcasecmp(mul,"g") ||
	0 == strcasecmp(mul,"gb"))
	retval = (off_t)value * 1024 * 1024 * 1024;
    if (0 == strcasecmp(mul,"m") ||
	0 == strcasecmp(mul,"mb"))
	retval = (off_t)value * 1024 * 1024;
    if (0 == strcasecmp(mul,"k") ||
	0 == strcasecmp(mul,"kb"))
	retval = (off_t)value * 1024;
    return retval;
}

static char*
str_mb(off_t value)
{
    static char buf[32];

    if (value > (1 << 30)) {
	value = (value * 10) >> 30;
	sprintf(buf,"%d.%d GB",(int)(value/10),(int)(value%10));
	return buf;
    }
    if (value > (1 << 20)) {
	value = (value * 10) >> 20;
	sprintf(buf,"%d.%d MB",(int)(value/10),(int)(value%10));
	return buf;
    }
    value >>= 10;
    sprintf(buf,"%3d kB",(int)value);
    return buf;
}

/* -------------------------------------------------------------------- */

char      *progname;
char      *input = "line";
char      *str_maxsize = "2GB";
int       level_trigger;

static void
usage(FILE *fp)
{
    fprintf(fp,
	    "\n"
	    "%s records sound in CD-Quality (44100/16bit/stereo).\n"
	    "It has a nice ascii-art input-level meter.  It is a\n"
	    "interactive curses application.  You'll need a fast\n"
	    "terminal, don't try this on a 9600 bps vt100...\n"
	    "\n"
	    "%s has several options:\n"
	    "  -h        this text\n"
	    "  -o file   output file basename [%s], a number and the .wav\n"
	    "            extention are added by %s.\n"
	    "  -i ctrl   mixer control [%s].  This should be the one\n"
	    "            where you can adjust the record level for\n"
	    "            your audio source, \"line\", \"mic\" and \"igain\"\n"
	    "            are good candidates.\n"
	    "  -m dev    set mixer device [%s]\n"
	    "  -d dev    set dsp device   [%s]\n"
	    "  -r rate   set sample rate  [%d]\n"
	    "  -p sec    peak seconds     [%.1f]\n"
	    "\n"
	    "for non-interactive usage only:\n"
	    "  -c        enable console (non-interactive) mode\n"
	    "  -v        be verbose (show progress)\n"
	    "  -t mm:ss  limit the time to record.  By default it records\n"
	    "            until stopped by a signal (^C)\n"
	    "  -s size   set max file size [%s]. You have to give number\n"
	    "            and unit without space inbetween, i.e. \"100mb\".\n"
	    "  -n num    limit amount of files recorded, quits when\n"
	    "            reached.\n"
	    "  -l        signal level triggered recording.\n"
	    "  -L level  same as above + specify trigger level [%d]\n"
	    "\n",
	    progname,progname,filename,progname,
	    input,mixer_dev,audio_dev,
	    rate,peak_seconds,str_maxsize,
	    level_trigger ? level_trigger : 1000);
}

int
main(int argc, char *argv[])
{
    int             c,key,vol,delay,auto_adjust;
    int             record,nr,wav=0;
    char            *outfile;
    fd_set          s;
    int             sec,maxhour,maxmin,maxsec;
    int             maxfiles = 0;
    size_t          maxsize;

    /* init some vars */
    progname = strrchr(argv[0],'/');
    progname = progname ? progname+1 : argv[0];
    maxsec  = 0;
    delay   = 0;
    auto_adjust   = 1;
    record = 0;
    nr = 0;

    setlocale(LC_ALL,"");

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "vhlci:o:d:m:r:t:s:L:p:n:")))
	    break;
	switch (c) {
	case 'v':
	    verbose = 1;
	    break;
	case 'l':
	    level_trigger = 1000;
	    break;
	case 'L':
	    level_trigger = atoi(optarg);
	    break;
	case 'i':
	    input = optarg;
	    break;
	case 'o':
	    filename = optarg;
	    break;
	case 'd':
	    audio_dev = optarg;
	    break;
	case 'm':
	    mixer_dev = optarg;
	    break;
	case 'c':
	    mode = CONSOLE;
	    break;
	case 'r':
	    rate = atoi(optarg);
	    break;
	case 'p':
	    peak_seconds = atof(optarg);
	    break;
	case 't':
	    if (3 != sscanf(optarg,"%d:%d:%d",&maxhour,&maxmin,&maxsec)) {
		maxhour = 0;
		if (2 != sscanf(optarg,"%d:%d",&maxmin,&maxsec)) {
		    fprintf(stderr,"time parse error\n");
		    exit(1);
		}
	    }
	    maxsec += maxmin  * 60;
	    maxsec += maxhour * 60 * 60;
	    break;
	case 's':
	    str_maxsize = optarg;
	    break;
	case 'n':
	    maxfiles = atoi(optarg);
	    break;
	case 'h':
	    usage(stdout);
	    exit(0);
	default:
	    usage(stderr);
	    exit(1);
	}
    }
    maxsize = parse_size(str_maxsize);
    if (0 == maxsize) {
	fprintf(stderr,"maxsize parse error [%s]\n",str_maxsize);
	exit(1);
    }

    mixer_open(mixer_dev,input);
    sound_open(rate);
    outfile = malloc(strlen(filename)+16);

    if (mode == NCURSES) {
	tty_raw();
	atexit(tty_restore);
    }

    signal(SIGINT,ctrlc);
    signal(SIGQUIT,ctrlc);
    signal(SIGTERM,ctrlc);
    signal(SIGHUP,ctrlc);

    if (mode == NCURSES) {
	mvprintw( 5,0,"record to   %s*.wav",filename);
	mvprintw( 7,0,"left/right  adjust mixer level for \"%s\"",input);
	mvprintw( 8,0,"space       starts/stops recording");
	/* line 9 is printed later */
	mvprintw(10,0,"            auto-adjust reduces the record level on overruns");
	mvprintw(11,0,"'N'         next file (same as space twice, but without break)");
	mvprintw(12,0,"'Q'         quit");
	mvprintw(LINES-3,0,"--");
	mvprintw(LINES-2,0,"(c) 1999-2003 Gerd Knorr <kraxel@bytesex.org>");

	for (;!stop;) {
	    refresh();
	    FD_ZERO(&s);
	    FD_SET(0,&s);
	    FD_SET(sound_fd,&s);
	    if (-1 == select(sound_fd+1,&s,NULL,NULL,NULL)) {
		if (EINTR == errno)
		    continue;
		perror("select");
		break;
	    }

	    if (FD_ISSET(sound_fd,&s)) {
		/* sound */
		if (-1 == sound_read())
		    break;
		if (delay)
		    delay--;
		if (auto_adjust && (0 == delay) &&
		    (maxl >= 32767 || maxr >= 32767)) {
		    /* auto-adjust */
		    vol = mixer_get_volume();
		    vol--;
		    if (vol < 0)
			vol = 0;
		    mixer_set_volume(vol);
		    delay = 3;
		}
		print_bar(0,input,mixer_get_volume(),-1,100);
		print_bar(1,"left",maxl,secl,32768);
		print_bar(2,"right",maxr,secr,32768);
		mvprintw(9,0,"'A'         toggle auto-adjust [%s] ",
			 auto_adjust ? "on" : "off");
		if (record) {
		    wav_write_audio(wav,sound_buffer,sound_blksize);
		    sec = wav_size / (rate*4);
		    mvprintw(3,0,"%s: %3d:%02d (%s) ",outfile,
			     sec/60,sec%60,str_mb(wav_size));
		} else {
		    mvprintw(3,0,"%c",ALIVE(sound_rcount));
		}
	    }

	    if (FD_ISSET(0,&s)) {
		/* tty in */
		switch (key = getch()) {
		case 'Q':
		case 'q':
		    stop = 1;
		    break;
		case 'A':
		case 'a':
		    auto_adjust = !auto_adjust;
		    break;
		case 'N':
		case 'n':
		    if (record) {
			record_stop(wav);
			wav = record_start(outfile,&nr);
		    }
		    break;
		case ' ':
		    if (!filename)
			break;
		    if (!record) {
			/* start */
			wav = record_start(outfile,&nr);
			record=1;
			auto_adjust=0;
		    } else {
			/* stop */
			record_stop(wav);
			record=0;
		    }
		    break;
		case KEY_RIGHT:
		    vol = mixer_get_volume();
		    vol++;
		    if (vol > 100)
			vol = 100;
		    mixer_set_volume(vol);
		    break;
		case KEY_LEFT:
		    vol = mixer_get_volume();
		    vol--;
		    if (vol < 0)
			vol = 0;
		    mixer_set_volume(vol);
		    break;
		}
	    }
	}
    }

    if (mode == CONSOLE) {
	if (!level_trigger) {
	    wav = record_start(outfile,&nr);
	    record=1;
	}

	for (;!stop;) {
	    if (-1 == sound_read())
		break;
	    if (level_trigger) {
		if (!record &&
		    (maxl > level_trigger ||
		     maxr > level_trigger)) {
		    wav = record_start(outfile,&nr);
		    record=1;
		}
		if (record &&
		    secl < level_trigger &&
		    secr < level_trigger) {
		    record_stop(wav);
		    record=0;
		    if (maxfiles && nr == maxfiles)
			break;
		}
	    }
	    if (!record) {
		printf("waiting for signal %c [%d/%d]...  \r",
		       ALIVE(sound_rcount), maxl,maxr);
		fflush(stdout);
		continue;
	    }

	    sec = (done_size + wav_size) / (rate*4);
	    if (maxsec && sec >= maxsec)
		break;
	    if (wav_size + sound_blksize + sizeof(WAVEHDR) > maxsize) {
		record_stop(wav);
		wav = record_start(outfile,&nr);
	    }
	    wav_write_audio(wav,sound_buffer,sound_blksize);
	    if (verbose) {
		int total = 10;
		int len   = (maxl+maxr)*total/32768/2;
		printf("|%*.*s%*.*s|  %s  %d:%02d",
		       len,len,full, total-len,total-len,empty,
		       outfile,sec/60,sec%60);
		if (maxsec)
		    printf("/%d:%02d",maxsec/60,maxsec%60);
		printf(" (%s",str_mb(wav_size));
		if (done_size)
		    printf(", %s total",str_mb(done_size + wav_size));
		printf(")      \r");
		fflush(stdout);
	    }
	}
    }

    if (record)
	record_stop(wav);
    mixer_close();
    exit(0);
}
