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

const char *HTMLSTART = {
"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n"
"<HTML>\n\n"
"<HEAD>\n"
"<TITLE>Webcam</TITLE>\n"
"<META HTTP-EQUIV=\"Refresh\" CONTENT=\"60\">\n"
"<META HTTP-EQUIV=\"Expires\" CONTENT=\"0\">\n"
"<META HTTP-EQUIV=\"Pragma\" CONTENT=\"no-cache\">\n"
"<META HTTP-EQUIV=\"Cache-Control\" content=\"no-cache\">\n"
"<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=iso-8859-1\">\n"
"</HEAD>\n\n" "<BODY BGCOLOR=\"#ffffff\">\n\n"
};
const char *HTMLEND = {
	"\n</BODY>\n\n" "</HTML>\n"
};
const char *HTMLIMAGE = {
	"<IMG VSPACE=5 SRC=\"%%webcam.jpg%%\">\n"
};

/* ---------------------------------------------------------------------- */
/* configuration                                                          */

int daemonize = 0;
char *archive = NULL;
char *tmpdir;
struct list_head connections;
struct list_head plugin_list;
struct plugin_s
{
	struct ng_filter *filter;
	struct list_head list;
};

char *grab_text = "webcam %Y-%m-%d %H:%M:%S";	/* strftime */
char *grab_infofile = NULL;
int grab_width = 320;
int grab_height = 240;
int grab_delay = 3;
int grab_wait = 0;
int grab_rotate = 0;
int grab_top = 0;
int grab_left = 0;
int grab_bottom = -1;
int grab_right = -1;
int grab_quality = 75;
int grab_trigger = 0;
char *grab_trigger_area = "0%,0%,100%,100%";
int grab_trigger_average = 0;
int grab_trigger_delay = 0;
int grab_times = -1;
int grab_fg_r = 255;
int grab_fg_g = 255;
int grab_fg_b = 255;
int grab_bg_r = -1;
int grab_bg_g = -1;
int grab_bg_b = -1;
char *grab_input = NULL;
char *grab_norm = NULL;

/* ---------------------------------------------------------------------- */
/* jpeg stuff                                                             */

static int
write_file (int fd, char *data, int width, int height)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *fp;
	int i;
	unsigned char *line;

	fp = fdopen (fd, "w");
	cinfo.err = jpeg_std_error (&jerr);
	jpeg_create_compress (&cinfo);
	jpeg_stdio_dest (&cinfo, fp);
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults (&cinfo);
	jpeg_set_quality (&cinfo, grab_quality, TRUE);
	jpeg_start_compress (&cinfo, TRUE);

	for (i = 0, line = data; i < height; i++, line += width * 3)
		jpeg_write_scanlines (&cinfo, &line, 1);

	jpeg_finish_compress (&(cinfo));
	jpeg_destroy_compress (&(cinfo));
	fclose (fp);

	return 0;
}

/* ---------------------------------------------------------------------- */
/* image transfer                                                         */

struct xfer_ops;

struct xfer_state
{
	char *name;
	struct list_head list;

	/* xfer options */
	char *host;
	char *user;
	char *pass;
	char *dir;
	char *file;
	char *tmpfile;
	int debug;
	int keepjpegs;
	int currentCount;
	char *htmlfile;
	char *custom_htmlfile;

	/* ftp options */
	int passive, autologin;

	/* function pointers + private date */
	struct xfer_ops *ops;
	void *data;
};

struct xfer_ops
{
	int (*open) (struct xfer_state *);
	void (*info) (struct xfer_state *);
	int (*xfer) (struct xfer_state *, char *image, int width, int height);
	void (*close) (struct xfer_state *);
};

static int
ftp_open (struct xfer_state *s)
{
	s->data = ftp_init (s->name, s->autologin, s->passive, s->debug);
	ftp_connect (s->data, s->host, s->user, s->pass, s->dir);
	return 0;
}

static void
ftp_info (struct xfer_state *s)
{
	fprintf (stderr, "ftp config [%s]:\n  %s@%s:%s\n  %s => %s\n",
					 s->name, s->user, s->host, s->dir, s->tmpfile, s->file);
}

static int
ftp_xfer (struct xfer_state *s, char *image, int width, int height)
{
	char filename[1024];
	int fh;

	sprintf (filename, "%s/webcamXXXXXX", tmpdir);
	if (-1 == (fh = mkstemp (filename)))
	{
		perror ("mkstemp");
		exit (1);
	}
	write_file (fh, image, width, height);

	if (!ftp_stillconnected (s->data))
		ftp_connect (s->data, s->host, s->user, s->pass, s->dir);

	char ftpfilename[1024];
	strcpy (ftpfilename, s->file);

	if (s->keepjpegs > 1)
	{
		//tack a number to the file. ..before the extension
		char *extpos, *ext = NULL;
		if ((extpos = strrchr (ftpfilename, '.')))
		{
			ext = strdup (extpos);
			*(extpos) = '\0';
		}

		sprintf (ftpfilename, "%s%02i", ftpfilename, s->currentCount);
		if (ext)
		{
			strcat (ftpfilename, ext);
			free (ext);
		}

	}

	ftp_upload (s->data, filename, ftpfilename, s->tmpfile);

	unlink (filename);

	return 0;
}

static void
ftp_close (struct xfer_state *s)
{
	ftp_fini (s->data);
}

static struct xfer_ops ftp_ops = {
	open:ftp_open,
	info:ftp_info,
	xfer:ftp_xfer,
	close:ftp_close,
};

static int
ssh_open (struct xfer_state *s)
{
	s->data =
		malloc (strlen (s->user) + strlen (s->host) + strlen (s->tmpfile) * 2 +
						strlen (s->dir) + strlen (s->file) + 32);
	sprintf (s->data, "ssh %s@%s \"cat >%s && mv %s %s/%s\"", s->user, s->host,
					 s->tmpfile, s->tmpfile, s->dir, s->file);
	return 0;
}

