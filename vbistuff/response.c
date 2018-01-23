#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "httpd.h"

/* ---------------------------------------------------------------------- */

static struct HTTP_STATUS {
    int   status;
    char *head;
    char *body;
} http[] = {
    { 200, "200 OK",                       NULL },
    { 400, "400 Bad Request",              "*PLONK*\n" },
    { 404, "404 Not Found",       "videotext page not found in cache\n" },
    { 408, "408 Request Timeout",          "Request Timeout\n" },
    { 500, "500 Internal Server Error",    "Sorry folks\n" },
    { 501, "501 Not Implemented",          "Sorry folks\n" },
    {   0, NULL,                        NULL }
};

/* ---------------------------------------------------------------------- */

#define RESPONSE_START			\
	"HTTP/1.1 %s\r\n"		\
	"Server: %s\r\n"		\
	"Connection: %s\r\n"
#define RFCTIME				\
	"%a, %d %b %Y %H:%M:%S GMT"
#define BOUNDARY			\
	"XXX_CUT_HERE_%ld_XXX"

void
mkerror(struct REQUEST *req, int status, int ka)
{
    int i;
    for (i = 0; http[i].status != 0; i++)
	if (http[i].status == status)
	    break;
    req->status = status;
    req->body   = http[i].body;
    req->lbody  = strlen(req->body);
    if (!ka)
	req->keep_alive = 0;
    req->lres = sprintf(req->hres,
			RESPONSE_START
			"Content-Type: text/plain\r\n"
			"Content-Length: %d\r\n",
			http[i].head,server_name,
			req->keep_alive ? "Keep-Alive" : "Close",
			req->lbody);
    if (401 == status)
	req->lres += sprintf(req->hres+req->lres,
			     "WWW-Authenticate: Basic, realm=\"webfs\"\r\n");
    req->lres += strftime(req->hres+req->lres,80,
			  "Date: " RFCTIME "\r\n\r\n",
			  gmtime(&now));
    req->state = STATE_WRITE_HEADER;
    if (debug)
	fprintf(stderr,"%03d: error: %d, connection=%s\n",
		req->fd, status, req->keep_alive ? "Keep-Alive" : "Close");
}

void
mkredirect(struct REQUEST *req)
{
    req->status = 302;
    req->body   = req->path;
    req->lbody  = strlen(req->body);
    req->lres = sprintf(req->hres,
			RESPONSE_START
			"Location: http://%s:%d%s\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: %d\r\n",
			"302 Redirect",server_name,
			req->keep_alive ? "Keep-Alive" : "Close",
			req->hostname,tcp_port,req->path,
			req->lbody);
    req->lres += strftime(req->hres+req->lres,80,
			  "Date: " RFCTIME "\r\n\r\n",
			  gmtime(&now));
    req->state = STATE_WRITE_HEADER;
    if (debug)
	fprintf(stderr,"%03d: 302 redirect: %s, connection=%s\n",
		req->fd, req->path, req->keep_alive ? "Keep-Alive" : "Close");
}

void
mkheader(struct REQUEST *req, int status, time_t mtime)
{
    int    i;
    for (i = 0; http[i].status != 0; i++)
	if (http[i].status == status)
	    break;
    req->status = status;
    req->lres = sprintf(req->hres,
			RESPONSE_START,
			http[i].head,server_name,
			req->keep_alive ? "Keep-Alive" : "Close");
    req->lres += sprintf(req->hres+req->lres,
			 "Content-Type: %s\r\n"
			 "Content-Length: %d\r\n",
			 req->mime,
			 req->lbody);
    if (mtime != -1)
	req->lres += strftime(req->hres+req->lres,80,
			      "Last-Modified: " RFCTIME "\r\n",
			      gmtime(&mtime));
    req->lres += strftime(req->hres+req->lres,80,
			  "Date: " RFCTIME "\r\n\r\n",
			  gmtime(&now));
    req->state = STATE_WRITE_HEADER;
    if (debug)
	fprintf(stderr,"%03d: %d, connection=%s\n",
		req->fd, status, req->keep_alive ? "Keep-Alive" : "Close");
}

/* ---------------------------------------------------------------------- */

void write_request(struct REQUEST *req)
{
    int rc;

    for (;;) {
	switch (req->state) {
	case STATE_WRITE_HEADER:
	    rc = write(req->fd,req->hres + req->written,
		       req->lres - req->written);
	    switch (rc) {
	    case -1:
		if (errno == EAGAIN)
		    return;
		if (errno == EINTR)
		    continue;
		xperror(LOG_INFO,"write",req->peerhost);
		/* fall through */
	    case 0:
		req->state = STATE_CLOSE;
		return;
	    default:
		req->written += rc;
		req->bc += rc;
		if (req->written != req->lres)
		    return;
	    }
	    req->written = 0;
	    if (req->head_only) {
		req->state = STATE_FINISHED;
		return;
	    } else {
		req->state = STATE_WRITE_BODY;
	    }
	    break;
	case STATE_WRITE_BODY:
	    rc = write(req->fd,req->body + req->written,
		       req->lbody - req->written);
	    switch (rc) {
	    case -1:
		if (errno == EAGAIN)
		    return;
		if (errno == EINTR)
		    continue;
		xperror(LOG_INFO,"write",req->peerhost);
		/* fall through */
	    case 0:
		req->state = STATE_CLOSE;
		return;
	    default:
		req->written += rc;
		req->bc += rc;
		if (req->written != req->lbody)
		    return;
	    }
	    req->state = STATE_FINISHED;
	    return;
	} /* switch(state) */
    } /* for (;;) */
}
