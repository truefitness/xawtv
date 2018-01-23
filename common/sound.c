#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "grab-ng.h"
#include "sound.h"

void
oss_levels(struct ng_audio_buf *buf, int *left, int *right)
{
    int lmax,rmax,i,level;
    signed char   *s = (signed char*)   buf->data;
    unsigned char *u = (unsigned char*) buf->data;

    lmax = 0;
    rmax = 0;
    switch (buf->fmt.fmtid) {
    case AUDIO_U8_MONO:
	i = 0;
	while (i < buf->size) {
	    level = abs((int)u[i++] - 128);
	    if (lmax < level)
		lmax = level, rmax = level;
	}
	break;
    case AUDIO_U8_STEREO:
	i = 0;
	while (i < buf->size) {
	    level = abs((int)u[i++] - 128);
	    if (lmax < level)
		lmax = level;
	    level = abs((int)u[i++] - 128);
	    if (rmax < level)
		rmax = level;
	}
	break;
    case AUDIO_S16_BE_MONO:
    case AUDIO_S16_LE_MONO:
	i = (AUDIO_S16_BE_MONO == buf->fmt.fmtid) ? 0 : 1;
	while (i < buf->size) {
	    level = abs((int)s[i]);
	    i += 2;
	    if (lmax < level)
		lmax = level, rmax = level;
	}
	break;
    case AUDIO_S16_LE_STEREO:
    case AUDIO_S16_BE_STEREO:
	i = (AUDIO_S16_BE_STEREO == buf->fmt.fmtid) ? 0 : 1;
	while (i < buf->size) {
	    level = abs((int)s[i]);
	    i += 2;
	    if (lmax < level)
		lmax = level;
	    level = abs((int)s[i]);
	    i += 2;
	    if (rmax < level)
		rmax = level;
	}
	break;
    }
    *left  = lmax;
    *right = rmax;
}
