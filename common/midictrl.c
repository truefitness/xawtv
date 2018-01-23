#include "config.h"

#ifdef HAVE_ALSA

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <X11/Intrinsic.h>

#include "grab-ng.h"
#include "commands.h"
#include "channel.h"
#include "midictrl.h"
#include "event.h"

extern int debug;

/* ------------------------------------------------------------------------ */

static char *midi_events[] = {
    "system", "result",
    "r2", "r3", "r4",
    "note", "noteon", "noteoff", "keypress",
    "r9",
    "controller", "pgmchange", "chanpress", "pitchbend",
    "control14", "nonregparam", "regparam",
    "r17", "r18", "r19",
    "songpos", "songsel", "qframe", "timesign", "keysign",
    "r25", "r26", "r27", "r28", "r29",
    "start", "continue", "stop", "setpos_tick", "setpos_time",
    "tempo", "clock", "tick",
    "r38", "r39",
    "tune_request", "reset", "sensing",
    "r43", "r44", "r45", "r46", "r47", "r48", "r49",
    "echo", "oss",
    "r52", "r53", "r54", "r55", "r56", "r57", "r58", "r59",
    "client_start", "client_exit", "client_change",
    "port_start", "port_exit", "port_change",
    "subscribed", "used", "unsubscribed", "unused",
    "sample", "sample_cluster", "sample_start", "sample_stop",
    "sample_freq", "sample_volume", "sample_loop",
    "sample_position", "sample_private1",
    "r79",
    "r80", "r81", "r82", "r83", "r84", "r85", "r86", "r87", "r88", "r89",
    "usr0", "usr1", "usr2", "usr3", "usr4",
    "usr5", "usr6", "usr7", "usr8", "usr9",
    "instr_begin", "instr_end", "instr_info", "instr_info_result",
    "instr_finfo", "instr_finfo_result", "instr_reset", "instr_status",
    "instr_status_result", "instr_put", "instr_get", "instr_get_result",
    "instr_free", "instr_list", "instr_list_result", "instr_cluster",
    "instr_cluster_get", "instr_cluster_result", "instr_change",
    "r119",
    "r120", "r121", "r122", "r123", "r124",
    "r125", "r126", "r127", "r128", "r129",
    "sysext", "bounce",
    "r132", "r133", "r134",
    "usr_var0", "usr_var1", "usr_var2", "usr_var3", "usr_var4",
    "ipcshm",
    "r141", "r142", "r143", "r144",
    "usr_varipc0", "usr_varipc1", "usr_varipc2", "usr_varipc3", "usr_varipc4",
    "kernel_error", "kernel_quote"
};
#define EV_NAME(x) (x < sizeof(midi_events)/sizeof(char*) ?	\
		    midi_events[x] : "UNKNOWN")

static void
midi_dump_ev(FILE *out, snd_seq_event_t *ev)
{
    fprintf(out,"midi ev:");
    if (ev->flags & SND_SEQ_TIME_STAMP_TICK)
	fprintf(out," tick %d",ev->time.tick);
    if (ev->flags & SND_SEQ_TIME_STAMP_REAL) {
	fprintf(out," real %d:%06d",
		ev->time.time.tv_sec,ev->time.time.tv_nsec);
    }
    if (ev->flags & SND_SEQ_TIME_MODE_ABS)
	fprintf(out," abs");
    if (ev->flags & SND_SEQ_TIME_MODE_REL)
	fprintf(out," rel");

    fprintf(out," [%d:%d] %s",
	    ev->source.client,ev->source.port,EV_NAME(ev->type));

    switch (ev->type) {
    case SND_SEQ_EVENT_NOTE:
	fprintf(out," ch=%d note=%d vel=%d off_vel=%d dur=%d",
		ev->data.note.channel,
		ev->data.note.note,
		ev->data.note.velocity,
		ev->data.note.off_velocity,
		ev->data.note.duration);
	break;
    case SND_SEQ_EVENT_NOTEON:
    case SND_SEQ_EVENT_NOTEOFF:
    case SND_SEQ_EVENT_KEYPRESS:
	fprintf(out," ch=%d note=%d vel=%d",
		ev->data.note.channel,
		ev->data.note.note,
		ev->data.note.velocity);
	break;
    case SND_SEQ_EVENT_CONTROLLER:
    case SND_SEQ_EVENT_PGMCHANGE:
    case SND_SEQ_EVENT_CONTROL14:
    case SND_SEQ_EVENT_NONREGPARAM:
    case SND_SEQ_EVENT_REGPARAM:
	fprintf(out," ch=%d par=%d val=%d",
		ev->data.control.channel,
		ev->data.control.param,
		ev->data.control.value);
	break;
    case SND_SEQ_EVENT_CHANPRESS:
    case SND_SEQ_EVENT_PITCHBEND:
	fprintf(out," ch=%d val=%d",
		ev->data.control.channel,
		ev->data.control.value);
	break;
    case SND_SEQ_EVENT_SONGPOS:
    case SND_SEQ_EVENT_SONGSEL:
    case SND_SEQ_EVENT_QFRAME:
    case SND_SEQ_EVENT_TIMESIGN:
    case SND_SEQ_EVENT_KEYSIGN:
	fprintf(out," val=%d",
		ev->data.control.value);
	break;
    }
    fprintf(out,"\n");
}

