#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <iconv.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libzvbi.h>

#include "httpd.h"
#include "vbi-data.h"

/* ---------------------------------------------------------------------- */

static char stylesheet[] =
#include "alevt.css.h"
;

static char page_about[] =
#include "about.html.h"
;

static wchar_t page_top[] = L""
#include "top.html.h"
;

static wchar_t page_bottom[] = L""
#include "bottom.html.h"
;

#if 0
/*                          01234567890123456789012345678901 */
static char graphic1[32] = " °°¯·'²²·°°°-°°°.:::.;;;.}/}.÷/#";
/*                          012345 6789 0123 456789012345678901 */
static char graphic2[32] = ".:::.\\{{.\\||.\\÷#_:::.¿¿[.||].###";
#endif

enum mime_type {
    TYPE_TEXT = 1,
    TYPE_HTML = 2,
};

#define CHARSET "utf-8"
#define TXTBUF  (25*41*8)
#define HTMLBUF (25*1024)

/* ---------------------------------------------------------------------- */

static int vbi_export_html(wchar_t *dest, int size,
			   struct vbi_state *vbi, struct vbi_page *pg,
			   int pagenr, int subnr)
{
    int x,y,i,link;
    int fg,bg,len=0;
    vbi_char *ch;
    wchar_t wch,*obuf;
    struct vbi_page dummy;

    obuf = dest;
    link = -1;
    len  = swprintf(obuf,size,page_top,pagenr,subnr);
    obuf += len; size -= len;
    for (y = 0; y < 25; y++) {
	ch = pg->text + 41*y;
	fg = -1;
	bg = -1;
	for (x = 0; x <= 40; x++) {
	    /* close link tags */
	    if (link >= 0) {
		if (0 == link) {
		    len = swprintf(obuf,size,L"</a>");
		    obuf += len; size -= len;
		}
		link--;
	    }

	    /* this char */
	    wch = ch[x].unicode;
	    if (ch[x].size > VBI_DOUBLE_SIZE)
		wch = ' ';
	    if (ch[x].conceal)
		wch = ' ';

	    /* handle colors */
	    if (fg != ch[x].foreground || bg != ch[x].background) {
		if (-1 != fg) {
		    len = swprintf(obuf,size,L"</span>");
		    obuf += len; size -= len;
		}
		fg  = ch[x].foreground;
		bg  = ch[x].background;
		len = swprintf(obuf,size,L"<span class=\"c%02d\">",
			       fg * 10 + bg);
		obuf += len;
		size -= len;
	    }

	    /* check for references to other pages */
	    if (y > 0 && -1 == link && x > 0 && x < 39 &&
		iswdigit(ch[x+0].unicode)  &&
		iswdigit(ch[x+1].unicode)  &&
		iswdigit(ch[x+2].unicode)  &&
		!iswdigit(ch[x-1].unicode) &&
		!iswdigit(ch[x+3].unicode)) {
		len = swprintf(obuf,size,L"<a href=\"/%lc%lc%lc/00.html\">",
			       (wchar_t)ch[x+0].unicode,
			       (wchar_t)ch[x+1].unicode,
			       (wchar_t)ch[x+2].unicode);
		obuf += len; size -= len;
		link = 2;
	    }
	    if (y > 0 && -1 == link && x > 0 && x < 40 &&
		'>' == ch[x+0].unicode &&
		'>' == ch[x+1].unicode) {
		len = swprintf(obuf,size,L"<a href=\"/%03x/00.html\">",
			       vbi_calc_page(pagenr, +0x01));
		link = 1;
	    }
	    if (y > 0 && -1 == link && x > 0 && x < 40 &&
		'<' == ch[x+0].unicode &&
		'<' == ch[x+1].unicode) {
		len = swprintf(obuf,size,L"<a href=\"/%03x/00.html\">",
			       vbi_calc_page(pagenr, -0x01));
		link = 1;
	    }

	    /* check for references to other subpages */
	    if (subnr > 0 && y > 0 && -1 == link && x > 0 && x < 39 &&
		iswdigit(ch[x+0].unicode)  &&
		'/' == ch[x+1].unicode     &&
		iswdigit(ch[x+2].unicode)  &&
		!iswdigit(ch[x-1].unicode) &&
		!iswdigit(ch[x+3].unicode)) {
		if (ch[x+0].unicode == ch[x+2].unicode) {
		    len = swprintf(obuf,size,L"<a href=\"01.html\">");
		} else {
		    len = swprintf(obuf,size,L"<a href=\"%02x.html\">",
				   ch[x+0].unicode-'0' +1);
		}
		len = swprintf(obuf,size,L"<a href=\"/%lc%lc%lc/00.html\">",
			       (wchar_t)ch[x+0].unicode,
			       (wchar_t)ch[x+1].unicode,
			       (wchar_t)ch[x+2].unicode);
		obuf += len; size -= len;
		link = 2;
	    }

#if 0
	    /* check for references to WWW pages */
	    if (html && y > 0 && -1 == link && x < W-9 &&
		(((tolower(L[x+0].ch) == 'w') &&
		  (tolower(L[x+1].ch) == 'w') &&
		  (tolower(L[x+2].ch) == 'w') &&
		  (L[x+3].ch == '.')) ||
		 ((tolower(L[x+0].ch) == 'h') &&
		  (tolower(L[x+1].ch) == 't') &&
		  (tolower(L[x+2].ch) == 't') &&
		  (tolower(L[x+3].ch) == 'p')))) {
		int offs = 0;

		len += sprintf(out+len,"<a href=\"");
		if(tolower(L[x].ch == 'w'))
		    len += sprintf(out+len,"http://");
		while ((L[x+offs].ch!=' ') && (x+offs < W)) {
		    len += sprintf(out+len,"%c",tolower(L[x+offs].ch));
		    offs++;
		}
		while ( (*(out+len-1)<'a') || (*(out+len-1)>'z') ) {
		    len--;
		    offs--;
		}
		len += sprintf(out+len,"\">");
		link = offs - 1;
	    }
#endif
	    /* FIXME: fasttext links */

	    if (size > 0) {
		*obuf = wch;
		obuf++;
		size--;
	    }
	}
	if (-1 != fg) {
	    len   = swprintf(obuf,size,L"</span>");
	    obuf += len;
	    size -= len;
	}
	if (size > 0) {
	    *obuf = '\n';
	    obuf++;
	    size--;
	}
    }
    len = swprintf(obuf,size,L"</pre>\n<div class=quick>\n");
    obuf += len; size -= len;

    if (0 != subnr) {
	/* link all subpages */
	for (i = 1; i <= VBI_MAX_SUBPAGES; i++) {
	    if (!vbi_fetch_vt_page(vbi->dec,&dummy,pagenr,i,
				   VBI_WST_LEVEL_1p5,0,0))
		continue;
	    if (i != subnr) {
		len = swprintf(obuf,size,L" <a href=\"/%03x/%02x.html\">%02x</a>",
			       pagenr, i, i);
	    } else {
		len = swprintf(obuf,size,L" %02x", i);
	    }
	    obuf += len; size -= len;
	    len = swprintf(obuf,size,L"<br>\n");
	    obuf += len; size -= len;
	}
    }
    /* page navigation links */
    len = swprintf(obuf,size,L"<a href=\"/%03x/00.html\">&lt;&lt;</a> &nbsp;",
		   vbi_calc_page(pagenr, -0x10));
    obuf += len; size -= len;
    len = swprintf(obuf,size,L"<a href=\"/%03x/00.html\">&lt;</a> &nbsp;",
		   vbi_calc_page(pagenr, -0x01));
    obuf += len; size -= len;
    len = swprintf(obuf,size,L"<a href=\"/%03x/%02x.html\">o</a> &nbsp;",
		   pagenr, subnr);
    obuf += len; size -= len;
    len = swprintf(obuf,size,L"<a href=\"/%03x/00.html\">&gt;</a> &nbsp;",
		   vbi_calc_page(pagenr, +0x01));
    obuf += len; size -= len;
    len = swprintf(obuf,size,L"<a href=\"/%03x/00.html\">&gt;&gt;</a>",
		   vbi_calc_page(pagenr, +0x10));
    obuf += len; size -= len;
    len = swprintf(obuf,size,L"<br>\n") ;
    obuf += len; size -= len;
    len = swprintf(obuf,size,page_bottom);
    obuf += len; size -= len;
    *obuf = 0;
    return obuf - dest;
}

