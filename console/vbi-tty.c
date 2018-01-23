/*
 * vbi-tty  --  terminal videotext browser
 *
 *   (c) 2002 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>
#include <locale.h>
#include <langinfo.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <linux/fb.h>

#include "vbi-data.h"
#include "vbi-tty.h"
#include "fbtools.h"

/* --------------------------------------------------------------------- */

struct termios  saved_attributes;
int             saved_fl;

static void tty_raw(void)
{
    struct termios tattr;

    fcntl(0,F_GETFL,&saved_fl);
    tcgetattr (0, &saved_attributes);

    fcntl(0,F_SETFL,O_NONBLOCK);
    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ICANON|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr (0, TCSAFLUSH, &tattr);
}

static void tty_restore(void)
{
    fcntl(0,F_SETFL,saved_fl);
    tcsetattr (0, TCSANOW, &saved_attributes);
}

/* FIXME: Yes, I know, hardcoding ANSI sequences is bad.  ncurses
 * can't handle multibyte locales like UTF-8 not yet, that's why that
 * dirty hack for now ... */
static void tty_clear(void)
{
    fprintf(stderr,"\033[H\033[2J");
}

static void tty_goto(int x, int y)
{
    fprintf(stderr,"\033[%d;%dH",y,x);
}

/* --------------------------------------------------------------------- */

static int have_fb = 0;
static int fb_fmt  = VBI_PIXFMT_RGBA32_LE;
static int switch_last;

static void fb_clear(void)
{
    fb_memset(fb_mem+fb_mem_offset,0,fb_fix.smem_len);
}

/* --------------------------------------------------------------------- */

struct vbi_tty {
    struct vbi_state  *vbi;
    struct vbi_page   pg;
    int               pgno,subno;
    int               newpage;
};

static void
vbi_fix_head(struct vbi_tty *tty, struct vbi_char *ch)
{
    int showno,showsub,red,i;

    showno  = tty->pg.pgno;
    showsub = tty->pg.subno;
    red     = 0;
    if (0 == showno) {
	showno  = tty->pgno;
	showsub = 0;
	red     = 1;
    }
    if (tty->newpage) {
	showno  = tty->newpage;
	showsub = 0;
	red     = 1;
    }

    for (i = 1; i <= 6; i++)
	ch[i].unicode = ' ';
    if (showno >= 0x100)
	ch[1].unicode = '0' + ((showno >> 8) & 0xf);
    if (showno >= 0x10)
	ch[2].unicode = '0' + ((showno >> 4) & 0xf);
    if (showno >= 0x1)
	ch[3].unicode = '0' + ((showno >> 0) & 0xf);
    if (showsub) {
	ch[4].unicode = '/';
	ch[5].unicode = '0' + ((showsub >> 4) & 0xf);
	ch[6].unicode = '0' + ((showsub >> 0) & 0xf);
    }
    if (red) {
	ch[1].foreground = VBI_RED;
	ch[2].foreground = VBI_RED;
	ch[3].foreground = VBI_RED;
    }
}

static void
vbi_render_page(struct vbi_tty *tty)
{
    char *data;
    int len;

    data = malloc(25*41*24);
    vbi_fetch_vt_page(tty->vbi->dec,&tty->pg,tty->pgno,tty->subno,
		      VBI_WST_LEVEL_1p5,25,1);
    vbi_fix_head(tty,tty->pg.text);
    if (have_fb) {
	vbi_draw_vt_page_region(&tty->pg, fb_fmt,
				fb_mem + fb_mem_offset,
				fb_fix.line_length,
				0,0,
				tty->pg.columns, tty->pg.rows,
				0,1);

    } else {
	len = vbi_export_txt(data,nl_langinfo(CODESET),25*41*8,
			     &tty->pg,&vbi_fullrect,VBI_ANSICOLOR);
	tty_goto(0,0);
	fwrite(data,len,1,stderr);
	tty_goto(42,0);
	free(data);
    }
}

static void
vbi_render_head(struct vbi_tty *tty, int pgno, int subno)
{
    static struct vbi_rect head = {
	x1:  0,  y1:  0,  x2: 41,  y2:  1,
    };
    struct vbi_page pg;
    char *data;
    int len;

    data = malloc(41*24);
    memset(&pg,0,sizeof(pg));
    vbi_fetch_vt_page(tty->vbi->dec,&pg,pgno,subno,
		      VBI_WST_LEVEL_1p5,1,1);
    vbi_fix_head(tty,pg.text);
    if (have_fb) {
	vbi_draw_vt_page_region(&pg, fb_fmt,
				fb_mem + fb_mem_offset,
				fb_fix.line_length,
				0,0,
				pg.columns, 1,
				0,1);
    } else {
	len = vbi_export_txt(data,nl_langinfo(CODESET),41*8,
			     &pg,&head,VBI_ANSICOLOR);
	tty_goto(0,0);
	fwrite(data,len,1,stderr);
	tty_goto(42,0);
	free(data);
    }
}

