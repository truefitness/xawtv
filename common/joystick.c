#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_LINUX_JOYSTICK_H
# include <linux/joystick.h>
#endif

#include "grab-ng.h"
#include "commands.h"
#include "joystick.h"
#include "event.h"

/*-----------------------------------------------------------------------*/

extern int debug;

#ifdef HAVE_LINUX_JOYSTICK_H
struct JOYTAB {
    int   class;
    int   number;
    int   value;
    char  *event;
};

static struct JOYTAB joytab[] = {
    { JS_EVENT_BUTTON, 0, 1,    "joy-button-0"   },
    { JS_EVENT_BUTTON, 1, 1,    "joy-button-1"   },
    { JS_EVENT_AXIS, 1, -32767, "joy-axis-up"    },
    { JS_EVENT_AXIS, 1,  32767, "joy-axis-down"  },
    { JS_EVENT_AXIS, 0,  32767, "joy-axis-left"  },
    { JS_EVENT_AXIS, 0, -32767, "joy-axis-right" },
};
#define NJOYTAB (sizeof(joytab)/sizeof(struct JOYTAB))

static struct event_entry joy_events[] = {
    {
	event:  "joy-button-0",
	action: "quit",
    },{
	event:  "joy-button-1",
	action: "fullscreen",
    },{
	event:  "joy-axis-up",
	action: "volume inc",
    },{
	event:  "joy-axis-down",
	action: "volume dec",
    },{
	event:  "joy-axis-left",
	action: "setchannel prev",
    },{
	event:  "joy-axis-right",
	action: "setchannel next",
    },{
	/* end of list */
    }
};

#endif

int joystick_tv_init(char *dev)
{
#ifdef HAVE_LINUX_JOYSTICK_H
    int fd;

    if (NULL == dev)
	return -1;
    if (-1 == (fd = open(dev, O_NONBLOCK))) {
	fprintf(stderr, "joystick: open %s: %s\n",dev,strerror(errno));
	return -1;
    }
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    event_register_list(joy_events);
    return fd;
#else
    if (debug)
	fprintf(stderr,"joystick: not enabled at compile time\n");
    return -1;
#endif
}

void joystick_tv_havedata(int js)
{
#ifdef HAVE_LINUX_JOYSTICK_H
    unsigned int i;
    struct js_event event;
    if (debug)
	fprintf(stderr, "joystick: received input\n");
    if (read(js, &event, sizeof(struct js_event))) {
	for (i = 0; i < NJOYTAB; i++)
	    if (joytab[i].class == (event.type)
		&& joytab[i].number == event.number
		&& joytab[i].value == event.value)
		break;
	if (i != NJOYTAB)
	    event_dispatch(joytab[i].event);
    }
#endif
}
