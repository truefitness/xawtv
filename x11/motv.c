/*
 * Openmotif user interface
 *
 *   (c) 2000-2002 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/XmStrDefs.h>
#include <Xm/Primitive.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/CascadeB.h>
#include <Xm/Separator.h>
#include <Xm/Scale.h>
#include <Xm/Protocols.h>
#include <Xm/Display.h>
#include <Xm/Text.h>
#include <Xm/FileSB.h>
#include <Xm/ComboBox.h>
#include <Xm/ScrolledW.h>
#include <Xm/MessageB.h>
#include <Xm/Frame.h>
#include <Xm/SelectioB.h>
#include <Xm/TransferP.h>
#include <Xm/DragIcon.h>
#include <X11/extensions/XShm.h>
#include <X11/Xmu/Editres.h>

#include "grab-ng.h"
#include "channel.h"
#include "commands.h"
#include "frequencies.h"
#include "capture.h"
#include "wmhooks.h"
#include "atoms.h"
#include "x11.h"
#include "xt.h"
#include "xv.h"
#include "man.h"
#include "icons.h"
#include "sound.h"
#include "complete.h"
#include "writefile.h"
#include "list.h"
#include "vbi-data.h"
#include "vbi-x11.h"
#include "blit.h"

/*----------------------------------------------------------------------*/

int jpeg_quality, mjpeg_quality, debug;

/*----------------------------------------------------------------------*/

static void PopupAction(Widget, XEvent*, String*, Cardinal*);
static void DebugAction(Widget, XEvent*, String*, Cardinal*);
static void IpcAction(Widget, XEvent*, String*, Cardinal*);
static void ontop_ac(Widget, XEvent*, String*, Cardinal*);
static void chan_makebutton(struct CHANNEL *channel);
static void channel_menu(void);

#ifdef HAVE_ZVBI
static void chscan_cb(Widget widget, XtPointer clientdata,
		      XtPointer call_data);
#endif
static void pref_manage_cb(Widget widget, XtPointer clientdata,
			   XtPointer call_data);
static void add_cmd_callback(Widget widget, String callback,char *command,
			     const char *arg1, const char *arg2);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction   },
    { "Command",     CommandAction     },
    { "Popup",       PopupAction       },
    { "Debug",       DebugAction       },
    { "Remote",      RemoteAction      },
    { "Zap",         ZapAction         },
    { "Scan",        ScanAction        },
    { "man",         man_action        },
    { "Ratio",       RatioAction       },
    { "Launch",      LaunchAction      },
#ifdef HAVE_ZVBI
    { "Vtx",         VtxAction         },
#endif
    { "Complete",    CompleteAction    },
    { "Ipc",         IpcAction         },
    { "Filter",      FilterAction      },
    { "StayOnTop",   ontop_ac          },
    { "Event",       EventAction       },
};

static String fallback_ressources[] = {
#include "MoTV.h"
    NULL
};

XtIntervalId audio_timer;

static Widget st_menu1,st_menu2;
static Widget control_shell,str_shell,levels_shell,levels_toggle;
static Widget launch_menu,opt_menu,cap_menu,freq_menu;
static Widget chan_viewport,chan_box;
static Widget st_freq,st_chan,st_name,st_key;
static Widget scale_shell,filter_shell;
static Widget w_full;
static Widget b_ontop;
#ifdef HAVE_ZVBI
static struct vbi_window *vtx;
#endif

/* properties */
static Widget prop_dlg,prop_name,prop_key,prop_channel,prop_button;
static Widget prop_group;

/* preferences */
static Widget pref_dlg,pref_fs_toggle,pref_fs_menu,pref_fs_option;
static Widget pref_osd,pref_ntsc,pref_partial,pref_quality;
static Widget pref_mix_toggle,pref_mix1_menu,pref_mix1_option;
static Widget pref_mix2_menu,pref_mix2_option;

/* streamer */
static Widget driver_menu, driver_option;
static Widget audio_menu, audio_option;
static Widget video_menu, video_option;
static Widget m_rate,m_fps,m_fvideo,m_status;
static Widget m_faudio,m_faudioL,m_faudioB;

static struct ng_writer *movie_driver;
static unsigned int i_movie_driver;
static unsigned int movie_audio;
static unsigned int movie_video;
static XtWorkProcId rec_work_id;

static struct MY_TOPLEVELS {
    char        *name;
    Widget      *shell;
    int         mapped;
} my_toplevels [] = {
    { "control",   &control_shell },
    { "streamer",  &str_shell     },
    { "scale",     &scale_shell   },
    { "filter",    &filter_shell  },
    { "levels",    &levels_shell  },
};
#define TOPLEVELS (sizeof(my_toplevels)/sizeof(struct MY_TOPLEVELS))

struct motif_attribute {
    struct motif_attribute  *next;
    struct ng_attribute     *attr;
    Widget                  widget;
};
static struct motif_attribute *motif_attrs;

struct filter_attribute {
    struct filter_attribute *next;
    struct ng_filter        *filter;
    struct ng_attribute     *attr;
    int                     value;
    Widget                  widget;
};
static struct filter_attribute *filter_attrs;

/*----------------------------------------------------------------------*/
/* debug/test code                                                      */

#if 0
static void
print_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *msg = (char*) clientdata;
    fprintf(stderr,"debug: %s\n",msg);
}

static void
toggle_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmToggleButtonCallbackStruct *tb = call_data;

    if (tb->reason != XmCR_VALUE_CHANGED)
	return;
    fprintf(stderr,"toggle: [%s] set=%s\n",
	    clientdata ? (char*)clientdata : "???",
	    tb->set ? "on" : "off");
}
#endif

void
DebugAction(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    fprintf(stderr,"debug: foo\n");
}

/*----------------------------------------------------------------------*/

void toolkit_set_label(Widget widget, char *str)
{
    XmString xmstr;

    xmstr = XmStringGenerate(str, NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(widget,XmNlabelString,xmstr,NULL);
    XmStringFree(xmstr);
}

static void delete_children(Widget widget)
{
    WidgetList children,list;
    Cardinal nchildren;
    unsigned int i;

    XtVaGetValues(widget,XtNchildren,&children,
		  XtNnumChildren,&nchildren,NULL);
    if (0 == nchildren)
	return;
    list = malloc(sizeof(Widget*)*nchildren);
    memcpy(list,children,sizeof(Widget*)*nchildren);
    for (i = 0; i < nchildren; i++)
	XtDestroyWidget(list[i]);
    free(list);
}

static void
watch_audio(XtPointer data, XtIntervalId *id)
{
    if (-1 != cur_sender)
	change_audio(channels[cur_sender]->audio);
    audio_timer = 0;
}

static void
new_channel(void)
{
    char line[1024];

    set_property(cur_freq,
		 (cur_channel == -1) ? NULL : chanlist[cur_channel].name,
		 (cur_sender == -1)  ? NULL : channels[cur_sender]->name);

    sprintf(line,"%d.%02d MHz",cur_freq / 16, (cur_freq % 16) * 100 / 16);
    toolkit_set_label(st_freq,line);
    toolkit_set_label(st_chan, (cur_channel != -1) ?
		      chanlist[cur_channel].name : "");
    toolkit_set_label(st_name, (cur_sender != -1) ?
		      channels[cur_sender]->name : "");
    toolkit_set_label(st_key,(cur_sender != -1 &&
			      NULL != channels[cur_sender]->key) ?
	channels[cur_sender]->key : "");

    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
    }
    if (scan_timer) {
	XtRemoveTimeOut(scan_timer);
	scan_timer = 0;
    }
    if (audio_timer) {
	XtRemoveTimeOut(audio_timer);
	audio_timer = 0;
    }
    audio_timer = XtAppAddTimeOut(app_context, 5000, watch_audio, NULL);
}

static void
do_ontop(Boolean state)
{
    unsigned int i;

    if (!wm_stay_on_top)
	return;

    stay_on_top = state;
    wm_stay_on_top(dpy,XtWindow(app_shell),stay_on_top);
    wm_stay_on_top(dpy,XtWindow(on_shell),stay_on_top);
    for (i = 0; i < TOPLEVELS; i++)
	wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),
		       (stay_on_top == -1) ? 0 : stay_on_top);
    XmToggleButtonSetState(b_ontop,state,False);
}

static void
ontop_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmToggleButtonCallbackStruct *tb = call_data;

    if (tb->reason != XmCR_VALUE_CHANGED)
	return;
    do_ontop(tb->set);
}

static void
ontop_ac(Widget widget, XEvent *event, String *params, Cardinal *num_params)
{
    do_ontop(stay_on_top ? False : True);
}

/*----------------------------------------------------------------------*/

static void
resize_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    static int width = 0, height = 0, first = 1;

    switch(event->type) {
    case ConfigureNotify:
	if (first) {
	    video_gd_init(tv,args.gl);
	    first = 0;
	}
	if (width  != event->xconfigure.width ||
	    height != event->xconfigure.height) {
	    width  = event->xconfigure.width;
	    height = event->xconfigure.height;
	    video_gd_configure(width, height);
	    XClearWindow(XtDisplay(tv),XtWindow(tv));
	}
	break;
    }
}

static void
PopupAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    unsigned int i;

    /* which window we are talking about ? */
    if (*num_params > 0) {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (0 == strcasecmp(my_toplevels[i].name,params[0]))
		break;
	}
	if (i == TOPLEVELS) {
	    fprintf(stderr,"PopupAction: oops: shell not found (name=%s)\n",
		    params[0]);
	    return;
	}
    } else {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (*(my_toplevels[i].shell) == widget)
		break;
	}
	if (i == TOPLEVELS) {
	    fprintf(stderr,"PopupAction: oops: shell not found (%p:%s)\n",
		    widget,XtName(widget));
	    return;
	}
    }

    /* popup/down window */
    if (!my_toplevels[i].mapped) {
	XtPopup(*(my_toplevels[i].shell), XtGrabNone);
	if (wm_stay_on_top && stay_on_top > 0)
	    wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),1);
	my_toplevels[i].mapped = 1;
    } else {
	XtPopdown(*(my_toplevels[i].shell));
	my_toplevels[i].mapped = 0;
    }
}

static void
popup_eh(Widget widget, XtPointer clientdata, XEvent *event, Boolean *cont)
{
    Widget popup = clientdata;

    XmMenuPosition(popup,(XButtonPressedEvent*)event);
    XtManageChild(popup);
}

static void
popupdown_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    int i = 0;
    PopupAction(clientdata, NULL, NULL, &i);
}

static void
destroy_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XtDestroyWidget(clientdata);
}

static void
free_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    free(clientdata);
}

static void
about_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget msgbox;

    msgbox = XmCreateInformationDialog(app_shell,"about_box",NULL,0);
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(msgbox,XmNokCallback,destroy_cb,msgbox);
    XtManageChild(msgbox);
}

static void
action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *calls, *action, *argv[8];
    int argc;

    calls = strdup(clientdata);
    action = strtok(calls,"(),");
    for (argc = 0; NULL != (argv[argc] = strtok(NULL,"(),")); argc++)
	/* nothing */;
    XtCallActionProc(widget,action,NULL,argv,argc);
    free(calls);
}

/*--- videotext ----------------------------------------------------------*/

#if TT
static void
rend_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmDisplayCallbackStruct *arg = call_data;
    XmRendition rend;
    Colormap cmap;
    XColor color,dummy;
    char fg[16],bg[16];
    Arg args[4];
    int n = 0;

    if (arg->reason != XmCR_NO_RENDITION)
	return;
    if (2 != sscanf(arg->tag,"%[a-z]_on_%[a-z]",fg,bg))
	return;

    XtVaGetValues(app_shell, XmNcolormap, &cmap, NULL);
    if (0 != strcmp(fg,"def"))
	if (XAllocNamedColor(dpy, cmap, fg, &color, &dummy))
	    XtSetArg(args[n],XmNrenditionForeground,color.pixel), n++;
    if (0 != strcmp(bg,"def"))
	if (XAllocNamedColor(dpy, cmap, bg, &color, &dummy))
	    XtSetArg(args[n],XmNrenditionBackground,color.pixel), n++;
    if (debug)
	fprintf(stderr,"rend_cb: %s: %d %s/%s\n",arg->tag,n,fg,bg);
    rend = XmRenditionCreate(app_shell, arg->tag, args, n);
    arg->render_table = XmRenderTableAddRenditions
	(arg->render_table,&rend,1,XmMERGE_NEW);
}
#endif

static void create_vtx(void)
{
    Widget shell,label;

    shell = XtVaCreateWidget("vtx",transientShellWidgetClass,
			     app_shell,
			     XtNoverrideRedirect,True,
			     XtNvisual,vinfo.visual,
			     XtNcolormap,colormap,
			     XtNdepth,vinfo.depth,
			     NULL);
    label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, shell,
				    NULL);
#if TT
    XtAddCallback(XmGetXmDisplay(dpy),XmNnoRenditionCallback,rend_cb,NULL);
#endif
#ifdef HAVE_ZVBI
    vtx = vbi_render_init(shell,label,NULL);
#endif
}

