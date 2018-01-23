extern int  gfx_init(int fd);
extern void (*gfx_scaler_on)(int offscreen, int pitch, int width, int height,
		      int left, int right, int top, int bottom);
extern void (*gfx_scaler_off)(void);