static void
ssh_info (struct xfer_state *s)
{
	fprintf (stderr, "ssh config [%s]:\n  %s@%s:%s\n  %s => %s\n",
					 s->name, s->user, s->host, s->dir, s->tmpfile, s->file);
}

static int
ssh_xfer (struct xfer_state *s, char *image, int width, int height)
{
	char filename[1024];
	char *cmd = s->data;
	unsigned char buf[4096];
	FILE *sshp, *imgdata;
	int len, fh;

	sprintf (filename, "%s/webcamXXXXXX", tmpdir);
	if (-1 == (fh = mkstemp (filename)))
	{
		perror ("mkstemp");
		exit (1);
	}
	write_file (fh, image, width, height);

	if ((sshp = popen (cmd, "w")) == NULL)
	{
		perror ("popen");
		exit (1);
	}
	if ((imgdata = fopen (filename, "rb")) == NULL)
	{
		perror ("fopen");
		exit (1);
	}
	for (;;)
	{
		len = fread (buf, 1, sizeof (buf), imgdata);
		if (len <= 0)
			break;
		fwrite (buf, 1, len, sshp);
	}
	fclose (imgdata);
	pclose (sshp);

	unlink (filename);
	return 0;
}

static void
ssh_close (struct xfer_state *s)
{
	char *cmd = s->data;
	free (cmd);
}

static struct xfer_ops ssh_ops = {
	open:ssh_open,
	info:ssh_info,
	xfer:ssh_xfer,
	close:ssh_close,
};

static int
local_open (struct xfer_state *s)
{
	char *t;

	if (s->dir != NULL && s->dir[0] != '\0')
	{
		t = malloc (strlen (s->tmpfile) + strlen (s->dir) + 2);
		sprintf (t, "%s/%s", s->dir, s->tmpfile);
		s->tmpfile = t;

		t = malloc (strlen (s->file) + strlen (s->dir) + 2);
		sprintf (t, "%s/%s", s->dir, s->file);
		s->file = t;
	}
	return 0;
}

static void
local_info (struct xfer_state *s)
{
	fprintf (stderr, "write config [%s]:\n  local transfer %s => %s\n",
					 s->name, s->tmpfile, s->file);
}

static int
local_xfer (struct xfer_state *s, char *image, int width, int height)
{
	int fh;

	if (-1 == (fh = open (s->tmpfile, O_CREAT | O_WRONLY | O_TRUNC, 0666)))
	{
		fprintf (stderr, "open %s: %s\n", s->tmpfile, strerror (errno));
		exit (1);
	}
	write_file (fh, image, width, height);
	if (rename (s->tmpfile, s->file))
	{
		fprintf (stderr, "can't move %s -> %s\n", s->tmpfile, s->file);
		exit (1);
	}
	return 0;
}

static void
local_close (struct xfer_state *s)
{
	/* nothing */
}

static struct xfer_ops local_ops = {
	open:local_open,
	info:local_info,
	xfer:local_xfer,
	close:local_close,
};

/* ---------------------------------------------------------------------- */
/* capture stuff                                                          */

static struct ng_video_buf *ng_buf;
const struct ng_vid_driver *drv;
void *h_drv;
struct ng_video_fmt fmt, gfmt;
struct ng_video_conv *conv;
struct ng_filter filter;
void *hconv;

static void
grab_init (void)
{
	struct ng_attribute *attr;
	int val, i;

	drv = ng_vid_open (ng_dev.video, ng_dev.driver, NULL, 0, &h_drv);
	if (NULL == drv)
	{
		fprintf (stderr, "no grabber device available\n");
		exit (1);
	}
	if (!(drv->capabilities (h_drv) & CAN_CAPTURE))
	{
		fprintf (stderr, "device does'nt support capture\n");
		exit (1);
	}

	if (grab_input)
	{
		attr = ng_attr_byid (drv->list_attrs (h_drv), ATTR_ID_INPUT);
		val = ng_attr_getint (attr, grab_input);
		if (-1 == val)
		{
			fprintf (stderr, "invalid input: %s\n", grab_input);
			exit (1);
		}
		attr->write (attr, val);
	}
	if (grab_norm)
	{
		attr = ng_attr_byid (drv->list_attrs (h_drv), ATTR_ID_NORM);
		val = ng_attr_getint (attr, grab_norm);
		if (-1 == val)
		{
			fprintf (stderr, "invalid norm: %s\n", grab_norm);
			exit (1);
		}
		attr->write (attr, val);
	}

	/* try native */
	fmt.fmtid = VIDEO_RGB24;
	fmt.width = grab_width;
	fmt.height = grab_height;
	if (0 == drv->setformat (h_drv, &fmt))
		return;

	/* check all available conversion functions */
	fmt.bytesperline = fmt.width * ng_vfmt_to_depth[fmt.fmtid] / 8;
	for (i = 0;;)
	{
		conv = ng_conv_find_to (fmt.fmtid, &i);
		if (NULL == conv)
			break;
		gfmt = fmt;
		gfmt.fmtid = conv->fmtid_in;
		gfmt.bytesperline = 0;
		if (0 == drv->setformat (h_drv, &gfmt))
		{
			fmt.width = gfmt.width;
			fmt.height = gfmt.height;
			hconv = conv->init (&fmt, conv->priv);
			return;
		}
	}
	fprintf (stderr, "can't get rgb24 data\n");
	exit (1);
}

static unsigned char *
grab_one (int *width, int *height, struct ng_video_buf **buf)
{
	struct ng_video_buf *ng_cap;

	if (NULL != *buf)
		ng_release_video_buf (*buf);
	if (NULL == (ng_cap = drv->getimage (h_drv)))
	{
		fprintf (stderr, "capturing image failed\n");
		exit (1);
	}

	if (NULL != conv)
	{
		*buf = ng_malloc_video_buf (&fmt, 3 * fmt.width * fmt.height);
		conv->frame (hconv, *buf, ng_cap);
		(*buf)->info = ng_cap->info;
		ng_release_video_buf (ng_cap);
	}
	else
	{
		*buf = ng_cap;
	}

	*width = (*buf)->fmt.width;
	*height = (*buf)->fmt.height;

	return (*buf)->data;
}