#if TT
static void
display_vtx(struct TEXTELEM *tt)
{
    static int first = 1;
    Dimension x,y,w,h,sw,sh;
    char tag[32];
    int i,lastline;
    XmString str,elem;

    if (NULL == tt) {
	XtPopdown(vtx_shell);
	return;
    }

    /* build xmstring */
    str = NULL;
    lastline = tt[0].line;
    for (i = 0; tt[i].len > 0; i++) {
	tt[i].str[tt[i].len] = 0;
	sprintf(tag,"%s_on_%s",
		tt[i].fg ? tt[i].fg : "def",
		tt[i].bg ? tt[i].bg : "def");
	if (NULL != str && tt[i].line != lastline) {
	    lastline = tt[i].line;
	    str = XmStringConcatAndFree(str,XmStringSeparatorCreate());
	}
	elem = XmStringGenerate(tt[i].str, NULL, XmMULTIBYTE_TEXT, tag);
	if (NULL != str)
	    str = XmStringConcatAndFree(str,elem);
	else
	    str = elem;
    }
    XtVaSetValues(vtx_label,XmNlabelString,str,NULL);
    XmStringFree(str);

    /* show popup */
    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,NULL);
    XtVaGetValues(vtx_shell,XtNwidth,&sw,XtNheight,&sh,NULL);
    XtVaSetValues(vtx_shell,XtNx,x+(w-sw)/2,XtNy,y+h-10-sh,NULL);
    XtPopup(vtx_shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(vtx_shell),1);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(vtx_shell), left_ptr);
	XDefineCursor(dpy, XtWindow(vtx_label), left_ptr);
    }
}
#endif

#ifdef HAVE_ZVBI
static void
display_subtitle(struct vbi_page *pg, struct vbi_rect *rect)
{
    static int first = 1;
    static Pixmap pix;
    Dimension x,y,w,h,sw,sh;

    if (NULL == pg) {
	XtPopdown(vtx->shell);
	return;
    }

    if (pix)
	XFreePixmap(dpy,pix);
    pix = vbi_export_pixmap(vtx,pg,rect);
    XtVaSetValues(vtx->tt,XmNlabelPixmap,pix,XmNlabelType,XmPIXMAP,NULL);

    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,NULL);
    XtVaGetValues(vtx->shell,XtNwidth,&sw,XtNheight,&sh,NULL);
    XtVaSetValues(vtx->shell,XtNx,x+(w-sw)/2,XtNy,y+h-10-sh,NULL);
    XtPopup(vtx->shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(vtx->shell),1);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(vtx->shell), left_ptr);
	XDefineCursor(dpy, XtWindow(vtx->tt), left_ptr);
    }
}
#endif

/*----------------------------------------------------------------------*/

static void
new_attr(struct ng_attribute *attr, int val)
{
    struct motif_attribute *a;
    WidgetList children;
    Cardinal nchildren;
    unsigned int i;

    for (a = motif_attrs; NULL != a; a = a->next) {
	if (a->attr->id == attr->id)
	    break;
    }
    if (NULL == a)
	return;

    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	XtVaGetValues(a->widget,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	for (i = 0; i < nchildren; i++) {
	    XmToggleButtonSetState(children[i],a->attr->choices[i].nr == val,
				   False);
	}
	break;
    case ATTR_TYPE_BOOL:
	XmToggleButtonSetState(a->widget,val,False);
	break;
    case ATTR_TYPE_INTEGER:
	XmScaleSetValue(a->widget,val);
	break;
    }
    return;
}

static void
new_volume(void)
{
    struct ng_attribute *attr;

    attr = ng_attr_byid(attrs,ATTR_ID_VOLUME);
    if (NULL != attr)
	new_attr(attr,cur_attrs[ATTR_ID_VOLUME]);
    attr = ng_attr_byid(attrs,ATTR_ID_MUTE);
    if (NULL != attr)
	new_attr(attr,cur_attrs[ATTR_ID_MUTE]);
}

static void
new_freqtab(void)
{
    WidgetList children;
    Cardinal nchildren;
    XmStringTable tab;
    int i;

    /* update menu */
    XtVaGetValues(freq_menu,XtNchildren,&children,
		  XtNnumChildren,&nchildren,NULL);
    for (i = 0; chanlist_names[i].str != NULL; i++)
	XmToggleButtonSetState(children[i],chanlist_names[i].nr == chantab,
			       False);

    /* update property window */
    tab = malloc(chancount*sizeof(*tab));
    for (i = 0; i < chancount; i++)
	tab[i] = XmStringGenerate(chanlist[i].name,
				  NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(prop_channel,
		  XmNitemCount,chancount,
		  XmNitems,tab,
		  XmNselectedItem,tab[0],
		  NULL);
    for (i = 0; i < chancount; i++)
	XmStringFree(tab[i]);
    free(tab);
}

/*----------------------------------------------------------------------*/

static void
chan_key_eh(Widget widget, XtPointer client_data, XEvent *event, Boolean *cont)
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
    XmTextSetString(prop_key,line);
}

static void
chan_add_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmString str;
    int i;

    /* init stuff */
    prop_button = NULL;
    XmTextSetString(prop_name,"");
    XmTextSetString(prop_key,"");
    XmTextSetString(prop_group,"main");
    i = (-1 == cur_channel) ? 0 : cur_channel;
    str = XmStringGenerate(chanlist[i].name, NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(prop_channel,XmNselectedItem,str,NULL);
    XmStringFree(str);

    XtManageChild(prop_dlg);
}

static void
chan_edit_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmString str;
    int i;

    /* find channel */
    prop_button = clientdata;
    for (i = 0; i < count; i++)
	if (prop_button == channels[i]->button)
	    break;
    if (i == count)
	return;

    /* init stuff */
    XmTextSetString(prop_name,channels[i]->name);
    XmTextSetString(prop_key,channels[i]->key);
    XmTextSetString(prop_group,channels[i]->group);
    i = (-1 == channels[i]->channel) ? 0 : channels[i]->channel;
    str = XmStringGenerate(chanlist[i].name, NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(prop_channel,XmNselectedItem,str,NULL);
    XmStringFree(str);

    XtManageChild(prop_dlg);
}

static void
chan_apply_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *name, *key, *cname, *group;
    struct CHANNEL *c;
    XmString str;
    int i,channel;
    Widget msgbox;

    /* find channel */
    i = count;
    if (NULL != prop_button)
	for (i = 0; i < count; i++)
	    if (prop_button == channels[i]->button)
		break;

    name  = XmTextGetString(prop_name);
    key   = XmTextGetString(prop_key);
    group = XmTextGetString(prop_group);
    if (0 == strlen(group))
	group = "main";

    XtVaGetValues(prop_channel,XmNselectedItem,&str,NULL);
    cname = XmStringUnparse(str,NULL,XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
			    NULL,0,0);
    channel = lookup_channel(cname);

    if (0 == strlen(name)) {
	msgbox = XmCreateErrorDialog(prop_dlg,"no_name",NULL,0);
	XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_HELP_BUTTON));
	XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_CANCEL_BUTTON));
	XtAddCallback(msgbox,XmNokCallback,destroy_cb,msgbox);
	XtManageChild(msgbox);
	return;
    }

    if (i == count) {
	/* add */
	c = add_channel(name);
	if (strlen(key) > 0)
	    c->key = strdup(key);
	c->cname   = strdup(cname);
	c->group   = strdup(group);
	c->channel = channel;
    } else {
	/* update */
	c = channels[i];
	free(c->name);
	c->name = strdup(name);
	if (c->key) {
	    free(c->key);
	    c->key = NULL;
	}
	if (0 != strlen(key))
	    c->key = strdup(key);
	c->cname   = strdup(cname);
	c->group   = strdup(group);
	c->channel = channel;
	XtRemoveAllCallbacks(c->button, XmNactivateCallback);
	add_cmd_callback(c->button, XmNactivateCallback,
			 "setstation", c->name, NULL);
	toolkit_set_label(c->button,c->name);
    }
#if 0
    c->input   = cur_attrs[ATTR_ID_INPUT];
    c->fine    = cur_fine;
#endif
    channel_menu();
}

static void
chan_save_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    save_config();
}

static void
chan_tune_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmComboBoxCallbackStruct *cb = call_data;
    char *line;

    line = XmStringUnparse(cb->item_or_text,NULL,
			   XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
			   NULL,0,0);
    do_va_cmd(3,"setchannel",line);
}

static void
create_prop(void)
{
    Widget label,rowcol;

    prop_dlg = XmCreatePromptDialog(control_shell,"prop",NULL,0);
    XtAddEventHandler(XtParent(prop_dlg), (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XtUnmanageChild(XmSelectionBoxGetChild(prop_dlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(prop_dlg,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(prop_dlg,XmDIALOG_TEXT));

    rowcol = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, prop_dlg,
				     NULL);
    label = XtVaCreateManagedWidget("nameL", xmLabelWidgetClass, rowcol,
				    NULL);
    prop_name = XtVaCreateManagedWidget("name", xmTextWidgetClass, rowcol,
					NULL);

    label = XtVaCreateManagedWidget("keyL", xmLabelWidgetClass, rowcol,
				    NULL);
    prop_key = XtVaCreateManagedWidget("key", xmTextWidgetClass, rowcol,
				       NULL);
    XtAddEventHandler(prop_key, KeyPressMask, False, chan_key_eh, NULL);

    label = XtVaCreateManagedWidget("groupL", xmLabelWidgetClass, rowcol,
				    NULL);
    prop_group = XtVaCreateManagedWidget("group", xmTextWidgetClass, rowcol,
					 NULL);

    label = XtVaCreateManagedWidget("channelL", xmLabelWidgetClass, rowcol,
				    NULL);
    prop_channel = XtVaCreateManagedWidget("channel",xmComboBoxWidgetClass,
					   rowcol,NULL);
    XtAddCallback(prop_channel,XmNselectionCallback, chan_tune_cb, NULL);

    XtAddCallback(prop_dlg,XmNokCallback, chan_apply_cb, NULL);
}

/*----------------------------------------------------------------------*/

static void
filter_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct filter_attribute *f = clientdata;
    Widget w;

    switch (f->attr->type) {
    case ATTR_TYPE_INTEGER:
	XmScaleGetValue(f->widget,&f->value);
	break;
    case ATTR_TYPE_BOOL:
	f->value = XmToggleButtonGetState(f->widget);
	break;
    case ATTR_TYPE_CHOICE:
	w = NULL;
	XtVaGetValues(f->widget,XmNmenuHistory,&w,NULL);
	f->value = ng_attr_getint(f->attr,XtName(w));
	break;
    }
    f->attr->write(f->attr,f->value);
}

static void
filter_add_ctrls(Widget rc, struct ng_filter *filter,
		 struct ng_attribute *attr)
{
    struct filter_attribute *f;
    Widget opt,push;
    XmString str;
    Arg args[2];
    int i;

    f = malloc(sizeof(*f));
    memset(f,0,sizeof(*f));
    f->filter = filter;
    f->attr   = attr;
    f->next   = filter_attrs;
    f->value  = attr->read(attr);
    filter_attrs = f;

    str = XmStringGenerate((char*)attr->name, NULL, XmMULTIBYTE_TEXT, NULL);
    switch (attr->type) {
    case ATTR_TYPE_INTEGER:
	f->widget = XtVaCreateManagedWidget("scale",
					    xmScaleWidgetClass,rc,
					    XmNtitleString,str,
					    XmNminimum,attr->min,
					    XmNmaximum,attr->max,
					    XmNdecimalPoints,attr->points,
					    NULL);
	XmScaleSetValue(f->widget,f->value);
	XtAddCallback(f->widget,XmNvalueChangedCallback,filter_cb,f);
	break;
    case ATTR_TYPE_BOOL:
	f->widget = XtVaCreateManagedWidget("bool",
					    xmToggleButtonWidgetClass,rc,
					    XmNlabelString,str,
					    NULL);
	XmToggleButtonSetState(f->widget,f->value,False);
	XtAddCallback(f->widget,XmNvalueChangedCallback,filter_cb,f);
	break;
    case ATTR_TYPE_CHOICE:
	f->widget = XmCreatePulldownMenu(rc,"choiceM",NULL,0);
	XtSetArg(args[0],XmNsubMenuId,f->widget);
	XtSetArg(args[1],XmNlabelString,str);
	opt = XmCreateOptionMenu(rc,"choiceO",args,2);
	XtManageChild(opt);
	for (i = 0; attr->choices[i].str != NULL; i++) {
	    push = XtVaCreateManagedWidget(attr->choices[i].str,
					   xmPushButtonWidgetClass,f->widget,
					   NULL);
	    XtAddCallback(push,XmNactivateCallback,filter_cb,f);
	    if (f->value == attr->choices[i].nr)
		XtVaSetValues(f->widget,XmNmenuHistory,push,NULL);
	}
	break;
    }
    XmStringFree(str);
}

static void
create_filter_prop(void)
{
    Widget rc1,frame,rc2;
    XmString str;
    struct list_head *item;
    struct ng_filter *filter;
    int j;

    filter_shell = XtVaAppCreateShell("filter","MoTV",
				      topLevelShellWidgetClass,
				      dpy,
				      XtNclientLeader,app_shell,
				      XtNvisual,vinfo.visual,
				      XtNcolormap,colormap,
				      XtNdepth,vinfo.depth,
				      XmNdeleteResponse,XmDO_NOTHING,
				      NULL);
    XtAddEventHandler(filter_shell, (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XmAddWMProtocolCallback(filter_shell,WM_DELETE_WINDOW,
			    popupdown_cb,filter_shell);

    rc1 = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, filter_shell,
				  NULL);

    list_for_each(item,&ng_filters) {
	filter = list_entry(item, struct ng_filter, list);
	if (NULL == filter->attrs)
	    continue;
	str = XmStringGenerate(filter->name, NULL,
			       XmMULTIBYTE_TEXT, NULL);
	frame = XtVaCreateManagedWidget("frame",xmFrameWidgetClass,rc1,NULL);
	XtVaCreateManagedWidget("label",xmLabelWidgetClass,frame,
				XmNlabelString,str,
				NULL);
	rc2 = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,frame,NULL);
	XmStringFree(str);
	for (j = 0; NULL != filter->attrs[j].name; j++)
	    filter_add_ctrls(rc2,filter,&filter->attrs[j]);
    }
}

/*----------------------------------------------------------------------*/

static void
scroll_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct motif_attribute *a = clientdata;
    int ret;
    char val[10];

    XmScaleGetValue(a->widget,&ret);
    sprintf(val,"%d", ret);
    do_va_cmd(3,"setattr",a->attr->name,val);
}


void
add_cmd_callback(Widget widget, String callback,
		 char *command, const char *arg1, const char *arg2)
{
    struct DO_CMD *cmd;

    cmd = malloc(sizeof(*cmd));
    cmd->argc    = 1;
    cmd->argv[0] = command;
    if (arg1) {
	cmd->argc    = 2;
	cmd->argv[1] = (char*)arg1;
    }
    if (arg2) {
	cmd->argc    = 3;
	cmd->argv[2] = (char*)arg2;
    }
    XtAddCallback(widget,callback,command_cb,cmd);
    XtAddCallback(widget,XmNdestroyCallback,free_cb,cmd);
}

static Widget
add_cmd_menuitem(const char *n, int nr, Widget parent, const char *l,
		 char *k, char *a,  int toggle,
		 char *c, const char *arg1, const char *arg2)
{
    char name[16];
    XmString label,accel;
    Widget w;
    WidgetClass class;
    String callback;

    sprintf(name,"%.10s%d",n,nr);
    label = XmStringGenerate((char*)l, NULL, XmMULTIBYTE_TEXT, NULL);
    if (toggle) {
	class    = xmToggleButtonWidgetClass;
	callback = XmNvalueChangedCallback;
    } else {
	class    = xmPushButtonWidgetClass;
	callback = XmNactivateCallback;
    }
    if (k && a) {
	accel = XmStringGenerate(k, NULL, XmMULTIBYTE_TEXT, NULL);
	w = XtVaCreateManagedWidget(name,class,parent,
				    XmNlabelString,label,
				    XmNacceleratorText,accel,
				    XmNaccelerator,a,
				    NULL);
    } else {
	w = XtVaCreateManagedWidget(name,class,parent,
				    XmNlabelString,label,
				    NULL);
    }
    if (toggle)
	XtVaSetValues(w,XmNindicatorType,toggle,NULL);
    if (c)
	add_cmd_callback(w,callback,c,arg1,arg2);
    XmStringFree(label);
    return w;
}

static void
add_attr_option(Widget menu, struct ng_attribute *attr)
{
    int i;
    struct motif_attribute *a;

    a = malloc(sizeof(*a));
    memset(a,0,sizeof(*a));
    a->attr = attr;

    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	a->widget = XmCreatePulldownMenu(menu,(char*)attr->name,NULL,0);
	XtVaCreateManagedWidget(attr->name,xmCascadeButtonWidgetClass,menu,
				XmNsubMenuId,a->widget,NULL);
	for (i = 0; attr->choices[i].str != NULL; i++)
	    add_cmd_menuitem(attr->name, i, a->widget,
			     attr->choices[i].str, NULL, NULL, XmONE_OF_MANY,
			     "setattr",attr->name,attr->choices[i].str);
	break;
    case ATTR_TYPE_BOOL:
	a->widget = XtVaCreateManagedWidget(attr->name,
					    xmToggleButtonWidgetClass,menu,
					    NULL);
	add_cmd_callback(a->widget,XmNvalueChangedCallback,
			 "setattr", attr->name, "toggle");
	break;
    }
    a->next = motif_attrs;
    motif_attrs = a;
}

