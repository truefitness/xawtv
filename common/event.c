#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "grab-ng.h"
#include "commands.h"
#include "event.h"
#include "parseconfig.h"

/* ----------------------------------------------------------------------- */

static struct event_entry *event_conf_list;
static struct event_entry *event_builtin_list;

/* ----------------------------------------------------------------------- */

static void parse_action(struct event_entry *entry)
{
    char *token,*h;

    strcpy(entry->argbuf,entry->action);
    h = entry->argbuf;
    for (;;) {
	while (' ' == *h  ||  '\t' == *h)
	    h++;
	if ('\0' == *h)
	    break;
	if ('"' == *h) {
	    /* quoted string */
	    h++;
	    token = h;
	    while ('\0' != *h  &&  '"' != *h)
		h++;
	} else {
	    /* normal string */
	    token = h;
	    while ('\0' != *h  &&  ' ' != *h  &&  '\t' != *h)
		h++;
	}
	if ('\0' != *h) {
	    *h = 0;
	    h++;
	}
	entry->argv[entry->argc++] = token;
    }
}

/* ----------------------------------------------------------------------- */

int event_register(char *event, char *action)
{
    struct event_entry *entry;

    entry = malloc(sizeof(*entry));
    memset(entry,0,sizeof(*entry));
    strncpy(entry->event,event,127);
    strncpy(entry->action,action,127);
    entry->next = event_conf_list;
    event_conf_list = entry;
    parse_action(entry);
    if (debug)
	fprintf(stderr,"ev: reg conf \"%s\" => \"%s\"\n",
		entry->event,entry->action);
    return 0;
}

int event_register_list(struct event_entry *entry)
{
    for (; NULL != entry &&  0 != entry->event[0]; entry++) {
	entry->next = event_builtin_list;
	event_builtin_list = entry;
	parse_action(entry);
	if (debug)
	    fprintf(stderr,"ev: reg built-in \"%s\" => \"%s\"\n",
		    entry->event,entry->action);
    }
    return 0;
}

void event_readconfig(void)
{
    char **list,*val;

    list = cfg_list_entries("eventmap");
    if (NULL == list)
	return;

    for (; *list != NULL; list++)
	if (NULL != (val = cfg_get_str("eventmap",*list)))
	    event_register(*list,val);
}

void event_writeconfig(FILE *fp)
{
    struct event_entry *entry;

    if (NULL == event_conf_list)
	return;

    fprintf(fp,"[eventmap]\n");
    for (entry = event_conf_list; NULL != entry; entry = entry->next)
	fprintf(fp,"%s = %s\n",entry->event,entry->action);
    fprintf(fp,"\n");
}

/* ----------------------------------------------------------------------- */

int event_dispatch(char *event)
{
    struct event_entry *entry = NULL;
    char *name,*arg,*h,*argv[EVENT_ARGV_SIZE];
    int argc;

    /* parse */
    if (NULL != (h = strchr(event,'('))) {
	name = event;
	arg = h+1;
	*h = 0;
	if (NULL != (h = strchr(arg,')')))
	    *h = 0;
	if (debug)
	    fprintf(stderr,"ev: dispatch name=%s arg=%s\n",name,arg);
    } else {
	name = event;
	arg = NULL;
	if (debug)
	    fprintf(stderr,"ev: dispatch name=%s\n",name);
    }

    /* search lists */
    if (NULL == entry)
	for (entry = event_conf_list; NULL != entry; entry = entry->next)
	    if (0 == strcasecmp(name,entry->event))
		break;
    if (NULL == entry)
	for (entry = event_builtin_list; NULL != entry; entry = entry->next)
	    if (0 == strcasecmp(name,entry->event))
		break;
    if (NULL == entry) {
	if (debug)
	    fprintf(stderr,"ev: 404: %s\n",name);
	return 0;
    }

    /* call action */
    memcpy(argv,entry->argv,sizeof(argv));
    argc = entry->argc;
    if (arg)
	argv[argc++] = arg;
    do_command(argc,argv);

    return 0;
}
