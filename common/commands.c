#define _GNU_SOURCE

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "grab-ng.h"

#include "capture.h"
#include "commands.h"
#include "writefile.h"
#include "channel.h"
#include "webcam.h"
#include "frequencies.h"
#include "sound.h"

/* ----------------------------------------------------------------------- */

/* feedback for the user */
void (*update_title)(char *message);
void (*display_message)(char *message);
void (*rec_status)(char *message);
#if TT
void (*vtx_message)(struct TEXTELEM *tt);
#endif
#ifdef HAVE_ZVBI
void (*vtx_subtitle)(struct vbi_page *pg, struct vbi_rect *rect);
#endif

/* for updating GUI elements / whatever */
void (*attr_notify)(struct ng_attribute *attr, int val);
void (*mute_notify)(int val);
void (*volume_notify)(void);
void (*freqtab_notify)(void);
void (*setfreqtab_notify)(void);
void (*setstation_notify)(void);

/* gets called _before_ channel switches */
void (*channel_switch_hook)(void);

/* capture overlay/grab/off */
void (*set_capture_hook)(int old, int new, int tmp_switch);

/* toggle fullscreen */
void (*fullscreen_hook)(void);
void (*exit_hook)(void);
void (*capture_get_hook)(void);
void (*capture_rel_hook)(void);
void (*movie_hook)(int argc, char **argv);

int debug;
int do_overlay;
char *snapbase = "snap";
int have_shmem;

unsigned int cur_tv_width,cur_tv_height;
int cur_movie,cur_attrs[256];

/* current hardware driver */
const struct ng_vid_driver  *drv;
void                        *h_drv;
int                          f_drv;

struct ng_attribute         *attrs = NULL;


/* ----------------------------------------------------------------------- */

static int setfreqtab_handler(char *name, int argc, char **argv);
static int setstation_handler(char *name, int argc, char **argv);
static int setchannel_handler(char *name, int argc, char **argv);

static int capture_handler(char *name, int argc, char **argv);
static int volume_handler(char *name, int argc, char **argv);
static int attr_handler(char *name, int argc, char **argv);
static int show_handler(char *name, int argc, char **argv);
static int list_handler(char *name, int argc, char **argv);
static int dattr_handler(char *name, int argc, char **argv);

static int snap_handler(char *name, int argc, char **argv);
static int webcam_handler(char *name, int argc, char **argv);
static int movie_handler(char *name, int argc, char **argv);
static int fullscreen_handler(char *name, int argc, char **argv);
static int msg_handler(char *name, int argc, char **argv);
static int showtime_handler(char *name, int argc, char **argv);
static int vdr_handler(char *name, int argc, char **argv);
#if TT
static int vtx_handler(char *name, int argc, char **argv);
#endif
static int exit_handler(char *name, int argc, char **argv);

static int keypad_handler(char *name, int argc, char **argv);

static struct COMMANDS {
    char  *name;
    int    min_args;
    int   (*handler)(char *name, int argc, char **argv);
} commands[] = {
    { "setstation", 0, setstation_handler },
    { "setchannel", 0, setchannel_handler },
    { "setfreq",    1, setchannel_handler },
    { "setfreqtab", 1, setfreqtab_handler },

    { "capture",    1, capture_handler    },

    { "setnorm",    1, attr_handler       },
    { "setinput",   1, attr_handler       },
    { "setattr",    1, attr_handler       },
    { "color",      0, attr_handler       },
    { "hue",        0, attr_handler       },
    { "bright",     0, attr_handler       },
    { "contrast",   0, attr_handler       },
    { "show",       0, show_handler       },
    { "list",       0, list_handler       },

    { "volume",     0, volume_handler     },
    { "attr",       0, dattr_handler      },

    { "snap",       0, snap_handler       },
    { "webcam",     1, webcam_handler     },
    { "movie",      1, movie_handler      },
    { "fullscreen", 0, fullscreen_handler },
    { "msg",        1, msg_handler        },
#if 0
    { "vtx",        0, vtx_handler        },
#endif
    { "message",    0, msg_handler        },
    { "exit",       0, exit_handler       },
    { "quit",       0, exit_handler       },
    { "bye",        0, exit_handler       },

    { "keypad",     1, keypad_handler     },
    { "showtime",   0, showtime_handler   },
    { "vdr",        1, vdr_handler        },

    { NULL, 0, NULL }
};

static int cur_dattr = 0;
static int dattr[] = {
    ATTR_ID_VOLUME,
    ATTR_ID_BRIGHT,
    ATTR_ID_CONTRAST,
    ATTR_ID_COLOR,
    ATTR_ID_HUE
};
#define NUM_DATTR (sizeof(dattr)/sizeof(char*))

static int keypad_state = -1;

/* ----------------------------------------------------------------------- */

void add_attrs(struct ng_attribute *new)
{
    struct ng_attribute *all;
    int nold,nnew;

    if (attrs)
	for (nold = 0; attrs[nold].name != NULL; nold++)
	    ;
    else
	nold = 0;
    for (nnew = 0; new[nnew].name != NULL; nnew++)
	;
    all = malloc(sizeof(struct ng_attribute) * (nold + nnew + 1));
    memset(all,0,sizeof(struct ng_attribute) * (nold + nnew + 1));
    memcpy(all,new,sizeof(struct ng_attribute)*nnew);
    if (attrs) {
	memcpy(all+nnew,attrs,sizeof(struct ng_attribute)*nold);
	free(attrs);
    }
    attrs = all;

#if 0
    {
	int i;
	fprintf(stderr,"  <attr>\n");
	for (i = 0; attrs[i].name != NULL; i++) {
	    fprintf(stderr,"  attr[%p]: %s \n",
		    attrs[i].handle,attrs[i].name);
	}
	fprintf(stderr,"  </attr>\n");
    }
#endif
}