/* use more columns until the menu fits onto the screen */
static void
menu_cols_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Dimension height,num;
    int i = 8;

    for (;i;i--) {
	XtVaGetValues(widget,
		      XtNheight,&height,
		      XmNnumColumns,&num,
		      NULL);
	if (height < XtScreen(widget)->height - 100)
	    break;
	XtVaSetValues(widget,XmNnumColumns,num+1,NULL);
    }
}

void
channel_menu(void)
{
    struct {
	char *name;
	Widget menu1;
	Widget menu2;
    } *sub = NULL;
    int subs = 0;

    Widget menu1,menu2;
    char ctrl[16],key[32],accel[64];
    int  i,j;

    if (0 == st_menu2) {
	st_menu2 = XmCreatePopupMenu(tv,"stationsM",NULL,0);
	XtAddEventHandler(tv,ButtonPressMask,False,popup_eh,st_menu2);
	XtAddCallback(st_menu2,XmNmapCallback,menu_cols_cb,NULL);
    }

    /* delete entries */
    delete_children(st_menu1);
    delete_children(st_menu2);

    /* rebuild everything */
    for (i = 0; i < count; i++) {
	if (channels[i]->key) {
	    if (2 == sscanf(channels[i]->key,
			    "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key)) {
		sprintf(accel,"%s<Key>%s",ctrl,key);
	    } else {
		sprintf(accel,"<Key>%s",channels[i]->key);
	    }
	} else {
	    accel[0] = 0;
	}

	menu1 = st_menu1;
	menu2 = st_menu2;
	if (0 != strcmp(channels[i]->group,"main")) {
	    for (j = 0; j < subs; j++)
		if (0 == strcmp(channels[i]->group,sub[j].name))
		    break;
	    if (j == subs) {
		subs++;
		sub = realloc(sub, subs * sizeof(*sub));
		sub[j].name  = channels[i]->group;
		sub[j].menu1 = XmCreatePulldownMenu(st_menu1,
						    channels[i]->group,
						    NULL,0);
		sub[j].menu2 = XmCreatePulldownMenu(st_menu2,
						    channels[i]->group,
						    NULL,0);
		XtVaCreateManagedWidget(channels[i]->group,
					xmCascadeButtonWidgetClass, st_menu1,
					XmNsubMenuId, sub[j].menu1,
					NULL);
		XtVaCreateManagedWidget(channels[i]->group,
					xmCascadeButtonWidgetClass, st_menu2,
					XmNsubMenuId, sub[j].menu2,
					NULL);
	    }
	    menu1 = sub[j].menu1;
	    menu2 = sub[j].menu2;
	}

	add_cmd_menuitem("station", i, menu1,
			 channels[i]->name, channels[i]->key, accel, FALSE,
			 "setstation",channels[i]->name,NULL);
	add_cmd_menuitem("station", i, menu2,
			 channels[i]->name, channels[i]->key, accel, FALSE,
			 "setstation",channels[i]->name,NULL);
	if (NULL == channels[i]->button)
	    chan_makebutton(channels[i]);
    }
    free(sub);
    calc_frequencies();
}

static void
chan_resize_eh(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    Widget clip;
    Dimension width;

    XtVaGetValues(chan_viewport,XmNclipWindow,&clip,NULL);
    XtVaGetValues(clip,XtNwidth,&width,NULL);
    XtVaSetValues(chan_box,XtNwidth,width,NULL);
}

static void
chan_del_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget button = clientdata;
    int i;

    for (i = 0; i < count; i++)
	if (button == channels[i]->button)
	    break;
    if (i == count)
	return;
    XtDestroyWidget(channels[i]->button);
    del_channel(i);
    if (cur_sender == i)
	cur_sender = -1;
    if (cur_sender > i)
	cur_sender--;
    channel_menu();
}

static void
chan_makebutton(struct CHANNEL *channel)
{
    Widget menu,push;

    if (NULL != channel->button)
	return;

    channel->button =
	XtVaCreateManagedWidget(channel->name,
				xmPushButtonWidgetClass, chan_box,
				NULL);
    add_cmd_callback(channel->button, XmNactivateCallback,
		     "setstation", channel->name, NULL);
    menu = XmCreatePopupMenu(channel->button,"menu",NULL,0);
    XtAddEventHandler(channel->button,ButtonPressMask,False,popup_eh,menu);
    push = XtVaCreateManagedWidget("del",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,chan_del_cb,channel->button);
    push = XtVaCreateManagedWidget("edit",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,chan_edit_cb,channel->button);
}

