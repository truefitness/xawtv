/*
 * (c) 1998-2002 Gerd Knorr
 *
 *    capture a image, compress as jpeg and upload to the webserver
 *    using the ftp utility or ssh
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <math.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "grab-ng.h"
#include "jpeglib.h"
#include "ftp.h"
#include "parseconfig.h"
#include "list.h"


/* ---------------------------------------------------------------------- */
/* configuration                                                          */

int   daemonize = 0;
char  *archive  = NULL;
char  *tmpdir;
struct list_head connections;

char  *grab_text   = "webcam %Y-%m-%d %H:%M:%S"; /* strftime */
char  *grab_infofile = NULL;
int   grab_width  = 320;
int   grab_height = 240;
int   grab_delay  = 3;
int   grab_wait   = 0;
int   grab_rotate = 0;
int   grab_top    = -1;
int   grab_left   = -1;
int   grab_bottom = -1;
int   grab_right  = -1;
int   grab_quality= 75;
int   grab_trigger= 0;
int   grab_times  = -1;
int   grab_fg_r   = 255;
int   grab_fg_g   = 255;
int   grab_fg_b   = 255;
int   grab_bg_r   = -1;
int   grab_bg_g   = -1;
int   grab_bg_b   = -1;
char  *grab_input = NULL;
char  *grab_norm  = NULL;

/* ---------------------------------------------------------------------- */
/* jpeg stuff                                                             */

static int
write_file(int fd, unsigned char *data, int width, int height)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    int i;
    unsigned char *line;

    fp = fdopen(fd,"w");
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, grab_quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    for (i = 0, line = data; i < height; i++, line += width*3)
	jpeg_write_scanlines(&cinfo, &line, 1);

    jpeg_finish_compress(&(cinfo));
    jpeg_destroy_compress(&(cinfo));
    fclose(fp);

    return 0;
}

/* ---------------------------------------------------------------------- */
/* image transfer                                                         */

struct xfer_ops;

struct xfer_state {
    char              *name;
    struct list_head  list;

    /* xfer options */
    char              *host;
    char              *user;
    char              *pass;
    char              *dir;
    char              *file;
    char              *tmpfile;
    int               debug;

    /* ftp options */
    int               passive,autologin;

    /* function pointers + private date */
    struct xfer_ops   *ops;
    void              *data;
};

struct xfer_ops {
    int  (*open)(struct xfer_state*);
    void (*info)(struct xfer_state*);
    int  (*xfer)(struct xfer_state*, char *image, int width, int height);
    void (*close)(struct xfer_state*);
};

static int ftp_open(struct xfer_state *s)
{
    s->data = ftp_init(s->name,s->autologin,s->passive,s->debug);
    ftp_connect(s->data,s->host,s->user,s->pass,s->dir);
    return 0;
}

static void ftp_info(struct xfer_state *s)
{
    fprintf(stderr,"ftp config [%s]:\n  %s@%s:%s\n  %s => %s\n",
	    s->name,s->user,s->host,s->dir,s->tmpfile,s->file);
}

static int ftp_xfer(struct xfer_state *s, char *image, int width, int height)
{
    char filename[1024];
    int fh;

    sprintf(filename,"%s/webcamXXXXXX",tmpdir);
    if (-1 == (fh = mkstemp(filename))) {
	perror("mkstemp");
	exit(1);
    }
    write_file(fh, image, width, height);
    if (ftp_connected(s->data))
	ftp_connect(s->data,s->host,s->user,s->pass,s->dir);
    ftp_upload(s->data,filename,s->file,s->tmpfile);
    unlink(filename);
    return 0;
}

static void ftp_close(struct xfer_state *s)
{
    ftp_fini(s->data);
}

static struct xfer_ops ftp_ops = {
    open:  ftp_open,
    info:  ftp_info,
    xfer:  ftp_xfer,
    close: ftp_close,
};

static int ssh_open(struct xfer_state *s)
{
    s->data = malloc(strlen(s->user)+strlen(s->host)+strlen(s->tmpfile)*2+
		     strlen(s->dir)+strlen(s->file)+32);
    sprintf(s->data, "ssh %s@%s \"cat >%s && mv %s %s/%s\"",
	    s->user,s->host,s->tmpfile,s->tmpfile,s->dir,s->file);
    return 0;
}