void init_overlay(void)
{
    do_va_cmd(2,"setfreqtab",(-1 != chantab)
	      ? chanlist_names[chantab].str : "europe-west");

    cur_capture = -1;
    switch (defaults.capture) {
    case CAPTURE_ON:
    case CAPTURE_OVERLAY:
	do_va_cmd(2,"capture","overlay");
	break;
    case CAPTURE_GRABDISPLAY:
	do_va_cmd(2,"capture","grabdisplay");
	break;
    default:
	do_va_cmd(2,"capture","off");
	break;
    }
}

/* ----------------------------------------------------------------------- */

int
do_va_cmd(int argc, ...)
{
    va_list ap;
    int  i;
    char *argv[32];

    va_start(ap,argc);
    for (i = 0; i < argc; i++)
	argv[i] = va_arg(ap,char*);
    argv[i] = NULL;
    va_end (ap);
    return do_command(argc,argv);
}

int
do_command(int argc, char **argv)
{
    int i;

    if (argc == 0) {
	fprintf(stderr,"do_command: no argument\n");
	return -1;
    }
    if (debug) {
	fprintf(stderr,"cmd:");
	for (i = 0; i < argc; i++) {
	    fprintf(stderr," \"%s\"",argv[i]);
	}
	fprintf(stderr,"\n");
    }

    for (i = 0; commands[i].name != NULL; i++)
	if (0 == strcasecmp(commands[i].name,argv[0]))
	    break;
    if (commands[i].name == NULL) {
	fprintf(stderr,"no handler for %s\n",argv[0]);
	return -1;
    }
    if (argc-1 < commands[i].min_args) {
	fprintf(stderr,"no enough args for %s\n",argv[0]);
	return -1;
    } else {
	return commands[i].handler(argv[0],argc-1,argv+1);
    }
}

char**
split_cmdline(char *line, int *count)
{
    static char cmdline[1024];
    static char *argv[32];
    int  argc,i;

    strcpy(cmdline,line);
    for (argc=0, i=0; argc<31;) {
	argv[argc++] = cmdline+i;
	while (cmdline[i] != ' ' &&
	       cmdline[i] != '\t' &&
	       cmdline[i] != '\0')
	    i++;
	if (cmdline[i] == '\0')
	    break;
	cmdline[i++] = '\0';
	while (cmdline[i] == ' ' ||
	       cmdline[i] == '\t')
	    i++;
	if (cmdline[i] == '\0')
	    break;
    }
    argv[argc] = NULL;

    *count = argc;
    return argv;
}

/* ----------------------------------------------------------------------- */

/* sharing code does'nt work well for this one ... */
static void
set_capture(int capture, int tmp_switch)
{
    static int last_on = CAPTURE_OVERLAY;

    if (set_capture_hook) {
	if (capture == CAPTURE_ON)
	    capture = last_on;

	if (capture == CAPTURE_OVERLAY) {
	    /* can we do overlay ?? */
	    if (!(f_drv & CAN_OVERLAY))
		capture = CAPTURE_GRABDISPLAY;
	    if (!do_overlay)
		capture = CAPTURE_GRABDISPLAY;
	}

	if (cur_capture != capture) {
	    set_capture_hook(cur_capture,capture,tmp_switch);
	    cur_capture = capture;
	}

	if (cur_capture != CAPTURE_OFF)
	    last_on = cur_capture;
    }
}

static void
set_attr(struct ng_attribute *attr, int val)
{
    if (NULL == attr)
	return;

    attr->write(attr,val);
    cur_attrs[attr->id] = val;
    if (attr_notify)
	attr_notify(attr,val);
}

static void set_volume(int val)
{
    struct ng_attribute *attr;

    cur_attrs[ATTR_ID_VOLUME] = val;
    if ((attr = ng_attr_byid(attrs, ATTR_ID_VOLUME)) != NULL)
	attr->write(attr, val);
}

static void set_mute(int val)
{
    struct ng_attribute *attr;

    cur_attrs[ATTR_ID_MUTE] = val;
    if ((attr = ng_attr_byid(attrs, ATTR_ID_MUTE)) != NULL)
	attr->write(attr, val);

    if (mute_notify)
        mute_notify(val);
}

static void
set_freqtab(int j)
{
    if (!(f_drv & CAN_TUNE))
	return;

    freq_newtab(j);

    /* cur_channel might be invalid (>chancount) right now */
    cur_channel = -1;
    /* this is valid for (struct CHANNEL*)->channel too    */
    calc_frequencies();

    if (freqtab_notify)
	freqtab_notify();
}