static void
create_control(void)
{
    Widget form,menubar,menu,smenu,push,clip,tool,rc,fr;
    char action[256];
    int i;

    control_shell = XtVaAppCreateShell("control","MoTV",
				       topLevelShellWidgetClass,
				       dpy,
				       XtNclientLeader,app_shell,
				       XtNvisual,vinfo.visual,
				       XtNcolormap,colormap,
				       XtNdepth,vinfo.depth,
				       XmNdeleteResponse,XmDO_NOTHING,
				       NULL);
    XtAddEventHandler(control_shell, (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XmAddWMProtocolCallback(control_shell,WM_DELETE_WINDOW,
			    popupdown_cb,control_shell);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, control_shell,
				   NULL);

    /* menbar */
    menubar = XmCreateMenuBar(form,"menubar",NULL,0);
    XtManageChild(menubar);

    tool = XtVaCreateManagedWidget("tool",xmRowColumnWidgetClass,form,NULL);

    /* status line */
    rc = XtVaCreateManagedWidget("status", xmRowColumnWidgetClass, form,
				 NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_freq = XtVaCreateManagedWidget("freq", xmLabelWidgetClass, fr, NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_chan = XtVaCreateManagedWidget("chan", xmLabelWidgetClass, fr, NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_name = XtVaCreateManagedWidget("name", xmLabelWidgetClass, fr, NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_key = XtVaCreateManagedWidget("key", xmLabelWidgetClass, fr, NULL);
#if 0
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_other = XtVaCreateManagedWidget("other", xmLabelWidgetClass, fr, NULL);
#endif

    /* channel buttons */
    chan_viewport = XmCreateScrolledWindow(form,"view",NULL,0);
    XtManageChild(chan_viewport);
    chan_box = XtVaCreateManagedWidget("box", xmRowColumnWidgetClass,
				       chan_viewport, NULL);
    XtVaGetValues(chan_viewport,XmNclipWindow,&clip,NULL);
    XtAddEventHandler(clip,StructureNotifyMask, True,
		      chan_resize_eh, NULL);

    /* menu - file */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("rec",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,popupdown_cb,str_shell);
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("quit",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,ExitCB,NULL);

#if 1
    /* menu - edit */
    if (f_drv & CAN_CAPTURE) {
	menu = XmCreatePulldownMenu(menubar,"editM",NULL,0);
	XtVaCreateManagedWidget("edit",xmCascadeButtonWidgetClass,menubar,
				XmNsubMenuId,menu,NULL);
	push = XtVaCreateManagedWidget("copy",xmPushButtonWidgetClass,menu,
				       NULL);
	XtAddCallback(push,XmNactivateCallback,action_cb,"Ipc(clipboard)");
    }
#endif

    /* menu - tv stations */
    st_menu1 = XmCreatePulldownMenu(menubar,"stationsM",NULL,0);
    XtVaCreateManagedWidget("stations",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,st_menu1,NULL);
    XtAddCallback(st_menu1,XmNmapCallback,menu_cols_cb,NULL);

    /* menu - tools (name?) */
    menu = XmCreatePulldownMenu(menubar,"toolsM",NULL,0);
    XtVaCreateManagedWidget("tools",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    w_full = XtVaCreateManagedWidget("full",xmToggleButtonWidgetClass,menu,
				     NULL);
    XtAddCallback(w_full,XmNvalueChangedCallback,action_cb,
		  "Command(fullscreen)");
    push = XtVaCreateManagedWidget("ontop",xmToggleButtonWidgetClass,menu,
				   NULL);
    b_ontop = push;
    XtAddCallback(push,XmNvalueChangedCallback,ontop_cb,NULL);
    push = XtVaCreateManagedWidget("levels",xmPushButtonWidgetClass,menu,
				   NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Popup(levels)");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("st_up",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,next)");
    push = XtVaCreateManagedWidget("st_dn",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,prev)");

    /* menu - tools / tuner */
    smenu = XmCreatePulldownMenu(menu,"tuneM",NULL,0);
    XtVaCreateManagedWidget("tune",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("ch_up",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,next)");
    push = XtVaCreateManagedWidget("ch_dn",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,prev)");
    push = XtVaCreateManagedWidget("fi_up",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,fine_up)");
    push = XtVaCreateManagedWidget("fi_dn",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,fine_down)");

    /* menu - tools / capture */
    smenu = XmCreatePulldownMenu(menu,"grabM",NULL,0);
    XtVaCreateManagedWidget("grab",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("ppm_f",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,ppm,full)");
    push = XtVaCreateManagedWidget("ppm_w",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,ppm,win)");
    push = XtVaCreateManagedWidget("jpg_f",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,jpeg,full)");
    push = XtVaCreateManagedWidget("jpg_w",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,jpeg,win)");

    /* menu - tools / aspect ratio */
    smenu = XmCreatePulldownMenu(menu,"ratioM",NULL,0);
    XtVaCreateManagedWidget("ratio",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("r_no",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Ratio(0,0)");
    push = XtVaCreateManagedWidget("r_43",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Ratio(4,3)");

    /* menu - tools / launch */
    launch_menu = XmCreatePulldownMenu(menu,"launchM",NULL,0);
    XtVaCreateManagedWidget("launch",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,launch_menu,NULL);

#ifdef HAVE_ZVBI
    /* menu - tools / subtitles */
    smenu = XmCreatePulldownMenu(menu,"subM",NULL,0);
    XtVaCreateManagedWidget("sub",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("s_off",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(stop)");
    push = XtVaCreateManagedWidget("s_150",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,150)");
    push = XtVaCreateManagedWidget("s_333",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,333)");
    push = XtVaCreateManagedWidget("s_777",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,777)");
    push = XtVaCreateManagedWidget("s_801",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,801)");
    push = XtVaCreateManagedWidget("s_888",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,888)");
#endif

    /* menu - internal options */
    opt_menu = menu = XmCreatePulldownMenu(menubar,"optionsM",NULL,0);
    XtVaCreateManagedWidget("options",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("add",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,chan_add_cb,NULL);
#ifdef HAVE_ZVBI
    push = XtVaCreateManagedWidget("scan",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,chscan_cb,NULL);
#endif
#if 1
    push = XtVaCreateManagedWidget("pref",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,pref_manage_cb,NULL);
#endif
    push = XtVaCreateManagedWidget("save",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,chan_save_cb,NULL);
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);

    cap_menu = XmCreatePulldownMenu(menu,"captureM",NULL,0);
    XtVaCreateManagedWidget("capture",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,cap_menu,NULL);
    push = XtVaCreateManagedWidget("overlay",xmToggleButtonWidgetClass,
				   cap_menu,XmNindicatorType,XmONE_OF_MANY,
				   NULL);
    add_cmd_callback(push,XmNvalueChangedCallback,"capture","overlay",NULL);
    push = XtVaCreateManagedWidget("grabdisplay",xmToggleButtonWidgetClass,
				   cap_menu,XmNindicatorType,XmONE_OF_MANY,
				   NULL);
    add_cmd_callback(push,XmNvalueChangedCallback,"capture","grab",NULL);
    push = XtVaCreateManagedWidget("none",xmToggleButtonWidgetClass,
				   cap_menu,XmNindicatorType,XmONE_OF_MANY,
				   NULL);
    add_cmd_callback(push,XmNvalueChangedCallback,"capture","off",NULL);

    freq_menu = XmCreatePulldownMenu(menu,"freqM",NULL,0);
    XtVaCreateManagedWidget("freq",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,freq_menu,NULL);
    for (i = 0; chanlist_names[i].str != NULL; i++) {
	push = XtVaCreateManagedWidget(chanlist_names[i].str,
				       xmToggleButtonWidgetClass,freq_menu,
				       XmNindicatorType,XmONE_OF_MANY,
				       NULL);
	add_cmd_callback(push,XmNvalueChangedCallback,
			 "setfreqtab", chanlist_names[i].str, NULL);
    }
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);

    /* menu - filter */
    if ((f_drv & CAN_CAPTURE)  &&  !list_empty(&ng_filters))  {
	struct list_head *item;
	struct ng_filter *filter;

	menu = XmCreatePulldownMenu(menubar,"filterM",NULL,0);
	XtVaCreateManagedWidget("filter",xmCascadeButtonWidgetClass,menubar,
				XmNsubMenuId,menu,NULL);
	push = XtVaCreateManagedWidget("fnone",
				       xmPushButtonWidgetClass,menu,
				       NULL);
	XtAddCallback(push,XmNactivateCallback,action_cb,"Filter()");
	list_for_each(item,&ng_filters) {
	    filter = list_entry(item, struct ng_filter, list);
	    push = XtVaCreateManagedWidget(filter->name,
					   xmPushButtonWidgetClass,menu,
					   NULL);
	    sprintf(action,"Filter(%s)",filter->name);
	    XtAddCallback(push,XmNactivateCallback,action_cb,strdup(action));
	}
	XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
	push = XtVaCreateManagedWidget("fopts",xmPushButtonWidgetClass,menu,
				       NULL);
	XtAddCallback(push,XmNactivateCallback,action_cb,"Popup(filter)");
    }

    /* menu - help */
    menu = XmCreatePulldownMenu(menubar,"helpM",NULL,0);
    push = XtVaCreateManagedWidget("help",xmCascadeButtonWidgetClass,menubar,
				   XmNsubMenuId,menu,NULL);
    XtVaSetValues(menubar,XmNmenuHelpWidget,push,NULL);
    push = XtVaCreateManagedWidget("man",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,man_cb,"motv");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("about",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,about_cb,NULL);

    /* toolbar */
    push = XtVaCreateManagedWidget("prev",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,prev)");
    push = XtVaCreateManagedWidget("next",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,next)");

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,tool,NULL);
    push = XtVaCreateManagedWidget("snap",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,jpeg,full)");
    push = XtVaCreateManagedWidget("movie",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,popupdown_cb,str_shell);
    push = XtVaCreateManagedWidget("mute",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(volume,mute,toggle)");

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,tool,NULL);
    push = XtVaCreateManagedWidget("exit",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,ExitCB,NULL);
}

static void create_attr_widgets(void)
{
    struct ng_attribute *attr;
    Widget push;
    XmString label,accel;
    char str[100],key[32],ctrl[16];
    Arg argv[8];
    Cardinal argc;
    int i;

    /* menu - driver options
       input + norm */
    attr = ng_attr_byid(attrs,ATTR_ID_NORM);
    if (NULL != attr)
	add_attr_option(opt_menu, attr);
    attr = ng_attr_byid(attrs,ATTR_ID_INPUT);
    if (NULL != attr)
	add_attr_option(opt_menu,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_AUDIO_MODE);
    if (NULL != attr)
	add_attr_option(opt_menu,attr);
    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_CHOICE)
	    continue;
	add_attr_option(opt_menu,attr);
    }

    /* bools */
    attr = ng_attr_byid(attrs,ATTR_ID_MUTE);
    if (NULL != attr)
	add_attr_option(opt_menu,attr);
    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_BOOL)
	    continue;
	add_attr_option(opt_menu,attr);
    }

    /* integer (scales) */
    push = XtVaCreateManagedWidget("scale",xmPushButtonWidgetClass,
				   opt_menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Popup(scale)");

    /* launch menu entries */
    for (i = 0; i < nlaunch; i++) {
	argc  = 0;
	label = NULL;
	accel = NULL;

	label = XmStringGenerate(launch[i].name,NULL,XmMULTIBYTE_TEXT,NULL);
	XtSetArg(argv[argc],XmNlabelString,label); argc++;
	if (NULL != launch[i].key) {
	    accel = XmStringGenerate(launch[i].key,NULL,XmMULTIBYTE_TEXT,NULL);
	    XtSetArg(argv[argc],XmNacceleratorText,accel); argc++;
	    if (2 == sscanf(launch[i].key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key))
		sprintf(str,"%s<Key>%s",ctrl,key);
	    else
		sprintf(str,"<Key>%s",launch[i].key);
	    XtSetArg(argv[argc],XmNaccelerator,str); argc++;
	}
	push = XtCreateManagedWidget(launch[i].name,
				     xmPushButtonWidgetClass,
				     launch_menu,argv,argc);
	if (label)
	    XmStringFree(label);
	if (accel)
	    XmStringFree(accel);

	/* translations */
	strcat(str,": Launch(");
	strcat(str,launch[i].name);
	strcat(str,")");
	XtOverrideTranslations(tv,XtParseTranslationTable(str));

	/* button callback */
	sprintf(str,"Launch(%s)",launch[i].name);
	XtAddCallback(push,XmNactivateCallback,action_cb,
		      strdup(str));
    }
}

static void
create_scale(void)
{
    Widget form,attach;
    struct ng_attribute *attr;
    struct motif_attribute *a;
    int vol = 0;
    XmString str;

    scale_shell = XtVaAppCreateShell("scale","MoTV",
				     topLevelShellWidgetClass,
				     dpy,
				     XtNclientLeader,app_shell,
				     XtNvisual,vinfo.visual,
				     XtNcolormap,colormap,
				     XtNdepth,vinfo.depth,
				     XmNdeleteResponse,XmDO_NOTHING,
				     NULL);
    XtAddEventHandler(scale_shell, (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XmAddWMProtocolCallback(scale_shell,WM_DELETE_WINDOW,
			    popupdown_cb,scale_shell);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, scale_shell,
				   NULL);
    /* scales */
    attach = NULL;
    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->type != ATTR_TYPE_INTEGER)
	    continue;
	if (attr->id == ATTR_ID_VOLUME) {
	    if (vol)
		continue;
	    vol++;
	}
	a = malloc(sizeof(*a));
	memset(a,0,sizeof(*a));
	a->attr = attr;
	a->next = motif_attrs;
	motif_attrs = a;
	if (a->attr->id < ATTR_ID_COUNT) {
	    a->widget = XtVaCreateManagedWidget(attr->name,
						xmScaleWidgetClass, form,
						XmNtopWidget,attach,
						XmNminimum,attr->min,
						XmNmaximum,attr->max,
						XmNdecimalPoints,attr->points,
						NULL);
	} else {
	    str = XmStringGenerate((char*)attr->name, NULL,
				   XmMULTIBYTE_TEXT, NULL);
	    a->widget = XtVaCreateManagedWidget(attr->name,
						xmScaleWidgetClass, form,
						XmNtopWidget,attach,
						XmNtitleString,str,
						XmNminimum,attr->min,
						XmNmaximum,attr->max,
						XmNdecimalPoints,attr->points,
						NULL);
	    XmStringFree(str);
	}
	XtAddCallback(a->widget,XmNvalueChangedCallback,scroll_cb,a);
	XtAddCallback(a->widget,XmNdragCallback,scroll_cb,a);
	attach = a->widget;
    }
}

/*----------------------------------------------------------------------*/

#if 0
void create_chanwin(void)
{
    Widget form,clip,menu,push;

}
#endif

/* gets called before switching away from a channel */
static void
pixit(void)
{
    Pixmap pix;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;

    if (cur_sender == -1)
	return;

    /* save picture settings */
    channels[cur_sender]->color    = cur_attrs[ATTR_ID_COLOR];
    channels[cur_sender]->bright   = cur_attrs[ATTR_ID_BRIGHT];
    channels[cur_sender]->hue      = cur_attrs[ATTR_ID_HUE];
    channels[cur_sender]->contrast = cur_attrs[ATTR_ID_CONTRAST];
    channels[cur_sender]->input    = cur_attrs[ATTR_ID_INPUT];
    channels[cur_sender]->norm     = cur_attrs[ATTR_ID_NORM];

    if (0 == pix_width || 0 == pix_height)
	return;

    /* capture mini picture */
    if (!(f_drv & CAN_CAPTURE))
	return;

    video_gd_suspend();
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = x11_dpy_fmtid;
    fmt.width  = pix_width;
    fmt.height = pix_height;
    if (NULL == (buf = ng_grabber_get_image(&fmt)))
	goto done1;
    buf = ng_filter_single(cur_filter,buf);
    if (0 == (pix = x11_create_pixmap(dpy,&vinfo,buf)))
	goto done2;
    x11_label_pixmap(dpy,colormap,pix,buf->fmt.height,
		     channels[cur_sender]->name);
    XtVaSetValues(channels[cur_sender]->button,
		  XmNlabelPixmap,pix,
		  XmNlabelType,XmPIXMAP,
		  NULL);
    if (channels[cur_sender]->pixmap)
	XFreePixmap(dpy,channels[cur_sender]->pixmap);
    channels[cur_sender]->pixmap = pix;
 done2:
    ng_release_video_buf(buf);
 done1:
    video_gd_restart();
}

/*----------------------------------------------------------------------*/

static void
do_capture(int from, int to, int tmp_switch)
{
    static int niced = 0;
    WidgetList children;

    /* off */
    switch (from) {
    case CAPTURE_GRABDISPLAY:
	video_gd_stop();
	if (!tmp_switch)
	    XClearArea(XtDisplay(tv), XtWindow(tv), 0,0,0,0, True);
	break;
    case CAPTURE_OVERLAY:
	video_overlay(0);
	break;
    }

    /* on */
    switch (to) {
    case CAPTURE_GRABDISPLAY:
	if (!niced)
	    nice(niced = 10);
	video_gd_start();
	break;
    case CAPTURE_OVERLAY:
	video_overlay(1);
	break;
    }

    /* update menu */
    XtVaGetValues(cap_menu,XtNchildren,&children,NULL);
    XmToggleButtonSetState(children[0],to == CAPTURE_OVERLAY,    False);
    XmToggleButtonSetState(children[1],to == CAPTURE_GRABDISPLAY,False);
    XmToggleButtonSetState(children[2],to == CAPTURE_OFF,        False);
}

static void
do_motif_fullscreen(void)
{
    XmToggleButtonSetState(w_full,!fs,False);
    do_fullscreen();
}

/*----------------------------------------------------------------------*/

struct FILE_DATA {
    Widget filebox;
    Widget text;
    Widget push;
};

static void
file_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmFileSelectionBoxCallbackStruct *cb = call_data;
    struct FILE_DATA *h = clientdata;
    char *line;

    if (cb->reason == XmCR_OK) {
	line = XmStringUnparse(cb->value,NULL,
			       XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
			       NULL,0,0);
	XmTextSetString(h->text,line);
    }
    XtUnmanageChild(h->filebox);
}

static void
file_browse_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct FILE_DATA *h = clientdata;
    Widget help;
    /* XmString str; */

    if (NULL == h->filebox) {
	h->filebox = XmCreateFileSelectionDialog(h->push,"filebox",NULL,0);
	help = XmFileSelectionBoxGetChild(h->filebox,XmDIALOG_HELP_BUTTON);
	XtUnmanageChild(help);
	XtAddCallback(h->filebox,XmNokCallback,file_done_cb,h);
	XtAddCallback(h->filebox,XmNcancelCallback,file_done_cb,h);
    }
#if 0
    str = XmStringGenerate(XmTextGetString(h->text),
			   NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(h->filebox,XmNdirMask,str,NULL);
    XmStringFree(str);
#endif
    XtManageChild(h->filebox);
}

static void
exec_player_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    char *filename;

    filename = XmTextGetString(m_fvideo);
    exec_player(filename);
}

static void
create_strwin(void)
{
    Widget form,push,rowcol,frame,fbox;
    struct FILE_DATA *h;
    Arg args[2];

    str_shell = XtVaAppCreateShell("streamer", "MoTV",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   XmNdeleteResponse,XmDO_NOTHING,
				   NULL);
    XtAddEventHandler(str_shell, (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XmAddWMProtocolCallback(str_shell,WM_DELETE_WINDOW,
			    popupdown_cb,str_shell);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, str_shell,
				   NULL);

    /* driver */
    frame = XtVaCreateManagedWidget("driverF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("driverL",xmLabelWidgetClass,frame,NULL);
    driver_menu = XmCreatePulldownMenu(form,"driverM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,driver_menu);
    driver_option = XmCreateOptionMenu(frame,"driver",args,1);
    XtManageChild(driver_option);

    /* video format + frame rate */
    frame = XtVaCreateManagedWidget("videoF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("videoL",xmLabelWidgetClass,frame,NULL);
    rowcol = XtVaCreateManagedWidget("videoB",xmRowColumnWidgetClass,
				     frame,NULL);
    video_menu = XmCreatePulldownMenu(rowcol,"videoM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,video_menu);
    video_option = XmCreateOptionMenu(rowcol,"video",args,1);
    XtManageChild(video_option);
    XtVaCreateManagedWidget("fpsL",xmLabelWidgetClass,rowcol,NULL);
    m_fps = XtVaCreateManagedWidget("fps",xmComboBoxWidgetClass,rowcol,NULL);

    /* audio format + sample rate */
    frame = XtVaCreateManagedWidget("audioF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("audioL",xmLabelWidgetClass,frame,NULL);
    rowcol = XtVaCreateManagedWidget("audioB",xmRowColumnWidgetClass,
				     frame,NULL);
    audio_menu = XmCreatePulldownMenu(rowcol,"audioM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,audio_menu);
    audio_option = XmCreateOptionMenu(rowcol,"audio",args,1);
    XtManageChild(audio_option);
    XtVaCreateManagedWidget("rateL",xmLabelWidgetClass,rowcol,NULL);
    m_rate = XtVaCreateManagedWidget("rate",xmComboBoxWidgetClass,rowcol,NULL);

    /* filenames */
    frame = XtVaCreateManagedWidget("fileF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("fileL",xmLabelWidgetClass,frame,NULL);
    fbox = XtVaCreateManagedWidget("fbox",xmRowColumnWidgetClass,
				   frame,NULL);

    rowcol = XtVaCreateManagedWidget("fvideoB",xmRowColumnWidgetClass,
				     fbox,NULL);
    XtVaCreateManagedWidget("fvideoL",xmLabelWidgetClass,rowcol,NULL);
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->text = XtVaCreateManagedWidget("fvideo",xmTextWidgetClass,
				      rowcol,NULL);
    m_fvideo = h->text;
    h->push = XtVaCreateManagedWidget("files",xmPushButtonWidgetClass,rowcol,
				      NULL);
    XtAddCallback(h->push,XmNactivateCallback,file_browse_cb,h);

    rowcol = XtVaCreateManagedWidget("faudioB",xmRowColumnWidgetClass,
				     fbox,NULL);
    m_faudioL = XtVaCreateManagedWidget("faudioL",xmLabelWidgetClass,rowcol,
					NULL);
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->text = XtVaCreateManagedWidget("faudio",xmTextWidgetClass,rowcol,
				      NULL);
    m_faudio = h->text;
    h->push = XtVaCreateManagedWidget("files",xmPushButtonWidgetClass,rowcol,
				      NULL);
    m_faudioB = h->push;
    XtAddCallback(h->push,XmNactivateCallback,file_browse_cb,h);

    /* seperator, buttons */
    m_status = XtVaCreateManagedWidget("status",xmLabelWidgetClass,form,NULL);
    rowcol = XtVaCreateManagedWidget("buttons",xmRowColumnWidgetClass,form,
				   NULL);
    push = XtVaCreateManagedWidget("rec", xmPushButtonWidgetClass, rowcol,
				   NULL);
    add_cmd_callback(push,XmNactivateCallback, "movie","start",NULL);
    push = XtVaCreateManagedWidget("stop", xmPushButtonWidgetClass, rowcol,
				   NULL);
    add_cmd_callback(push,XmNactivateCallback, "movie","stop",NULL);
    push = XtVaCreateManagedWidget("play", xmPushButtonWidgetClass, rowcol,
				   NULL);
    XtAddCallback(push,XmNactivateCallback,exec_player_cb,NULL);
    push = XtVaCreateManagedWidget("cancel", xmPushButtonWidgetClass, rowcol,
				   NULL);
    XtAddCallback(push,XmNactivateCallback, popupdown_cb, str_shell);
}

static void
update_movie_menus(void)
{
    struct list_head *item;
    struct ng_writer *writer;
    static int first = 1;
    Widget push;
    XmString str;
    Boolean sensitive;
    int i;

    /* drivers  */
    if (first) {
	first = 0;
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    str = XmStringGenerate((char*)writer->desc,
				   NULL, XmMULTIBYTE_TEXT, NULL);
	    push = XtVaCreateManagedWidget(writer->name,
					   xmPushButtonWidgetClass,driver_menu,
					   XmNlabelString,str,
					   NULL);
	    XmStringFree(str);
	    add_cmd_callback(push,XmNactivateCallback,
			     "movie","driver",writer->name);
	    if (NULL == movie_driver ||
		(NULL != mov_driver && 0 == strcasecmp(mov_driver,writer->name))) {
		movie_driver = writer;
		i_movie_driver = i;
		XtVaSetValues(driver_option,XmNmenuHistory,push,NULL);
	    }
	    i++;
	}
    }

    /* audio formats */
    delete_children(audio_menu);
    for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	str = XmStringGenerate
	    ((char*)(movie_driver->audio[i].desc ?
		     movie_driver->audio[i].desc :
		     ng_afmt_to_desc[movie_driver->audio[i].fmtid]),
	     NULL, XmMULTIBYTE_TEXT, NULL);
	push = XtVaCreateManagedWidget(movie_driver->audio[i].name,
				       xmPushButtonWidgetClass,audio_menu,
				       XmNlabelString,str,
				       NULL);
	XmStringFree(str);
	add_cmd_callback(push,XmNactivateCallback,
			 "movie","audio",movie_driver->audio[i].name);
	if (NULL != mov_audio)
	    if (0 == strcasecmp(mov_audio,movie_driver->audio[i].name)) {
		XtVaSetValues(audio_option,XmNmenuHistory,push,NULL);
		movie_audio = i;
	    }
    }
    str = XmStringGenerate("no sound", NULL, XmMULTIBYTE_TEXT, NULL);
    push = XtVaCreateManagedWidget("none",xmPushButtonWidgetClass,audio_menu,
				   XmNlabelString,str,NULL);
    XmStringFree(str);
    add_cmd_callback(push,XmNactivateCallback, "movie","audio","none");

    /* video formats */
    delete_children(video_menu);
    for (i = 0; NULL != movie_driver->video[i].name; i++) {
	str = XmStringGenerate
	    ((char*)(movie_driver->video[i].desc ?
		     movie_driver->video[i].desc :
		     ng_vfmt_to_desc[movie_driver->video[i].fmtid]),
	     NULL, XmMULTIBYTE_TEXT, NULL);
	push = XtVaCreateManagedWidget(movie_driver->video[i].name,
				       xmPushButtonWidgetClass,video_menu,
				       XmNlabelString,str,
				       NULL);
	XmStringFree(str);
	add_cmd_callback(push,XmNactivateCallback,
			 "movie","video",movie_driver->video[i].name);
	if (NULL != mov_video)
	    if (0 == strcasecmp(mov_video,movie_driver->video[i].name)) {
		XtVaSetValues(video_option,XmNmenuHistory,push,NULL);
		movie_video = i;
	    }
    }

    /* need audio filename? */
    sensitive = movie_driver->combined ? False : True;
    XtVaSetValues(m_faudio, XtNsensitive,sensitive, NULL);
    XtVaSetValues(m_faudioL, XtNsensitive,sensitive, NULL);
    XtVaSetValues(m_faudioB, XtNsensitive,sensitive, NULL);
}

static void
init_movie_menus(void)
{
    update_movie_menus();

    if (mov_rate)
	do_va_cmd(3,"movie","rate",mov_rate);
    if (mov_fps)
	do_va_cmd(3,"movie","fps",mov_fps);
}

static void
do_movie_record(int argc, char **argv)
{
    char *fvideo,*faudio;
    struct ng_video_fmt video;
    struct ng_audio_fmt audio;
    const struct ng_writer *wr;
    WidgetList children;
    Cardinal nchildren;
    Widget text;
    int i,rate,fps;

    /* set parameters */
    if (argc > 1 && 0 == strcasecmp(argv[0],"driver")) {
	struct list_head *item;
	struct ng_writer *writer;

	if (debug)
	    fprintf(stderr,"set driver: %s\n",argv[1]);
	XtVaGetValues(driver_menu,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    if (0 == strcasecmp(argv[1],writer->name)) {
		movie_driver = writer;
		i_movie_driver = i;
	    }
	    i++;
	}
	update_movie_menus();
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"audio")) {
	if (debug)
	    fprintf(stderr,"set audio: %s\n",argv[1]);
	XtVaGetValues(audio_menu,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	    if (0 == strcasecmp(argv[1],movie_driver->audio[i].name)) {
		XtVaSetValues(audio_option,XmNmenuHistory,children[i],NULL);
		movie_audio = i;
	    }
	}
	if (0 == strcmp(argv[1],"none")) {
	    XtVaSetValues(audio_option,XmNmenuHistory,children[i],NULL);
	    movie_audio = i;
	}
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"video")) {
	if (debug)
	    fprintf(stderr,"set video: %s\n",argv[1]);
	XtVaGetValues(video_menu,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	for (i = 0; NULL != movie_driver->video[i].name; i++) {
	    if (0 == strcasecmp(argv[1],movie_driver->video[i].name)) {
		XtVaSetValues(video_option,XmNmenuHistory,children[i],NULL);
		movie_video = i;
	    }
	}
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"rate")) {
	XtVaGetValues(m_rate,XmNtextField,&text,NULL);
	XmTextSetString(text,argv[1]);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fps")) {
	XtVaGetValues(m_fps,XmNtextField,&text,NULL);
	XmTextSetString(text,argv[1]);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fvideo")) {
	XmTextSetString(m_fvideo,argv[1]);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"faudio")) {
	XmTextSetString(m_faudio,argv[1]);
    }

    /* start */
    if (argc > 0 && 0 == strcasecmp(argv[0],"start")) {
	if (0 != cur_movie)
	    return; /* records already */
	cur_movie = 1;
	movie_blit = (cur_capture == CAPTURE_GRABDISPLAY);
	video_gd_suspend();
	XmToggleButtonSetState(levels_toggle,0,True);

	fvideo = XmTextGetString(m_fvideo);
	faudio = XmTextGetString(m_faudio);
	fvideo = tilde_expand(fvideo);
	faudio = tilde_expand(faudio);

	XtVaGetValues(m_rate,XmNtextField,&text,NULL);
	rate = atoi(XmTextGetString(text));
	XtVaGetValues(m_fps,XmNtextField,&text,NULL);
	fps = (int)(atof(XmTextGetString(text))*1000);

	memset(&video,0,sizeof(video));
	memset(&audio,0,sizeof(audio));

	wr = movie_driver;
	video.fmtid  = wr->video[movie_video].fmtid;
	video.width  = cur_tv_width;
	video.height = cur_tv_height;
	audio.fmtid  = wr->audio[movie_audio].fmtid;
	audio.rate   = rate;

	movie_state = movie_writer_init
	    (fvideo, faudio, wr,
	     &video, wr->video[movie_video].priv, fps,
	     &audio, wr->audio[movie_audio].priv, args.dspdev,
	     args.bufcount,args.parallel);
	if (NULL == movie_state) {
	    /* init failed */
	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    toolkit_set_label(m_status, "error [init]");
	    return;
	}
	if (0 != movie_writer_start(movie_state)) {
	    /* start failed */
	    movie_writer_stop(movie_state);
	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    toolkit_set_label(m_status, "error [start]");
	    return;
	}
	rec_work_id  = XtAppAddWorkProc(app_context,rec_work,NULL);
	toolkit_set_label(m_status, "recording");
	return;
    }

    /* stop */
    if (argc > 0 && 0 == strcasecmp(argv[0],"stop")) {
	if (0 == cur_movie)
	    return; /* nothing to stop here */

	movie_writer_stop(movie_state);
	XtRemoveWorkProc(rec_work_id);
	rec_work_id = 0;
	video_gd_restart();
	cur_movie = 0;
	return;
    }
}

static void
do_rec_status(char *message)
{
    toolkit_set_label(m_status, message);
}

/*----------------------------------------------------------------------*/

#ifdef HAVE_ZVBI
#define CHSCAN 250
static int chscan;
static int chvbi;
static XtIntervalId chtimer;
static Widget chdlg,chscale;

static void
chscan_timeout(XtPointer client_data, XtIntervalId *id)
{
    struct CHANNEL *c;
    char title[32];
    XmString xmstr;

    if (!x11_vbi_tuned()) {
	if (debug)
	    fprintf(stderr,"scan [%s]: no station\n",chanlist[chscan].name);
	goto next_station;
    }

    if (0 != x11_vbi_station[0]) {
	if (debug)
	    fprintf(stderr,"scan [%s]: %s\n",chanlist[chscan].name,
		    x11_vbi_station);
	c = add_channel(x11_vbi_station);
	c->cname   = strdup(chanlist[chscan].name);
	c->channel = chscan;
	cur_sender = count-1;
	channel_menu();
	goto next_station;
    }

    if (chvbi++ > 3) {
	if (debug)
	    fprintf(stderr,"scan [%s]: no vbi name\n",chanlist[chscan].name);
	sprintf(title,"%s [no name]",chanlist[chscan].name);
	c = add_channel(title);
	c->cname   = strdup(chanlist[chscan].name);
	c->channel = chscan;
	cur_sender = count-1;
	channel_menu();
	goto next_station;
    }

    if (debug)
	fprintf(stderr,"scan [%s] vbi ...\n",chanlist[chscan].name);
    chtimer = XtAppAddTimeOut(app_context, CHSCAN, chscan_timeout, NULL);
    return;

 next_station:
    chscan++;
    if (chscan >= chancount) {
	/* all done */
	x11_vbi_stop();
	chtimer = 0;
	if (count)
	    do_va_cmd(2,"setchannel",channels[0]->name);
	XtDestroyWidget(chdlg);
	chdlg = NULL;
	return;
    }
    chvbi  = 0;
    if (channel_switch_hook)
	channel_switch_hook();
    xmstr = XmStringGenerate(chanlist[chscan].name, NULL,
			     XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(chscale,XmNtitleString,xmstr,NULL);
    XmStringFree(xmstr);
    XmScaleSetValue(chscale,chscan);
    do_va_cmd(2,"setchannel",chanlist[chscan]);
    x11_vbi_station[0] = 0;
    chtimer = XtAppAddTimeOut(app_context, CHSCAN, chscan_timeout,NULL);
    return;
}

static void
chscan_start_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmString xmstr;

    /* check */
    if (!(f_drv & CAN_TUNE))
	return;
    if (channel_switch_hook)
	channel_switch_hook();

    /* clear */
    while (count) {
	XtDestroyWidget(channels[count-1]->button);
	del_channel(count-1);
    }
    cur_sender = -1;
    channel_menu();

    x11_vbi_start(args.vbidev);
    chscan = 0;
    chvbi  = 0;
    xmstr = XmStringGenerate(chanlist[chscan].name, NULL,
			     XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(chscale,XmNtitleString,xmstr,XmNmaximum,chancount,NULL);
    XmStringFree(xmstr);
    XmScaleSetValue(chscale,chscan);
    do_va_cmd(2,"setchannel",chanlist[chscan]);
    x11_vbi_station[0] = 0;
    chtimer = XtAppAddTimeOut(app_context, CHSCAN, chscan_timeout,NULL);
}

static void
chscan_cancel_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    x11_vbi_stop();
    if (chtimer)
	XtRemoveTimeOut(chtimer);
    chtimer = 0;
    XtDestroyWidget(chdlg);
    chdlg = NULL;
}

static void
chscan_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget rc;

    chdlg = XmCreatePromptDialog(control_shell,"chscan",NULL,0);
    XtAddEventHandler(XtParent(chdlg), (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XtUnmanageChild(XmSelectionBoxGetChild(chdlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(chdlg,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(chdlg,XmDIALOG_TEXT));

    rc = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,chdlg,NULL);
    XtVaCreateManagedWidget("hints",xmLabelWidgetClass,rc,NULL);
    chscale = XtVaCreateManagedWidget("channel",xmScaleWidgetClass,rc,NULL);
    XtRemoveAllCallbacks(XmSelectionBoxGetChild(chdlg,XmDIALOG_OK_BUTTON),
			 XmNactivateCallback);
    XtAddCallback(XmSelectionBoxGetChild(chdlg,XmDIALOG_OK_BUTTON),
		  XmNactivateCallback,chscan_start_cb,NULL);
    XtAddCallback(chdlg,XmNcancelCallback,chscan_cancel_cb,NULL);
    XtManageChild(chdlg);
}
#endif

/*----------------------------------------------------------------------*/

static void
pref_menu(Widget option, Widget menu, int enable)
{
    delete_children(menu);
    XtVaSetValues(XmOptionButtonGadget(option),XtNsensitive,enable,NULL);
    XtVaSetValues(XmOptionLabelGadget(option),XtNsensitive,enable,NULL);
    if (!enable)
	XtVaCreateManagedWidget("none",xmPushButtonWidgetClass,menu,NULL);
}

#if defined(HAVE_LIBXXF86VM) || defined(HAVE_LIBXRANDR)

static void
pref_fs(void)
{
    Widget push;
    char s[32];
    int i,on;

    on = XmToggleButtonGetState(pref_fs_toggle);
    if (on) {
#if defined(HAVE_LIBXXF86VM)
	if (0 == have_randr  &&  0 == args.vidmode) {
	    args.vidmode = 1;
	    xfree_vm_init(dpy);
	}
#endif
	if (0 == have_randr  &&  0 == have_vm) {
	    on = 0;
	    XtVaSetValues(pref_fs_toggle,XtNsensitive,0,NULL);
	}
    }

    XmToggleButtonSetState(pref_fs_toggle,on,False);
    if (on) {
	pref_menu(pref_fs_option,pref_fs_menu,1);
#if defined(HAVE_LIBXRANDR)
	if (have_randr) {
	    for (i = 0; i < nrandr; i++) {
		sprintf(s,"%d x %d",randr[i].width,randr[i].height);
		push = XtVaCreateManagedWidget(s,xmPushButtonWidgetClass,
					       pref_fs_menu,NULL);
		if (randr[i].width  == fs_width &&
		    randr[i].height == fs_height) {
		    XtVaSetValues(pref_fs_menu,XmNmenuHistory,push,NULL);
		}
	    }
	}
#endif
#if defined(HAVE_LIBXXF86VM)
	if (!have_randr) {
	    for (i = 0; i < vm_count; i++) {
		sprintf(s,"%d x %d",
			vm_modelines[i]->hdisplay,
			vm_modelines[i]->vdisplay);
		push = XtVaCreateManagedWidget(s,xmPushButtonWidgetClass,
					       pref_fs_menu,NULL);
		if (vm_modelines[i]->hdisplay == fs_width &&
		    vm_modelines[i]->vdisplay == fs_height) {
		    XtVaSetValues(pref_fs_menu,XmNmenuHistory,push,NULL);
		}
	    }
	}
#endif
    } else {
	pref_menu(pref_fs_option,pref_fs_menu,0);
    }
}

static void
pref_fst_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    pref_fs();
}
#endif

static void
pref_mix2(void)
{
    struct ng_mix_driver *mix;
    Widget push,w = NULL;
    char *name;
    int i,on;
    struct ng_devinfo *info = NULL;

    on = XmToggleButtonGetState(pref_mix_toggle);
    XtVaGetValues(pref_mix1_menu,XmNmenuHistory,&w,NULL);
    if (w) {
	name = XtName(w);
	if (!list_empty(&ng_mix_drivers) && 0 != strcmp(name,"none")) {
	    mix = list_entry(ng_mix_drivers.next,struct ng_mix_driver,list);
	    info = mix->channels(name);
	}
    }

    if (NULL != info && on) {
	pref_menu(pref_mix2_option,pref_mix2_menu,1);
	for (i = 0; 0 != strlen(info[i].name); i++) {
	    push = XtVaCreateManagedWidget(info[i].device,
					   xmPushButtonWidgetClass,
					   pref_mix2_menu,NULL);
	    toolkit_set_label(push,info[i].name);
	    if (strcasecmp(info[i].device,mixerctl) == 0)
		XtVaSetValues(pref_mix2_menu,XmNmenuHistory,push,NULL);
	}
    } else {
	pref_menu(pref_mix2_option,pref_mix2_menu,0);
    }
}

static void
pref_mix2_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    pref_mix2();
}

static void
pref_mix1(void)
{
    struct ng_mix_driver *mix;
    Widget push;
    int on,i;
    struct ng_devinfo *info = NULL;

    on = XmToggleButtonGetState(pref_mix_toggle);
    if (!list_empty(&ng_mix_drivers)) {
	mix = list_entry(ng_mix_drivers.next,struct ng_mix_driver,list);
	info = mix->probe();
    }
    if (NULL != info && on) {
	pref_menu(pref_mix1_option,pref_mix1_menu,1);
	for (i = 0; 0 != strlen(info[i].name); i++) {
	    push = XtVaCreateManagedWidget(info[i].device,
					   xmPushButtonWidgetClass,
					   pref_mix1_menu,
					   NULL);
	    XtAddCallback(push,XmNactivateCallback,pref_mix2_cb,NULL);
	    toolkit_set_label(push,info[i].name);
	    if (0 == i  ||  0 == strcmp(info[i].device,mixerdev))
		XtVaSetValues(pref_mix1_menu,XmNmenuHistory,push,NULL);
	}
    } else {
	pref_menu(pref_mix1_option,pref_mix1_menu,0);
    }
}

static void
pref_mix1_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    pref_mix1();
    pref_mix2();
}

static void
pref_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cb = call_data;
    Widget w;
    char *name = NULL;
    int on,width,height;

    if (cb->reason == XmCR_OK  ||  cb->reason == XmCR_APPLY) {
#ifdef HAVE_LIBXXF86VM
	on = XmToggleButtonGetState(pref_fs_toggle);
	if (on) {
	    XtVaGetValues(pref_fs_menu,XmNmenuHistory,&w,NULL);
	    name = XtName(w);
	    sscanf(name,"%d x %d",&width,&height);
	    fs_width  = width;
	    fs_height = height;
	} else {
	    fs_width  = 0;
	    fs_height = 0;
	}
#endif
	on = XmToggleButtonGetState(pref_mix_toggle);
	if (on) {
	    w = NULL;
	    XtVaGetValues(pref_mix1_menu,XmNmenuHistory,&w,NULL);
	    if (w)
		strcpy(mixerdev,XtName(w));
	    w = NULL;
	    XtVaGetValues(pref_mix2_menu,XmNmenuHistory,&w,NULL);
	    if (w)
		strcpy(mixerctl,XtName(w));
	} else {
	    mixerdev[0] = '\0';
	    mixerctl[0] = '\0';
	}
	use_osd = XmToggleButtonGetState(pref_osd);
	keypad_ntsc = XmToggleButtonGetState(pref_ntsc);
	keypad_partial = XmToggleButtonGetState(pref_partial);
	ng_jpeg_quality = atoi(XmTextGetString(pref_quality));
    }
    if (cb->reason == XmCR_OK) {
	save_config();
    }
    XtUnmanageChild(pref_dlg);
}

static void
pref_manage_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char tmp[16];

#ifdef HAVE_LIBXXF86VM
    XmToggleButtonSetState(pref_fs_toggle,fs_width && fs_height,False);
    pref_fs();
#endif
    XmToggleButtonSetState(pref_mix_toggle,strlen(mixerdev) > 0,False);
    pref_mix1();
    pref_mix2();
    XmToggleButtonSetState(pref_osd,use_osd,False);
    XmToggleButtonSetState(pref_ntsc,keypad_ntsc,False);
    XmToggleButtonSetState(pref_partial,keypad_partial,False);
    sprintf(tmp,"%d",ng_jpeg_quality);
    XmTextSetString(pref_quality,tmp);
    XtManageChild(pref_dlg);
}

static void
create_pref(void)
{
    Widget rc1,frame,rc2,rc3;
    Arg args[2];

    pref_dlg = XmCreatePromptDialog(control_shell,"pref",NULL,0);
    XtAddEventHandler(XtParent(pref_dlg), (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XtUnmanageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_HELP_BUTTON));
    XtManageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_APPLY_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_TEXT));

    rc1 = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, pref_dlg,
				  NULL);

#ifdef HAVE_LIBXXF86VM
    /* first frame */
    frame = XtVaCreateManagedWidget("fsF",xmFrameWidgetClass,rc1,NULL);
    XtVaCreateManagedWidget("fsL",xmLabelWidgetClass,frame,NULL);
    rc2 = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,frame,NULL);

    /* fullscreen */
    pref_fs_toggle = XtVaCreateManagedWidget("fsT",xmToggleButtonWidgetClass,
					     rc2,NULL);
    XtAddCallback(pref_fs_toggle,XmNvalueChangedCallback,pref_fst_cb,NULL);
    pref_fs_menu = XmCreatePulldownMenu(rc2,"fsM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,pref_fs_menu);
    pref_fs_option = XmCreateOptionMenu(rc2,"fsO",args,1);
    XtManageChild(pref_fs_option);
#endif

    /* second frame */
    frame = XtVaCreateManagedWidget("mixF",xmFrameWidgetClass,rc1,NULL);
    XtVaCreateManagedWidget("mixL",xmLabelWidgetClass,frame,NULL);
    rc2 = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,frame,NULL);
    pref_mix_toggle = XtVaCreateManagedWidget("mixT",xmToggleButtonWidgetClass,
					      rc2,NULL);
    XtAddCallback(pref_mix_toggle,XmNvalueChangedCallback,pref_mix1_cb,NULL);

    pref_mix1_menu = XmCreatePulldownMenu(rc2,"mix1M",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,pref_mix1_menu);
    pref_mix1_option = XmCreateOptionMenu(rc2,"mix1O",args,1);
    XtManageChild(pref_mix1_option);

    pref_mix2_menu = XmCreatePulldownMenu(rc2,"mix2M",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,pref_mix2_menu);
    pref_mix2_option = XmCreateOptionMenu(rc2,"mix2O",args,1);
    XtManageChild(pref_mix2_option);

    /* third frame */
    frame = XtVaCreateManagedWidget("optF",xmFrameWidgetClass,rc1,NULL);
    XtVaCreateManagedWidget("optL",xmLabelWidgetClass,frame,NULL);
    rc2 = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,frame,NULL);

    /* options */
    pref_osd = XtVaCreateManagedWidget("osd",xmToggleButtonWidgetClass,
				       rc2,NULL);
    pref_ntsc = XtVaCreateManagedWidget("keypad-ntsc",
					xmToggleButtonWidgetClass,
					rc2,NULL);
    pref_partial = XtVaCreateManagedWidget("keypad-partial",
					   xmToggleButtonWidgetClass,
					   rc2,NULL);
    rc3 = XtVaCreateManagedWidget("jpeg", xmRowColumnWidgetClass,
				  rc2,NULL);
    XtVaCreateManagedWidget("label",xmLabelWidgetClass,rc3,NULL);
    pref_quality = XtVaCreateManagedWidget("quality",
					   xmTextWidgetClass,
					   rc3,NULL);

    /* buttons */
    XtAddCallback(pref_dlg,XmNokCallback,pref_done_cb,NULL);
    XtAddCallback(pref_dlg,XmNapplyCallback,pref_done_cb,NULL);
    XtAddCallback(pref_dlg,XmNcancelCallback,pref_done_cb,NULL);
}

