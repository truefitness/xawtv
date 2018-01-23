#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/fb.h>

#include "byteswap.h"

#include "fbtools.h"
#include "matrox.h"

/* ---------------------------------------------------------------------- */
/* generic                                                                */

void (*gfx_scaler_on)(int offscreen, int pitch, int width, int height,
		      int left, int right, int top, int bottom);
void (*gfx_scaler_off)(void);

static unsigned char	*bmmio;
static uint32_t		*mmio;

static void
wrio4(int adr, unsigned long val)
{
#if BYTE_ORDER == LITTLE_ENDIAN
    mmio[adr] = val;
#else
    mmio[adr] = SWAP4(val);
#endif
    /* usleep(10); */
}

/* ---------------------------------------------------------------------- */
/* Matrox G200/G400                                                      */

#define BES_BASE	0x3d00
#define BESA1ORG	((BES_BASE+0x00)>>2)
#define BESA2ORG	((BES_BASE+0x04)>>2)
#define BESB1ORG	((BES_BASE+0x08)>>2)
#define BESB2ORG	((BES_BASE+0x0c)>>2)
#define BESA1CORG	((BES_BASE+0x10)>>2)
#define BESA2CORG	((BES_BASE+0x14)>>2)
#define BESB1CORG	((BES_BASE+0x18)>>2)
#define BESB2CORG	((BES_BASE+0x1c)>>2)
#define BESCTL		((BES_BASE+0x20)>>2)
#define BESPITCH	((BES_BASE+0x24)>>2)
#define BESHCOORD	((BES_BASE+0x28)>>2)
#define BESVCOORD	((BES_BASE+0x2c)>>2)
#define BESHISCAL	((BES_BASE+0x30)>>2)
#define BESVISCAL	((BES_BASE+0x34)>>2)
#define BESHSRCST	((BES_BASE+0x38)>>2)
#define BESHSRCEND	((BES_BASE+0x3c)>>2)

#define BESV1WGHT	((BES_BASE+0x48)>>2)
#define BESV2WGHT	((BES_BASE+0x4c)>>2)
#define BESHSRCLST	((BES_BASE+0x50)>>2)
#define BESV1SRCLST	((BES_BASE+0x54)>>2)
#define BESV2SRCLST	((BES_BASE+0x58)>>2)
#define BESGLOBCTL	((BES_BASE+0xc0)>>2)
#define BESSTATUS	((BES_BASE+0xc4)>>2)

#define PALWTADD        0x3c00
#define X_DATAREG       0x3c0a
#define XKEYOPMODE      0x51

static void
matrox_scaler_on(int offscreen, int pitch, int width, int height,
		 int left, int right, int top, int bottom)
{
    /* color keying (turn it off) */
    bmmio[PALWTADD]  = XKEYOPMODE;
    bmmio[X_DATAREG] = 0;

    /* src */
    wrio4(BESA1ORG,   offscreen);
    wrio4(BESA2ORG,   offscreen);
    wrio4(BESB1ORG,   offscreen);
    wrio4(BESB2ORG,   offscreen);
    wrio4(BESPITCH,   pitch/2);

    /* dest */
    wrio4(BESHCOORD,  (left << 16) | right);
    wrio4(BESVCOORD,  (top << 16) | bottom);

    /* scale horiz */
    wrio4(BESHISCAL,   width*65536/(right-left) & 0x001ffffc);
    wrio4(BESHSRCST,   0 << 16);
    wrio4(BESHSRCEND,  width << 16);
    wrio4(BESHSRCLST,  (width-1) << 16);

    /* scale vert */
    wrio4(BESVISCAL,   height*65536/(bottom-top) & 0x001ffffc);
    wrio4(BESV1WGHT,   0);
    wrio4(BESV2WGHT,   0);
    wrio4(BESV1SRCLST, height-1);
    wrio4(BESV2SRCLST, height-1);

    /* turn on (enable, horizontal+vertical interpolation filters */
    wrio4(BESCTL,     (1 << 0) | (1 << 10) | (1 << 11));
    wrio4(BESGLOBCTL, 0);
}

static void
matrox_scaler_off(void)
{
    /* turn off */
    wrio4(BESCTL, 0);
}

/* ---------------------------------------------------------------------- */
/* ATI Mach64 VT+GT                                                       */