static void ssh_info(struct xfer_state *s)
{
    fprintf(stderr,"ssh config [%s]:\n  %s@%s:%s\n  %s => %s\n",
	    s->name,s->user,s->host,s->dir,s->tmpfile,s->file);
}

static int ssh_xfer(struct xfer_state *s, char *image, int width, int height)
{
    char filename[1024];
    char *cmd = s->data;
    unsigned char buf[4096];
    FILE *sshp, *imgdata;
    int len,fh;

    sprintf(filename,"%s/webcamXXXXXX",tmpdir);
    if (-1 == (fh = mkstemp(filename))) {
	perror("mkstemp");
	exit(1);
    }
    write_file(fh, image, width, height);

    if ((sshp=popen(cmd, "w")) == NULL) {
	perror("popen");
	exit(1);
    }
    if ((imgdata = fopen(filename,"rb"))==NULL) {
	perror("fopen");
	exit(1);
    }
    for (;;) {
	len = fread(buf,1,sizeof(buf),imgdata);
	if (len <= 0)
	    break;
	fwrite(buf,1,len,sshp);
    }
    fclose(imgdata);
    pclose(sshp);

    unlink(filename);
    return 0;
}

static void ssh_close(struct xfer_state *s)
{
    char *cmd = s->data;
    free(cmd);
}

static struct xfer_ops ssh_ops = {
    open:  ssh_open,
    info:  ssh_info,
    xfer:  ssh_xfer,
    close: ssh_close,
};

static int local_open(struct xfer_state *s)
{
    char *t;

    if (s->dir != NULL && s->dir[0] != '\0' ) {
	t = malloc(strlen(s->tmpfile)+strlen(s->dir)+2);
	sprintf(t, "%s/%s", s->dir, s->tmpfile);
	s->tmpfile = t;

	t = malloc(strlen(s->file)+strlen(s->dir)+2);
	sprintf(t, "%s/%s", s->dir, s->file);
	s->file = t;
    }
    return 0;
}

static void local_info(struct xfer_state *s)
{
    fprintf(stderr,"write config [%s]:\n  local transfer %s => %s\n",
	    s->name,s->tmpfile,s->file);
}

static int local_xfer(struct xfer_state *s, char *image, int width, int height)
{
    int fh;

    if (-1 == (fh = open(s->tmpfile,O_CREAT|O_WRONLY|O_TRUNC,0666))) {
	fprintf(stderr,"open %s: %s\n",s->tmpfile,strerror(errno));
	exit(1);
    }
    write_file(fh, image, width, height);
    if (rename(s->tmpfile, s->file) ) {
	fprintf(stderr, "can't move %s -> %s\n", s->tmpfile, s->file);
	exit(1);
    }
    return 0;
}

static void local_close(struct xfer_state *s)
{
    /* nothing */
}

static struct xfer_ops local_ops = {
    open:  local_open,
    info:  local_info,
    xfer:  local_xfer,
    close: local_close,
};

/* ---------------------------------------------------------------------- */
/* capture stuff                                                          */

const struct ng_vid_driver  *drv;
void                        *h_drv;
struct ng_video_fmt         fmt,gfmt;
struct ng_video_conv        *conv;
void                        *hconv;

