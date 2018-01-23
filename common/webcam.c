#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>

#include "grab-ng.h"
#include "writefile.h"
#include "webcam.h"

extern int jpeg_quality;
extern int debug;
char *webcam;

struct WEBCAM {
    pthread_mutex_t lock;
    pthread_cond_t  wait;
    char *filename;
    struct ng_video_buf *buf;
};

static void*
webcam_writer(void *arg)
{
    struct WEBCAM *web = arg;
    int rename,fd,old;
    char tmpfilename[512];
    struct ng_video_fmt *fmt;

    if (debug)
	fprintf(stderr,"webcam_writer start\n");
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&old);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&old);
    pthread_mutex_lock(&web->lock);
    for (;;) {
	while (web->buf == NULL) {
	    if (debug)
		fprintf(stderr,"webcam_writer: waiting for data\n");
	    pthread_cond_wait(&web->wait, &web->lock);
	}
	fmt = &web->buf->fmt;
	if (debug)
	    fprintf(stderr,"webcam_writer: %d %dx%d \n",
		    fmt->fmtid,fmt->width,fmt->height);
	rename = 1;
	sprintf(tmpfilename,"%s.$$$",web->filename);
	switch (fmt->fmtid) {
	case VIDEO_MJPEG:
	case VIDEO_JPEG:
	    if (-1 == (fd = open(tmpfilename,O_CREAT|O_WRONLY,0666))) {
		fprintf(stderr,"open(%s): %s\n",tmpfilename,
			strerror(errno));
		goto done;
	    }
	    write(fd,web->buf->data,web->buf->size);
	    close(fd);
	    break;
#if 0 /* FIXME */
	case VIDEO_BGR24:
	    data = malloc(web->buf->size);
	    memcpy(data,web->buf->data,web->buf->size);
	    swap_rgb24(data,fmt->width*fmt->height);
	    write_jpeg(tmpfilename,data,jpeg_quality,0);
	    free(data);
	    break;
#endif
	case VIDEO_RGB24:
	    write_jpeg(tmpfilename,web->buf,ng_jpeg_quality,0);
	    break;
	default:
	    fprintf(stderr,"webcam_writer: can't deal with format=%d\n",
		    fmt->fmtid);
	    rename = 0;
	}
	if (rename) {
	    unlink(web->filename);
	    if (-1 == link(tmpfilename,web->filename)) {
		fprintf(stderr,"link(%s,%s): %s\n",
			tmpfilename,web->filename,strerror(errno));
		goto done;
	    }
	    unlink(tmpfilename);
	}
	free(web->filename);
	ng_release_video_buf(web->buf);
	web->buf = NULL;
    }
 done:
    pthread_mutex_unlock(&web->lock);
    if (debug)
	fprintf(stderr,"webcam_writer done\n");
    return NULL;
}

/* ----------------------------------------------------------------------- */

static struct WEBCAM *web;
static pthread_t tweb;

void
webcam_init()
{
    web = malloc(sizeof(struct WEBCAM));
    memset(web,0,sizeof(struct WEBCAM));
    pthread_mutex_init(&web->lock, NULL);
    pthread_create(&tweb,NULL,webcam_writer,web);
    return;
}

void
webcam_exit()
{
    if (web) {
	pthread_cancel(tweb);
	free(web);
	web = NULL;
    }
}

int
webcam_put(char *filename, struct ng_video_buf *buf)
{
    int ret = 0;

    if (NULL == web)
	webcam_init();

    if (-1 == pthread_mutex_trylock(&web->lock)) {
	if (debug)
	    fprintf(stderr,"webcam_put: locked\n");
	return -1;
    }
    if (NULL != web->buf) {
	if (debug)
	    fprintf(stderr,"webcam_put: still has data\n");
	ret = -1;
	goto done;
    }

    web->filename = strdup(filename);
    web->buf = buf;
    buf->refcount++;
    if (debug)
	fprintf(stderr,"webcam_put: ok\n");
    pthread_cond_signal(&web->wait);

 done:
    pthread_mutex_unlock(&web->lock);
    return ret;
}
