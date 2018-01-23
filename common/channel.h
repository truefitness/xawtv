#ifndef X_DISPLAY_MISSING
# include <X11/Xlib.h>
# include <X11/Intrinsic.h>
#endif

#define CAPTURE_OFF          0
#define CAPTURE_OVERLAY      1
#define CAPTURE_GRABDISPLAY  2
#define CAPTURE_ON           9

struct CHANNEL {
    char  *name;
    char  *key;
    char  *group;
    int   midi;

    char  *cname;     /* name of the channel  */
    int   channel;    /* index into tvtuner[] */
    int   fine;
    int   freq;
    int   audio;

    int   capture;
    int   input;
    int   norm;

    int   color;
    int   bright;
    int   hue;
    int   contrast;

#ifndef X_DISPLAY_MISSING
    /* FIXME */
    Pixmap  pixmap;
    Widget  button;
#endif
};

extern struct CHANNEL  defaults;
extern struct CHANNEL  **channels;
extern int             count;

extern int have_config;
extern int jpeg_quality;
extern int keypad_ntsc;
extern int keypad_partial;
extern int use_osd;
extern int osd_x, osd_y;
extern int use_wm_fullscreen;
extern int fs_width,fs_height,fs_xoff,fs_yoff;
extern int pix_width,pix_height,pix_cols;
extern int last_sender, cur_sender;
extern int cur_channel, cur_fine;
extern int cur_capture, cur_freq;
extern struct ng_filter *cur_filter;

extern char *mov_driver;
extern char *mov_video;
extern char *mov_fps;
extern char *mov_audio;
extern char *mov_rate;

extern char mixerdev[32],mixerctl[16];
extern char *midi;

int  lookup_channel(char *channel);
int  get_freq(int i);
int  cf2freq(char *name, int fine);

struct CHANNEL* add_channel(char *name);
void hotkey_channel(struct CHANNEL *channel);
void configure_channel(struct CHANNEL *channel);
void del_channel(int nr);
void calc_frequencies(void);

void read_config(char *conffile, int *argc, char **argv);
void parse_config(int parse_channels);
void save_config(void);

/* ----------------------------------------------------------------------- */

struct LAUNCH {
    char *name;
    char *key;
    char *cmdline;
};

extern struct LAUNCH *launch;
extern int nlaunch;

/* ----------------------------------------------------------------------- */

extern struct STRTAB booltab[];
extern struct STRTAB captab[];

int str_to_int(char *str, struct STRTAB *tab);
const char* int_to_str(int n, struct STRTAB *tab);
