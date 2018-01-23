#include "vbi-data.h"

#define TT 0
#if TT
#define VTX_COUNT 256
#define VTX_LEN   64

struct TEXTELEM {
    char  str[VTX_LEN];
    char  *fg;
    char  *bg;
    int   len;
    int   line;
    int   x,y;
};
#endif

/*------------------------------------------------------------------------*/

/* feedback for the user */
extern void (*update_title)(char *message);
extern void (*display_message)(char *message);
extern void (*rec_status)(char *message);
#if TT
extern void (*vtx_message)(struct TEXTELEM *tt);
#endif
#ifdef HAVE_ZVBI
extern void (*vtx_subtitle)(struct vbi_page *pg, struct vbi_rect *rect);
#endif

/* for updating GUI elements / whatever */
extern void (*attr_notify)(struct ng_attribute *attr, int val);
extern void (*mute_notify)(int val);
extern void (*volume_notify)(void);
extern void (*freqtab_notify)(void);
extern void (*setfreqtab_notify)(void);
extern void (*setstation_notify)(void);

/* gets called _before_ channel switches */
extern void (*channel_switch_hook)(void);

/* capture overlay/grab/off */
extern void (*set_capture_hook)(int old, int new, int tmp_switch);

/* toggle fullscreen */
extern void (*fullscreen_hook)(void);
extern void (*exit_hook)(void);
extern void (*capture_get_hook)(void);
extern void (*capture_rel_hook)(void);
extern void (*movie_hook)(int argc, char **argv);

extern int debug;
extern int do_overlay;
extern char *snapbase;
extern int have_shmem;
extern unsigned int cur_tv_width,cur_tv_height;
extern int cur_movie,cur_attrs[256];
extern struct movie_parm m_parm;

extern const struct ng_vid_driver *drv;
extern void                       *h_drv;
extern int                         f_drv;

extern struct ng_attribute        *attrs;

/*------------------------------------------------------------------------*/

void attr_init(void);
void audio_init(void);
void audio_on(void);
void audio_off(void);
void set_defaults(void);

void add_attrs(struct ng_attribute *new);
void init_overlay(void);

int do_va_cmd(int argc, ...);
int do_command(int argc, char **argv);
char** split_cmdline(char *line, int *count);
void keypad_timeout(void);