static int wchar_to_charset(wchar_t *istr, int ilen, char *charset,
			     char **ostr, int *olen)
{
    size_t   il,ol;
    wchar_t  *err;
    char     *ib,*ob;
    iconv_t  ic;
    int      rc;

    ic = iconv_open(CHARSET,"WCHAR_T");
    if (NULL == ic) {
	if (debug)
	    fprintf(stderr,"iconf_open failed on %s => %s\n",
		    CHARSET,"WCHAR_T");
	return -1;
    }
    ib  = (char*)istr;
    il  = ilen * sizeof(wchar_t);
    ob  = malloc(ilen * 8);
    ol  = ilen * 8;
    *ostr = ob;

    for (;;) {
	rc = iconv(ic,&ib,&il,&ob,&ol);
	if (rc >= 0) {
	    /* done */
	    break;
	}
	/* handle errors */
	err = (wchar_t*)ib;
	if (EILSEQ == errno && *err != '?') {
	    *err = '?';
	    continue;
	}
	/* unknown error */
	if (debug)
	    fprintf(stderr,"unknown iconv error\n");
	return -1;
    }
    iconv_close(ic);
    *olen = ob - *ostr;
    return 0;
}

static int vbi_render_page(struct REQUEST *req,
			   struct vbi_state *vbi, struct vbi_page *pg,
			   int pagenr, int subnr, enum mime_type type)
{
    wchar_t  *wpage;
    int      wlen;

    switch (type) {
    case TYPE_TEXT:
	req->body  = malloc(TXTBUF);
	req->lbody = vbi_export_txt(req->body, CHARSET, TXTBUF, pg,
				    &vbi_fullrect, 0);
	req->mime  = "text/plain; charset=" CHARSET;
	break;
    case TYPE_HTML:
	wpage = malloc(HTMLBUF*sizeof(wchar_t));
	wlen  = vbi_export_html(wpage,HTMLBUF,vbi,pg,pagenr,subnr);
	if (-1 == wchar_to_charset(wpage,wlen,CHARSET,&req->body,&req->lbody))
	    return -1;
	free(wpage);
	req->mime  = "text/html; charset=" CHARSET;
	break;
    }

    req->free_the_mallocs = 1;
    mkheader(req,200,-1);
    return 0;
}

