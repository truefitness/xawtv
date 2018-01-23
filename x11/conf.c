/*
 * channel editor.
 *
 *   (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sys/time.h>

#include "config.h"

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/AsciiText.h>

#include "grab-ng.h"
#include "channel.h"
#include "frequencies.h"
#include "wmhooks.h"
#include "commands.h"
#include "conf.h"

/*-------------------------------------------------------------------------*/

void pixit(void);
void set_channel(struct CHANNEL *channel);
void channel_menu(void);

extern Widget   app_shell,conf_shell;
extern Display  *dpy;
extern Atom     wm_protocols[2];
extern int      debug;
extern int      stay_on_top;
extern XVisualInfo vinfo;
extern Colormap colormap;

Widget conf_channel, conf_name, conf_key, conf_list;

static Widget last_command, viewport;
static String *channel_list;

/*-------------------------------------------------------------------------*/

static void list_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XawListReturnStruct *lr = call_data;

    if (channel_switch_hook)
	channel_switch_hook();
    do_va_cmd(2,"setstation",lr->string);
}

static void add_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct CHANNEL *channel;
    char *name,*key;

    XtVaGetValues(conf_name,XtNstring,&name,NULL);
    XtVaGetValues(conf_key,XtNlabel,&key,NULL);

    if (0 == strlen(name))
	return;

    channel = add_channel(name);
    if (strlen(key) > 0)
	channel->key = strdup(key);
    channel->cname = (cur_channel != -1) ? chanlist[cur_channel].name : "???";
    channel->channel = cur_channel;
    channel->input = cur_attrs[ATTR_ID_INPUT];
    channel->fine = cur_fine;
    fprintf(stderr,"add_cb #1: %d %s\n",channel->channel,channel->cname);
    configure_channel(channel);
    fprintf(stderr,"add_cb #2: %d %s\n",channel->channel,channel->cname);
    channel_menu();
    fprintf(stderr,"add_cb #3: %d %s\n",channel->channel,channel->cname);
}

static void del_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    if(cur_sender == -1)
	return;
    XtDestroyWidget(channels[cur_sender]->button);
    del_channel(cur_sender);
    channel_menu();
    cur_sender = -1;
    conf_station_switched();
}

static void modify_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *name,*key;

    XtVaGetValues(conf_name,XtNstring,&name,NULL);
    XtVaGetValues(conf_key,XtNlabel,&key,NULL);

    if (0 == strlen(name))
	return;

    /* no channel yet ... */
    if (-1 == cur_sender) {
	add_cb(widget, clientdata, call_data);
	return;
    }

    free(channels[cur_sender]->name);
    channels[cur_sender]->name = strdup(name);

    if (channels[cur_sender]->key)
	free(channels[cur_sender]->key);
    if (0 != strlen(key))
	channels[cur_sender]->key = strdup(key);
    else
	channels[cur_sender]->key = 0;
    hotkey_channel(channels[cur_sender]);
    channel_menu();
    conf_station_switched();
}

static void save_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    save_config();
}

static void close_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XtCallActionProc(conf_shell,"Popup",NULL,NULL,0);
}

static void key_eh(Widget widget, XtPointer client_data,
		   XEvent *event, Boolean *cont)
{
    XKeyEvent *ke = (XKeyEvent*)event;
    KeySym sym;
    char *key;
    char line[64];

    sym = XKeycodeToKeysym(dpy,ke->keycode,0);
    if (NoSymbol == sym) {
	fprintf(stderr,"can't translate keycode %d\n",ke->keycode);
	return;
    }
    key = XKeysymToString(sym);

    line[0] = '\0';
    if (ke->state & ShiftMask)   strcpy(line,"Shift+");
    if (ke->state & ControlMask) strcpy(line,"Ctrl+");
    strcat(line,key);
    XtVaSetValues(conf_key,XtNlabel,line,NULL);
}

/*-------------------------------------------------------------------------*/

#define FIX_RIGHT_TOP       \
    XtNleft,XawChainRight,  \
    XtNright,XawChainRight, \
    XtNtop,XawChainTop,     \
    XtNbottom,XawChainTop

