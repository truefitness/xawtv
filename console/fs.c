/*
 * text rendering for the framebuffer console
 * pick fonts from X11 font server or
 * use linux consolefont psf files.
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/fb.h>

#include "fbtools.h"
#include "fs.h"

/* ------------------------------------------------------------------ */

#define BIT_ORDER       BitmapFormatBitOrderMSB
#ifdef BYTE_ORDER
#undef BYTE_ORDER
#endif
#define BYTE_ORDER      BitmapFormatByteOrderMSB
#define SCANLINE_UNIT   BitmapFormatScanlineUnit8
#define SCANLINE_PAD    BitmapFormatScanlinePad8
#define EXTENTS         BitmapFormatImageRectMin

#define SCANLINE_PAD_BYTES 1
#define GLWIDTHBYTESPADDED(bits, nBytes)                                    \
	((nBytes) == 1 ? (((bits)  +  7) >> 3)          /* pad to 1 byte  */\
	:(nBytes) == 2 ? ((((bits) + 15) >> 3) & ~1)    /* pad to 2 bytes */\
	:(nBytes) == 4 ? ((((bits) + 31) >> 3) & ~3)    /* pad to 4 bytes */\
	:(nBytes) == 8 ? ((((bits) + 63) >> 3) & ~7)    /* pad to 8 bytes */\
	: 0)

static const unsigned fs_masktab[] = {
    (1 << 7), (1 << 6), (1 << 5), (1 << 4),
    (1 << 3), (1 << 2), (1 << 1), (1 << 0),
};

/* ------------------------------------------------------------------ */

static unsigned int       bpp,black,white;

static void (*setpixel)(void *ptr, unsigned int color);

static void setpixel1(void *ptr, unsigned int color)
{
    unsigned char *p = ptr;
    *p = color;
}
static void setpixel2(void *ptr, unsigned int color)
{
    unsigned short *p = ptr;
    *p = color;
}
static void setpixel3(void *ptr, unsigned int color)
{
    unsigned char *p = ptr;
    *(p++) = (color >> 16) & 0xff;
    *(p++) = (color >>  8) & 0xff;
    *(p++) =  color        & 0xff;
}
static void setpixel4(void *ptr, unsigned int color)
{
    unsigned long *p = ptr;
    *p = color;
}

int fs_init_fb(int white8)
{
    switch (fb_var.bits_per_pixel) {
    case 8:
	white = white8; black = 0; bpp = 1;
	setpixel = setpixel1;
	break;
    case 15:
    case 16:
	if (fb_var.green.length == 6)
	    white = 0xffff;
	else
	    white = 0x7fff;
	black = 0; bpp = 2;
	setpixel = setpixel2;
	break;
    case 24:
	white = 0xffffff; black = 0; bpp = fb_var.bits_per_pixel/8;
	setpixel = setpixel3;
	break;
    case 32:
	white = 0xffffff; black = 0; bpp = fb_var.bits_per_pixel/8;
	setpixel = setpixel4;
	break;
    default:
	fprintf(stderr, "Oops: %i bit/pixel ???\n",
		fb_var.bits_per_pixel);
	return -1;
    }
    return 0;
}

void fs_render_fb(unsigned char *ptr, int pitch,
		  FSXCharInfo *charInfo, unsigned char *data)
{
    int row,bit,bpr,x;

    bpr = GLWIDTHBYTESPADDED((charInfo->right - charInfo->left),
			     SCANLINE_PAD_BYTES);
    for (row = 0; row < (charInfo->ascent + charInfo->descent); row++) {
	for (x = 0, bit = 0; bit < (charInfo->right - charInfo->left); bit++) {
	    if (data[bit>>3] & fs_masktab[bit&7])
		setpixel(ptr+x,white);
	    x += bpp;
	}
	data += bpr;
	ptr += pitch;
    }
}

int fs_puts(struct fs_font *f, unsigned int x, unsigned int y,
	    unsigned char *str)
{
    unsigned char *pos,*start;
    int i,c,j,w;

    pos  = fb_mem+fb_mem_offset;
    pos += fb_fix.line_length * y;
    for (i = 0; str[i] != '\0'; i++) {
	c = str[i];
	if (NULL == f->eindex[c])
	    continue;
	/* clear with bg color */
	start = pos + x*bpp + f->fontHeader.max_bounds.descent * fb_fix.line_length;
	w = (f->eindex[c]->width+1)*bpp;
	for (j = 0; j < f->height; j++) {
	    memset(start,0,w);
	    start += fb_fix.line_length;
	}
	/* draw char */
	start = pos + x*bpp + fb_fix.line_length * (f->height-f->eindex[c]->ascent);
	fs_render_fb(start,fb_fix.line_length,f->eindex[c],f->gindex[c]);
	x += f->eindex[c]->width;
	if (x > fb_var.xres - f->width)
	    return -1;
    }
    return x;
}