/*---------------------------------------------------------------------- */
/* selection & dnd support                                               */

static struct ng_video_buf*
convert_buffer(struct ng_video_buf *in, int out_fmt)
{
    struct ng_video_conv *conv;
    struct ng_convert_handle *ch;
    struct ng_video_fmt ofmt;
    int i;

    /* find converter */
    for (i = 0;;) {
	conv = ng_conv_find_to(out_fmt,&i);
	if (NULL == conv)
	    break;
	if (conv->fmtid_in == in->fmt.fmtid)
	    goto found;
    }
    return NULL;

 found:
    memset(&ofmt,0,sizeof(ofmt));
    ofmt.fmtid  = out_fmt;
    ofmt.width  = in->fmt.width;
    ofmt.height = in->fmt.height;
    ch = ng_convert_alloc(conv,&in->fmt,&ofmt);
    return ng_convert_single(ch,in);
}

static struct ng_video_buf*
scale_rgb_buffer(struct ng_video_buf *in, int scale)
{
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;
    char *src,*dst;
    unsigned int x,y;

    fmt = in->fmt;
    fmt.width  = in->fmt.width  / scale;
    fmt.height = in->fmt.height / scale;
    while (fmt.width & 0x03)
	fmt.width++;
    fmt.bytesperline = fmt.width * 3;
    buf = ng_malloc_video_buf(&fmt, fmt.width * fmt.height * 3);

    /* scale down */
    dst = buf->data;
    for (y = 0; y < fmt.height; y++) {
	src = in->data + y * scale * in->fmt.bytesperline;
	for (x = 0; x < fmt.width; x++) {
	    dst[0] = src[0];
	    dst[1] = src[1];
	    dst[2] = src[2];
	    dst += 3;
	    src += 3*scale;
	}
    }
    return buf;
}

