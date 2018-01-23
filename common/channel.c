/*
    channel for Bt848 frame grabber driver

    Copyright (C) 1996,97 Marcus Metzler (mocm@thp.uni-koeln.de)
	      (c) 1998-2003 Gerd Knorr <kraxel@bytesex.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>

#ifndef NO_X11
# include <X11/Xlib.h>
# include <X11/Intrinsic.h>
# include <X11/StringDefs.h>
# include <X11/Xaw/XawInit.h>
# include <X11/Xaw/Command.h>
# include <X11/Xaw/Paned.h>
#endif

#include "grab-ng.h"
#include "channel.h"
#include "commands.h"
#include "frequencies.h"
#include "sound.h"
#include "parseconfig.h"
#include "event.h"

/* ----------------------------------------------------------------------- */
/* misc common stuff, not only channel related                             */

struct CHANNEL defaults = {
    name:     "defaults",
    group:    "main",
    capture:  CAPTURE_ON,
    channel:  -1,
    audio:    -1,
    color:    -1,
    bright:   -1,
    hue:      -1,
    contrast: -1,
};

struct CHANNEL  **channels  = NULL;
int             count       = 0;
int             alloc_count = 0;

int last_sender = -1, cur_sender = -1, cur_channel = -1, cur_fine = 0;
int cur_freq;
struct ng_filter *cur_filter;

int cur_capture = CAPTURE_OFF;
int have_config;
int keypad_ntsc = 0;
int keypad_partial = 1;
int use_wm_fullscreen = 1;
int use_osd = 1;
int osd_x = 30;
int osd_y = 20;
int fs_width,fs_height,fs_xoff,fs_yoff;
int pix_width=128, pix_height=96, pix_cols=1;

char *mov_driver = NULL;
char *mov_video  = NULL;
char *mov_fps    = NULL;
char *mov_audio  = NULL;
char *mov_rate   = NULL;

#ifndef NO_X11
extern Widget chan_box, chan_viewport, tv, opt_paned, launch_paned;
#endif

static char *mixer = NULL;
char mixerdev[32],mixerctl[16];
char *midi = NULL;

struct LAUNCH *launch = NULL;
int nlaunch           = 0;

/* ----------------------------------------------------------------------- */

int lookup_channel(char *channel)
{
#if 0
    /* Hmm, why the heck that used to be that complex?
     * Any good reason I forgot ? */
    int    i,nr1,nr2;
    char   tag1[5],tag2[5];

    if (NULL == channel)
	return -1;

    if (isdigit(channel[0])) {
	tag1[0] = 0;
	nr1  = atoi(channel);
    } else {
	sscanf(channel,"%4[A-Za-z]%d",tag1,&nr1);
    }

    for (i = 0; i < chancount; i++) {
	if (isdigit(chanlist[i].name[0])) {
	    tag2[0] = 0;
	    nr2  = atoi(chanlist[i].name);
	} else {
	    sscanf(chanlist[i].name,"%4[A-Za-z]%d",tag2,&nr2);
	}
	if (tag1[0] && tag2[0])
	    if (nr1 == nr2 && 0 == strcmp(tag1,tag2))
		break;
	if (!tag1[0] && !tag2[0])
	    if (nr1 == nr2)
		break;
    }
    if (i == chancount)
	return -1;

    return i;
#else
    int i;

    if (NULL == channel)
	return -1;
    for (i = 0; i < chancount; i++)
	if (0 == strcasecmp(chanlist[i].name,channel))
	    break;
    if (i == chancount)
	return -1;
    return i;
#endif
}

int get_freq(int i)
{
    if (i < 0 || i >= chancount)
	return -1;
    return chanlist[i].freq*16/1000;
}

int  cf2freq(char *name, int fine)
{
    int i;

    if (-1 == (i = lookup_channel(name)))
	return -1;
    return get_freq(i)+fine;
}

/* ----------------------------------------------------------------------- */

struct STRTAB captab[] = {
    {  CAPTURE_ON,          "on"          },
    {  CAPTURE_ON,          "yes"         },
    {  CAPTURE_ON,          "true"        },
    {  CAPTURE_OFF,         "off"         },
    {  CAPTURE_OFF,         "no"          },
    {  CAPTURE_OFF,         "false"       },
    {  CAPTURE_OVERLAY,     "over"        },
    {  CAPTURE_OVERLAY,     "overlay"     },
    {  CAPTURE_GRABDISPLAY, "grab"        },
    {  CAPTURE_GRABDISPLAY, "grabdisplay" },
    {  -1, NULL,     },
};

