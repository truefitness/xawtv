extern int have_xv;
void xv_video_init(unsigned int port, int hwscan);

#ifdef HAVE_LIBXV
void xv_video(Window win, int width, int height, int on);
#endif
