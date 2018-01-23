#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include "config.h"

#ifdef HAVE_LIBLIRC_CLIENT
# include <lirc/lirc_client.h>
#endif
#include "grab-ng.h"
#include "commands.h"
#include "lirc.h"
#include "event.h"

/*-----------------------------------------------------------------------*/

extern int debug;

#ifdef HAVE_LIBLIRC_CLIENT
static struct lirc_config *config = NULL;

static struct event_entry lirc_events[] = {
    {
	event:  "lirc-key-ch+",
	action: "setstation next",
    },{
	event:  "lirc-key-ch-",
	action: "setstation prev",
    },{
	event:  "lirc-key-vol+",
	action: "volume inc",
    },{
	event:  "lirc-key-vol-",
	action: "volume dec",
    },{
	event:  "lirc-key-mute",
	action: "volume mute",
    },{
	event:  "lirc-key-full_screen",
	action: "fullscreen toggle",
    },{
	event:  "lirc-key-source",
	action: "setinput next",
    },{
	event:  "lirc-key-reserved",
	action: "quit",
    },{
	event:  "lirc-key-0",
	action: "keypad 0",
    },{
	event:  "lirc-key-1",
	action: "keypad 1",
    },{
	event:  "lirc-key-2",
	action: "keypad 2",
    },{
	event:  "lirc-key-3",
	action: "keypad 3",
    },{
	event:  "lirc-key-4",
	action: "keypad 4",
    },{
	event:  "lirc-key-5",
	action: "keypad 5",
    },{
	event:  "lirc-key-6",
	action: "keypad 6",
    },{
	event:  "lirc-key-7",
	action: "keypad 7",
    },{
	event:  "lirc-key-8",
	action: "keypad 8",
    },{
	event:  "lirc-key-9",
	action: "keypad 9",
    },{
	/* end of list */
    }
};
#endif

int lirc_tv_init()
{
#ifdef HAVE_LIBLIRC_CLIENT
    int fd;

    if (-1 == (fd = lirc_init("xawtv",debug))) {
	if (debug)
	    fprintf(stderr,"lirc: no infrared remote support available\n");
	return -1;
    }
    if (0 != lirc_readconfig(NULL,&config,NULL)) {
	config = NULL;
    }
    if (debug)
	fprintf(stderr, "lirc: ~/.lircrc file %sfound\n",
		config ? "" : "not ");

    fcntl(fd,F_SETFL,O_NONBLOCK);
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    event_register_list(lirc_events);
    if (debug)
	fprintf(stderr,"lirc: init ok\n");

    return fd;
#else
    if (debug)
	fprintf(stderr,"lirc: not enabled at compile time\n");
    return -1;
#endif
}

int lirc_tv_havedata()
{
#ifdef HAVE_LIBLIRC_CLIENT
    char *code,event[32],*cmd,**argv;
    int dummy,repeat,argc;
    int ret=-1;

    strcpy(event,"lirc-key-");
    while (lirc_nextcode(&code)==0  &&  code!=NULL) {
	ret = 0;
	if (3 != sscanf(code,"%x %x %20s",&dummy,&repeat,event+9)) {
	    fprintf(stderr,"lirc: oops, parse error: %s",code);
	    continue;
	}
	if (debug)
	    fprintf(stderr,"lirc: key=%s repeat=%d\n", event+9, repeat);
	if (config) {
	    /* use ~/.lircrc */
	    while (lirc_code2char(config,code,&cmd)==0 && cmd != NULL) {
		if (debug)
		    fprintf(stderr,"lirc: cmd \"%s\"\n", cmd);
		if (0 == strcasecmp(cmd,"eventmap")) {
		    event_dispatch(event);
		} else {
		    argv = split_cmdline(cmd,&argc);
		    do_command(argc,argv);
		}
	    }
	} else {
	    /* standalone mode */
	    if (!repeat)
		event_dispatch(event);
	}
	free(code);
    }
    return ret;
#else
    return 0;
#endif
}