static void
vbi_newdata(struct vbi_event *ev, void *user)
{
    struct vbi_tty *tty = user;

    switch (ev->type) {
    case VBI_EVENT_TTX_PAGE:
	if (tty->pgno  == ev->ev.ttx_page.pgno &&
	    (tty->subno == ev->ev.ttx_page.subno ||
	     tty->subno == VBI_ANY_SUBNO)) {
	    vbi_render_page(tty);
	} else {
	    vbi_render_head(tty,
			    ev->ev.ttx_page.pgno,
			    ev->ev.ttx_page.subno);
	}
	break;
    }
}

static void
vbi_setpage(struct vbi_tty *tty, int pgno, int subno)
{
    tty->pgno = pgno;
    tty->subno = subno;
    tty->newpage = 0;
    memset(&tty->pg,0,sizeof(struct vbi_page));
    vbi_fetch_vt_page(tty->vbi->dec,&tty->pg,tty->pgno,tty->subno,
		      VBI_WST_LEVEL_1p5,25,1);
    vbi_render_page(tty);
}

/* --------------------------------------------------------------------- */

void vbi_tty(char *device, int debug, int sim)
{
    struct vbi_state  *vbi;
    struct vbi_tty    *tty;
    fd_set            set;
    struct winsize    win;
    struct timeval    tv;
    char              key[11];
    int               rc,subno,last;

    setlocale(LC_ALL,"");
    vbi = vbi_open(device,debug,sim);
    if (NULL == vbi)
	exit(1);

    if (0 /* 0 == fb_probe() */ ) {
	have_fb = 1;
	fb_init(NULL,NULL,0);
	fb_catch_exit_signals();
	fb_switch_init();
	switch_last = fb_switch_state;
    } else {
	if (-1 != ioctl(0,TIOCGWINSZ,&win) && win.ws_row < 26) {
	    fprintf(stderr,"Terminal too small (need 26 rows, have %d)\n",
		    win.ws_row);
	    exit(1);
	}
    }
    tty_raw();
    have_fb ? fb_clear() : tty_clear();

    tty = malloc(sizeof(*tty));
    memset(tty,0,sizeof(*tty));
    tty->vbi = vbi;
    vbi_event_handler_add(vbi->dec,~0,vbi_newdata,tty);
    vbi_setpage(tty,0x100,VBI_ANY_SUBNO);

    for (last = 0; !last;) {
	FD_ZERO(&set);
	FD_SET(0,&set);
	FD_SET(vbi->fd,&set);
	tv.tv_sec  = 1;
	tv.tv_usec = 0;
	rc = select(vbi->fd+1,&set,NULL,NULL,&tv);
	if (-1 == rc) {
	    tty_restore();
	    if (have_fb)
		fb_cleanup();
	    perror("select");
	    exit(1);
	}
	if (0 == rc) {
	    if (have_fb)
		fb_cleanup();
	    tty_restore();
	    fprintf(stderr,"oops: timeout\n");
	    exit(1);
	}
	if (FD_ISSET(0,&set)) {
	    /* tty input */
	    rc = read(0, key, 10);
	    key[rc] = 0;
	    if (1 == rc) {
		switch (key[0]) {
		case 'q':
		case 'Q':
		    /* quit */
		    last = 1;
		    break;
		case 'L' & 0x1f:
		    /* refresh */
		    have_fb ? fb_clear() : tty_clear();
		    vbi_render_page(tty);
		    break;
		case 'i':
		case 'I':
		    /* index */
		    vbi_setpage(tty,0x100,VBI_ANY_SUBNO);
		    break;
		case ' ':
		case 'l':
		case 'L':
		    /* next page */
		    vbi_setpage(tty,vbi_calc_page(tty->pgno,+1),VBI_ANY_SUBNO);
		    break;
		case '\x7f':
		case 'h':
		case 'H':
		    /* prev page */
		    vbi_setpage(tty,vbi_calc_page(tty->pgno,-1),VBI_ANY_SUBNO);
		    break;
		case 'k':
		case 'K':
		    /* next subpage */
		    subno = (tty->subno != VBI_ANY_SUBNO) ?
			tty->subno : tty->pg.subno;
		    subno = vbi_calc_subpage(tty->vbi->dec,tty->pgno,subno,+1);
		    vbi_setpage(tty,tty->pgno,subno);
		    break;
		case 'j':
		case 'J':
		    /* prev subpage */
		    subno = (tty->subno != VBI_ANY_SUBNO) ?
			tty->subno : tty->pg.subno;
		    subno = vbi_calc_subpage(tty->vbi->dec,tty->pgno,subno,-1);
		    vbi_setpage(tty,tty->pgno,subno);
		    break;
		default:
		    if (key[0] >= '0'  &&  key[0] <= '9') {
			tty->newpage *= 16;
			tty->newpage += key[0] - '0';
			if (tty->newpage >= 0x100)
			    vbi_setpage(tty,tty->newpage,VBI_ANY_SUBNO);
		    }
		}
	    }
	}
	if (FD_ISSET(vbi->fd,&set)) {
	    vbi_hasdata(vbi);
	}
    }
    if (have_fb)
	fb_cleanup();
    tty_goto(0,0);
    tty_restore();
}