/* just malloc memory for a new channel ... */
struct CHANNEL*
add_channel(char *name)
{
    struct CHANNEL *channel;

    if (alloc_count == count) {
	alloc_count += 16;
	if (alloc_count == 16)
	    channels = malloc(sizeof(struct CHANNEL*)*alloc_count);
	else
	    channels = realloc(channels,sizeof(struct CHANNEL*)*alloc_count);
    }
    channel = channels[count++] = malloc(sizeof(struct CHANNEL));
    memcpy(channel,&defaults,sizeof(struct CHANNEL));
    channel->name = strdup(name);
    return channel;
}

#ifndef NO_X11

#define PANED_FIX               \
	XtNallowResize, False,  \
	XtNshowGrip,    False,  \
	XtNskipAdjust,  True

void hotkey_channel(struct CHANNEL *channel)
{
    char str[100],key[32],ctrl[16];

    if (NULL == channel->key)
	return;
    if (2 == sscanf(channel->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
		    ctrl,key))
	sprintf(str,"%s<Key>%s: Command(setstation,\"%s\")",
		ctrl,key,channel->name);
    else
	sprintf(str,"<Key>%s: Command(setstation,\"%s\")",
		channel->key,channel->name);
    XtOverrideTranslations(tv,XtParseTranslationTable(str));
    XtOverrideTranslations(opt_paned,XtParseTranslationTable(str));
    XtOverrideTranslations(chan_viewport,XtParseTranslationTable(str));
}

static void
launch_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *argv[2];

    argv[0] = (char*)clientdata;
    argv[1] = NULL;
    XtCallActionProc(widget,"Launch",NULL,argv,1);
}

static void
hotkey_launch(struct LAUNCH *launch)
{
    Widget c;
    char str[100],key[32],ctrl[16],label[64];

    if (NULL == launch->key)
	return;
    if (2 == sscanf(launch->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
		    ctrl,key))
	sprintf(str,"%s<Key>%s: Launch(\"%s\")",ctrl,key,launch->name);
    else
	sprintf(str,"<Key>%s: Launch(\"%s\")",launch->key,launch->name);
    XtOverrideTranslations(tv,XtParseTranslationTable(str));
    XtOverrideTranslations(opt_paned,XtParseTranslationTable(str));
    XtOverrideTranslations(chan_viewport,XtParseTranslationTable(str));

    sprintf(label,"%-20s %s",launch->name,launch->key);
    c = XtVaCreateManagedWidget(launch->name, commandWidgetClass,
				launch_paned,
				PANED_FIX,
				XtNlabel,label,
				NULL);
    XtAddCallback(c,XtNcallback,launch_cb,(XtPointer)(launch->name));
}

static void
button_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct CHANNEL *channel = clientdata;
    do_va_cmd(2,"setstation",channel->name);
}

/* ... and initalize later */
void configure_channel(struct CHANNEL *channel)
{
    channel->button =
	XtVaCreateManagedWidget(channel->name,
				commandWidgetClass, chan_box,
				XtNwidth,pix_width,
				XtNheight,pix_height,
				NULL);
    XtAddCallback(channel->button,XtNcallback,button_cb,(XtPointer*)channel);
    hotkey_channel(channel);
}
#endif

/* delete channel */
void
del_channel(int i)
{
    free(channels[i]->name);
    if (channels[i]->key)
	free(channels[i]->key);
    free(channels[i]);
    count--;
    if (i < count)
	memmove(channels+i,channels+i+1,(count-i)*sizeof(struct CHANNEL*));
}

void
calc_frequencies()
{
    int i;

    for (i = 0; i < count; i++) {
	if (NULL == channels[i]->cname)
	    continue;
	channels[i]->channel = lookup_channel(channels[i]->cname);
	if (-1 == channels[i]->channel)
	    channels[i]->freq = -1;
	else
	    channels[i]->freq = get_freq(channels[i]->channel)
		+ channels[i]->fine;
    }
}

/* ----------------------------------------------------------------------- */