/* ---------------------------------------------------------------------- */

#define MSG_MAXLEN   256

#define CHAR_HEIGHT  11
#define CHAR_WIDTH   6
#define CHAR_START   4
#include "font-6x11.h"

static char *
get_message (void)
{
	static char buffer[MSG_MAXLEN + 1];
	FILE *fp;
	char *p;

	if (NULL == grab_infofile)
		return grab_text;

	if (NULL == (fp = fopen (grab_infofile, "r")))
	{
		fprintf (stderr, "open %s: %s\n", grab_infofile, strerror (errno));
		return grab_text;
	}

	fgets (buffer, MSG_MAXLEN, fp);
	fclose (fp);
	if (NULL != (p = strchr (buffer, '\n')))
		*p = '\0';

	return buffer;
}

static void
add_text (char *image, int width, int height)
{
	time_t t;
	struct tm *tm;
	char line[MSG_MAXLEN + 1], *ptr;
	int i, x, y, f, len;

	time (&t);
	tm = localtime (&t);
	len = strftime (line, MSG_MAXLEN, get_message (), tm);
	fprintf (stderr, "%s\n", line);

	for (y = 0; y < CHAR_HEIGHT; y++)
	{
		ptr = image + 3 * width * (height - CHAR_HEIGHT - 2 + y) + 12;
		for (x = 0; x < len; x++)
		{
			f = fontdata[line[x] * CHAR_HEIGHT + y];
			for (i = CHAR_WIDTH - 1; i >= 0; i--)
			{
				if (f & (CHAR_START << i))
				{
					ptr[0] = grab_fg_r;
					ptr[1] = grab_fg_g;
					ptr[2] = grab_fg_b;
				}
				else if (grab_bg_r != -1)
				{
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
correct_distor (unsigned char *in, int width, int height,
								int grab_zoom, int grap_k, int cx, int cy,
								int grab_sensorw, int grab_sensorh)
{
	static unsigned char *corrimg = NULL;

	int i, j, di, dj;
	float dr, cr, ca, sensor_w, sensor_h, sx, zoom, k;

	sensor_w = grab_dist_sensorw / 100.0;
	sensor_h = grab_dist_sensorh / 100.0;
	zoom = grab_zoom / 100.0;
	k = grap_k / 100.0;

	if (corrimg == NULL && (corrimg = malloc (width * height * 3)) == NULL)
	{
		fprintf (stderr, "out of memory\n");
		exit (1);
	}

	sensor_w = 6.4;
	sensor_h = 4.8;

	// calc ratio x/y
	sx = width * sensor_h / (height * sensor_w);

	// calc new value of k in the coordonates systeme of computer
	k = k * height / sensor_h;

	// Clear image
	for (i = 0; i < height * width * 3; i++)
		corrimg[i] = 255;

	for (j = 0; j < height; j++)
	{
		for (i = 0; i < width; i++)
		{

			// compute radial distortion / parameters of center of image
			cr = sqrt ((i - cx) / sx * (i - cx) / sx + (j - cy) * (j - cy));
			ca = atan (cr / k / zoom);
			dr = k * tan (ca / 2);

			if (i == cx && j == cy)
			{
				di = cx;
				dj = cy;
			}
			else
			{
				di = (i - cx) * dr / cr + cx;
				dj = (j - cy) * dr / cr + cy;
			}

			if (dj < height && di < width && di >= 0 && dj >= 0 &&
					j < height && i < width && i >= 0 && j >= 0)
			{
				corrimg[3 * (j * width + i)] = in[3 * (dj * width + di)];
				corrimg[3 * (j * width + i) + 1] = in[3 * (dj * width + di) + 1];
				corrimg[3 * (j * width + i) + 2] = in[3 * (dj * width + di) + 2];
			}
		}
	}
	return corrimg;
}

/* ---------------------------------------------------------------------- */

/*static unsigned int
compare_images(unsigned char *last, unsigned char *current,
	       int width, int height)
{
	unsigned char *p1 = last;
	unsigned char *p2 = current;
	int avg, diff, max, i = width*height*3;

	for (max = 0, avg = 0; --i; p1++,p2++)
	{
		//diff = (*p1 < *p2) ? (*p2 - *p1) : (*p1 - *p2);
		diff = abs(*p1 - *p2);
		avg += diff;
		if (diff > max)
			max = diff;
	}

	avg = avg / width / height;
	fprintf(stderr,"compare: max=%d,avg=%d\n",max,avg);

	// return max
	return max;
}*/

static unsigned int
compare_images (unsigned char *last, unsigned char *current,
								int offsetx, int offsety, int iwidth, int iheight,
								int imagewidth)
{
	unsigned char *p1 = last;
	unsigned char *p2 = current;
	unsigned int p;
	int x, y, diff, max = 0;
	unsigned int avg = 0;

	offsety *= 3;
	offsetx *= 3;
	iheight *= 3;
	iwidth *= 3;

	for (y = offsety; y < offsety + iheight; y += 3)
	{
		p = imagewidth * y;

		for (x = offsetx; x < offsetx + iwidth; x++)
		{
			p1 = last + p + x;
			p2 = current + p + x;

			diff = abs (*p1 - *p2);
			avg += diff;
			if (diff > max)
				max = diff;
		}
	}

	avg = 3 * avg / iwidth / iheight;
	fprintf (stderr, "compare: max=%d,avg=%d\n", max, avg);

	// return max and average
	return (max << 8) | avg;
}

/*int max2 = 0;
static unsigned int
compare_images(unsigned char *saved, unsigned char *last, unsigned char *current, int width, int height)
{
    unsigned char *p1 = last;
    unsigned char *p2 = current;
    unsigned char *p3 = saved;
    int avg, diff, max, i = width*height*3;
    int avg2, diff2;

    for (max = 0, avg = 0, max2=0,avg2=0; --i; p1++,p2++,p3++)
    {
			diff = abs(*p1 - *p2);
			avg += diff;
			if (diff > max)
				max = diff;

			diff2 = abs(*p3 - *p2);
			avg2 += diff2;
			if (diff2 > max2)
				max2 = diff2;

    }

    avg = avg / width / height;
    avg2 = avg2 / width / height;
    fprintf(stderr,"compare: max=%d,%d,avg=%d,%d\n",max,max2,avg,avg2);


    return max;
}*/

static unsigned char *
rotate_image (unsigned char *in, int *wp, int *hp, int rot,
							int top, int left, int bottom, int right)
{
	static unsigned char *rotimg = NULL;

	int i, j;

	int w = *wp;
	int ow = (right - left);
	int oh = (bottom - top);

	if (rotimg == NULL && (rotimg = malloc (ow * oh * 3)) == NULL)
	{
		fprintf (stderr, "out of memory\n");
		exit (1);
	}

	switch (rot)
	{
	default:
	case 0:
		for (j = 0; j < oh; j++)
		{
			int ir = (j + top) * w + left;
			int or = j * ow;
			for (i = 0; i < ow; i++)
			{
				rotimg[3 * (or + i)] = in[3 * (ir + i)];
				rotimg[3 * (or + i) + 1] = in[3 * (ir + i) + 1];
				rotimg[3 * (or + i) + 2] = in[3 * (ir + i) + 2];
			}
		}
		*wp = ow;
		*hp = oh;
		break;
	case 1:
		for (i = 0; i < ow; i++)
		{
			int rr = (ow - 1 - i) * oh;
			int ic = i + left;
			for (j = 0; j < oh; j++)
			{
				rotimg[3 * (rr + j)] = in[3 * ((j + top) * w + ic)];
				rotimg[3 * (rr + j) + 1] = in[3 * ((j + top) * w + ic) + 1];
				rotimg[3 * (rr + j) + 2] = in[3 * ((j + top) * w + ic) + 2];
			}
		}
		*wp = oh;
		*hp = ow;
		break;
	case 2:
		for (j = 0; j < oh; j++)
		{
			int ir = (j + top) * w;
			for (i = 0; i < ow; i++)
			{
				rotimg[3 * ((oh - 1 - j) * ow + (ow - 1 - i))] =
					in[3 * (ir + i + left)];
				rotimg[3 * ((oh - 1 - j) * ow + (ow - 1 - i)) + 1] =
					in[3 * (ir + i + left) + 1];
				rotimg[3 * ((oh - 1 - j) * ow + (ow - 1 - i)) + 2] =
					in[3 * (ir + i + left) + 2];
			}
		}
		*wp = ow;
		*hp = oh;
		break;
	case 3:
		for (i = 0; i < ow; i++)
		{
			int rr = i * oh;
			int ic = i + left;
			rr += oh - 1;
			for (j = 0; j < oh; j++)
			{
				rotimg[3 * (rr - j)] = in[3 * ((j + top) * w + ic)];
				rotimg[3 * (rr - j) + 1] = in[3 * ((j + top) * w + ic) + 1];
				rotimg[3 * (rr - j) + 2] = in[3 * ((j + top) * w + ic) + 2];
			}
		}
		*wp = oh;
		*hp = ow;
		break;
	}

	return rotimg;
}

//-----------------------------------------------------
// T.Mohan - 28 July 2002
// # strnsub() is based on source by Erik Bachmann and
// # was released to public domain on 27 Oct 1995.
//-----------------------------------------------------

static char *
strnsub (char *pszString, char *pszPattern, char *pszReplacement,
				 int iMaxLength, int bOnce)
{
	char *pszSubstring, *pszTmpSubstring, *pszlast, *pszTempReplacement;
	int iPatternLength, iReplacementLength;

	pszSubstring = pszString;
	pszTmpSubstring = NULL;
	iPatternLength = strlen (pszPattern);
	pszlast = NULL;

	if (!strcmp (pszPattern, pszReplacement))
		return NULL;								// Pattern == replacement: loop

	while ((pszSubstring = strstr (pszSubstring, pszPattern)))
	{
		iReplacementLength = strlen (pszReplacement);
		pszTempReplacement = pszReplacement;

		if ((strlen (pszString) + (iReplacementLength - iPatternLength)) >
				iMaxLength)
			break;										// Not enough space for replacement

		if (pszTmpSubstring == NULL)	//allocate some memory only once
		{
			if ((pszTmpSubstring =
					 (char *) calloc (iMaxLength, sizeof (char))) == NULL)
				return NULL;						// oops, not enough memory?
		}

		strcpy (pszTmpSubstring, pszSubstring + iPatternLength);

		while (iReplacementLength--)
		{														// Copy replacement
			*pszSubstring++ = *pszTempReplacement++;
		}

		strcpy (pszSubstring, pszTmpSubstring);

		pszlast = pszSubstring - iPatternLength;

		if (bOnce)
			break;										//just change one

	}

	if (pszTmpSubstring)
		free (pszTmpSubstring);

	return pszlast;

}

//-----------------------------------------------------
// T.Mohan - 28 July 2002
//-----------------------------------------------------
static int
ftp_upload_htmlfile (struct xfer_state *s)
{

#define MAXLINELEN 1024

	FILE *filecustom, *filewrite;
	int filetemp;
	char line[MAXLINELEN];
	char tmpfname[1024];
	char jpegnumbered[1024], jpegfilename[1024];
	char pattern[20], pattern_newest[20];
	char *pszFound;

	strcpy (pattern, "%%webcam.jpg%%");	//replace with numbered .jpg's
	strcpy (pattern_newest, "%%newest.jpg%%");	//replace with newest .jpg
	pszFound = NULL;
	filecustom = filewrite = NULL;

	//open the custom html file if requested
	if (s->custom_htmlfile)
	{
		if ((filecustom = fopen (s->custom_htmlfile, "r")) == NULL)
		{
			perror (s->htmlfile);
			return 1;
		}
	}

	//create a temporary file
	sprintf (tmpfname, "%s/webcamhtmlXXXXXX", tmpdir);

	if (-1 == (filetemp = mkstemp (tmpfname)))
	{
		perror ("mkstemp create");
		fclose (filecustom);
		return 1;
	}

	if ((filewrite = fdopen (filetemp, "w")) == NULL)
	{
		perror ("htmlfile open for write");
		fclose (filecustom);
		unlink (tmpfname);
		return 1;
	}

	if (s->keepjpegs > 1)					//format the filename
	{
		strcpy (jpegfilename, s->file);
		char *extpos, *ext = NULL;

		if ((extpos = strrchr (jpegfilename, '.')))
		{
			ext = strdup (extpos);
			*(extpos) = '\0';
		}

		//end up with something like 'webcam%02i.jpg'
		sprintf (jpegfilename, "%s%%02i", jpegfilename);

		if (ext)										//tack on the extension if there is one
		{
			strcat (jpegfilename, ext);
			free (ext);
		}
	}
	else
	{
		strcpy (jpegnumbered, s->file);
	}

	//find pattern and replace with approriate filename in custom html file
	int count = s->currentCount;
	if (filecustom)
	{
		int actualcount = 0;

		while (fgets (line, sizeof (line), filecustom) != NULL)
		{
			if (s->keepjpegs > 1)
				sprintf (jpegnumbered, jpegfilename, count);

			//replace patterns in entire line with the numbered jpeg
			if (strnsub (line, pattern, jpegnumbered, MAXLINELEN, FALSE))
			{
				if (--count < 1)
					count = s->keepjpegs;

				actualcount++;
			}

			//replace tag for %%newest.jpg%%
			if (s->keepjpegs > 1)
				sprintf (jpegnumbered, jpegfilename, s->currentCount);

			strnsub (line, pattern_newest, jpegnumbered, MAXLINELEN, FALSE);

			//write line
			fputs (line, filewrite);
		}

		if ((s->keepjpegs > 1) && (actualcount != s->keepjpegs))
			fprintf (stderr,
							 "\n*** Warning: Found %i patterns, but keepjpegs = %i\n\n",
							 actualcount, s->keepjpegs);
	}
	else
	{
		fputs (HTMLSTART, filewrite);

		int i = s->keepjpegs;
		while (i--)
		{
			strcpy (line, HTMLIMAGE);

			if (s->keepjpegs > 1)
				sprintf (jpegnumbered, jpegfilename, count);

			if (strnsub (line, pattern, jpegnumbered, MAXLINELEN, TRUE))
			{
				if (--count < 1)
					count = s->keepjpegs;
			}

			//replace tag for %%newest.jpg%%
			//sprintf(jpegnumbered,jpegfilename,s->currentCount);
			//strnsub(line, pattern_newest, jpegnumbered, MAXLINELEN, FALSE);

			fputs (line, filewrite);
		}

		fputs (HTMLEND, filewrite);
	}

	if (filecustom)
		fclose (filecustom);
	if (filewrite)
		fclose (filewrite);

	//upload the html file
	sprintf (jpegfilename, "%s%s", s->tmpfile, ".html");
	fprintf (stderr, "%s,%s,%s\n", tmpfname, s->htmlfile, jpegfilename);
	ftp_upload (s->data, tmpfname, s->htmlfile, jpegfilename);

	unlink (tmpfname);

	return 0;
}

static void
parse_trigger_area (char *pszTriggerArea, int *ixoffset, int *iyoffset,
										int *width, int *height)
{
	if (!ixoffset || !iyoffset || !width || !height)
		return;
	if (!pszTriggerArea)
		pszTriggerArea = "0%,0%,100%,100%";

	char *str, *perc, *tmpStr;
	int count = -1, ta[4], err = 0;

	tmpStr = strdup (pszTriggerArea);
	str = strtok (tmpStr, ",");

	while (str && (++count < 4))
	{
		if ((perc = strstr (str, "%")))
			*perc = '\0';

		if (perc)
			ta[count] = atoi (str) * ((count % 2) ? *height : *width) / 100;
		else
			ta[count] = atoi (str);

		str = strtok (NULL, ",");
	}

	free (tmpStr);

	//some foolproofing...
	if ((ta[0] < 0) || (ta[0] > *width))
	{
		ta[0] = 0;
		err = 1;
	}
	if ((ta[1] < 0) || (ta[1] > *height))
	{
		ta[1] = 0;
		err = 1;
	}
	if ((ta[2] < 0) || (ta[2] > *width))
	{
		ta[2] = *width;
		err = 1;
	}
	if ((ta[3] < 0) || (ta[3] > *height))
	{
		ta[3] = *height;
		err = 1;
	}
	if (ta[0] >= ta[2])
	{
		ta[2] = *width;
		err = 1;
	}
	if (ta[1] >= ta[3])
	{
		ta[3] = *height;
		err = 1;
	}
	if (ta[2] <= ta[0])
	{
		ta[0] = 0;
		err = 1;
	}
	if (ta[3] <= ta[1])
	{
		ta[1] = 0;
		err = 1;
	}

	if (err)
		fprintf (stderr, "\n*** Invalid trigger area was fixed.\n\n");

	*ixoffset = ta[0];
	*iyoffset = ta[1];
	*width = ta[2] - ta[0];
	*height = ta[3] - ta[1];
}

static void
draw_trigger_area (unsigned char *image, int offsetx, int offsety,
									 int iTriggerwidth, int iTriggerheight, int iImageWidth)
{
	unsigned char *p;
	int x, y;

	offsety *= 3;
	offsetx *= 3;
	iTriggerheight *= 3;
	iTriggerwidth *= 3;

	for (y = offsety; y < offsety + iTriggerheight; y += 3)
	{
		p = image + iImageWidth * y;

		for (x = offsetx; x < offsetx + iTriggerwidth; x++)
		{
			//trigger area is drawn inverted
			*(p + x) = 255 - *(p + x);
		}
	}

}

static char *
trim (char *p)
{
	while ((*p == ' '))
		p++;
	while ((p[strlen (p) - 1]) == ' ')
		p[strlen (p) - 1] = '\0';

	return p;
}

static struct ng_filter *
get_plugin (char *name)
{
	int i;

	for (i = 0; NULL != ng_filters[i]; i++)
		if (!strcmp (ng_filters[i]->name, name))
			return ng_filters[i];

	return NULL;
}

static void
parse_plugins (void)
{
	char *name, *attributes, *str;
	int count = 0;
	struct ng_filter *plugin;
	struct plugin_s *currfilter = NULL;

	INIT_LIST_HEAD (&plugin_list);

	if ((str = cfg_get_str ("plugins", "names")) == NULL)
		return;

	while ((name = strtok (str, ",")))
	{
		str = NULL;

		//remove asterix for spaces
		strnsub (name, "*", " ", strlen (name), FALSE);
		name = trim (name);

		//plugin actually exists?
		if ((plugin = get_plugin (name)) == NULL)
		{
			fprintf (stderr, "plugin: [%s] not found.\n", name);
			continue;
		}

		currfilter = malloc (sizeof (*currfilter));
		memset (currfilter, 0, sizeof (*currfilter));
		currfilter->filter = plugin;
		list_add_tail (&currfilter->list, &plugin_list);

		fprintf (stderr, "plugin: [%s] found.\n", name);

		count++;
	}

	//parse the attributes
	struct list_head *item;
	name = NULL;
	list_for_each (item, &plugin_list)
	{
		struct plugin_s *currfilter = NULL;

		currfilter = list_entry (item, struct plugin_s, list);

		if (name)
			free (name);
		name = strdup (currfilter->filter->name);
		//replace spaces in the name with asterix
		strnsub (name, " ", "*", strlen (name), FALSE);
		attributes = cfg_get_str ("plugins", name);
		//remove asterix for spaces
		strnsub (name, "*", " ", strlen (name), FALSE);

		//parse the attributes
		if (!attributes)
		{
			if (currfilter->filter->attrs)
				fprintf (stderr, "plugin: [%s] Using default attributes.\n", name);
			continue;
		}

		char *attr = attributes;
		char attrname[64], attrvalue[192];
		struct ng_attribute *nga = NULL;
		while ((attributes = strtok (attr, ",")))
		{
			attr = NULL;
			if (sscanf (attributes, " %63[^=] = %191[^\n]", attrname, attrvalue) !=
					2)
				fprintf (stderr, "plugin: [%s] Parse error for attribute %s\n", name,
								 attrname);

			trim (attrname);

			if (currfilter->filter->attrs == NULL)
			{
				fprintf (stderr, "plugin: [%s] has no attributes to set.\n", name);
				break;
			}

			if (!(nga = ng_attr_byname (currfilter->filter->attrs, attrname)))
			{
				fprintf (stderr, "plugin: [%s] Unkown attribute: %s\n", name,
								 attrname);
			}
			else
			{
				nga->write (nga, atoi (attrvalue));
				fprintf (stderr, "plugin: [%s] Wrote attribute: %s, value: %s\n",
								 name, attrname, attrvalue);
			}

		}

	}

	if (name)
		free (name);

	if (count)
		fprintf (stderr, "plugin: Using %d plugin%s.\n", count,
						 (count == 1) ? "" : "s");


}

/* ---------------------------------------------------------------------- */

int
main (int argc, char *argv[])
{
	unsigned char *image, *val, *gimg, *lastimg = NULL;
	int width, height, i, fh;
	char filename[1024];
	char **sections;
	struct list_head *item;
	struct xfer_state *s = NULL;
	int iTrigX, iTrigY, iTrigWidth, iTrigHeight;
	struct plugin_s *currfilter;

	/* read config */
	if (argc > 1)
	{
		strcpy (filename, argv[1]);
	}
	else
	{
		sprintf (filename, "%s/%s", getenv ("HOME"), ".webcamrc");
	}

	fprintf (stderr, "reading config file: %s\n", filename);
	cfg_parse_file (filename);
	ng_init ();

	//setup the plugins
	parse_plugins ();

	if (NULL != (val = cfg_get_str ("grab", "device")))
		ng_dev.video = val;
	if (NULL != (val = cfg_get_str ("grab", "driver")))
		ng_dev.driver = val;
	if (NULL != (val = cfg_get_str ("grab", "text")))
		grab_text = val;
	if (NULL != (val = cfg_get_str ("grab", "infofile")))
		grab_infofile = val;
	if (NULL != (val = cfg_get_str ("grab", "input")))
		grab_input = val;
	if (NULL != (val = cfg_get_str ("grab", "norm")))
		grab_norm = val;
	if (-1 != (i = cfg_get_int ("grab", "width")))
		grab_width = i;
	if (-1 != (i = cfg_get_int ("grab", "height")))
		grab_height = i;
	if (-1 != (i = cfg_get_int ("grab", "delay")))
		grab_delay = i;
	if (-1 != (i = cfg_get_int ("grab", "wait")))
		grab_wait = i;
	if (-1 != (i = cfg_get_int ("grab", "rotate")))
		grab_rotate = i;
	if (-1 != (i = cfg_get_int ("grab", "top")))
		grab_top = i;
	if (-1 != (i = cfg_get_int ("grab", "left")))
		grab_left = i;
	grab_bottom = cfg_get_int ("grab", "bottom");
	grab_right = cfg_get_int ("grab", "right");
	if (-1 != (i = cfg_get_int ("grab", "quality")))
		grab_quality = i;
	if (-1 != (i = cfg_get_int ("grab", "trigger")))
		grab_trigger = i;
	if (NULL != (val = cfg_get_str ("grab", "trigger_area")))
		grab_trigger_area = val;
	if (-1 != (i = cfg_get_int ("grab", "trigger_average")))
		grab_trigger_average = i;
	if (-1 != (i = cfg_get_int ("grab", "trigger_delay")))
		grab_trigger_delay = i;
	if (-1 != (i = cfg_get_int ("grab", "once")))
		if (i)
			grab_times = 1;
	if (-1 != (i = cfg_get_int ("grab", "times")))
		grab_times = i;
	if (NULL != (val = cfg_get_str ("grab", "archive")))
		archive = val;

	if (-1 != (i = cfg_get_int ("grab", "fg_red")))
		if (i >= 0 && i <= 255)
			grab_fg_r = i;
	if (-1 != (i = cfg_get_int ("grab", "fg_green")))
		if (i >= 0 && i <= 255)
			grab_fg_g = i;
	if (-1 != (i = cfg_get_int ("grab", "fg_blue")))
		if (i >= 0 && i <= 255)
			grab_fg_b = i;
	if (-1 != (i = cfg_get_int ("grab", "bg_red")))
		if (i >= 0 && i <= 255)
			grab_bg_r = i;
	if (-1 != (i = cfg_get_int ("grab", "bg_green")))
		if (i >= 0 && i <= 255)
			grab_bg_g = i;
	if (-1 != (i = cfg_get_int ("grab", "bg_blue")))
		if (i >= 0 && i <= 255)
			grab_bg_b = i;

	if (-1 != (i = cfg_get_int ("grab", "distor")))
		grab_dist_on = i;
	if (-1 != (i = cfg_get_int ("grab", "distor_k")))
		grab_dist_k = i;
	if (-1 != (i = cfg_get_int ("grab", "distor_cx")))
		grab_dist_cx = i;
	if (-1 != (i = cfg_get_int ("grab", "distor_cy")))
		grab_dist_cy = i;
	if (-1 != (i = cfg_get_int ("grab", "distor_zoom")))
		grab_dist_zoom = i;
	if (-1 != (i = cfg_get_int ("grab", "distor_sensorw")))
		grab_dist_sensorw = i;
	if (-1 != (i = cfg_get_int ("grab", "distor_sensorh")))
		grab_dist_sensorh = i;

	if (grab_top < 0)
		grab_top = 0;
	if (grab_left < 0)
		grab_left = 0;
	if (grab_bottom > grab_height)
		grab_bottom = grab_height;
	if (grab_right > grab_width)
		grab_right = grab_width;
	if (grab_bottom < 0)
		grab_bottom = grab_height;
	if (grab_right < 0)
		grab_right = grab_width;
	if (grab_top >= grab_bottom)
		grab_top = 0;
	if (grab_left >= grab_right)
		grab_left = 0;

	if (grab_dist_k < 1 || grab_dist_k > 10000)
		grab_dist_k = 700;
	if (grab_dist_cx < 0 || grab_dist_cx > grab_width)
		grab_dist_cx = grab_width / 2;
	if (grab_dist_cy < 0 || grab_dist_cy > grab_height)
		grab_dist_cy = grab_height / 2;
	if (grab_dist_zoom < 1 || grab_dist_zoom > 1000)
		grab_dist_zoom = 100;
	if (grab_dist_sensorw < 1 || grab_dist_sensorw > 9999)
		grab_dist_sensorw = 640;
	if (grab_dist_sensorh < 1 || grab_dist_sensorh > 9999)
		grab_dist_sensorh = 480;

	INIT_LIST_HEAD (&connections);
	for (sections = cfg_list_sections (); *sections != NULL; sections++)
	{
		if ((0 == strcasecmp (*sections, "grab"))
				|| (0 == strcasecmp (*sections, "plugins")))
			continue;

		/* init + set defaults */
		s = malloc (sizeof (*s));
		memset (s, 0, sizeof (*s));
		s->name = *sections;
		s->host = "www";
		s->user = "webcam";
		s->pass = "xxxxxx";
		s->dir = "public_html/images";
		s->file = "webcam.jpeg";
		s->tmpfile = "uploading.jpeg";
		s->passive = 1;
		s->autologin = 0;
		s->ops = &ftp_ops;
		s->keepjpegs = 1;						//number of jpeg files to rotate through. 0 & 1 are synonymous
		s->htmlfile = NULL;
		s->custom_htmlfile = NULL;
		s->currentCount = 0;

		/* from config */
		if (NULL != (val = cfg_get_str (*sections, "host")))
			s->host = val;
		if (NULL != (val = cfg_get_str (*sections, "user")))
			s->user = val;
		if (NULL != (val = cfg_get_str (*sections, "pass")))
			s->pass = val;
		if (NULL != (val = cfg_get_str (*sections, "dir")))
			s->dir = val;
		if (NULL != (val = cfg_get_str (*sections, "file")))
			s->file = val;
		if (NULL != (val = cfg_get_str (*sections, "tmp")))
			s->tmpfile = val;
		if (-1 != (i = cfg_get_int (*sections, "passive")))
			s->passive = i;
		if (-1 != (i = cfg_get_int (*sections, "auto")))
			s->autologin = i;
		if (-1 != (i = cfg_get_int (*sections, "debug")))
			s->debug = i;
		if (-1 != (i = cfg_get_int (*sections, "local")))
			if (i)
				s->ops = &local_ops;
		if (-1 != (i = cfg_get_int (*sections, "ssh")))
			if (i)
				s->ops = &ssh_ops;
		if (-1 != (i = cfg_get_int (*sections, "keepjpegs")))
			s->keepjpegs = (i < 1) ? 1 : i;
		if (NULL != (val = cfg_get_str (*sections, "htmlfile")))
			s->htmlfile = val;
		if (NULL != (val = cfg_get_str (*sections, "custom_htmlfile")))
			s->custom_htmlfile = val;

		/* all done */
		list_add_tail (&s->list, &connections);
	}

	/* init everything */
	grab_init ();
	sleep (grab_wait);
	tmpdir = (NULL != getenv ("TMPDIR")) ? getenv ("TMPDIR") : "/tmp";
	list_for_each (item, &connections)
	{
		s = list_entry (item, struct xfer_state, list);
		s->ops->open (s);
	}

	/* print config */
	fprintf (stderr, "video4linux webcam v1.5 - (c) 1998-2002 Gerd Knorr\n");
	fprintf (stderr, "grabber config:\n  size %dx%d [%s]\n",
					 fmt.width, fmt.height, ng_vfmt_to_desc[gfmt.fmtid]);
	fprintf (stderr, "  input %s, norm %s, jpeg quality %d\n",
					 grab_input, grab_norm, grab_quality);
	fprintf (stderr, "  rotate=%d, top=%d, left=%d, bottom=%d, right=%d\n",
					 grab_rotate, grab_top, grab_left, grab_bottom, grab_right);
	list_for_each (item, &connections)
	{
		s = list_entry (item, struct xfer_state, list);
		s->ops->info (s);
	}

	/* run as daemon - detach from terminal */
	if (daemonize)
	{
		switch (fork ())
		{
		case -1:
			perror ("fork");
			exit (1);
		case 0:
			close (0);
			close (1);
			close (2);
			setsid ();
			break;
		default:
			exit (0);
		}
	}


	/* main loop */
	for (;;)
	{
		/* grab a new one */
		gimg = grab_one (&width, &height, &ng_buf);

		if (grab_top != 0 || grab_left != 0 ||
				grab_right != width || grab_bottom != height)
		{														//don't call rotate_image() if nothing to do
			gimg = rotate_image (gimg, &width, &height, grab_rotate,
													 grab_top, grab_left, grab_bottom, grab_right);
		}

		if (grab_dist_on)
		{
			gimg = correct_distor (gimg, width, height,
														 grab_dist_zoom, grab_dist_k,
														 grab_dist_cx, grab_dist_cy,
														 grab_dist_sensorw, grab_dist_sensorh);
		}

		image = gimg;

		if (grab_trigger)
		{
			/* look if it has changed */
			if (NULL != lastimg)
			{
				i = compare_images (lastimg, image, iTrigX, iTrigY, iTrigWidth,
														iTrigHeight, width);

				if (grab_trigger_average)
					i = i & 0xFF;
				else
					i = i >> 8;

				if (i < grab_trigger)
				{
					if (grab_trigger_delay)
						usleep (grab_trigger_delay * 1000);

					continue;
				}

			}
			else
			{
				lastimg = malloc (width * height * 3);

				iTrigWidth = width;
				iTrigHeight = height;
				fprintf (stderr, "width: %d,height:%d\n", width, height);
				parse_trigger_area (grab_trigger_area, &iTrigX, &iTrigY, &iTrigWidth,
														&iTrigHeight);
			}

			memcpy (lastimg, image, width * height * 3);

			//draw inverted trigger area if debug > 1
			if (s->debug > 1)
				draw_trigger_area (image, iTrigX, iTrigY, iTrigWidth, iTrigHeight,
													 width);

		}

		if (!list_empty (&plugin_list))
		{
			//if we're using plugins ng_buf will have to be valid
			// to pass to the filters.
			//assign the current image to ng_buf and make sure fmt is
			// valid due to possible size change in rotate_image()
			if (!conv && !ng_buf)
				ng_buf = ng_malloc_video_buf (&fmt, 3 * fmt.width * fmt.height);

			ng_buf->fmt.width = width;
			ng_buf->fmt.height = height;
			ng_buf->fmt.bytesperline = width * (ng_vfmt_to_depth[fmt.fmtid] / 8);
			ng_buf->size = ng_buf->fmt.bytesperline * height;
			memcpy (ng_buf->data, image, ng_buf->size);

			//call the plugins....
			list_for_each (item, &plugin_list)
			{
				currfilter = list_entry (item, struct plugin_s, list);
				ng_buf = ng_filter_single (currfilter->filter, ng_buf);
				image = ng_buf->data;
			}

		}

		/* ok, label it and upload */
		add_text (image, width, height);

		list_for_each (item, &connections)
		{
			s = list_entry (item, struct xfer_state, list);

			if (++s->currentCount > s->keepjpegs)
				s->currentCount = 1;

			s->ops->xfer (s, image, width, height);

			if (s->htmlfile)					// || s->custom_htmlfile)
				ftp_upload_htmlfile (s);
		}

		if (archive)
		{
			time_t t;
			struct tm *tm;

			time (&t);
			tm = localtime (&t);
			strftime (filename, sizeof (filename) - 1, archive, tm);
			if (-1 == (fh = open (filename, O_CREAT | O_WRONLY | O_TRUNC, 0666)))
			{
				fprintf (stderr, "open %s: %s\n", filename, strerror (errno));
				exit (1);
			}
			write_file (fh, image, width, height);
		}

		if (-1 != grab_times && --grab_times == 0)
		{
			fprintf (stderr, "grab \"times\" reached 0\n");
			break;
		}
		if (grab_delay > 0)
		{
			fprintf (stderr, "delay for %d seconds...\n", grab_delay);
			sleep (grab_delay);
		}
	}

	list_for_each (item, &connections)
	{
		s = list_entry (item, struct xfer_state, list);
		s->ops->close (s);
	}

	return 0;
}