struct ipc_data {
    struct list_head     list;
    Atom                 atom;
    struct ng_video_buf  *buf;
    char                 *filename;
    Pixmap               pix;
    Pixmap               icon_pixmap;
    Widget               icon_widget;
};
struct list_head ipc_selections;

static void
ipc_iconify(Widget widget, struct ipc_data *ipc)
{
    struct ng_video_buf *small;
    int scale,depth;
    Arg args[4];
    Cardinal n=0;

    /* calc size */
    for (scale = 1;; scale++) {
	if (ipc->buf->fmt.width  / scale < 128 &&
	    ipc->buf->fmt.height / scale < 128)
	    break;
    }

    /* scale down & create pixmap */
    small = scale_rgb_buffer(ipc->buf,scale);
    small = convert_buffer(small, x11_dpy_fmtid);
    ipc->icon_pixmap = x11_create_pixmap(dpy,&vinfo,small);

    /* build DnD icon */
    n = 0;
    depth = DefaultDepthOfScreen(XtScreen(widget));
    XtSetArg(args[n], XmNpixmap, ipc->icon_pixmap); n++;
    XtSetArg(args[n], XmNwidth,  small->fmt.width); n++;
    XtSetArg(args[n], XmNheight, small->fmt.height); n++;
    XtSetArg(args[n], XmNdepth,  depth); n++;
    ipc->icon_widget = XmCreateDragIcon(widget,"dragicon",args,n);

    ng_release_video_buf(small);
}

