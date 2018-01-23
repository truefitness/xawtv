/*
 * dump current mixer settings
 *
 *   (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#ifdef HAVE_SOUNDCARD_H
# include <soundcard.h>
#endif
#ifdef HAVE_SYS_SOUNDCARD_H
# include <sys/soundcard.h>
#endif

char *labels[] = SOUND_DEVICE_LABELS;
char *names[]  = SOUND_DEVICE_NAMES;

static int
dump_mixer(char *devname)
{
#ifdef SOUND_MIXER_INFO
    struct mixer_info info;
#endif
    int               mix,i,devmask,recmask,recsrc,stereomask,volume;

    if (-1 == (mix = open(devname,O_RDONLY)))
	return -1;

    printf("%s",devname);
#ifdef SOUND_MIXER_INFO
    if (-1 != ioctl(mix,SOUND_MIXER_INFO,&info))
	printf(" = %s (%s)",info.id,info.name);
#endif
    printf("\n");

    if (-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_DEVMASK),&devmask) ||
	-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_STEREODEVS),&stereomask) ||
	-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_RECMASK),&recmask) ||
	-1 == ioctl(mix,MIXER_READ(SOUND_MIXER_RECSRC),&recsrc)) {
	perror("mixer ioctl");
	return -1;
    }

    for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
	if ((1<<i) & devmask) {
	    if (-1 == ioctl(mix,MIXER_READ(i),&volume)) {
		perror("mixer read volume");
		return -1;
	    }
	    printf("  %-10s (%2d) :  %s  %s%s",
		   names[i],i,
		   (1<<i) & stereomask ? "stereo" : "mono  ",
		   (1<<i) & recmask    ? "rec"    : "   ",
		   (1<<i) & recsrc     ? "*"      : " ");
	    if ((1<<i) & stereomask)
		printf("  %d/%d\n",volume & 0xff,(volume >> 8) & 0xff);
	    else
		printf("  %d\n",volume & 0xff);
	}
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    char devname[32];
    int i;

    /* first mixer device.  If "mixer0" does'nt work, try "mixer" */
    if (-1 == dump_mixer("/dev/mixer0"))
	dump_mixer("/dev/mixer");
    /* other more devices */
    for (i = 1; i < 8; i++) {
	sprintf(devname,"/dev/mixer%d",i);
	dump_mixer(devname);
    }
    return 0;
}