static void
grab_init(void)
{
    struct ng_attribute *attr;
    int val,i;

    drv = ng_vid_open(&ng_dev.video,ng_dev.driver,NULL,0,&h_drv);
    if (NULL == drv) {
	fprintf(stderr,"no grabber device available\n");
	exit(1);
    }
    if (!(drv->capabilities(h_drv) & CAN_CAPTURE)) {
	fprintf(stderr,"device does'nt support capture\n");
	exit(1);
    }

    if (grab_input) {
	attr = ng_attr_byid(drv->list_attrs(h_drv),ATTR_ID_INPUT);
	val  = ng_attr_getint(attr,grab_input);
	if (-1 == val) {
	    fprintf(stderr,"invalid input: %s\n",grab_input);
	    exit(1);
	}
	attr->write(attr,val);
    }
    if (grab_norm) {
	attr = ng_attr_byid(drv->list_attrs(h_drv),ATTR_ID_NORM);
	val  = ng_attr_getint(attr,grab_norm);
	if (-1 == val) {
	    fprintf(stderr,"invalid norm: %s\n",grab_norm);
	    exit(1);
	}
	attr->write(attr,val);
    }

    /* try native */
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = grab_width;
    fmt.height = grab_height;
    if (0 == drv->setformat(h_drv,&fmt))
	return;

    /* check all available conversion functions */
    fmt.bytesperline = fmt.width*ng_vfmt_to_depth[fmt.fmtid]/8;
    for (i = 0;;) {
	conv = ng_conv_find_to(fmt.fmtid, &i);
	if (NULL == conv)
	    break;
	gfmt = fmt;
	gfmt.fmtid = conv->fmtid_in;
	gfmt.bytesperline = 0;
	if (0 == drv->setformat(h_drv,&gfmt)) {
	    fmt.width  = gfmt.width;
	    fmt.height = gfmt.height;
	    hconv = conv->init(&fmt,conv->priv);
	    return;
	}
    }
    fprintf(stderr,"can't get rgb24 data\n");
    exit(1);
}

static unsigned char*
grab_one(int *width, int *height)
{
    static struct ng_video_buf *cap,*buf;

    if (NULL != buf)
	ng_release_video_buf(buf);
    if (NULL == (cap = drv->getimage(h_drv))) {
	fprintf(stderr,"capturing image failed\n");
	exit(1);
    }

    if (NULL != conv) {
	buf = ng_malloc_video_buf(&fmt,3*fmt.width*fmt.height);
	conv->frame(hconv,buf,cap);
	buf->info = cap->info;
	ng_release_video_buf(cap);
    } else {
	buf = cap;
    }

    *width  = buf->fmt.width;
    *height = buf->fmt.height;
    return buf->data;
}

/* ---------------------------------------------------------------------- */

#define MSG_MAXLEN   256

#define CHAR_HEIGHT  11
#define CHAR_WIDTH   6
#define CHAR_START   4
#include "font-6x11.h"

static char*
get_message(void)
{
    static char buffer[MSG_MAXLEN+1];
    FILE *fp;
    char *p;

    if (NULL == grab_infofile)
	return grab_text;

    if (NULL == (fp = fopen(grab_infofile, "r"))) {
	fprintf(stderr,"open %s: %s\n",grab_infofile,strerror(errno));
	return grab_text;
    }

    fgets(buffer, MSG_MAXLEN, fp);
    fclose(fp);
    if (NULL != (p = strchr(buffer,'\n')))
	*p = '\0';
    return buffer;
}

static void
add_text(char *image, int width, int height)
{
    time_t      t;
    struct tm  *tm;
    char        line[MSG_MAXLEN+1],*ptr;
    int         i,x,y,f,len;

    time(&t);
    tm = localtime(&t);
    len = strftime(line,MSG_MAXLEN,get_message(),tm);
    // fprintf(stderr,"%s\n",line);

    for (y = 0; y < CHAR_HEIGHT; y++) {
	ptr = image + 3 * width * (height-CHAR_HEIGHT-2+y) + 12;
	for (x = 0; x < len; x++) {
	    f = fontdata[line[x] * CHAR_HEIGHT + y];
	    for (i = CHAR_WIDTH-1; i >= 0; i--) {
		if (f & (CHAR_START << i)) {
		    ptr[0] = grab_fg_r;
		    ptr[1] = grab_fg_g;
		    ptr[2] = grab_fg_b;
		} else if (grab_bg_r != -1) {
		    ptr[0] = grab_bg_r;
		    ptr[1] = grab_bg_g;
		    ptr[2] = grab_bg_b;
		}
		ptr += 3;
	    }
	}
    }
}

/* ---------------------------------------------------------------------- */
/* Frederic Helin <Frederic.Helin@inrialpes.fr> - 15/07/2002              */
/* Correction fonction of stereographic radial distortion                 */