int fs_textwidth(struct fs_font *f, unsigned char *str)
{
    int width = 0;
    int i,c;

    for (i = 0; str[i] != '\0'; i++) {
	c = str[i];
	if (NULL == f->eindex[c])
	    continue;
	width += f->eindex[c]->width;
    }
    return width;
}

void fs_render_tty(FSXCharInfo *charInfo, unsigned char *data)
{
    int bpr,row,bit,on;

    bpr = GLWIDTHBYTESPADDED((charInfo->right - charInfo->left),
			     SCANLINE_PAD_BYTES);
    for (row = 0; row < (charInfo->ascent + charInfo->descent); row++) {
	fprintf(stdout,"|");
	for (bit = 0; bit < (charInfo->right - charInfo->left); bit++) {
	    on = data[bit>>3] & fs_masktab[bit&7];
	    fprintf(stdout,"%s",on ? "##" : "  ");
	}
	fprintf(stdout,"|\n");
	data += bpr;
    }
    fprintf(stdout,"--\n");
}

/* ------------------------------------------------------------------ */


void fs_free(struct fs_font *f)
{
    if (f->gindex)
	free(f->gindex);
#if 0 /* FIXME */
    if (f->glyphs)
	FSFree((char *) f->glyphs);
#endif
    free(f);
}

/* ------------------------------------------------------------------ */
/* load console font file                                             */

static char *default_font[] = {
    /* why the heck every f*cking distribution picks another
       location for these fonts ??? */
    "/usr/share/consolefonts/lat1-16.psf",
    "/usr/share/consolefonts/lat1-16.psf.gz",
    "/usr/share/consolefonts/lat1-16.psfu.gz",
    "/usr/share/kbd/consolefonts/lat1-16.psf",
    "/usr/share/kbd/consolefonts/lat1-16.psf.gz",
    "/usr/share/kbd/consolefonts/lat1-16.psfu.gz",
    "/usr/lib/kbd/consolefonts/lat1-16.psf",
    "/usr/lib/kbd/consolefonts/lat1-16.psf.gz",
    "/usr/lib/kbd/consolefonts/lat1-16.psfu.gz",
    "/lib/kbd/consolefonts/lat1-16.psf",
    "/lib/kbd/consolefonts/lat1-16.psf.gz",
    "/lib/kbd/consolefonts/lat1-16.psfu.gz",
    NULL
};

struct fs_font* fs_consolefont(char **filename)
{
    int  i;
    char *h,command[256];
    struct fs_font *f = NULL;
    FILE *fp;

    if (NULL == filename)
	filename = default_font;

    for(i = 0; filename[i] != NULL; i++) {
	if (-1 == access(filename[i],R_OK))
	    continue;
	break;
    }
    if (NULL == filename[i]) {
	fprintf(stderr,"can't find console font file\n");
	return NULL;
    }

    h = filename[i]+strlen(filename[i])-3;
    if (0 == strcmp(h,".gz")) {
	sprintf(command,"zcat %s",filename[i]);
	fp = popen(command,"r");
    } else {
	fp = fopen(filename[i], "r");
    }
    if (NULL == fp) {
	fprintf(stderr,"can't open %s: %s\n",filename[i],strerror(errno));
	return NULL;
    }

    if (fgetc(fp) != 0x36 ||
	fgetc(fp) != 0x04) {
	fprintf(stderr,"can't use font %s\n",filename[i]);
	return NULL;
    }
    fprintf(stderr,"using linux console font \"%s\"\n",filename[i]);

    f = malloc(sizeof(*f));
    memset(f,0,sizeof(*f));

    fgetc(fp);
    f->maxenc = 256;
    f->width  = 8;
    f->height = fgetc(fp);
    f->fontHeader.min_bounds.left    = 0;
    f->fontHeader.max_bounds.right   = f->width;
    f->fontHeader.max_bounds.descent = 0;
    f->fontHeader.max_bounds.ascent  = f->height;

    f->glyphs  = malloc(f->height * 256);
    f->extents = malloc(sizeof(FSXCharInfo)*256);
    fread(f->glyphs, 256, f->height, fp);
    fclose(fp);

    f->eindex  = malloc(sizeof(FSXCharInfo*)   * 256);
    f->gindex  = malloc(sizeof(unsigned char*) * 256);
    for (i = 0; i < 256; i++) {
	f->eindex[i] = f->extents +i;
	f->gindex[i] = f->glyphs  +i * f->height;
	f->eindex[i]->left    = 0;
	f->eindex[i]->right   = 7;
	f->eindex[i]->width   = 8;
	f->eindex[i]->descent = 0;
	f->eindex[i]->ascent  = f->height;
    }
    return f;
}


