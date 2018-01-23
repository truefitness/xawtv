#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "httpd.h"

/* ---------------------------------------------------------------------- */

void
read_request(struct REQUEST *req, int pipelined)
{
    int             rc;
    char            *h;

 restart:
    rc = read(req->fd, req->hreq + req->hdata, MAX_HEADER - req->hdata);
    switch (rc) {
    case -1:
	if (errno == EAGAIN) {
	    if (pipelined)
		break; /* check if there is already a full request */
	    else
		return;
	}
	if (errno == EINTR)
	    goto restart;
	xperror(LOG_INFO,"read",req->peerhost);
	/* fall through */
    case 0:
	req->state = STATE_CLOSE;
	return;
    default:
	req->hdata += rc;
	req->hreq[req->hdata] = 0;
    }

    /* check if this looks like a http request after
       the first few bytes... */
    if (req->hdata < 5)
	return;
    if (strncmp(req->hreq,"GET ",4)  != 0  &&
	strncmp(req->hreq,"PUT ",4)  != 0  &&
	strncmp(req->hreq,"HEAD ",5) != 0  &&
	strncmp(req->hreq,"POST ",5) != 0) {
	mkerror(req,400,0);
	return;
    }

    /* header complete ?? */
    if (NULL != (h = strstr(req->hreq,"\r\n\r\n")) ||
	NULL != (h = strstr(req->hreq,"\n\n"))) {
	if (*h == '\r') {
	    h += 4;
	    *(h-2) = 0;
	} else {
	    h += 2;
	    *(h-1) = 0;
	}
	req->lreq  = h - req->hreq;
	req->state = STATE_PARSE_HEADER;
	return;
    }

    if (req->hdata == MAX_HEADER) {
	/* oops: buffer full, but found no complete request ... */
	mkerror(req,400,0);
	return;
    }
    return;
}

/* ---------------------------------------------------------------------- */

static int
unhex(unsigned char c)
{
    if (c < '@')
	return c - '0';
    return (c & 0x0f) + 9;
}

static void
unquote(unsigned char *dst, unsigned char *src)
{
    int i,j,q,n=strlen(src);

    q=0;
    for (i=0, j=0; i<n; i++, j++) {
	if (src[i] == '?')
	    q = 1;
	if (q && src[i] == '+') {
	    dst[j] = ' ';
	} else if (src[i] == '%') {
	    dst[j] = (unhex(src[i+1]) << 4) | unhex(src[i+2]);
	    i += 2;
	} else {
	    dst[j] = src[i];
	}
    }
    dst[j] = 0;
}

void
parse_request(struct REQUEST *req)
{
    char filename[2048], proto[5], *h;
    int  port;

    if (debug > 2)
	fprintf(stderr,"%s\n",req->hreq);

    /* parse request. Hehe, scanf is powerfull :-) */
    if (4 != sscanf(req->hreq, "%4[A-Z] %255[^ \t\r\n] HTTP/%d.%d",
		    req->type,filename,&(req->major),&(req->minor))) {
	mkerror(req,400,0);
	return;
    }
    if (filename[0] == '/') {
	strcpy(req->uri,filename);
    } else {
	port = 0;
	*proto = 0;
	if (4 != sscanf(filename,"%4[a-zA-Z]://%64[a-zA-Z0-9.-]:%d%255[^ \t\r\n]",
			proto,req->hostname,&port,req->uri) &&
	    3 != sscanf(filename,"%4[a-zA-Z]://%64[a-zA-Z0-9.-]%255[^ \t\r\n]",
			proto,req->hostname,req->uri)) {
	    mkerror(req,400,0);
	    return;
	}
	if (*proto != 0 && 0 != strcasecmp(proto,"http")) {
	    mkerror(req,400,0);
	    return;
	}
    }

    unquote(req->path,req->uri);
    if (debug)
	fprintf(stderr,"%03d: %s \"%s\" HTTP/%d.%d\n",
		req->fd, req->type, req->path, req->major, req->minor);

    if (0 != strcmp(req->type,"GET") &&
	0 != strcmp(req->type,"HEAD")) {
	mkerror(req,501,0);
	return;
    }

    if (0 == strcmp(req->type,"HEAD")) {
	req->head_only = 1;
    }

    /* checks */
    if (req->path[0] != '/') {
	mkerror(req,400,0);
	return;
    }

    /* parse header lines */
    req->keep_alive = req->minor;
    for (h = req->hreq; h - req->hreq < req->lreq;) {
	h = strchr(h,'\n');
	if (NULL == h)
	    break;
	h++;

	if (0 == strncasecmp(h,"Connection: ",12)) {
	    req->keep_alive = (0 == strncasecmp(h+12,"Keep-Alive",10));

	} else if (0 == strncasecmp(h,"Host: ",6)) {
	    sscanf(h+6,"%64[a-zA-Z0-9.-]",req->hostname);
	}
    }

    /* make sure we have a hostname */
    if (req->hostname[0] == '\0' || canonicalhost)
	strncpy(req->hostname,server_host,64);

    /* lowercase hostname */
    for (h = req->hostname; *h != 0; h++) {
	if (*h < 'A' || *h > 'Z')
	    continue;
	*h += 32;
    }

    /* handle request (more to come) */
    buildpage(req);
    return;
}