static void
set_title(void)
{
    static char  title[256];
    const char *norm;

    keypad_state = -1;
    if (update_title) {
	if (-1 != cur_sender) {
	    sprintf(title,"%s",channels[cur_sender]->name);
	} else if (-1 != cur_channel) {
	    sprintf(title,"channel %s",chanlist[cur_channel].name);
	    if (cur_fine != 0)
		sprintf(title+strlen(title)," (%d)",cur_fine);
	    norm = ng_attr_getstr(ng_attr_byid(attrs,ATTR_ID_NORM),
				  cur_attrs[ATTR_ID_NORM]);
	    sprintf(title+strlen(title)," (%s/%s)",
		    norm ? norm : "???", chanlists[chantab].name);
	} else {
	    sprintf(title,"%.3f MHz",cur_freq/16.0);
	}
	update_title(title);
    }
}

static void
set_msg_int(struct ng_attribute *attr, int val)
{
    static char  title[256];

    if (display_message) {
	sprintf(title,"%s: %d%%",attr->name,
		ng_attr_int2percent(attr,val));
	display_message(title);
    }
}

static void
set_msg_bool(const char *name, int val)
{
    static char  title[256];

    if (display_message) {
	sprintf(title,"%s: %s",name, val ? "on" : "off");
	display_message(title);
    }
}

static void
set_msg_str(const char *name, const char *val)
{
    static char  title[256];

    if (display_message) {
	sprintf(title,"%s: %s",name,val);
	display_message(title);
    }
}

/* ----------------------------------------------------------------------- */

static int update_int(struct ng_attribute *attr, int old, char *new)
{
    int value = old;
    int step;

    step = (attr->max - attr->min) * 3 / 100;
    if (step == 0)
	step = 1;

    if (0 == strcasecmp(new,"inc"))
	value += step;
    else if (0 == strcasecmp(new,"dec"))
	value -= step;
    else if (0 == strncasecmp(new,"+=",2))
	value += ng_attr_parse_int(attr,new+2);
    else if (0 == strncasecmp(new,"-=",2))
	value -= ng_attr_parse_int(attr,new+2);
    else if (isdigit(new[0]) || '+' == new[0] || '-' == new[0])
	value = ng_attr_parse_int(attr,new);
    else
	fprintf(stderr,"update_int: can't parse %s\n",new);

    if (value < attr->min)
	value = attr->min;
    if (value > attr->max)
	value = attr->max;
    return value;
}

/* ----------------------------------------------------------------------- */

void
attr_init(void)
{
    struct ng_attribute *attr;
    int val;

    for (attr = attrs; attr != NULL && attr->name != NULL; attr++) {
	if (attr->id == ATTR_ID_VOLUME ||
	    attr->id == ATTR_ID_MUTE)
	    continue;
	val = attr->read(attr);
	if (attr_notify)
	    attr_notify(attr,val);
	cur_attrs[attr->id] = val;
    }
    if (-1 == defaults.color &&
	NULL != ng_attr_byid(attrs,ATTR_ID_COLOR))
	defaults.color = cur_attrs[ATTR_ID_COLOR];
    if (-1 == defaults.bright &&
	NULL != ng_attr_byid(attrs,ATTR_ID_BRIGHT))
	defaults.bright = cur_attrs[ATTR_ID_BRIGHT];
    if (-1 == defaults.hue &&
	NULL != ng_attr_byid(attrs,ATTR_ID_HUE))
	defaults.hue = cur_attrs[ATTR_ID_HUE];
    if (-1 == defaults.contrast &&
	NULL != ng_attr_byid(attrs,ATTR_ID_CONTRAST))
	defaults.contrast = cur_attrs[ATTR_ID_CONTRAST];
}

void
audio_init(void)
{
    struct ng_attribute *attr;

    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_VOLUME)))
	cur_attrs[ATTR_ID_VOLUME] = attr->read(attr);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_MUTE)))
	cur_attrs[ATTR_ID_MUTE] = attr->read(attr);
    if (volume_notify)
	volume_notify();
}

void audio_on(void)
{
    set_mute(0);
}

void audio_off(void)
{
    set_mute(1);
}

void
set_defaults(void)
{
    struct ng_attribute *attr;

    /* image parameters */
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_COLOR)))
	set_attr(attr,defaults.color);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_BRIGHT)))
	set_attr(attr,defaults.bright);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_HUE)))
	set_attr(attr,defaults.hue);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_CONTRAST)))
	set_attr(attr,defaults.contrast);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_INPUT)))
	set_attr(attr,defaults.input);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_NORM)))
	set_attr(attr,defaults.norm);
    set_capture(defaults.capture,0);

    cur_channel  = defaults.channel;
    cur_fine     = defaults.fine;
    cur_freq     = defaults.freq;
    if (f_drv & CAN_TUNE)
	drv->setfreq(h_drv,defaults.freq);
}

/* ----------------------------------------------------------------------- */

#ifndef HAVE_STRCASESTR
static char* strcasestr(char *haystack, char *needle)
{
    int hlen = strlen(haystack);
    int nlen = strlen(needle);
    int offset;

    for (offset = 0; offset <= hlen - nlen; offset++)
	if (0 == strncasecmp(haystack+offset,needle,nlen))
	    return haystack+offset;
    return NULL;
}
#endif