int grab_dist_on = 0;
int grab_dist_k = 700;
int grab_dist_cx = -1;
int grab_dist_cy = -1;
int grab_dist_zoom = 50;
int grab_dist_sensorw = 640;
int grab_dist_sensorh = 480;

static unsigned char *
correct_distor(unsigned char * in, int width, int height,
	       int grab_zoom, int grap_k, int cx, int cy,
	       int grab_sensorw, int grab_sensorh)
{
    static unsigned char * corrimg = NULL;

    int i, j, di, dj;
    float dr, cr,ca, sensor_w, sensor_h, sx, zoom, k;

    sensor_w = grab_dist_sensorw/100.0;
    sensor_h = grab_dist_sensorh/100.0;
    zoom = grab_zoom / 100.0;
    k = grap_k / 100.0;

    if (corrimg == NULL && (corrimg = malloc(width*height*3)) == NULL ) {
	fprintf(stderr, "out of memory\n");
	exit(1);
    }

    sensor_w = 6.4;
    sensor_h = 4.8;

    // calc ratio x/y
    sx = width * sensor_h / (height * sensor_w);

    // calc new value of k in the coordonates systeme of computer
    k = k * height / sensor_h;

    // Clear image
    for (i = 0; i < height*width*3; i++) corrimg[i] = 255;

    for (j = 0; j < height ; j++) {
	for (i = 0; i < width ; i++) {

	    // compute radial distortion / parameters of center of image
	    cr  = sqrt((i-cx)/sx*(i-cx)/sx+(j-cy)*(j-cy));
	    ca  = atan(cr/k/zoom);
	    dr = k * tan(ca/2);

	    if (i == cx && j == cy) {di = cx; dj = cy;}
	    else {
		di = (i-cx) * dr / cr + cx;
		dj = (j-cy) * dr / cr + cy;
	    }

	    if (dj<height && di < width && di >= 0  && dj >= 0 &&
		j<height &&  i < width &&  i >= 0  &&  j >= 0 ) {
		corrimg[3*(j*width + i)  ] = in[3*(dj*width + di)  ];
		corrimg[3*(j*width + i)+1] = in[3*(dj*width + di)+1];
		corrimg[3*(j*width + i)+2] = in[3*(dj*width + di)+2];
	    }
	}
    }
    return corrimg;
}

/* ---------------------------------------------------------------------- */

static unsigned int
compare_images(unsigned char *last, unsigned char *current,
	       int width, int height)
{
    unsigned char *p1 = last;
    unsigned char *p2 = current;
    int avg, diff, max, i = width*height*3;

    for (max = 0, avg = 0; --i; p1++,p2++) {
	diff = (*p1 < *p2) ? (*p2 - *p1) : (*p1 - *p2);
	avg += diff;
	if (diff > max)
	    max = diff;
    }
    avg = avg / width / height;
    fprintf(stderr,"compare: max=%d,avg=%d\n",max,avg);
    /* return avg */
    return max;
}


