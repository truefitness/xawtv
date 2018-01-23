/*
 * (c) 1998-2002 Gerd Knorr
 *
 *   functions to handle ftp uploads using the ftp utility
 *
 */
#include "config.h"
#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>

#include "ftp.h"

/* ---------------------------------------------------------------------- */
/* FTP stuff                                                              */

struct ftp_state {
    char *name;
    int connected;
    int debug;
    int pty, pid;
    char tty_name[32];
};

static
int open_pty(struct ftp_state *s)
{
#ifdef HAVE_GETPT
    int master;
    char *slave;

    if (-1 == (master = getpt()))
	return -1;
    if (-1 == grantpt(master) ||
	-1 == unlockpt(master) ||
	NULL == (slave = ptsname(master))) {
	close(master);
	return -1;
    }
    strcpy(s->tty_name,slave);
    return master;
#else
    static char pty_name[32];
    static char s1[] = "pqrs";
    static char s2[] = "0123456789abcdef";

    char *p1,*p2;
    int pty;

    for (p1 = s1; *p1; p1++) {
	for (p2 = s2; *p2; p2++) {
	    sprintf(pty_name,"/dev/pty%c%c",*p1,*p2);
	    sprintf(s->tty_name,"/dev/tty%c%c",*p1,*p2);
	    if (-1 == access(s->tty_name,R_OK|W_OK))
		continue;
	    if (-1 != (pty = open(pty_name,O_RDWR)))
		return pty;
	}
    }
    return -1;
#endif
}

struct ftp_state* ftp_init(char *name, int autologin, int passive, int debug)
{
    static char *doauto[] = { "ftp", NULL }; /* allow autologin via ~/.netrc */
    static char *noauto[] = { "ftp", "-n", NULL };
    struct ftp_state *s;

    s = malloc(sizeof(*s));
    memset(s,0,sizeof(*s));
    s->name  = name;
    s->debug = debug;
    if (-1 == (s->pty = open_pty(s))) {
	fprintf(stderr,"can't grab pty\n");
	exit(1);
    }
    switch (s->pid = fork()) {
    case -1:
	perror("fork");
	exit(1);
    case 0:
	/* child */
	close(s->pty);
	close(0); close(1); close(2);
	setsid();
	open(s->tty_name,O_RDWR); dup(0); dup(0);

	/* need english messages from ftp */
	setenv("LC_ALL","C",1);

	if (autologin)
	    execvp(doauto[0],doauto);
	else
	    execvp(noauto[0],noauto);
	perror("execvp");
	exit(1);
    default:
	/* parent */
	break;
    }
    ftp_recv(s);

    /* initialisation */
    if (passive) {
	ftp_send(s,1,"pass");
	ftp_recv(s);
    }
    return s;
}

void
ftp_send(struct ftp_state *s, int argc, ...)
{
    va_list ap;
    char line[256],*arg;
    int length,i;

    va_start(ap,argc);
    memset(line,0,256);
    length = 0;
    for (i = 0; i < argc; i++) {
	if (i)
	    line[length++] = ' ';
	arg = va_arg(ap,char*);
	length += strlen(arg);
	strcat(line,arg);
    }
    line[length++] = '\n';
    va_end (ap);

    if (s->debug)
	fprintf(stderr,"[%s]>> %s",s->name,line);
    if (length != write(s->pty,line,length)) {
	fprintf(stderr,"ftp: write error\n");
	exit(1);
    }
}

int
ftp_recv(struct ftp_state *s)
{
    char line[512],*p,*n;
    int length, done, status, ret=0;
    fd_set set;

    for (done = 0; !done;) {
	FD_ZERO(&set);
	FD_SET(s->pty,&set);
	select(s->pty+1,&set,NULL,NULL,NULL);

	switch (length = read(s->pty,line,511)) {
	case -1:
	    perror("ftp: read error");
	    exit(1);
	case 0:
	    fprintf(stderr,"ftp: EOF\n");
	    exit(1);
	}
	line[length] = 0;

	for (p=line; p && *p; p = n) {
	    /* split into lines */
	    if (NULL != (n = strchr(p,'\n')) || NULL != (n = strchr(p,'\r')))
		*(n++) = 0;
	    else
		n = NULL;
	    if (s->debug)
		fprintf(stderr,"[%s]<< %s\n",s->name,p);

	    /* prompt? */
	    if (NULL != strstr(p,"ftp>")) {
		done = 1;
	    }

	    /* line dropped ? */
	    if (NULL != strstr(p,"closed connection")) {
		fprintf(stderr,"ftp: lost connection\n");
		s->connected = 0;
	    }
	    if (NULL != strstr(p,"Not connected")) {
		if (!ftp_connected(s))
		    fprintf(stderr,"ftp: lost connection\n");
		s->connected = 0;
	    }

	    /* status? */
	    if (1 == sscanf(p,"%d",&status)) {
		ret = status;
	    }
	}
    }
    return ret;
}

void
ftp_connect(struct ftp_state *s, char *host, char *user, char *pass, char *dir)
{
    int delay = 0, status;

    for (;;) {
	/* Wiederholungsversuche mit wachsendem Intervall, 10 min max. */
	if (delay) {
	    fprintf(stderr,"ftp: connect failed, sleeping %d sec\n",delay);
	    sleep(delay);
	    delay *= 2;
	    if (delay > 600)
		delay = 600;
	} else {
	    delay = 5;
	}

	/* (re-) connect */
	ftp_send(s,1,"close");
	ftp_recv(s);
	ftp_send(s,2,"open",host);
	status = ftp_recv(s);
	if (230 == status) {
	    fprintf(stderr,"ftp: connected to %s, login ok\n",host);
	    s->connected = 1;
	    goto login_ok;
	}
	if (220 != status && 530 != status)
	    continue;

	fprintf(stderr,"ftp: connected to %s\n",host);
	s->connected = 1;

	/* login */
	ftp_send(s,3,"user",user,pass);
	if (230 != ftp_recv(s)) {
	    if (!ftp_connected(s))
		continue;
	    fprintf(stderr,"ftp: login incorrect\n");
	    exit(1);
	}

    login_ok:
	/* set directory */
	ftp_send(s,2,"cd",dir);
	if (250 != ftp_recv(s)) {
	    if (!s->connected)
		continue;
	    fprintf(stderr,"ftp: cd %s failed\n",dir);
	    exit(1);
	}

	/* initialisation */
	ftp_send(s,1,"bin");
	ftp_recv(s);
	ftp_send(s,1,"umask 022");
	ftp_recv(s);

	/* ok */
	break;
    }
}

int ftp_connected(struct ftp_state *s)
{
    return s->connected;
}

void
ftp_upload(struct ftp_state *s, char *local, char *remote, char *tmp)
{
    ftp_send(s,3,"put",local,tmp);
    ftp_recv(s);
    ftp_send(s,3,"rename",tmp,remote);
    ftp_recv(s);
}

void ftp_fini(struct ftp_state *s)
{
    ftp_send(s,1,"bye");
    ftp_recv(s);
    kill(s->pid,SIGTERM);
    close(s->pty);
    free(s);
}