static struct ipc_data*
ipc_find(Atom selection)
{
    struct list_head   *item;
    struct ipc_data    *ipc;

    list_for_each(item,&ipc_selections) {
	ipc = list_entry(item, struct ipc_data, list);
	if (ipc->atom == selection)
	    return ipc;
    }
    return NULL;
}

static struct ipc_data*
ipc_init(Atom selection)
{
    struct ipc_data *ipc;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;

    /* capture a frame and save a copy */
    video_gd_suspend();
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = cur_tv_width;
    fmt.height = cur_tv_height;
    buf = ng_grabber_get_image(&fmt);
    buf = ng_filter_single(cur_filter,buf);
    ipc = malloc(sizeof(*ipc));
    memset(ipc,0,sizeof(*ipc));
    ipc->buf = ng_malloc_video_buf(&buf->fmt,buf->size);
    ipc->atom = selection;
    ipc->buf->info = buf->info;
    memcpy(ipc->buf->data,buf->data,buf->size);
    ng_release_video_buf(buf);
    video_gd_restart();

    list_add_tail(&ipc->list,&ipc_selections);
    return ipc;
}

static void
ipc_tmpfile(struct ipc_data *ipc)
{
    static char *base = "motv";
    struct ng_video_buf *buf;
    char *tmpdir;
    int fd;

    if (NULL != ipc->filename)
	return;

    tmpdir = getenv("TMPDIR");
    if (NULL == tmpdir)
	tmpdir="/tmp";
    ipc->filename = malloc(strlen(tmpdir)+strlen(base)+16);
    sprintf(ipc->filename,"%s/%s-XXXXXX",tmpdir,base);
    fd = mkstemp(ipc->filename);

    ipc->buf->refcount++;
    buf = convert_buffer(ipc->buf, VIDEO_JPEG);
    write(fd,buf->data,buf->size);
    ng_release_video_buf(buf);
}

static void
ipc_pixmap(struct ipc_data *ipc)
{
    struct ng_video_buf *buf;

    if (0 != ipc->pix)
	return;

    ipc->buf->refcount++;
    buf = convert_buffer(ipc->buf, x11_dpy_fmtid);
    ipc->pix = x11_create_pixmap(dpy,&vinfo,buf);
    ng_release_video_buf(buf);
    return;
}

static void
ipc_fini(Atom selection)
{
    struct ipc_data *ipc;

    ipc = ipc_find(selection);
    if (NULL == ipc)
	return;

    /* free stuff */
    if (ipc->buf)
	ng_release_video_buf(ipc->buf);
    if (ipc->filename) {
	unlink(ipc->filename);
	free(ipc->filename);
    }
    if (ipc->icon_widget)
	XtDestroyWidget(ipc->icon_widget);
    if (ipc->icon_pixmap)
	XFreePixmap(dpy,ipc->icon_pixmap);
    if (ipc->pix)
	XFreePixmap(dpy,ipc->pix);

    list_del(&ipc->list);
    free(ipc);
}

static Atom ipc_unique_atom(Widget widget)
{
    char id_name[32];
    Atom id;
    int i;

    for (i = 0;; i++) {
	sprintf(id_name,"_MOTV_IMAGE_%lX_%d",XtWindow(widget),i);
	id = XInternAtom(XtDisplay(widget),id_name,False);
	if (NULL == ipc_find(id))
	    break;
    }
    return id;
}