static int setstation_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr;
    int i, orig_mute;

    if (!(f_drv & CAN_TUNE))
	return 0;

    if (0 == argc) {
	set_title();
	return 0;
    }

    if (cur_movie) {
	if (display_message)
	    display_message("grabber busy");
	return -1;
    }

    if (count && 0 == strcasecmp(argv[0],"next")) {
	i = (cur_sender+1) % count;
    } else if (count && 0 == strcasecmp(argv[0],"prev")) {
	i = (cur_sender+count-1) % count;
    } else if (count && 0 == strcasecmp(argv[0],"back")) {
	if (-1 == last_sender)
	    return -1;
	i = last_sender;
    } else {
	/* search the configured channels first... */
	for (i = 0; i < count; i++)
	    if (0 == strcasecmp(channels[i]->name,argv[0]))
		break;
	/* ... next try substring matches ... */
	if (i == count)
	    for (i = 0; i < count; i++)
		if (NULL != strcasestr(channels[i]->name,argv[0]))
		    break;
	/* ... next try using the argument as index ... */
	if (i == count)
	    if (isdigit(argv[0][0]))
		i = atoi(argv[0]);
	if (i == count) {
	    /* ... sorry folks */
	    fprintf(stderr,"station \"%s\" not found\n",argv[0]);
	    return -1;
	}
    }

    /* ok ?? */
    if (i < 0 || i >= count)
	return -1;

    /* switch ... */
    if (channel_switch_hook)
	channel_switch_hook();
    set_capture(CAPTURE_OFF,1);
    orig_mute = cur_attrs[ATTR_ID_MUTE];
    if (!orig_mute)
	set_mute(1);

    last_sender = cur_sender;
    cur_sender = i;

    /* image parameters */
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_COLOR)))
	set_attr(attr,channels[i]->color);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_BRIGHT)))
	set_attr(attr,channels[i]->bright);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_HUE)))
	set_attr(attr,channels[i]->hue);
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_CONTRAST)))
	set_attr(attr,channels[i]->contrast);

    /* input / norm */
    if (cur_attrs[ATTR_ID_INPUT] != channels[i]->input)
	if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_INPUT)))
	    set_attr(attr,channels[i]->input);
    if (cur_attrs[ATTR_ID_NORM] != channels[i]->norm)
	if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_NORM)))
	    set_attr(attr,channels[i]->norm);

    /* station */
    cur_channel  = channels[i]->channel;
    cur_fine     = channels[i]->fine;
    cur_freq     = channels[i]->freq;
    if (f_drv & CAN_TUNE)
	drv->setfreq(h_drv,channels[i]->freq);
    set_capture(channels[i]->capture,0);

    set_title();
    if (setstation_notify)
	setstation_notify();

    if (!orig_mute) {
	usleep(20000);
	set_mute(0);
    }
    return 0;
}

static int setchannel_handler(char *name, int argc, char **argv)
{
    int i, c, orig_mute;

    if (!(f_drv & CAN_TUNE))
	return 0;

    if (0 == argc) {
	set_title();
	return 0;
    }

    if (cur_movie) {
	if (display_message)
	    display_message("grabber busy");
	return -1;
    }

    if (0 == strcasecmp("setfreq",name)) {
	cur_freq = (unsigned long)(atof(argv[0])*16);
	cur_sender = -1;
	cur_channel = -1;
	cur_fine = 0;
    } else {
	if (0 == strcasecmp(argv[0],"next")) {
	    cur_channel = (cur_channel+1) % chancount;
	    cur_fine = defaults.fine;
	} else if (0 == strcasecmp(argv[0],"prev")) {
	    cur_channel = (cur_channel+chancount-1) % chancount;
	    cur_fine = defaults.fine;
	} else if (0 == strcasecmp(argv[0],"fine_up")) {
	    cur_fine++;
	} else if (0 == strcasecmp(argv[0],"fine_down")) {
	    cur_fine--;
	} else {
	    if (-1 != (c = lookup_channel(argv[0]))) {
		cur_channel = c;
		cur_fine = defaults.fine;
	    }
	}

	if (0 != strncmp(argv[0],"fine",4)) {
	    /* look if there is a known station on that channel */
	    for (i = 0; i < count; i++) {
		if (cur_channel == channels[i]->channel) {
		    char *argv[2];
		    argv[0] = channels[i]->name;
		    argv[1] = NULL;
		    return setstation_handler("", argc, argv);
		}
	    }
	}
	cur_sender  = -1;
	if (-1 != cur_channel)
	    cur_freq = get_freq(cur_channel)+cur_fine;
	else {
	    cur_freq += cur_fine;
	    cur_fine = 0;
	}
    }

    if (channel_switch_hook)
	channel_switch_hook();
    set_capture(CAPTURE_OFF,1);
    orig_mute = cur_attrs[ATTR_ID_MUTE];
    if (!orig_mute)
	set_mute(1);

    if (f_drv & CAN_TUNE)
	drv->setfreq(h_drv,cur_freq);
    set_capture(defaults.capture,0);

    set_title();
    if (setstation_notify)
	setstation_notify();

    if (!orig_mute) {
	usleep(20000);
	set_mute(0);
    }
    return 0;
}

/* ----------------------------------------------------------------------- */

static void
print_choices(char *name, char *value, struct STRTAB *tab)
{
    int i;

    fprintf(stderr,"unknown %s: '%s' (available: ",name,value);
    for (i = 0; tab[i].str != NULL; i++)
	fprintf(stderr,"%s'%s'", (0 == i) ? "" : ", ", tab[i].str);
    fprintf(stderr,")\n");
}

static int setfreqtab_handler(char *name, int argc, char **argv)
{
    int i;

    if (!(f_drv & CAN_TUNE))
	return 0;

    i = str_to_int(argv[0],chanlist_names);
    if (i != -1)
	set_freqtab(i);
    else
	print_choices("freqtab",argv[0],chanlist_names);
    return 0;
}