static void
init_channel(char *name, struct CHANNEL *c)
{
    struct ng_attribute *attr;
    char *val; int n,i;

    if (NULL != (val = cfg_get_str(name,"capture"))) {
	if (-1 != (i = str_to_int(val,captab)))
	    c->capture = i;
	else
	    fprintf(stderr,"config: invalid value for capture: %s\n",val);
    }
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_INPUT)) &&
	(NULL != (val = cfg_get_str(name,"input")) ||
	 NULL != (val = cfg_get_str(name,"source")))) { /* obsolete */
	if (-1 != (i = ng_attr_getint(attr,val)))
	    c->input = i;
	else {
	    fprintf(stderr,"config: invalid value for input: %s\n",val);
	    ng_attr_listchoices(attr);
	}
    }
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_NORM)) &&
	NULL != (val = cfg_get_str(name,"norm"))) {
	if (-1 != (i = ng_attr_getint(attr,val)))
	    c->norm = i;
	else {
	    fprintf(stderr,"config: invalid value for norm: %s\n",val);
	    ng_attr_listchoices(attr);
	}
    }
    if (NULL != (attr = ng_attr_byid(attrs,ATTR_ID_AUDIO_MODE)) &&
	NULL != (val = cfg_get_str(name,"audio"))) {
	if (-1 != (i = ng_attr_getint(attr,val)))
	    c->audio = i;
	else {
	    fprintf(stderr,"config: invalid value for audio: %s\n",val);
	    ng_attr_listchoices(attr);
	}
    }

    if (NULL != (val = cfg_get_str(name,"channel")))
	c->cname   = strdup(val);
    if (NULL != (val = cfg_get_str(name,"freq")))
	c->freq   = (int)(atof(val)*16);
    if (0 != (n = cfg_get_signed_int(name,"fine")))
	c->fine = n;

    if (NULL != (val = cfg_get_str(name,"key")))
	c->key  = strdup(val);
    if (NULL != (val = cfg_get_str(name,"group")))
	c->group  = strdup(val);
    if (NULL != (val = cfg_get_str(name,"midi")))
	c->midi = atoi(val);

    attr = ng_attr_byid(attrs,ATTR_ID_COLOR);
    if (attr && NULL != (val = cfg_get_str(name,"color")))
	c->color = ng_attr_parse_int(attr,val);
    attr = ng_attr_byid(attrs,ATTR_ID_BRIGHT);
    if (attr && NULL != (val = cfg_get_str(name,"bright")))
	c->bright = ng_attr_parse_int(attr,val);
    attr = ng_attr_byid(attrs,ATTR_ID_HUE);
    if (attr && NULL != (val = cfg_get_str(name,"hue")))
	c->hue = ng_attr_parse_int(attr,val);
    attr = ng_attr_byid(attrs,ATTR_ID_CONTRAST);
    if (attr && NULL != (val = cfg_get_str(name,"contrast")))
	c->contrast = ng_attr_parse_int(attr,val);
}