static void
ipc_convert(Widget widget, XtPointer ignore, XtPointer call_data)
{
    XmConvertCallbackStruct *ccs = call_data;
    struct ipc_data *ipc;
    Atom *targs;
    Pixmap *pix;
    unsigned long *ldata;
    unsigned char *cdata;
    char *filename;
    int n;

    if (debug) {
	char *y = !ccs->type      ? NULL : XGetAtomName(dpy,ccs->type);
	char *t = !ccs->target    ? NULL : XGetAtomName(dpy,ccs->target);
	char *s = !ccs->selection ? NULL : XGetAtomName(dpy,ccs->selection);
	fprintf(stderr,"conv: target=%s type=%s selection=%s\n",t,y,s);
	if (y) XFree(y);
	if (t) XFree(t);
	if (s) XFree(s);
    }

    /* tell which formats we can handle */
    if ((ccs->target == XA_TARGETS) ||
	(ccs->target == _MOTIF_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_DEFERRED_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_EXPORT_TARGETS)) {
	n = 0;
	targs = (Atom*)XtMalloc(sizeof(Atom)*12);
	if (ccs->target != _MOTIF_CLIPBOARD_TARGETS) {
	    targs[n++] = XA_TARGETS;
	    targs[n++] = MIME_IMAGE_PPM;
	    targs[n++] = XA_PIXMAP;
	    targs[n++] = XA_FOREGROUND;
	    targs[n++] = XA_BACKGROUND;
	    targs[n++] = XA_COLORMAP;
	    targs[n++] = MIME_IMAGE_JPEG;
	    targs[n++] = XA_FILE_NAME;
	    targs[n++] = XA_FILE;
	    targs[n++] = MIME_TEXT_URI_LIST;
	    targs[n++] = _NETSCAPE_URL;
	}
	if (ccs->target == _MOTIF_EXPORT_TARGETS) {
	    /* save away drag'n'drop data */
	    ipc_init(ccs->selection);
	}
	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;

    } else if (ccs->target == _MOTIF_SNAPSHOT) {
	/* save away clipboard data */
	n = 0;
	targs = (Atom*)XtMalloc(sizeof(Atom));
	targs[n++] = ipc_unique_atom(widget);
	ipc_init(targs[0]);
	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    /* find data */
    ipc = ipc_find(ccs->selection);
    if (NULL == ipc) {
	/* shouldn't happen */
	fprintf(stderr,"oops: selection data not found\n");
	ccs->status = XmCONVERT_REFUSE;
	return;
    }

    if (ccs->target == _MOTIF_LOSE_SELECTION ||
	ccs->target == XA_DONE) {
	/* cleanup */
	ipc_fini(ccs->selection);
	ccs->value  = NULL;
	ccs->length = 0;
	ccs->type   = XA_INTEGER;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    /* convert data */
    if (ccs->target == XA_BACKGROUND ||
	ccs->target == XA_FOREGROUND ||
	ccs->target == XA_COLORMAP) {
	n = 0;
	ldata = (Atom*)XtMalloc(sizeof(Atom)*8);
	if (ccs->target == XA_BACKGROUND) {
	    ldata[n++] = WhitePixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_FOREGROUND) {
	    ldata[n++] = BlackPixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_COLORMAP) {
	    ldata[n++] = DefaultColormapOfScreen(XtScreen(widget));
	    ccs->type  = XA_COLORMAP;
	}
	ccs->value  = ldata;
	ccs->length = n;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_PIXMAP) {
	/* xfer pixmap */
	ipc_pixmap(ipc);
	pix = (Pixmap*)XtMalloc(sizeof(Pixmap));
	pix[0] = ipc->pix;
	if (debug)
	    fprintf(stderr,"conv: pixmap id is 0x%lx\n",pix[0]);
	ccs->value  = pix;
	ccs->length = 1;
	ccs->type   = XA_DRAWABLE;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == MIME_IMAGE_PPM) {
	cdata = XtMalloc(ipc->buf->size + 32);
	n = sprintf(cdata,"P6\n%d %d\n255\n",
		    ipc->buf->fmt.width,ipc->buf->fmt.height);
	memcpy(cdata+n,ipc->buf->data,ipc->buf->size);
	ccs->value  = cdata;
	ccs->length = n+ipc->buf->size;
	ccs->type   = MIME_IMAGE_PPM;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == MIME_IMAGE_JPEG) {
	struct ng_video_buf *buf;
	ipc->buf->refcount++;
	buf = convert_buffer(ipc->buf, VIDEO_JPEG);
	cdata = XtMalloc(buf->size);
	memcpy(cdata,buf->data,buf->size);
	ng_release_video_buf(buf);
	ccs->value  = cdata;
	ccs->length = buf->size;
	ccs->type   = MIME_IMAGE_JPEG;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_FILE_NAME       ||
	       ccs->target == XA_FILE            ||
	       ccs->target == XA_STRING          ||
	       ccs->target == MIME_TEXT_URI_LIST ||
	       ccs->target == _NETSCAPE_URL) {
	/* xfer filename (image via tmp file) */
	ipc_tmpfile(ipc);
	if (ccs->target == MIME_TEXT_URI_LIST ||
	    ccs->target == _NETSCAPE_URL) {
	    /* filename => url */
	    filename = XtMalloc(strlen(ipc->filename)+8);
	    sprintf(filename,"file:%s\r\n",ipc->filename);
	    ccs->type = ccs->target;
	    if (debug)
		fprintf(stderr,"conv: tmp url is %s\n",filename);
	} else {
	    filename = XtMalloc(strlen(ipc->filename));
	    strcpy(filename,ipc->filename);
	    ccs->type = XA_STRING;
	    if (debug)
		fprintf(stderr,"conv: tmp file is %s\n",filename);
	}
	ccs->value  = filename;
	ccs->length = strlen(filename);
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else {
	/* shouldn't happen */
	fprintf(stderr,"oops: unknown target\n");
	ccs->status = XmCONVERT_REFUSE;
    }
}

static void
ipc_finish(Widget widget, XtPointer ignore, XtPointer call_data)
{
    if (debug)
	fprintf(stderr,"conv: transfer finished\n");
    ipc_fini(_MOTIF_DROP);
}

void IpcAction(Widget widget, XEvent *event, String *argv, Cardinal *argc)
{
    struct    ipc_data *ipc;
    Widget    drag;
    Arg       args[4];
    Cardinal  n=0;

    if (0 == *argc)
	return;
    if (!(f_drv & CAN_CAPTURE)) {
	if (debug)
	    fprintf(stderr,"ipc: can't capture - cancel\n");
	return;
    }

    if (debug)
	fprintf(stderr,"ipc: %s\n",argv[0]);
    if (0 == strcmp(argv[0],"drag")) {
	ipc_fini(_MOTIF_DROP);
	ipc = ipc_init(_MOTIF_DROP);
	ipc_iconify(widget,ipc);
	n = 0;
	XtSetArg(args[n], XmNdragOperations, XmDROP_COPY); n++;
	XtSetArg(args[n], XmNsourcePixmapIcon, ipc->icon_widget); n++;
	drag = XmeDragSource(tv, NULL, event, args, n);
	XtAddCallback(drag, XmNdragDropFinishCallback, ipc_finish, NULL);
    }
    if (0 == strcmp(argv[0],"primary")) {
#if 0
	ipc_fini(XA_PRIMARY);
	ipc_init(XA_PRIMARY);
	XmePrimarySource(tv,XtLastTimestampProcessed(dpy));
#else
	fprintf(stderr,"FIXME [primary called]\n");
#endif
    }
    if (0 == strcmp(argv[0],"clipboard")) {
	XmeClipboardSource(tv,XmCOPY,XtLastTimestampProcessed(dpy));
    }
}

/*----------------------------------------------------------------------*/

Widget levels_left, levels_right;
XtInputId levels_id;
const struct ng_dsp_driver *levels_dsp;
void *levels_hdsp;

static void
levels_input(XtPointer clientdata, int *src, XtInputId *id)
{
    struct ng_audio_buf *buf;
    int left, right;

    buf = levels_dsp->read(levels_hdsp,0);
    oss_levels(buf,&left,&right);
    XmScaleSetValue(levels_left,left);
    XmScaleSetValue(levels_right,right);
    if (debug > 1)
	fprintf(stderr,"levels: left = %3d, right = %3d\r",left,right);
}

static void
levels_toggle_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmToggleButtonCallbackStruct *tb = call_data;
    struct ng_audio_fmt a;

    if (tb->reason != XmCR_VALUE_CHANGED)
	return;

    if (tb->set  &&  NULL == levels_dsp) {
	/* enable */
	a.fmtid = AUDIO_U8_STEREO;
	a.rate  = 44100;
	levels_dsp = ng_dsp_open(args.dspdev,&a,1,&levels_hdsp);
	if (levels_dsp) {
	    levels_dsp->startrec(levels_hdsp);
	    levels_id  = XtAppAddInput(app_context,levels_dsp->fd(levels_hdsp),
				       (XtPointer)XtInputReadMask,
				       levels_input,NULL);
	    if (debug)
		fprintf(stderr,"levels: started sound monitor\n");
	}
    }
    if (!tb->set  &&  NULL != levels_hdsp) {
	/* disable */
	XtRemoveInput(levels_id);
	levels_dsp->close(levels_hdsp);
	levels_dsp = NULL;
	levels_hdsp = NULL;
	XmScaleSetValue(levels_left,0);
	XmScaleSetValue(levels_right,0);
	if (debug)
	    fprintf(stderr,"levels: stopped sound monitor\n");
    }
}

static void
create_levels(void)
{
    Widget rc;

    levels_shell = XtVaAppCreateShell("levels", "MoTV",
				      topLevelShellWidgetClass,
				      dpy,
				      XtNclientLeader,app_shell,
				      XtNvisual,vinfo.visual,
				      XtNcolormap,colormap,
				      XtNdepth,vinfo.depth,
				      XmNdeleteResponse,XmDO_NOTHING,
				      NULL);
    XtAddEventHandler(levels_shell, (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    XmAddWMProtocolCallback(levels_shell,WM_DELETE_WINDOW,
			    popupdown_cb,levels_shell);
    rc = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, levels_shell,
				 NULL);

    levels_toggle = XtVaCreateManagedWidget("enable",
					    xmToggleButtonWidgetClass,rc,
					    NULL);
    XtAddCallback(levels_toggle,XmNvalueChangedCallback,
		  levels_toggle_cb,NULL);

    levels_left = XtVaCreateManagedWidget("left",xmScaleWidgetClass,rc,
					  NULL);
    levels_right = XtVaCreateManagedWidget("right",xmScaleWidgetClass,rc,
					   NULL);
}

/*----------------------------------------------------------------------*/

struct stderr_handler {
    Widget box;
    XmString str;
    int pipe;
    XtInputId id;
};

static void
stderr_input(XtPointer clientdata, int *src, XtInputId *id)
{
    struct stderr_handler *h = clientdata;
    XmString item;
    Widget label;
    char buf[1024];
    int rc;

    rc = read(h->pipe,buf,sizeof(buf)-1);
    if (rc <= 0) {
	/* Oops */
	XtRemoveInput(h->id);
	close(h->pipe);
	XtDestroyWidget(h->box);
	free(h);
    }
    buf[rc] = 0;
    item = XmStringGenerate(buf, NULL, XmMULTIBYTE_TEXT,NULL);
    h->str = XmStringConcatAndFree(h->str,item);
    label = XmMessageBoxGetChild(h->box,XmDIALOG_MESSAGE_LABEL);
    XtVaSetValues(label,XmNlabelString,h->str,NULL);
    XtManageChild(h->box);
};

static void
stderr_ok_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct stderr_handler *h = clientdata;

    XmStringFree(h->str);
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
    XtUnmanageChild(h->box);
}

static void
stderr_close_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct stderr_handler *h = clientdata;

    XmStringFree(h->str);
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
}

static void
stderr_init(void)
{
    struct stderr_handler *h;
    int p[2];

    if (debug)
	return;
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
    h->box = XmCreateErrorDialog(app_shell,"errbox",NULL,0);
    XtUnmanageChild(XmMessageBoxGetChild(h->box,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(h->box,XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(h->box,XmNokCallback,stderr_ok_cb,h);
    XtAddCallback(XtParent(h->box),XmNpopdownCallback,stderr_close_cb,h);
    pipe(p);
    dup2(p[1],2);
    close(p[1]);
    h->pipe = p[0];
    h->id = XtAppAddInput(app_context,h->pipe,(XtPointer)XtInputReadMask,
			  stderr_input,h);
}

/*----------------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
    int            i;
    unsigned long  freq;

    hello_world("motv");
    XtSetLanguageProc(NULL,NULL,NULL);
    app_shell = XtVaAppInitialize(&app_context, "MoTV",
				  opt_desc, opt_count,
				  &argc, argv,
				  fallback_ressources,
				  NULL);
    XtAddEventHandler(app_shell, (EventMask) 0, True,
                      (XtEventHandler) _XEditResCheckMessages, NULL);
    dpy = XtDisplay(app_shell);
    x11_icons_init(dpy,0);
    init_atoms(dpy);

    /* handle command line args */
    ng_init();
    handle_cmdline_args();

    /* device scan */
    if (args.hwscan) {
	fprintf(stderr,"looking for available devices\n");
#ifdef HAVE_LIBXV
	xv_video_init(-1,1);
#endif
	grabber_scan();
    }

    /* look for a useful visual */
    visual_init("motv","MoTV");

    /* remote display? */
    do_overlay = !args.remote;
    if (do_overlay)
	x11_check_remote();
    v4lconf_init();

    /* x11 stuff */
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    x11_misc_init(dpy);
    XmAddWMProtocolCallback(app_shell,WM_DELETE_WINDOW,ExitCB,NULL);
    if (debug)
	fprintf(stderr,"main: dga extention...\n");
    xfree_dga_init(dpy);
    if (debug)
	fprintf(stderr,"main: xinerama extention...\n");
    xfree_xinerama_init(dpy);
#ifdef HAVE_LIBXV
    if (debug)
	fprintf(stderr,"main: xvideo extention [video]...\n");
    if (args.xv_video)
	xv_video_init(args.xv_port,0);
    if (debug)
	fprintf(stderr,"main: xvideo extention [image]...\n");
    if (args.xv_image)
	xv_image_init(dpy);
#endif

    /* set hooks (command.c) */
    update_title        = new_title;
    display_message     = new_message;
#if TT
    vtx_message         = display_vtx;
#endif
#ifdef HAVE_ZVBI
    vtx_subtitle        = display_subtitle;
#endif
    set_capture_hook    = do_capture;
    fullscreen_hook     = do_motif_fullscreen;
    attr_notify         = new_attr;
    volume_notify       = new_volume;
    freqtab_notify      = new_freqtab;
    setstation_notify   = new_channel;
    movie_hook          = do_movie_record;
    rec_status          = do_rec_status;
    exit_hook           = do_exit;
    capture_get_hook    = video_gd_suspend;
    capture_rel_hook    = video_gd_restart;
    channel_switch_hook = pixit;

    if (debug)
	fprintf(stderr,"main: init main window...\n");
    tv = video_init(app_shell,&vinfo,xmPrimitiveWidgetClass,
		    args.bpp,args.gl);
    XtAddEventHandler(XtParent(tv),StructureNotifyMask, True,
		      resize_event, NULL);
    if (debug)
	fprintf(stderr,"main: install signal handlers...\n");
    xt_siginit();
    if (NULL == drv) {
	if (debug)
	    fprintf(stderr,"main: open grabber device...\n");
	grabber_init();
    }

    /* create windows */
    XSetIOErrorHandler(x11_ctrl_alt_backspace);
    if (debug)
	fprintf(stderr,"main: checking wm...\n");
    wm_detect(dpy);
    if (debug)
	fprintf(stderr,"main: creating windows ...\n");
    create_onscreen(xmLabelWidgetClass);
    create_vtx();
    create_strwin();
    stderr_init();

    /* read config file + related settings */
    if (debug)
	fprintf(stderr,"main: init frequency tables ...\n");
    freq_init();
    if (args.readconfig) {
	if (debug)
	    fprintf(stderr,"main: read config file ...\n");
	read_config(args.conffile ? args.conffile : NULL, &argc, argv);
    }
    if (0 != strlen(mixerdev)) {
	struct ng_attribute *attr;
	if (debug)
	    fprintf(stderr,"main: open mixer device...\n");
	if (NULL != (attr = ng_mix_init(mixerdev,mixerctl)))
	    add_attrs(attr);
    }

    create_control();
    create_prop();
    create_pref();
    create_levels();
    create_filter_prop();

    init_movie_menus();
    create_scale();
    create_attr_widgets();
    INIT_LIST_HEAD(&ipc_selections);

    xt_vm_randr_input_init(dpy);

    if (debug)
	fprintf(stderr,"main: mapping main window ...\n");
    XtRealizeWidget(app_shell);
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);
    if (f_drv & CAN_CAPTURE)
	XtAddCallback(tv,XmNconvertCallback,ipc_convert,NULL);

    XtVaSetValues(app_shell,
		  XtNwidthInc,  WIDTH_INC,
		  XtNheightInc, HEIGHT_INC,
		  XtNminWidth,  WIDTH_INC,
		  XtNminHeight, HEIGHT_INC,
		  NULL);

    /* mouse pointer magic */
    XtAddEventHandler(tv, PointerMotionMask, True, mouse_event, NULL);
    mouse_event(tv,NULL,NULL,NULL);

    /* init hw */
    if (debug)
	fprintf(stderr,"main: initialize hardware ...\n");
    attr_init();
    audio_on();
    audio_init();

    /* build channel list */
    if (args.readconfig) {
	if (debug)
	    fprintf(stderr,"main: parse channels from config file ...\n");
	parse_config(1);
    }
    channel_menu();

    xt_handle_pending(dpy);
    init_overlay();

    set_property(0,NULL,NULL);
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
	    if (count > 0) {
		if (debug)
		    fprintf(stderr,"main: tuning first station\n");
		do_va_cmd(2,"setstation","0");
	    } else {
		if (debug)
		    fprintf(stderr,"main: setting defaults\n");
		set_defaults();
	    }
	} else {
	    if (debug)
		fprintf(stderr,"main: known station tuned, not changing\n");
	}
    }
    XtAddEventHandler(tv,ExposureMask, True, tv_expose_event, NULL);

    if (args.fullscreen) {
	do_motif_fullscreen();
    } else {
	XtAppAddWorkProc(app_context,MyResize,NULL);
    }

    xt_main_loop();
    return 0;
}
