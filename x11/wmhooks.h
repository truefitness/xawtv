void wm_detect(Display *dpy);
void (*wm_stay_on_top)(Display *dpy, Window win, int state);
void (*wm_fullscreen)(Display *dpy, Window win, int state);