static unsigned char *
rotate_image(unsigned char * in, int *wp, int *hp, int rot,
	     int top, int left, int bottom, int right)
{
    static unsigned char * rotimg = NULL;

    int i, j;

    int w = *wp;
    int ow = (right-left);
    int oh = (bottom-top);

    if (rotimg == NULL && (rotimg = malloc(ow*oh*3)) == NULL ) {
	fprintf(stderr, "out of memory\n");
	exit(1);
    }
    switch (rot) {
    default:
    case 0:
	for (j = 0; j < oh; j++) {
	    int ir = (j+top)*w+left;
	    int or = j*ow;
	    for (i = 0; i < ow; i++) {
		rotimg[3*(or + i)]   = in[3*(ir+i)];
		rotimg[3*(or + i)+1] = in[3*(ir+i)+1];
		rotimg[3*(or + i)+2] = in[3*(ir+i)+2];
	    }
	}
	*wp = ow;
	*hp = oh;
	break;
    case 1:
	for (i = 0; i < ow; i++) {
	    int rr = (ow-1-i)*oh;
	    int ic = i+left;
	    for (j = 0; j < oh; j++) {
		rotimg[3*(rr+j)]   = in[3*((j+top)*w+ic)];
		rotimg[3*(rr+j)+1] = in[3*((j+top)*w+ic)+1];
		rotimg[3*(rr+j)+2] = in[3*((j+top)*w+ic)+2];
	    }
	}
	*wp = oh;
	*hp = ow;
	break;
    case 2:
	for (j = 0; j < oh; j++) {
	    int ir = (j+top)*w;
	    for (i = 0; i < ow; i++) {
		rotimg[3*((oh-1-j)*ow + (ow-1-i))] = in[3*(ir+i+left)];
		rotimg[3*((oh-1-j)*ow + (ow-1-i))+1] = in[3*(ir+i+left)+1];
		rotimg[3*((oh-1-j)*ow + (ow-1-i))+2] = in[3*(ir+i+left)+2];
	    }
	}
	*wp = ow;
	*hp = oh;
	break;
    case 3:
	for (i = 0; i < ow; i++) {
	    int rr = i*oh;
	    int ic = i+left;
	    rr += oh-1;
	    for (j = 0; j < oh; j++) {
		rotimg[3*(rr-j)]   = in[3*((j+top)*w+ic)];
		rotimg[3*(rr-j)+1] = in[3*((j+top)*w+ic)+1];
		rotimg[3*(rr-j)+2] = in[3*((j+top)*w+ic)+2];
	    }
	}
	*wp = oh;
	*hp = ow;
	break;
    }
    return rotimg;
}


/* ---------------------------------------------------------------------- */

static int make_dirs(char *filename)
{
    char *dirname,*h;
    int retval = -1;

    dirname = strdup(filename);
    if (NULL == dirname)
	goto done;
    h = strrchr(dirname,'/');
    if (NULL == h)
	goto done;
    *h = 0;

    if (-1 == (retval = mkdir(dirname,0777)))
	if (ENOENT == errno)
	    if (0 == make_dirs(dirname))
		retval = mkdir(dirname,0777);

 done:
    free(dirname);
    return retval;
}