void
read_config(char *conffile, int *argc, char **argv)
{
    struct list_head *item;
    char filename[100];
    char *val;
    int  i;

    if (conffile) {
	if (0 == cfg_parse_file(conffile))
	    have_config = 1;
    } else {
	sprintf(filename,"%.*s/%s",(int)sizeof(filename)-8,
		getenv("HOME"),".xawtv");
	if (0 == cfg_parse_file(CONFIGFILE))
	    have_config = 1;
	if (0 == cfg_parse_file(filename))
	    have_config = 1;
    }
    if (argc)
	cfg_parse_options(argc,argv);

    /* misc global settings */
    if (NULL != (val = cfg_get_str("global","mixer"))) {
	mixer = strdup(val);
	if (2 != sscanf(mixer,"%31[^:]:%15s",mixerdev,mixerctl)) {
	    strcpy(mixerdev,ng_dev.mixer);
	    strncpy(mixerctl,val,15);
	    mixerctl[15] = 0;
	}
    }

    if (NULL != (val = cfg_get_str("global","midi")))
	midi = strdup(val);

    if (NULL != (val = cfg_get_str("global","freqtab"))) {
	for (i = 0; chanlists[i].name != NULL; i++)
	    if (0 == strcasecmp(val,chanlists[i].name))
		break;
	if (chanlists[i].name != NULL) {
	    freq_newtab(i);
	} else
	    fprintf(stderr,"invalid value for freqtab: %s\n",val);
    }

    if (NULL != (val = cfg_get_str("global","fullscreen"))) {
	if (2 != sscanf(val,"%d x %d",&fs_width,&fs_height)) {
	    fprintf(stderr,"invalid value for fullscreen: %s\n",val);
	    fs_width = fs_height = 0;
	}
    }

    if (NULL != (val = cfg_get_str("global","pixsize"))) {
	if (2 != sscanf(val,"%d x %d",&pix_width,&pix_height)) {
	    fprintf(stderr,"invalid value for pixsize: %s\n",val);
	    pix_width = 128;
	    pix_height = 96;
	}
    }
    if (-1 != (i = cfg_get_int("global","pixcols")))
	pix_cols = i;

    if (NULL != (val = cfg_get_str("global","wm-off-by"))) {
	if (2 != sscanf(val,"%d %d",&fs_xoff,&fs_yoff)) {
	    fprintf(stderr,"invalid value for wm-off-by: %s\n",val);
	    fs_xoff = fs_yoff = 0;
	}
    }
    if (NULL != (val = cfg_get_str("global","ratio"))) {
	if (2 != sscanf(val,"%d:%d",&ng_ratio_x,&ng_ratio_y)) {
	    fprintf(stderr,"invalid value for ratio: %s\n",val);
	    ng_ratio_x = ng_ratio_y = 0;
	}
    }

    if (-1 != (i = cfg_get_int("global","jpeg-quality")))
	ng_jpeg_quality = i;

    if (NULL != (val = cfg_get_str("global","keypad-ntsc")))
	if (-1 != (i = str_to_int(val,booltab)))
	    keypad_ntsc = i;
    if (NULL != (val = cfg_get_str("global","keypad-partial")))
	if (-1 != (i = str_to_int(val,booltab)))
	    keypad_partial = i;
    if (NULL != (val = cfg_get_str("global","osd")))
	if (-1 != (i = str_to_int(val,booltab)))
	    use_osd = i;
    if (NULL != (val = cfg_get_str("global","osd-position")))
	if (2 != sscanf(val,"%d , %d",&osd_x,&osd_y))
	    fprintf(stderr,"invalid values for osd-position: %s\n",val);
    if (NULL != (val = cfg_get_str("global","use-wm-fullscreen")))
	if (-1 != (i = str_to_int(val,booltab)))
	    use_wm_fullscreen = i;

    if (NULL != (val = cfg_get_str("global","mov-driver")))
	mov_driver = val;
    if (NULL != (val = cfg_get_str("global","mov-video")))
	mov_video = val;
    if (NULL != (val = cfg_get_str("global","mov-fps")))
	mov_fps = val;
    if (NULL != (val = cfg_get_str("global","mov-audio")))
	mov_audio = val;
    if (NULL != (val = cfg_get_str("global","mov-rate")))
	mov_rate = val;

    if (NULL != (val = cfg_get_str("global","filter"))) {
	list_for_each(item,&ng_filters) {
	    struct ng_filter *f = list_entry(item, struct ng_filter, list);
	    if (0 == strcasecmp(f->name, val))
		cur_filter = f;
	}
    }
}

void
parse_config(int parse_channels)
{
    char key[16], cmdline[128];
    char **list,*val;
#ifndef NO_X11
    int i;
#endif

    /* launch */
    list = cfg_list_entries("launch");
    if (NULL != list) {
	for (; *list != NULL; list++) {
	    if (NULL != (val = cfg_get_str("launch",*list)) &&
		2 == sscanf(val,"%15[^,], %127[^\n]",
			    key,cmdline)) {
		launch = realloc(launch,sizeof(struct LAUNCH)*(nlaunch+1));
		launch[nlaunch].name    = strdup(*list);
		launch[nlaunch].key     = strdup(key);
		launch[nlaunch].cmdline = strdup(cmdline);
#ifndef NO_X11
		hotkey_launch(launch+nlaunch);
#endif
		nlaunch++;
	    } else {
		fprintf(stderr,"invalid value in section [launch]: %s\n",val);
	    }
	}
    }

    /* events */
    event_readconfig();

    if (!parse_channels)
        return;

    /* channels */
    init_channel("defaults",&defaults);
    for (list = cfg_list_sections(); *list != NULL; list++) {
	if (0 == strcmp(*list,"defaults")) continue;
	if (0 == strcmp(*list,"global"))   continue;
	if (0 == strcmp(*list,"launch"))   continue;
	if (0 == strcmp(*list,"eventmap")) continue;
	init_channel(*list,add_channel(*list));
    }

    /* calculate channel frequencies */
    defaults.channel = lookup_channel(defaults.cname);
    defaults.freq    = get_freq(defaults.channel) + defaults.fine;
    calc_frequencies();
#ifndef NO_X11
    for (i = 0; i < count; i++)
	configure_channel(channels[i]);
#endif
}

/* ----------------------------------------------------------------------- */