#define OVERLAY_X_Y_START         0x0000
#define OVERLAY_X_Y_END           0x0001
#define OVERLAY_VIDEO_KEY_CLR     0x0002
#define OVERLAY_VIDEO_KEY_MSK     0x0003
#define OVERLAY_GRAPHICS_KEY_CLR  0x0004
#define OVERLAY_GRAPHICS_KEY_MSK  0x0005
#define OVERLAY_KEY_CNTL          0x0006

#define OVERLAY_SCALE_INC         0x0008
#define OVERLAY_SCALE_CNTL        0x0009
#define SCALER_HEIGHT_WIDTH       0x000a
#define SCALER_TEST               0x000b
#define SCALER_BUF0_OFFSET        0x000d
#define SCALER_BUF1_OFFSET        0x000e
#define SCALER_BUF_PITCH          0x000f

#define VIDEO_FORMAT              0x0012
#define CAPTURE_CONFIG            0x0014

#define SCALER_COLOR_CNTL         0x0054
#define SCALER_H_COEFF0           0x0055
#define SCALER_H_COEFF1           0x0056
#define SCALER_H_COEFF2           0x0057
#define SCALER_H_COEFF3           0x0058
#define SCALER_H_COEFF4           0x0059

/* does'nt work for all color depth yet... */
static void
mach64_scaler_on(int offscreen, int pitch, int width, int height,
		 int left, int right, int top, int bottom)
{
    int v,h;

    v = (height << 12) / (bottom-top);
    h = (width << 12) / (right-left);

    wrio4(OVERLAY_SCALE_CNTL,    0);
    wrio4(OVERLAY_SCALE_INC,     (h << 16) | v);
    wrio4(VIDEO_FORMAT,          (12 << 16));
    wrio4(SCALER_BUF0_OFFSET,    offscreen);
    wrio4(SCALER_BUF1_OFFSET,    offscreen);
    wrio4(SCALER_BUF_PITCH,      pitch/2);
    wrio4(SCALER_HEIGHT_WIDTH,   (width << 16) | height);
    wrio4(CAPTURE_CONFIG,        0);

#if 1
    /* from gatos, don't know what this does, have no specs :-( */
    wrio4(SCALER_COLOR_CNTL, 0x00101000);
    wrio4(SCALER_H_COEFF0,   0x00002000);
    wrio4(SCALER_H_COEFF1,   0x0D06200D);
    wrio4(SCALER_H_COEFF2,   0x0D0A1C0D);
    wrio4(SCALER_H_COEFF3,   0x0C0E1A0C);
    wrio4(SCALER_H_COEFF4,   0x0C14140C);
#endif

    wrio4(OVERLAY_X_Y_START,     (left << 16) | top);
    wrio4(OVERLAY_X_Y_END,       ((right-1) << 16) | (bottom-1));
    wrio4(OVERLAY_VIDEO_KEY_MSK, 0);
    wrio4(OVERLAY_VIDEO_KEY_CLR, 0);
    wrio4(OVERLAY_KEY_CNTL,      1);

    wrio4(SCALER_TEST,           0); /* 2 == test mode */
    wrio4(OVERLAY_SCALE_CNTL,    (1<<31) | (1<<30));
}

static void
mach64_scaler_off(void)
{
    /* off */
    wrio4(OVERLAY_SCALE_CNTL, 0);
}

/* ---------------------------------------------------------------------- */
/* generic                                                                */

int
gfx_init(int fd)
{
    int off;

    switch (fb_fix.accel) {
    case FB_ACCEL_MATROX_MGAG200:
#ifdef FB_ACCEL_MATROX_MGAG400
    case FB_ACCEL_MATROX_MGAG400:
#endif
	gfx_scaler_on  = matrox_scaler_on;
	gfx_scaler_off = matrox_scaler_off;
	break;
    case FB_ACCEL_ATI_MACH64VT:
    case FB_ACCEL_ATI_MACH64GT:
	gfx_scaler_on  = mach64_scaler_on;
	gfx_scaler_off = mach64_scaler_off;
	break;
    default:
	return -1;
    }
    fb_var.accel_flags = 0;
    if (0 != ioctl(fd,FBIOPUT_VSCREENINFO,&fb_var)) {
	perror("FBIOPUT_VSCREENINFO");
	return -1;
    }
    bmmio = mmap(NULL, fb_fix.mmio_len, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, fb_fix.smem_len);
    if ((void*)-1 == bmmio) {
	perror("mmap");
	return -1;
    }
    off = (unsigned long)fb_fix.mmio_start -
	((unsigned long)fb_fix.mmio_start & ~(getpagesize()-1));
    bmmio += off;
    mmio = (uint32_t*)bmmio;
    return 0;
}
