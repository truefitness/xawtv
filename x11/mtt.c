/*
 * mtt - teletext test app.
 *
 * This is just main() for the standalone version, the actual
 * code is in vbi-gui.c (motif) and vbi-tty.c (terminal).
 *
 *   (c) 2002 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>
#include <locale.h>
#include <langinfo.h>
#include <termios.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/PushB.h>
#include <Xm/DrawingA.h>
#include <Xm/Protocols.h>

#include "icons.h"
#include "atoms.h"
#include "vbi-data.h"
#include "vbi-gui.h"
#include "vbi-tty.h"

#include "grab-ng.h"
#include "channel.h"
#include "frequencies.h"
#include "commands.h"

/* --------------------------------------------------------------------- */

XtAppContext  app_context;
Widget        app_shell;
Display       *dpy;
int           debug;

static String fallback_ressources[] = {
#include "mtt.h"
    NULL
};

struct ARGS {
    char  *device;
    int   help;
    int   tty;
    int   debug;
    int   sim;
} args;

XtResource args_desc[] = {
    /* name, class, type, size, offset, default_type, default_addr */
    {
	/* Strings */
	"device",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,device),
	XtRString, "/dev/vbi0",
    },{
	/* Integer */
	"help",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,help),
	XtRString, "0"
    },{
	"tty",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,tty),
	XtRString, "0"
    },{
	"debug",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,debug),
	XtRString, "0"
    },{
	"sim",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,sim),
	XtRString, "0"
    }
};
const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-c",          "device",      XrmoptionSepArg, NULL },
    { "-device",     "device",      XrmoptionSepArg, NULL },

    { "-tty",        "tty",         XrmoptionNoArg,  "1" },
    { "-sim",        "sim",         XrmoptionNoArg,  "1" },
    { "-debug",      "debug",       XrmoptionNoArg,  "1" },

    { "-h",          "help",        XrmoptionNoArg,  "1" },
    { "-help",       "help",        XrmoptionNoArg,  "1" },
    { "--help",      "help",        XrmoptionNoArg,  "1" },
};
const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));

/* --------------------------------------------------------------------- */

static void
debug_action(Widget widget, XEvent *event,
	     String *params, Cardinal *num_params)
{
    fprintf(stderr,"debug_action: called\n");
}

static XtActionsRec actionTable[] = {
    { "debug", debug_action },
};

/* --------------------------------------------------------------------- */

static void usage(void)
{
    fprintf(stderr,
	    "\n"
	    "mtt -- teletext application\n"
	    "\n"
	    "usage: mtt [ options ]\n"
	    "options:\n"
	    "  -help         print this text\n"
	    "  -debug        enable debug messages\n"
	    "  -device <dev> use vbi device <dev> instead of /dev/vbi0\n"
	    "  -tty          use terminal mode\n"
	    "\n"
	    "--\n"
	    "Gerd Knorr <kraxel@bytesex.org>\n");
}

static void vbi_data(XtPointer data, int *fd, XtInputId *iproc)
{
    struct vbi_state *vbi = data;
    vbi_hasdata(vbi);
}

static int main_tty(int argc, char **argv)
{
    char *dev = "/dev/vbi0";
    int debug = 0;
    int sim   = 0;

    argc--;
    argv++;
    for (;argc;) {
	if (0 == strcmp(argv[0],"-c") ||
	    0 == strcmp(argv[0],"-device")) {
	    dev = argv[1];
	    argc -= 2;
	    argv += 2;

	} else if (0 == strcmp(argv[0],"-tty")) {
	    argc -= 1;
	    argv += 1;

	} else if (0 == strcmp(argv[0],"-debug")) {
	    debug = 1;
	    argc -= 1;
	    argv += 1;

	} else if (0 == strcmp(argv[0],"-sim")) {
	    sim   = 1;
	    argc -= 1;
	    argv += 1;

	} else if (0 == strcmp(argv[0],"-h") &&
		   0 == strcmp(argv[0],"-help") &&
		   0 == strcmp(argv[0],"--help")) {
	    usage();
	    exit(0);

	} else
	    break;
    }
    vbi_tty(dev,debug,sim);
    exit(0);
}

int
main(int argc, char **argv)
{
    struct vbi_state *vbi;
    char **av;
    int ac;

    ac = argc;
    av = malloc(sizeof(char*)*(argc+1));
    memcpy(av,argv,sizeof(char*)*(argc+1));

    XtSetLanguageProc(NULL,NULL,NULL);
    XtToolkitInitialize();
    app_context = XtCreateApplicationContext();
    XtAppSetFallbackResources(app_context,fallback_ressources);
    dpy = XtOpenDisplay(app_context, NULL,
			NULL,"mtt",
			opt_desc, opt_count,
			&argc, argv);
    if (NULL == dpy)
	main_tty(ac,av);
    app_shell = XtVaAppCreateShell(NULL,"mtt",
				   applicationShellWidgetClass,dpy,NULL);
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    x11_icons_init(dpy,0);
    init_atoms(dpy);
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    if (args.help) {
	usage();
	exit(1);
    }
    if (args.tty)
	main_tty(ac,av);

    freq_init();
    read_config(NULL, &argc, argv);
    parse_config(1);
    do_va_cmd(2,"setfreqtab",(-1 != chantab)
	      ? chanlist_names[chantab].str : "europe-west");

    vbi = vbi_open(args.device,args.debug,args.sim);
    if (NULL == vbi)
	exit(1);
    if (args.debug)
	vbi_event_handler_add(vbi->dec,~0,vbi_dump_event,vbi);
    XtAppAddInput(app_context, vbi->fd, (XtPointer) XtInputReadMask,
		  vbi_data, vbi);

    vbi_create_widgets(app_shell,vbi);
    XtRealizeWidget(app_shell);
    XtAppMainLoop(app_context);
    return 0;
}
