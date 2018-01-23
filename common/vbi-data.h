#ifndef _VBI_DATA_H
#define _VBI_DATA_H 1

#ifdef HAVE_ZVBI
#include <libzvbi.h>

#define VBI_MAX_SUBPAGES 64

struct vbi_state {
    vbi_decoder             *dec;
    vbi_capture             *cap;
    vbi_raw_decoder         *par;
    vbi_sliced              *sliced;
    uint8_t                 *raw;
    char                    *err;
    int                     lines,fd,sim,debug;
    double                  ts;
    struct timeval          tv;
};

struct vbi_rect {
    int x1,x2,y1,y2;
};

enum vbi_txt_colors {
    VBI_NOCOLOR   = 0,
    VBI_ANSICOLOR = 1,
};

extern char *vbi_colors[8];
extern struct vbi_rect vbi_fullrect;

struct vbi_state* vbi_open(char *dev, int debug, int sim);
int vbi_hasdata(struct vbi_state *vbi);
void vbi_close(struct vbi_state *vbi);
void vbi_dump_event(struct vbi_event *ev, void *user);
int vbi_to_utf8(struct vbi_char *src, unsigned char *dest, int n);
int vbi_calc_page(int pagenr, int offset);
int vbi_calc_subpage(struct vbi_decoder *dec, int pgno, int subno, int offset);
int vbi_export_txt(char *dest, char *charset, int size,
		   struct vbi_page *pg, struct vbi_rect *rect,
		   enum vbi_txt_colors);
void vbi_find_subtitle(struct vbi_page *pg, struct vbi_rect *rect);

#endif /* HAVE_ZVBI */
#endif /* _VBI_DATA_H */
