extern char *webcam;
int webcam_put(char *filename, struct ng_video_buf *buf);
void webcam_init(void);
void webcam_exit(void);