static int capture_handler(char *name, int argc, char **argv)
{
    int i, temp = 0;

    if (0 == strcasecmp(argv[0],"toggle")) {
	i = (cur_capture == CAPTURE_OFF) ? CAPTURE_ON : CAPTURE_OFF;
    } else {
	i = str_to_int(argv[0],captab);
    }
    if (argc == 2 && !strcasecmp(argv[1], "temp"))
	temp = 1;
    if (i != -1)
	set_capture(i, temp);
    return 0;
}

/* ----------------------------------------------------------------------- */

static int volume_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *vol = ng_attr_byid(attrs,ATTR_ID_VOLUME);

    if (0 == argc)
	goto display;

    if (0 == strcasecmp(argv[0],"mute")) {
	/* mute on/off/toggle */
	if (argc > 1) {
	    switch (str_to_int(argv[1],booltab)) {
	    case 0:  set_mute(0); break;
	    case 1:  set_mute(1); break;
	    default: set_mute(!cur_attrs[ATTR_ID_MUTE]);
	    }
	} else {
	    set_mute(!cur_attrs[ATTR_ID_MUTE]);
	}
    } else if (vol) {
	/* volume */
	set_volume(update_int(vol, vol->read(vol), argv[0]));
    }
    if (volume_notify)
	volume_notify();

 display:
    if (cur_attrs[ATTR_ID_MUTE])
	set_msg_str("volume","muted");
    else {
	if (vol)
	    set_msg_int(vol,cur_attrs[ATTR_ID_VOLUME]);
	else
	    set_msg_str("volume","unmuted");
    }
    return 0;
}

static int attr_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr;
    int val,arg=0;

    if (0 == strcasecmp(name,"setnorm")) {
	attr = ng_attr_byname(attrs,"norm");

    } else if (0 == strcasecmp(name,"setinput")) {
	attr = ng_attr_byname(attrs,"input");

    } else if (0 == strcasecmp(name,"setattr") &&
	       argc > 0) {
	attr = ng_attr_byname(attrs,argv[arg++]);

    } else {
	attr = ng_attr_byname(attrs,name);
    }

    if (NULL == attr) {
	fprintf(stderr,"cmd: %s: attribute not found\nvalid choices are:",
		(arg > 0) ? argv[0] : name);
	for (attr = attrs; attr->name != NULL; attr++)
	    fprintf(stderr,"%s \"%s\"",
		    (attr != attrs) ? "," : "", attr->name);
	fprintf(stderr,"\n");
	return -1;
    }

    if (!cur_movie && capture_get_hook)
	capture_get_hook();
    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	if (argc > arg) {
	    if (0 == strcasecmp("next", argv[arg])) {
		val = cur_attrs[attr->id];
		val++;
		if (NULL == attr->choices[val].str)
		    val = 0;
	    } else {
		val = ng_attr_getint(attr, argv[arg]);
	    }
	    if (-1 == val) {
		fprintf(stderr,"invalid value for %s: %s\n",attr->name,argv[arg]);
		ng_attr_listchoices(attr);
	    } else {
		set_attr(attr,val);
		set_msg_str(attr->name,attr->choices[val].str);
	    }
	}
	break;
    case ATTR_TYPE_INTEGER:
	if (argc > arg) {
	    cur_attrs[attr->id] = attr->read(attr);
	    val = update_int(attr,cur_attrs[attr->id],argv[arg]);
	    set_attr(attr,val);
	}
	set_msg_int(attr,cur_attrs[attr->id]);
	break;
    case ATTR_TYPE_BOOL:
	if (argc > arg) {
	    val = str_to_int(argv[arg],booltab);
	    if (-1 == val) {
		if (0 == strcasecmp(argv[arg],"toggle"))
		    val = !cur_attrs[attr->id];
	    }
	    set_attr(attr,val);
	}
	set_msg_bool(attr->name,cur_attrs[attr->id]);
	break;
    }
    if (!cur_movie && capture_rel_hook)
	capture_rel_hook();
    return 0;
}

static int show_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr;
    char *n[2] = { NULL, NULL };
    int val;

    if (0 == argc) {
	for (attr = attrs; attr->name != NULL; attr++) {
	    n[0] = (char*)attr->name;
	    show_handler("show", 1, n);
	}
	return 0;
    }

    attr = ng_attr_byname(attrs,argv[0]);
    if (NULL == attr) {
	fprintf(stderr,"fixme: 404 %s\n",argv[0]);
	return 0;
    }
    val = cur_attrs[attr->id];
    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	printf("%s: %s\n", attr->name, ng_attr_getstr(attr,val));
	break;
    case ATTR_TYPE_INTEGER:
	printf("%s: %d\n", attr->name, val);
	break;
    case ATTR_TYPE_BOOL:
	printf("%s: %s\n", attr->name, val ? "on" : "off");
	break;
    }
    return 0;
}