/* ---------------------------------------------------------------------- */

void buildpage(struct REQUEST *req)
{
    int pagenr, subpage;
    char type[10];
    struct vbi_page page;
    enum mime_type t;

    /* style sheet */
    if (0 == strcmp(req->path,"/alevt.css")) {
	req->mime  = "text/css; charset=us-ascii";
	req->body  = stylesheet;
	req->lbody = sizeof(stylesheet);
	mkheader(req,200,start);
	return;
    }

    /* about */
    if (0 == strcmp(req->path,"/about.html")) {
	req->mime  = "text/html; charset=utf-8";
	req->body  = page_about;
	req->lbody = sizeof(page_about);
	mkheader(req,200,start);
	return;
    }

    /* entry page */
    if (0 == strcmp(req->path,"/")) {
	strcpy(req->path,"/100/00.html");
	mkredirect(req);
	return;
    }

    /* pages */
    if (3 == sscanf(req->path,"/%3x/%2x.%8s",&pagenr,&subpage,type)) {
	t = 0;
	if (0 == strcmp(type,"html"))
	    t = TYPE_HTML;
	if (0 == strcmp(type,"txt"))
	    t = TYPE_TEXT;
	if (0 == t) {
	    mkerror(req,404,1);
	    return;
	}
	if (debug)
	    fprintf(stderr,"%03d: lookup %03x/%02x [%s]\n",
		    req->fd,pagenr,subpage,type);
	memset(&page,0,sizeof(page));
	if (!vbi_fetch_vt_page(vbi->dec,&page,pagenr,subpage,
			       VBI_WST_LEVEL_1p5,25,1)) {
	    if (!vbi_fetch_vt_page(vbi->dec,&page,pagenr,VBI_ANY_SUBNO,
				   VBI_WST_LEVEL_1p5,25,1)) {
		mkerror(req,404,1);
		return;
	    }
	    sprintf(req->path,"/%03x/%02x.html",pagenr,page.subno);
	    mkredirect(req);
	    return;
	}
	if (-1 == vbi_render_page(req,vbi,&page,pagenr,subpage,t))
	    mkerror(req,500,1);
	return;
    }

    /* goto form */
    if (1 == sscanf(req->path,"/goto/?p=%d",&pagenr)) {
	sprintf(req->path,"/%d/00.html",pagenr);
	mkredirect(req);
	return;
    }

    mkerror(req,404,1);
    return;
}