int
main(int argc, char *argv[])
{
    unsigned char *image,*val,*gimg,*lastimg = NULL;
    int width, height, i, fh;
    char filename[1024];
    char **sections;
    struct list_head *item;
    struct xfer_state *s;

    /* read config */
    if (argc > 1) {
	strcpy(filename,argv[1]);
    } else {
	sprintf(filename,"%s/%s",getenv("HOME"),".webcamrc");
    }
    fprintf(stderr,"reading config file: %s\n",filename);
    cfg_parse_file(filename);
    ng_init();

    if (NULL != (val = cfg_get_str("grab","device")))
	ng_dev.video = val;
    if (NULL != (val = cfg_get_str("grab","driver")))
	ng_dev.driver = val;
    if (NULL != (val = cfg_get_str("grab","text")))
	grab_text = val;
    if (NULL != (val = cfg_get_str("grab","infofile")))
	grab_infofile = val;
    if (NULL != (val = cfg_get_str("grab","input")))
	grab_input = val;
    if (NULL != (val = cfg_get_str("grab","norm")))
	grab_norm = val;
    if (-1 != (i = cfg_get_int("grab","width")))
	grab_width = i;
    if (-1 != (i = cfg_get_int("grab","height")))
	grab_height = i;
    if (-1 != (i = cfg_get_int("grab","delay")))
	grab_delay = i;
    if (-1 != (i = cfg_get_int("grab","wait")))
	grab_wait = i;
    if (-1 != (i = cfg_get_int("grab","rotate")))
	grab_rotate = i;
    if (-1 != (i = cfg_get_int("grab","top")))
	grab_top = i;
    if (-1 != (i = cfg_get_int("grab","left")))
	grab_left = i;
    grab_bottom = cfg_get_int("grab","bottom");
    grab_right = cfg_get_int("grab","right");
    if (-1 != (i = cfg_get_int("grab","quality")))
	grab_quality = i;
    if (-1 != (i = cfg_get_int("grab","trigger")))
	grab_trigger = i;
    if (-1 != (i = cfg_get_int("grab","once")))
	if (i > 0)
	    grab_times = 1;
    if (-1 != (i = cfg_get_int("grab","times")))
	grab_times = i;
    if (NULL != (val = cfg_get_str("grab","archive")))
	archive = val;

    if (-1 != (i = cfg_get_int("grab","fg_red")))
	if (i >= 0 && i <= 255)
	    grab_fg_r = i;
    if (-1 != (i = cfg_get_int("grab","fg_green")))
	if (i >= 0 && i <= 255)
	    grab_fg_g = i;
    if (-1 != (i = cfg_get_int("grab","fg_blue")))
	if (i >= 0 && i <= 255)
	    grab_fg_b = i;
    if (-1 != (i = cfg_get_int("grab","bg_red")))
	if (i >= 0 && i <= 255)
	    grab_bg_r = i;
    if (-1 != (i = cfg_get_int("grab","bg_green")))
	if (i >= 0 && i <= 255)
	    grab_bg_g = i;
    if (-1 != (i = cfg_get_int("grab","bg_blue")))
	if (i >= 0 && i <= 255)
	    grab_bg_b = i;

    if (-1 != (i = cfg_get_int("grab","distor")))
      grab_dist_on = i;
    if (-1 != (i = cfg_get_int("grab","distor_k")))
      grab_dist_k = i;
    if (-1 != (i = cfg_get_int("grab","distor_cx")))
      grab_dist_cx = i;
    if (-1 != (i = cfg_get_int("grab","distor_cy")))
      grab_dist_cy = i;
    if (-1 != (i = cfg_get_int("grab","distor_zoom")))
      grab_dist_zoom =i;
    if (-1 != (i = cfg_get_int("grab","distor_sensorw")))
      grab_dist_sensorw = i;
    if (-1 != (i = cfg_get_int("grab","distor_sensorh")))
      grab_dist_sensorh = i;

    /* defaults */
    if (grab_top < 0)    grab_top    = 0;
    if (grab_left < 0)   grab_left   = 0;
    if (grab_bottom < 0) grab_bottom = grab_height;
    if (grab_right < 0)  grab_right  = grab_width;

    if (grab_bottom > grab_height) grab_bottom = grab_height;
    if (grab_right > grab_width)   grab_right  = grab_width;

    if (grab_top >= grab_bottom) {
	fprintf(stderr, "config error: top must be smaller than bottom\n");
	exit(1);
    }
    if (grab_left >= grab_right) {
	fprintf(stderr, "config error: left must be smaller than right\n");
	exit(1);
    }

    if (grab_dist_k < 1 || grab_dist_k > 10000)
	grab_dist_k = 700;
    if (grab_dist_cx < 0 || grab_dist_cx > grab_width)
	grab_dist_cx = grab_width / 2;
    if (grab_dist_cy < 0 || grab_dist_cy > grab_height)
	grab_dist_cy = grab_height / 2;
    if (grab_dist_zoom < 1 || grab_dist_zoom > 1000)
	grab_dist_zoom = 100;
    if (grab_dist_sensorw < 1 ||  grab_dist_sensorw >9999)
	grab_dist_sensorw = 640;
    if (grab_dist_sensorh < 1 ||  grab_dist_sensorh >9999)
	grab_dist_sensorh = 480;

    INIT_LIST_HEAD(&connections);
    for (sections = cfg_list_sections(); *sections != NULL; sections++) {
	if (0 == strcasecmp(*sections,"grab"))
	    continue;

	/* init + set defaults */
	s = malloc(sizeof(*s));
	memset(s,0,sizeof(*s));
	s->name      = *sections;
	s->host      = "www";
	s->user      = "webcam";
	s->pass      = "xxxxxx";
	s->dir       = "public_html/images";
	s->file      = "webcam.jpeg";
	s->tmpfile   = "uploading.jpeg";
	s->passive   = 1;
	s->autologin = 0;
	s->ops       = &ftp_ops;

	/* from config */
	if (NULL != (val = cfg_get_str(*sections,"host")))
	    s->host = val;
	if (NULL != (val = cfg_get_str(*sections,"user")))
	    s->user = val;
	if (NULL != (val = cfg_get_str(*sections,"pass")))
	    s->pass = val;
	if (NULL != (val = cfg_get_str(*sections,"dir")))
	    s->dir = val;
	if (NULL != (val = cfg_get_str(*sections,"file")))
	    s->file = val;
	if (NULL != (val = cfg_get_str(*sections,"tmp")))
	    s->tmpfile = val;
	if (-1 != (i = cfg_get_int(*sections,"passive")))
	    s->passive = i;
	if (-1 != (i = cfg_get_int(*sections,"auto")))
	    s->autologin = i;
	if (-1 != (i = cfg_get_int(*sections,"debug")))
	    s->debug = i;
	if (-1 != (i = cfg_get_int(*sections,"local")))
	    if (i)
		s->ops = &local_ops;
	if (-1 != (i = cfg_get_int(*sections,"ssh")))
	    if (i)
		s->ops = &ssh_ops;

	/* all done */
	list_add_tail(&s->list,&connections);
    }

    /* init everything */
    grab_init();
    sleep(grab_wait);
    tmpdir = (NULL != getenv("TMPDIR")) ? getenv("TMPDIR") : "/tmp";
    list_for_each(item,&connections) {
	s = list_entry(item, struct xfer_state, list);
	(s->ops->open)(s);
    }

    /* print config */
    fprintf(stderr,"video4linux webcam v1.5 - (c) 1998-2002 Gerd Knorr\n");
    fprintf(stderr,"grabber config:\n  size %dx%d [%s]\n",
	    fmt.width,fmt.height,ng_vfmt_to_desc[gfmt.fmtid]);
    fprintf(stderr,"  input %s, norm %s, jpeg quality %d\n",
	    grab_input,grab_norm, grab_quality);
    fprintf(stderr,"  rotate=%d, top=%d, left=%d, bottom=%d, right=%d\n",
	   grab_rotate, grab_top, grab_left, grab_bottom, grab_right);
    list_for_each(item,&connections) {
	s = list_entry(item, struct xfer_state, list);
	s->ops->info(s);
    }

    /* run as daemon - detach from terminal */
    if (daemonize) {
	switch (fork()) {
	case -1:
	    perror("fork");
	    exit(1);
	case 0:
	    close(0); close(1); close(2); setsid();
	    break;
	default:
	    exit(0);
	}
    }

    /* main loop */
    for (;;) {
	/* grab a new one */
	gimg = grab_one(&width,&height);

	if (grab_dist_on)
	    gimg = correct_distor(gimg, width, height,
				  grab_dist_zoom, grab_dist_k,
				  grab_dist_cx, grab_dist_cy,
				  grab_dist_sensorw, grab_dist_sensorh);

	image = rotate_image(gimg, &width, &height, grab_rotate,
			     grab_top, grab_left, grab_bottom, grab_right);

	if (grab_trigger) {
	    /* look if it has changed */
	    if (NULL != lastimg) {
		i = compare_images(lastimg,image,width,height);
		if (i < grab_trigger)
		    continue;
	    } else {
		lastimg = malloc(width*height*3);
	    }
	    memcpy(lastimg,image,width*height*3);
	}

	/* ok, label it and upload */
	add_text(image,width,height);
	list_for_each(item,&connections) {
	    s = list_entry(item, struct xfer_state, list);
	    s->ops->xfer(s,image,width,height);
	}
	if (archive) {
	    time_t      t;
	    struct tm  *tm;

	    time(&t);
	    tm = localtime(&t);
	    strftime(filename,sizeof(filename)-1,archive,tm);
	again:
	    if (-1 == (fh = open(filename,O_CREAT|O_WRONLY|O_TRUNC,0666))) {
		if (ENOENT == errno) {
		    if (0 == make_dirs(filename))
			goto again;
		}
		fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
		exit(1);
	    }
	    write_file(fh, image, width, height);
	}

	if (-1 != grab_times && --grab_times == 0)
	    break;
	if (grab_delay > 0)
	    sleep(grab_delay);
    }
    list_for_each(item,&connections) {
	s = list_entry(item, struct xfer_state, list);
	s->ops->close(s);
    }
    return 0;
}