static int list_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr;
    int val,i;

    printf("%-10.10s | type   | %-7.7s | %-7.7s | %s\n",
	   "attribute","current","default","comment");
    printf("-----------+--------+---------+--------"
	   "-+-------------------------------------\n");
    for (attr = attrs; attr->name != NULL; attr++) {
	val = cur_attrs[attr->id];
	switch (attr->type) {
	case ATTR_TYPE_CHOICE:
	    printf("%-10.10s | choice | %-7.7s | %-7.7s |",
		   attr->name,
		   ng_attr_getstr(attr,val),
		   ng_attr_getstr(attr,attr->defval));
	    for (i = 0; attr->choices[i].str != NULL; i++)
		printf(" %s",attr->choices[i].str);
	    printf("\n");
	    break;
	case ATTR_TYPE_INTEGER:
	    printf("%-10.10s | int    | %7d | %7d | range is %d => %d\n",
		   attr->name, val, attr->defval,
		   attr->min, attr->max);
	    break;
	case ATTR_TYPE_BOOL:
	    printf("%-10.10s | bool   | %-7.7s | %-7.7s |\n",
		   attr->name,
		   val ? "on" : "off",
		   attr->defval ? "on" : "off");
	    break;
	}
    }
    return 0;
}

static int dattr_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr = NULL;
    unsigned int i;

    if (argc > 0 && 0 == strcasecmp(argv[0],"next")) {
	for (i = 0; i < NUM_DATTR; i++) {
	    cur_dattr++;
	    cur_dattr %= NUM_DATTR;
	    attr = ng_attr_byid(attrs,dattr[cur_dattr]);
	    if (NULL != attr)
		break;
	}
	if (NULL == attr)
	    return 0;
	argc = 0;
    }
    if (NULL == attr)
	attr = ng_attr_byid(attrs,dattr[cur_dattr]);
    if (NULL == attr)
	return 0;
    return attr_handler((char*)attr->name,argc,argv);
}

/* ----------------------------------------------------------------------- */

static int snap_handler(char *hname, int argc, char **argv)
{
    char message[512];
    char *tmpfilename = NULL;
    char *filename = NULL;
    char *name;
    int   jpeg = 0;
    int   ret = 0;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf = NULL;

    if (!(f_drv & CAN_CAPTURE)) {
	fprintf(stderr,"grabbing: not supported [try -noxv switch?]\n");
	return -1;
    }

    if (cur_movie) {
	if (display_message)
	    display_message("grabber busy");
	return -1;
    }

    if (capture_get_hook)
	capture_get_hook();

    /* format */
    if (argc > 0) {
	if (0 == strcasecmp(argv[0],"jpeg"))
	    jpeg = 1;
	if (0 == strcasecmp(argv[0],"ppm"))
	    jpeg = 0;
    }

    /* size */
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = 2048;
    fmt.height = 1572;
    if (argc > 1) {
	if (0 == strcasecmp(argv[1],"full")) {
	    /* nothing */
	} else if (0 == strcasecmp(argv[1],"win")) {
	    fmt.width  = cur_tv_width;
	    fmt.height = cur_tv_height;
	} else if (2 == sscanf(argv[1],"%dx%d",&fmt.width,&fmt.height)) {
	    /* nothing */
	} else {
	    return -1;
	}
    }

    /* filename */
    if (argc > 2)
	filename = argv[2];

    if (NULL == (buf = ng_grabber_get_image(&fmt))) {
	if (display_message)
	    display_message("grabbing failed");
	ret = -1;
	goto done;
    }
    buf = ng_filter_single(cur_filter,buf);

    if (NULL == filename) {
	if (-1 != cur_sender) {
	    name = channels[cur_sender]->name;
	} else if (-1 != cur_channel) {
	    name = chanlist[cur_channel].name;
	} else {
	    name = "unknown";
	}
	filename = snap_filename(snapbase, name, jpeg ? "jpeg" : "ppm");
    }
    tmpfilename = malloc(strlen(filename)+8);
    sprintf(tmpfilename,"%s.$$$",filename);

    if (jpeg) {
	if (-1 == write_jpeg(tmpfilename, buf, ng_jpeg_quality, 0)) {
	    sprintf(message,"open %s: %s\n",tmpfilename,strerror(errno));
	} else {
	    sprintf(message,"saved jpeg: %s",filename);
	}
    } else {
	if (-1 == write_ppm(tmpfilename, buf)) {
	    sprintf(message,"open %s: %s\n",tmpfilename,strerror(errno));
	} else {
	    sprintf(message,"saved ppm: %s",filename);
	}
    }
    unlink(filename);
    if (-1 == link(tmpfilename,filename)) {
	fprintf(stderr,"link(%s,%s): %s\n",
		tmpfilename,filename,strerror(errno));
	goto done;
    }
    unlink(tmpfilename);
    if (display_message)
	display_message(message);

done:
    if (tmpfilename)
	free(tmpfilename);
    if (NULL != buf)
	ng_release_video_buf(buf);
    if (capture_rel_hook)
	capture_rel_hook();
    return ret;
}

static int webcam_handler(char *hname, int argc, char **argv)
{
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;

    if (webcam)
	free(webcam);
    webcam = strdup(argv[0]);

    /* if either avi recording or grabdisplay is active, we do
       /not/ stop capture and switch the video format.  The next
       capture will send a copy of the frame to the webcam thread
       and it has to deal with it as-is */
    if (cur_movie)
	return 0;
    if (cur_capture == CAPTURE_GRABDISPLAY)
	return 0;

    /* if no capture is running we can switch to RGB first to make
       the webcam happy */
    if (capture_get_hook)
	capture_get_hook();
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = cur_tv_width;
    fmt.height = cur_tv_height;
    buf = ng_grabber_get_image(&fmt);
    if (buf)
	ng_release_video_buf(buf);
    if (capture_rel_hook)
	capture_rel_hook();
    return 0;
}

