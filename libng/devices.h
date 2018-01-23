
struct ng_device_config {
    char *video;
    char *driver;
    char *radio;
    char *vbi;
    char *dsp;
    char *mixer;
    char *video_scan[32];
    char *mixer_scan[32];
};
extern struct ng_device_config ng_dev;

void ng_device_init(void);