/* ------------------------------------------------------------------------ */

int midi_open(struct midi_handle *h, char *name)
{
    char *func;
    int rc;

    func = "snd_seq_open";
#if SND_LIB_VERSION >= 0x000900
    if ((rc = snd_seq_open(&h->seq, "default", SND_SEQ_OPEN_INPUT,
			   SND_SEQ_NONBLOCK)) < 0)
	goto err;
#else
    if ((rc = snd_seq_open(&h->seq, SND_SEQ_OPEN_IN)) < 0)
	goto err;
#endif

    func = "snd_seq_set_client_name";
    if ((rc = snd_seq_set_client_name(h->seq, name)) < 0)
	goto err;

    func = "snd_seq_create_simple_port";
    if ((rc = snd_seq_create_simple_port
	 (h->seq,"name",
	  SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
	  SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
	goto err;
    h->port = rc;

    if (debug)
	fprintf(stderr,"midi: open ok [%d:%d]\n",
		snd_seq_client_id(h->seq),h->port);
#if SND_LIB_VERSION >= 0x000900
    {
	struct pollfd p;
	snd_seq_poll_descriptors(h->seq, &p, 1, POLLIN);
	h->fd = p.fd;
    }
#else
    h->fd = snd_seq_file_descriptor(h->seq);
#endif
    return h->fd;

 err:
    fprintf(stderr, "midi: %s: %s\n",func,snd_strerror(rc));
    if (h->seq) {
	snd_seq_close(h->seq);
	h->seq = NULL;
    }
    return -1;
}

int midi_close(struct midi_handle *h)
{
    if (debug)
	fprintf(stderr,"midi: close\n");
    if (h->ev) {
	snd_seq_free_event(h->ev);
	h->ev = NULL;
    }
    if (h->seq) {
	snd_seq_close(h->seq);
	h->seq = NULL;
    }
    return 0;
}

int midi_connect(struct midi_handle *h, char *arg)
{
    int client, port, rc;
    snd_seq_port_subscribe_t *psubs;
#if SND_LIB_VERSION >= 0x000900
    snd_seq_addr_t           addr;
#else
    snd_seq_port_subscribe_t subs;
#endif

    if (2 == sscanf(arg,"%d:%d",&client,&port)) {
	/* nothing */
    } else {
	return -1;
    }
#if SND_LIB_VERSION >= 0x000900
    snd_seq_port_subscribe_malloc(&psubs);
    addr.client = client;
    addr.port   = port;
    snd_seq_port_subscribe_set_sender(psubs,&addr);
    addr.client = snd_seq_client_id(h->seq);
    addr.port   = h->port;
    snd_seq_port_subscribe_set_dest(psubs,&addr);
#else
    psubs = &subs;
    memset(&subs,0,sizeof(subs));
    subs.sender.client = client;
    subs.sender.port   = port;
    subs.dest.client   = snd_seq_client_id(h->seq);
    subs.dest.port     = h->port;
#endif
    if ((rc = snd_seq_subscribe_port(h->seq, psubs)) < 0) {
	fprintf(stderr, "midi: snd_seq_subscribe_port: %s\n",snd_strerror(rc));
    } else
	if (debug)
	    fprintf(stderr,"midi: subscribe ok [%d:%d]\n",client,port);

#if SND_LIB_VERSION >= 0x000900
    snd_seq_port_subscribe_free(psubs);
#endif
    return rc;
}

int midi_read(struct midi_handle *h)
{
    int rc;

    if (h->ev) {
	snd_seq_free_event(h->ev);
	h->ev = NULL;
    }
    if ((rc = snd_seq_event_input(h->seq,&h->ev)) < 0) {
	fprintf(stderr, "midi: snd_seq_event_input: %s\n",snd_strerror(rc));
	return -1;
    }
    if (debug > 1)
	midi_dump_ev(stderr,h->ev);
    return 0;
}

/* ------------------------------------------------------------------------ */

#ifdef STANDALONE

int debug = 2;

int main(int argc, char *argv[])
{
    struct midi_handle h;

    memset(&h,0,sizeof(h));
    if (-1 == midi_open(&h, "midi dump"))
	exit(1);

    if (argc > 1)
	midi_connect(&h,argv[1]);

    for (;;)
	midi_read(&h);

    midi_close(&h);
    exit(0);
}

#else /* STANDALONE */

void midi_translate(struct midi_handle *h)
{
    char event[64];
    int i;

    switch (h->ev->type) {
    case SND_SEQ_EVENT_NOTEON:
	if (0 == h->ev->data.note.velocity)
	    return;
	for (i = 0; i < count; i++) {
	    if (channels[i]->midi != 0 &&
		channels[i]->midi == h->ev->data.note.note) {
		do_va_cmd(2,"setstation",channels[i]->name);
		return;
	    }
	}
	sprintf(event,"midi-note-%d",h->ev->data.note.note);
	event_dispatch(event);
	break;
    case SND_SEQ_EVENT_CONTROLLER:
	sprintf(event,"midi-ctrl-%d(%d%%)",
		h->ev->data.control.param,
		h->ev->data.control.value*100/128);
	event_dispatch(event);
    }
}

#endif /* STANDALONE */
#endif /* HAVE_ALSA */