void
save_config()
{
    struct ng_attribute *attr;
    char filename1[100], filename2[100];
    FILE *fp;
    int i;

    sprintf(filename1,"%s/%s",getenv("HOME"),".xawtv");
    sprintf(filename2,"%s/%s",getenv("HOME"),".xawtv~");

    /* delete old backup */
    unlink(filename2);

    /* current becomes backup */
    if (0 == link(filename1,filename2))
	unlink(filename1);

    /* write new one... */
    fp = fopen(filename1,"w");
    if (NULL == fp) {
	fprintf(stderr,"can't open config file %s\n",filename1);
	return;
    }

    fprintf(fp,"[global]\n");
    if (fs_width && fs_height)
	fprintf(fp,"fullscreen = %d x %d\n",fs_width,fs_height);
    if (fs_xoff || fs_yoff)
	fprintf(fp,"wm-off-by = %+d%+d\n",fs_xoff,fs_yoff);
    if (ng_ratio_x || ng_ratio_y)
	fprintf(fp,"ratio = %d:%d\n",ng_ratio_x,ng_ratio_y);
    fprintf(fp,"freqtab = %s\n",chanlists[chantab].name);
    fprintf(fp,"pixsize = %d x %d\n",pix_width,pix_height);
    fprintf(fp,"pixcols = %d\n",pix_cols);
    fprintf(fp,"jpeg-quality = %d\n",ng_jpeg_quality);
    fprintf(fp,"keypad-ntsc = %s\n",int_to_str(keypad_ntsc,booltab));
    fprintf(fp,"keypad-partial = %s\n",int_to_str(keypad_partial,booltab));
    fprintf(fp,"osd = %s\n",int_to_str(use_osd,booltab));
    fprintf(fp,"osd-position = %d , %d\n",osd_x,osd_y);
    fprintf(fp,"use-wm-fullscreen = %s\n",
	    int_to_str(use_wm_fullscreen,booltab));
    if (mixer)
	fprintf(fp,"mixer = %s\n",mixer);
    if (midi)
	fprintf(fp,"midi = %s\n",midi);

    if (mov_driver)
	fprintf(fp,"mov-driver = %s\n",mov_driver);
    if (mov_video)
	fprintf(fp,"mov-video = %s\n",mov_video);
    if (mov_fps)
	fprintf(fp,"mov-fps = %s\n",mov_fps);
    if (mov_audio)
	fprintf(fp,"mov-audio = %s\n",mov_audio);
    if (mov_rate)
	fprintf(fp,"mov-rate = %s\n",mov_rate);

    fprintf(fp,"\n");

    if (nlaunch > 0) {
	fprintf(fp,"[launch]\n");
	for (i = 0; i < nlaunch; i++) {
	    fprintf(fp,"%s = %s, %s\n",
		    launch[i].name,launch[i].key,launch[i].cmdline);
	}
	fprintf(fp,"\n");
    }

    /* events */
    event_writeconfig(fp);

    /* write help */
    fprintf(fp,"# [Station name]\n");
    fprintf(fp,"# capture = overlay | grabdisplay | on | off\n");
    fprintf(fp,"# input = Television | Composite1 | S-Video | ...\n");
    fprintf(fp,"# norm = PAL | NTSC | SECAM | ... \n");
    fprintf(fp,"# channel = #\n");
    fprintf(fp,"# fine = # (-128..+127)\n");
    fprintf(fp,"# key = keysym | modifier+keysym\n");
    fprintf(fp,"# color = #\n");
    fprintf(fp,"# bright = #\n");
    fprintf(fp,"# hue = #\n");
    fprintf(fp,"# contrast = #\n");
    fprintf(fp,"\n");

    /* write defaults */
    fprintf(fp,"[defaults]\n");
    fprintf(fp,"group = %s\n",defaults.group);

    fprintf(fp,"norm = %s\n",
	    ng_attr_getstr(ng_attr_byid(attrs,ATTR_ID_NORM),
			   cur_attrs[ATTR_ID_NORM]));
    fprintf(fp,"input = %s\n",
	    ng_attr_getstr(ng_attr_byid(attrs,ATTR_ID_INPUT),
			   cur_attrs[ATTR_ID_INPUT]));
    fprintf(fp,"capture = %s\n",int_to_str(cur_capture,captab));

    attr = ng_attr_byid(attrs,ATTR_ID_COLOR);
    if (attr && attr->defval != cur_attrs[ATTR_ID_COLOR])
	fprintf(fp,"color = %d%%\n",
		ng_attr_int2percent(attr,cur_attrs[ATTR_ID_COLOR]));
    attr = ng_attr_byid(attrs,ATTR_ID_BRIGHT);
    if (attr && attr->defval != cur_attrs[ATTR_ID_BRIGHT])
	fprintf(fp,"bright = %d%%\n",
		ng_attr_int2percent(attr,cur_attrs[ATTR_ID_BRIGHT]));
    attr = ng_attr_byid(attrs,ATTR_ID_HUE);
    if (attr && attr->defval != cur_attrs[ATTR_ID_HUE])
	fprintf(fp,"hue = %d%%\n",
		ng_attr_int2percent(attr,cur_attrs[ATTR_ID_HUE]));
    attr = ng_attr_byid(attrs,ATTR_ID_CONTRAST);
    if (attr && attr->defval != cur_attrs[ATTR_ID_CONTRAST])
	fprintf(fp,"contrast = %d%%\n",
		ng_attr_int2percent(attr,cur_attrs[ATTR_ID_CONTRAST]));
    fprintf(fp,"\n");

    /* write channels */
    for (i = 0; i < count; i++) {
	fprintf(fp,"[%s]\n",channels[i]->name);
	if (NULL != channels[i]->cname) {
	    fprintf(fp,"channel = %s\n",chanlist[channels[i]->channel].name);
	    if (0 != channels[i]->fine)
		fprintf(fp,"fine = %+d\n", channels[i]->fine);
	} else {
	    fprintf(fp,"freq = %.2f\n",(float)(channels[i]->freq)/16);
	}

	if ( channels[i]->norm != cur_attrs[ATTR_ID_NORM])
	    fprintf(fp,"norm = %s\n",
		    ng_attr_getstr(ng_attr_byid(attrs,ATTR_ID_NORM),
				   channels[i]->norm));
	if (channels[i]->input != cur_attrs[ATTR_ID_INPUT])
	    fprintf(fp,"input = %s\n",
		    ng_attr_getstr(ng_attr_byid(attrs,ATTR_ID_INPUT),
				   channels[i]->input));

	if (channels[i]->key != NULL)
	    fprintf(fp,"key = %s\n",channels[i]->key);
	if (0 != strcmp(channels[i]->group,defaults.group))
	    fprintf(fp,"group = %s\n",channels[i]->group);
	if (channels[i]->midi != 0)
	    fprintf(fp,"midi = %d\n",channels[i]->midi);
	if (channels[i]->capture != cur_capture)
	    fprintf(fp,"capture = %s\n",
		    int_to_str(channels[i]->capture,captab));

	attr = ng_attr_byid(attrs,ATTR_ID_COLOR);
	if (attr && cur_attrs[ATTR_ID_COLOR] != channels[i]->color)
	    fprintf(fp,"color = %d%%\n",
		    ng_attr_int2percent(attr,channels[i]->color));
	attr = ng_attr_byid(attrs,ATTR_ID_BRIGHT);
	if (attr && cur_attrs[ATTR_ID_BRIGHT] != channels[i]->bright)
	    fprintf(fp,"bright = %d%%\n",
		    ng_attr_int2percent(attr,channels[i]->bright));
	attr = ng_attr_byid(attrs,ATTR_ID_HUE);
	if (attr && cur_attrs[ATTR_ID_HUE] != channels[i]->hue)
	    fprintf(fp,"hue = %d%%\n",
		    ng_attr_int2percent(attr,channels[i]->hue));
	attr = ng_attr_byid(attrs,ATTR_ID_CONTRAST);
	if (attr && cur_attrs[ATTR_ID_CONTRAST] != channels[i]->contrast)
	    fprintf(fp,"contrast = %d%%\n",
		    ng_attr_int2percent(attr,channels[i]->contrast));

	fprintf(fp,"\n");
    }
    fclose(fp);
}

/* ----------------------------------------------------------------------- */

struct STRTAB booltab[] = {
    {  0, "no" },
    {  0, "false" },
    {  0, "off" },
    {  1, "yes" },
    {  1, "true" },
    {  1, "on" },
    { -1, NULL }
};

int
str_to_int(char *str, struct STRTAB *tab)
{
    int i;

    if (str[0] >= '0' && str[0] <= '9')
	return atoi(str);
    for (i = 0; tab[i].str != NULL; i++)
	if (0 == strcasecmp(str,tab[i].str))
	    return(tab[i].nr);
    return -1;
}

const char*
int_to_str(int n, struct STRTAB *tab)
{
    int i;

    for (i = 0; tab[i].str != NULL; i++)
	if (tab[i].nr == n)
	    return tab[i].str;
    return NULL;
}