void
create_confwin(void)
{
    Widget form, label, command;

    conf_shell = XtVaAppCreateShell("Config", "Xawtv",
				    topLevelShellWidgetClass,
				    dpy,
				    XtNclientLeader,app_shell,
				    XtNvisual,vinfo.visual,
				    XtNcolormap,colormap,
				    XtNdepth,vinfo.depth,
				    NULL);
    XtOverrideTranslations(conf_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    form = XtVaCreateManagedWidget("form", formWidgetClass, conf_shell,
				   NULL);

    /* list */
    viewport =
	XtVaCreateManagedWidget("viewport", viewportWidgetClass, form,
				XtNleft,XawChainLeft,
				XtNright,XawChainRight,
				XtNtop,XawChainTop,
				XtNbottom,XawChainBottom,
				NULL);
    conf_list =
	XtVaCreateManagedWidget("list", listWidgetClass, viewport,
				NULL);
    XtAddCallback(conf_list,XtNcallback,list_cb,(XtPointer)NULL);

    /* Einstellungen */
    label =
	XtVaCreateManagedWidget("lchannel", labelWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				NULL);
    conf_channel =
	XtVaCreateManagedWidget("channel", labelWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, label,
				NULL);
    label =
	XtVaCreateManagedWidget("lkey", labelWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, conf_channel,
				NULL);
    conf_key =
	XtVaCreateManagedWidget("key", labelWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, label,
				NULL);
    label =
	XtVaCreateManagedWidget("lname", labelWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, conf_key,
				NULL);
    conf_name =
	XtVaCreateManagedWidget("name", asciiTextWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, label,
				NULL);
    XtAddEventHandler(conf_key, KeyPressMask, False, key_eh, NULL);

    /* buttons */
    command =
	XtVaCreateManagedWidget("add", commandWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, conf_name,
				NULL);
    XtAddCallback(command,XtNcallback,add_cb,(XtPointer)NULL);
    command =
	XtVaCreateManagedWidget("delete", commandWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, command,
				NULL);
    XtAddCallback(command,XtNcallback,del_cb,(XtPointer)NULL);
    command =
	XtVaCreateManagedWidget("modify", commandWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, command,
				NULL);
    XtAddCallback(command,XtNcallback,modify_cb,(XtPointer)NULL);
    command =
	XtVaCreateManagedWidget("save", commandWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, command,
				NULL);
    XtAddCallback(command,XtNcallback,save_cb,(XtPointer)NULL);
    last_command = command =
	XtVaCreateManagedWidget("close", commandWidgetClass, form,
				FIX_RIGHT_TOP,
				XtNfromHoriz, viewport,
				XtNfromVert, command,
				NULL);
    XtAddCallback(command,XtNcallback,close_cb,(XtPointer)NULL);

    XtInstallAllAccelerators(conf_name, conf_shell);
}

/*-------------------------------------------------------------------------*/

void conf_station_switched(void)
{
    char line[128] = "???";

    /* channel */
    if (cur_channel != -1) {
	strcpy(line,chanlist[cur_channel].name);
	if (cur_fine != 0)
	    sprintf(line+strlen(line)," (%+d)",cur_fine);
    }
    XtVaSetValues(conf_channel, XtNlabel, line, NULL);

    if (cur_sender == -1) {
	XtVaSetValues(conf_key, XtNlabel, "", NULL);
	XtVaSetValues(conf_name, XtNstring, "", NULL);
	XawListUnhighlight(conf_list);
    } else {
	if (channels[cur_sender]->key)
	    XtVaSetValues(conf_key,XtNlabel,channels[cur_sender]->key, NULL);
	else
	    XtVaSetValues(conf_key,XtNlabel,"", NULL);
#if 0
	/* This is needed for Xaw3d
	   libXaw3d doesn't get the memory management right with
	   the international ressource set to true.  Keeps crashing
	   without the strdup() */
	XtVaSetValues(conf_name,
		      XtNstring, strdup(channels[cur_sender]->name),
		      NULL);
#else
	XtVaSetValues(conf_name, XtNstring, channels[cur_sender]->name, NULL);
#endif
	XawListHighlight(conf_list,cur_sender);
    }
}

void
conf_list_update(void)
{
    int i;

    if (channel_list)
	free(channel_list);

    XawListUnhighlight(conf_list);
    if (count) {
	/* rebuild list */
	channel_list = malloc((count+1)*sizeof(String));
	for (i = 0; i < count; i++)
	    channel_list[i] = channels[i]->name;
	channel_list[i] = NULL;
    } else {
	/* empty list */
	channel_list    = malloc(2*sizeof(String));
	channel_list[0] = "empty";
	channel_list[1] = NULL;
    }
    XtVaSetValues(conf_list,
		  XtNlist, channel_list,
		  XtNnumberStrings, 0,
		  NULL);
}