static int movie_handler(char *name, int argc, char **argv)
{
    if (!movie_hook)
	return 0;
    movie_hook(argc,argv);
    return 0;
}

static int
fullscreen_handler(char *name, int argc, char **argv)
{
    if (fullscreen_hook)
	fullscreen_hook();
    return 0;
}

static int
msg_handler(char *name, int argc, char **argv)
{
    if (display_message)
	display_message(argv[0]);
    return 0;
}

static int
showtime_handler(char *name, int argc, char **argv)
{
    char timestr[6];
    struct tm *times;
    time_t timet;

    timet = time(NULL);
    times = localtime(&timet);
    strftime(timestr, 6, "%k:%M", times);
    if (display_message)
	display_message(timestr);
    return 0;
}

static int
exit_handler(char *name, int argc, char **argv)
{
    if (exit_hook)
	exit_hook();
    return 0;
}

/* ----------------------------------------------------------------------- */

static char *strfamily(int family)
{
    switch (family) {
    case PF_INET6: return "ipv6";
    case PF_INET:  return "ipv4";
    case PF_UNIX:  return "unix";
    }
    return "????";
}

static int
tcp_connect(struct addrinfo *ai, char *host, char *serv)
{
    struct addrinfo *res,*e;
    char uhost[INET6_ADDRSTRLEN+1];
    char userv[33];
    int sock,rc,opt=1;

    ai->ai_flags = AI_CANONNAME;
    if (debug)
	fprintf(stderr,"tcp: lookup %s:%s ... ",host,serv);
    if (0 != (rc = getaddrinfo(host, serv, ai, &res))) {
	fprintf(stderr,"tcp: getaddrinfo (%s:%s): %s\n",
		host,serv,gai_strerror(rc));
	return -1;
    }
    if (debug)
	fprintf(stderr,"ok\n");
    for (e = res; e != NULL; e = e->ai_next) {
	if (0 != getnameinfo((struct sockaddr*)e->ai_addr,e->ai_addrlen,
			     uhost,INET6_ADDRSTRLEN,userv,32,
			     NI_NUMERICHOST | NI_NUMERICSERV)) {
	    fprintf(stderr,"tcp: getnameinfo (peer): oops\n");
	    continue;
	}
	if (debug)
	    fprintf(stderr,"tcp: trying %s (%s:%s) ... ",
		    strfamily(e->ai_family),uhost,userv);
	if (-1 == (sock = socket(e->ai_family, e->ai_socktype,
				 e->ai_protocol))) {
	    fprintf(stderr,"tcp: socket: %s\n",strerror(errno));
	    continue;
	}
	setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
	if (-1 == connect(sock,e->ai_addr,e->ai_addrlen)) {
	    fprintf(stderr,"tcp: connect: %s\n",strerror(errno));
	    close(sock);
	    continue;
	}
	if (debug)
	    fprintf(stderr,"ok\n");
	fcntl(sock,F_SETFL,O_NONBLOCK);
	fcntl(sock,F_SETFD,FD_CLOEXEC);
	return sock;
    }
    return -1;
}

static int tcp_readbuf(int sock, int timeout, char *dest, char dlen)
{
    struct timeval tv;
    fd_set set;
    int rc;

 again:
    FD_ZERO(&set);
    FD_SET(sock,&set);
    tv.tv_sec  = timeout;
    tv.tv_usec = 0;
    rc = select(sock+1,&set,NULL,NULL,&tv);
    if (-1 == rc && EINTR == errno)
	goto again;
    if (-1 == rc) {
	if (debug)
	    perror("tcp: select");
	return -1;
    }
    if (0 == rc) {
	if (debug)
	    fprintf(stderr,"tcp: select timeout\n");
	return -1;
    }
    rc = read(sock,dest,dlen-1);
    if (-1 == rc) {
	if (debug)
	    perror("tcp: read");
	return -1;
    }
    dest[rc] = 0;
    return rc;
}

static int vdr_sock = -1;

static int
vdr_handler(char *name, int argc, char **argv)
{
    char line[80];
    struct addrinfo ask;
    int i,rc;
    unsigned int l,len;

 reconnect:
    if (-1 == vdr_sock) {
	memset(&ask,0,sizeof(ask));
	ask.ai_family = PF_UNSPEC;
	ask.ai_socktype = SOCK_STREAM;
	vdr_sock = tcp_connect(&ask,"localhost","2001");
	if (-1 == vdr_sock)
	    return -1;
	if (debug)
	    fprintf(stderr,"vdr: connected\n");

	/* skip greeting line */
	if (-1 == tcp_readbuf(vdr_sock,3,line,sizeof(line)))
	    goto oops;
	if (debug)
	    fprintf(stderr,"vdr: << %s",line);
    }

    /* send command */
    line[0] = 0;
    for (i = 0, len = 0; i < argc; i++) {
	l = strlen(argv[i]);
	if (len+l+4 > sizeof(line))
	    break;
	if (len) {
	    strcpy(line+len," ");
	    len++;
	}
	strcpy(line+len,argv[i]);
	len += l;
    }
    strcpy(line+len,"\r\n");
    len += 2;
    if (len != (rc = write(vdr_sock,line,len))) {
	if (-1 == rc  &&  EPIPE == errno) {
	    if (debug)
		fprintf(stderr,"tcp: write: broken pipe, trying reconnect\n");
	    close(vdr_sock);
	    vdr_sock = -1;
	    goto reconnect;
	}
	if (debug)
	    perror("tcp: write");
	goto oops;
    }
    if (debug)
	fprintf(stderr,"vdr: >> %s",line);

    /* skip answer */
    if (-1 == tcp_readbuf(vdr_sock,3,line,sizeof(line)))
	goto oops;
    if (debug)
	fprintf(stderr,"vdr: << %s",line);

#if 0
    /* play nicely and close the handle -- vdr can handle only one
     * connection at the same time.  Drawback is that it increases
     * latencies ... */
    close(vdr_sock);
    vdr_sock = -1;
#endif
    return 0;

oops:
    close(vdr_sock);
    vdr_sock = -1;
    return -1;
}

/* ----------------------------------------------------------------------- */

#if TT
static struct TEXTELEM*
parse_vtx(int lines, char **text)
{
    static char *names[8] = { "black", "red", "green", "yellow",
			      "blue", "magenta", "cyan", "white" };
    static struct TEXTELEM tt[VTX_COUNT];
    int i,n,t,ansi;
    char *ansi_fg,*ansi_bg;

    /* parse */
    t = 0;
    memset(tt,0,sizeof(tt));
    for (i = 0; i < lines; i++) {
	tt[t].line = i;
	ansi_fg = NULL; ansi_bg = NULL;
	for (n = 0; text[i][n] != 0;) {
	    if (text[i][n] == '\033') {
		if (tt[t].len) {
		    t++;
		    if (VTX_COUNT == t)
			return tt;
		}
		n++;
		if (text[i][n] == '[') {
		    /* ANSI color tty sequences */
		    n++;
		    for (ansi=1;ansi;) {
			switch (text[i][n]) {
			case '3':
			    n++;
			    if (text[i][n] >= '0' && text[i][n] < '8') {
				ansi_fg  = names[text[i][n]-'0'];
				n++;
			    }
			    break;
			case '4':
			    n++;
			    if (text[i][n] >= '0' && text[i][n] < '8') {
				ansi_bg  = names[text[i][n]-'0'];
				n++;
			    }
			    break;
			case '1':
			case ';':
			    n++;
			    break;
			case 'm':
			    n++;
			    /* ok, commit */
			    ansi=0;
			    tt[t].fg = ansi_fg;
			    tt[t].bg = ansi_bg;
			    break;
			default:
			    /* error */
			    ansi=0;
			}
		    }
		} else {
		    /* old way: ESC fg bg */
		    if (text[i][n] >= '0' && text[i][n] < '8') {
			tt[t].fg  = names[text[i][n]-'0'];
			n++;
		    }
		    if (text[i][n] >= '0' && text[i][n] < '8') {
			tt[t].bg  = names[text[i][n]-'0'];
			n++;
		    }
		}
		tt[t].line = i;
	    } else {
		tt[t].str[tt[t].len++] = text[i][n];
		n++;
		if (tt[t].len >= VTX_LEN-1) {
		    t++;
		    if (VTX_COUNT == t)
			return tt;
		    tt[t].line = i;
		}
	    }
	}
	if (tt[t].len) {
	    t++;
	    if (VTX_COUNT == t)
		break;
	}
    }
    return tt;
}

static int
vtx_handler(char *name, int argc, char **argv)
{
    struct TEXTELEM *tt;

    if (vtx_message) {
	if (argc) {
	    tt = parse_vtx(argc,argv);
	    vtx_message(tt);
	} else {
	    vtx_message(NULL);
	}
    }
    return 0;
}
#endif

/* ----------------------------------------------------------------------- */

#define CH_MAX (keypad_ntsc ? 99 : count)

static int
keypad_handler(char *name, int argc, char **argv)
{
    int n = atoi(argv[0])%10;
    char msg[8],ch[8];

    if (debug)
	fprintf(stderr,"keypad: key %d\n",n);
    if (-1 == keypad_state) {
	if ((keypad_partial   &&  n > 0 && n <= CH_MAX) ||
	    (!keypad_partial  &&  n > 0 && n <= CH_MAX && 10*n > CH_MAX)) {
	    if (keypad_ntsc) {
		sprintf(ch,"%d",n);
		do_va_cmd(2,"setchannel",ch,NULL);
	    } else
		do_va_cmd(2,"setstation",channels[n-1]->name,NULL);
	}
	if (n >= 0 && 10*n <= CH_MAX) {
	    if (debug)
		fprintf(stderr,"keypad: hang: %d\n",n);
	    keypad_state = n;
	    if (display_message) {
		sprintf(msg,"%d_",n);
		display_message(msg);
	    }
	}
    } else {
	if ((n+keypad_state*10) <= CH_MAX)
	    n += keypad_state*10;
	keypad_state = -1;
	if (debug)
	    fprintf(stderr,"keypad: ok: %d\n",n);
	if (n > 0 && n <= CH_MAX) {
	    if (keypad_ntsc) {
		sprintf(ch,"%d",n);
		do_va_cmd(2,"setchannel",ch,NULL);
	    } else
		do_va_cmd(2,"setstation",channels[n-1]->name,NULL);
	}
    }
    return 0;
}

void
keypad_timeout(void)
{
    if (debug)
	fprintf(stderr,"keypad: timeout\n");
    if (keypad_state == cur_sender+1)
	set_title();
    keypad_state = -1;
}
