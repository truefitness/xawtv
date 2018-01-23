/*  SHOWRIFF.c
 *  Extracts some infos from RIFF files
 *  (c)94 UP-Vision Computergrafik for c't
 *  Written in ANSI-C. No special header files needed to compile.
 *
 *  modified by Gerd Knorr:
 *   - dos2unix :-)
 *   - fixed warnings
 *   - added some tags (checked xanim sources for info)
 *   - LFS + OpenDML + mjpeg
 *   - bytesex fixes
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>

#if BYTE_ORDER == BIG_ENDIAN
# define SWAP2(x) (((x>>8) & 0x00ff) |\
		   ((x<<8) & 0xff00))

# define SWAP4(x) (((x>>24) & 0x000000ff) |\
		   ((x>>8)  & 0x0000ff00) |\
		   ((x<<8)  & 0x00ff0000) |\
		   ((x<<24) & 0xff000000))
#else
# define SWAP2(a) (a)
# define SWAP4(a) (a)
#endif

#ifndef HAVE_FTELLO
# define ftello ftell
#endif
#ifndef HAVE_FSEEKO
# define fseeko fseek
#endif

typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef DWORD FOURCC;             /* Type of FOUR Character Codes */
typedef uint8_t boolean;
#define TRUE  1
#define FALSE 0
#define BUFSIZE 4096

/* Macro to convert expressions of form 'F','O','U','R' to
   numbers of type FOURCC: */

#if BYTE_ORDER == BIG_ENDIAN
# define MAKEFOURCC(a,b,c,d) ((((DWORD)a)<<24) | (((DWORD)b)<<16) | \
			      (((DWORD)c)<< 8) | ( (DWORD)d)      )
#else
# define MAKEFOURCC(a,b,c,d) ( ((DWORD)a)      | (((DWORD)b)<< 8) | \
			      (((DWORD)c)<<16) | (((DWORD)d)<<24)  )
#endif

/* The only FOURCCs interpreted by this program: */

#define RIFFtag MAKEFOURCC('R','I','F','F')
#define LISTtag MAKEFOURCC('L','I','S','T')
#define avihtag MAKEFOURCC('a','v','i','h')
#define strhtag MAKEFOURCC('s','t','r','h')
#define strftag MAKEFOURCC('s','t','r','f')
#define vidstag MAKEFOURCC('v','i','d','s')
#define audstag MAKEFOURCC('a','u','d','s')
#define dmlhtag MAKEFOURCC('d','m','l','h')

#define avi_tag MAKEFOURCC('A','V','I',' ')
#define wavetag MAKEFOURCC('W','A','V','E')
#define fmt_tag MAKEFOURCC('f','m','t',' ')

#define MJPGtag MAKEFOURCC('M','J','P','G')
#define _00dbtag MAKEFOURCC('0','0','d','b')
#define _00dctag MAKEFOURCC('0','0','d','c')

/* Build a string from a FOURCC number
   (s must have room for at least 5 chars) */

static void FOURCC2Str(FOURCC fcc, char* s)
{
#if BYTE_ORDER == BIG_ENDIAN
    s[0]=(fcc >> 24) & 0xFF;
    s[1]=(fcc >> 16) & 0xFF;
    s[2]=(fcc >>  8) & 0xFF;
    s[3]=(fcc      ) & 0xFF;
#else
    s[0]=(fcc      ) & 0xFF;
    s[1]=(fcc >>  8) & 0xFF;
    s[2]=(fcc >> 16) & 0xFF;
    s[3]=(fcc >> 24) & 0xFF;
#endif
    s[4]=0;
}

static DWORD fcc_type;
static DWORD riff_type;
static int stop_on_errors = 1;
static int print_mjpeg = 0;

#define EoLST  0
#define INT32  1
#define INT16  2
#define FLAGS  3
#define CCODE  4

struct FLAGLIST {
    int  bit;
    char *name;
};

struct VAL {
    int  type;
    char *name;
    struct FLAGLIST *flags;
};

struct FLAGLIST flags_avih[] = {
	{ 0x00000010, "hasindex" },
	{ 0x00000020, "useindex" },
	{ 0x00000100, "interleaved" },
	{ 0x00010000, "for_capture" },
	{ 0x00020000, "copyrighted" },
	{ 0, NULL }
};

struct VAL names_avih[] = {
    { INT32,  "us_frame" },
    { INT32,  "max_bps" },
    { INT32,  "pad_gran" },
    { FLAGS,  "flags", flags_avih },
    { INT32,  "tot_frames" },
    { INT32,  "init_frames" },
    { INT32,  "streams" },
    { INT32,  "sug_bsize" },
    { INT32,  "width" },
    { INT32,  "height" },
    { INT32,  "scale" },
    { INT32,  "rate" },
    { INT32,  "start" },
    { INT32,  "length" },
    { EoLST,  NULL }
};

struct VAL names_strh[] = {
    { CCODE,  "fcc_handler" },
    { FLAGS,  "flags" },
    { INT32,  "priority" },
    { INT32,  "init_frames" },
    { INT32,  "scale" },
    { INT32,  "rate" },
    { INT32,  "start" },
    { INT32,  "length" },
    { INT32,  "sug_bsize" },
    { INT32,  "quality" },
    { INT32,  "samp_size" },
    { EoLST,  NULL }
};

struct VAL names_strf_vids[] = {
    { INT32,  "size" },
    { INT32,  "width" },
    { INT32,  "height" },
    { INT16,  "planes" },
    { INT16,  "bit_cnt" },
    { CCODE,  "compression" },
    { INT32,  "image_size" },
    { INT32,  "xpels_meter" },
    { INT32,  "ypels_meter" },
    { INT32,  "num_colors" },
    { INT32,  "imp_colors" },
    { EoLST,  NULL }
};

struct VAL names_strf_auds[] = {
    { INT16,  "format" },
    { INT16,  "channels" },
    { INT32,  "rate" },
    { INT32,  "av_bps" },
    { INT16,  "blockalign" },
    { INT16,  "size" },
    { EoLST,  NULL }
};

struct VAL names_dmlh[] = {
    { INT32,  "frames" },
    { EoLST,  NULL }
};

static void dump_vals(FILE *f, int count, struct VAL *names)
{
    DWORD i,j,val32;
    WORD  val16;

    for (i = 0; names[i].type != EoLST; i++) {
	switch (names[i].type) {
	case INT32:
	    fread(&val32,4,1,f);
	    val32 = SWAP4(val32);
	    printf("\t%-12s = %d\n",names[i].name,val32);
	    break;
	case CCODE:
	    fread(&val32,4,1,f);
	    val32 = SWAP4(val32);
	    if (val32) {
		printf("\t%-12s = %c%c%c%c (0x%x)\n",names[i].name,
		       (int)( val32        & 0xff),
		       (int)((val32 >>  8) & 0xff),
		       (int)((val32 >> 16) & 0xff),
		       (int)((val32 >> 24) & 0xff),
		       val32);
	    } else {
		printf("\t%-12s = unset (0)\n",names[i].name);
	    }
	    break;
	case FLAGS:
	    fread(&val32,4,1,f);
	    val32 = SWAP4(val32);
	    printf("\t%-12s = 0x%x\n",names[i].name,val32);
	    if (names[i].flags) {
		for (j = 0; names[i].flags[j].bit != 0; j++)
		    if (names[i].flags[j].bit & val32)
			printf("\t\t0x%x: %s\n",
			       names[i].flags[j].bit,names[i].flags[j].name);
	    }
	    break;
	case INT16:
	    fread(&val16,2,1,f);
	    val16 = SWAP2(val16);
	    printf("\t%-12s = %ld\n",names[i].name,(long)val16);
	    break;
	}
    }
}

struct JPEG_MARKER {
    unsigned char  val;
    char           *name;
    char           *desc;
} mark[] = {
    { 0xc0, "SOF0 ", "start of frame 0"   },
    { 0xc4, "DHT  ", "huffmann table(?)"  },
    { 0xd8, "SOI  ", "start of image"     },
    { 0xda, "SOS  ", "start of scan"      },
    { 0xdb, "DQT  ", "quantization table" },
    { 0xe0, "APP0 ", NULL                 },
    { 0xfe, "COM  ", "comment"            },
    { 0,    NULL,    NULL }
};

static void hexlog(unsigned char *buf, int count)
{
    int l,i;

    for (l = 0; l < count; l+= 16) {
	printf("\t  ");
	for (i = l; i < l+16; i++) {
	    if (i < count)
		printf("%02x ",buf[i]);
	    else
		printf("   ");
	    if ((i%4) == 3)
		printf(" ");
	}
	for (i = l; i < l+16; i++) {
	    if (i < count)
		printf("%c",isprint(buf[i]) ? buf[i] : '.');
	}
	printf("\n");
    }
}

static void dump_jpeg(unsigned char *buf, int len)
{
    int i,j,type,skip;

    if (!print_mjpeg)
	return;

    for (i = 0; i < len;) {
	if (buf[i] != 0xff) {
	    printf("\tjpeg @ 0x%04x: oops 0x%02x 0x%02x 0x%02x 0x%02x\n",
		   i,buf[i],buf[i+1],buf[i+2],buf[i+3]);
	    return;
	}
	type = buf[i+1];
	for (j = 0; 0 != mark[j].val; j++)
	    if (mark[j].val == type)
		break;
	printf("\tjpeg @ 0x%04x: %s (0x%x)  %s\n",i,
	       mark[j].name ? mark[j].name : "???", type,
	       mark[j].desc ? mark[j].desc : "???");
	i+=2;
	skip = buf[i] << 8 | buf[i+1];
	switch (type) {
	case 0xd8:
	    skip=0;
	    break;
	case 0xc0: /* SOF0 */
	    printf("\t\tsize: %dx%d prec=%d comp=%d\n",
		   buf[i+6] | (buf[i+5] << 8),
		   buf[i+4] | (buf[i+3] << 8),
		   buf[i+2], buf[i+7]);
	    for (j = 0; j < buf[i+7]; j++) {
		printf("\t\tcomp%d: id=%d h=%d v=%d qt=%d\n",j,
		       buf[i+3*j+8],
		       (buf[i+3*j+9] >> 4) & 0x0f,
		       buf[i+3*j+9]        & 0x0f,
		       buf[i+3*j+10]);
	    }
	    break;
	case 0xc4: /* DHT */
	    printf("\t\tindex=%d\n",buf[i+2]);
	    break;
	case 0xdb: /* DQT */
	    printf("\t\tprec=%d, nr=%d\n",
		   (buf[i+2] >> 4) & 0x0f,
		   buf[i+2]        & 0x0f);
	    break;
	case 0xda:
	    return;
	}
	hexlog(buf+i+2,skip-2);
	i += skip;
    }
}

static unsigned char*
off_t_to_char(off_t val, int base, int len)
{
    static const unsigned char digit[] = "0123456789abcdef";
    static unsigned char outbuf[32];
    unsigned char *p = outbuf + sizeof(outbuf);
    int i;

    *(--p) = 0;
    for (i = 0; i < len || val > 0; i++) {
	*(--p) = digit[ val % base ];
	val = val / base;
    }
    return p;
}

/* Reads a chunk ID and the chunk's size from file f at actual
   file position : */

static boolean ReadChunkHead(FILE* f, FOURCC* ID, DWORD* size)
{
    if (!fread(ID,sizeof(FOURCC),1,f)) return(FALSE);
    if (!fread(size,sizeof(DWORD),1,f)) return(FALSE);
    *size = SWAP4(*size);
    return(TRUE);
}

/* Processing of a chunk. (Will be called recursively!).
   Processes file f starting at position filepos.
   f contains filesize bytes.
   If DesiredTag!=0, ProcessChunk tests, whether the chunk begins
   with the DesiredTag. If the read chunk is not identical to
   DesiredTag, an error message is printed.
   RekDepth determines the recursion depth of the chunk.
   chunksize is set to the length of the chunk's data (excluding
   header and padding byte).
   ProcessChunk prints out information of the chunk to stdout
   and returns FALSE, if an error occured. */

static boolean ProcessChunk(FILE* f, size_t filepos, size_t filesize,
			    FOURCC DesiredTag, int RekDepth,
			    DWORD* chunksize)
{
    unsigned char   buf[BUFSIZE];
    int    buflen;
    char   tagstr[5];          /* FOURCC of chunk converted to string */
    FOURCC chunkid;            /* read FOURCC of chunk                */
    size_t datapos;            /* position of data in file to process */

    if (filepos>filesize-1) {  /* Oops. Must be something wrong!      */
	printf("  *****  Error: Data would be behind end of file!\n");
	if (stop_on_errors)
	    return(FALSE);
    }
    fseeko(f,filepos,SEEK_SET);    /* Go to desired file position!     */

    if (!ReadChunkHead(f,&chunkid,chunksize)) {  /* read chunk header */
	printf("  *****  Error reading chunk at filepos 0x%s\n",
	       off_t_to_char(filepos,16,1));
	return(FALSE);
    }
    FOURCC2Str(chunkid,tagstr);       /* now we can PRINT the chunkid */
    if (DesiredTag) {                 /* do we have to test identity? */
	if (DesiredTag!=chunkid) {
	    char ds[5];
	    FOURCC2Str(DesiredTag,ds);
	    printf("\n\n *** Error: Expected chunk '%s', found '%s'\n",
		   ds,tagstr);
	    return(FALSE);
	}
    }

    datapos=filepos+sizeof(FOURCC)+sizeof(DWORD); /* here is the data */

    /* print out header: */
    printf("(0x%s) %*c  ID:<%s>   Size: 0x%08x\n",
	   off_t_to_char(filepos,16,8),(RekDepth+1)*4,' ',tagstr,*chunksize);

    if (datapos + ((*chunksize+1)&~1) > filesize) {      /* too long? */
	printf("  *****  Error: Chunk exceeds file\n");
	if (stop_on_errors)
	    return(FALSE);
    }

    switch (chunkid) {

  /* Depending on the ID of the chunk and the internal state, the
     different IDs can be interpreted. At the moment the only
     interpreted chunks are RIFF- and LIST-chunks. For all other
     chunks only their header is printed out. */

    case RIFFtag:
    case LISTtag: {

	DWORD datashowed;
	FOURCC formtype;       /* format of chunk                     */
	char   formstr[5];     /* format of chunk converted to string */
	DWORD  subchunksize;   /* size of a read subchunk             */

	fread(&formtype,sizeof(FOURCC),1,f);    /* read the form type */
	FOURCC2Str(formtype,formstr);           /* make it printable  */

	/* print out the indented form of the chunk: */
	if (chunkid==RIFFtag) {
	    printf("%12c %*c  Form Type = <%s>\n",
		   ' ',(RekDepth+1)*4,' ',formstr);
	    riff_type = formtype;
	} else
	    printf("%12c %*c  List Type = <%s>\n",
		   ' ',(RekDepth+1)*4,' ',formstr);

	datashowed=sizeof(FOURCC);    /* we showed the form type      */
	datapos+=datashowed;          /* for the rest of the routine  */

	while (datashowed<*chunksize) {      /* while not showed all: */

	    long subchunklen;           /* complete size of a subchunk  */

	    /* recurse for subchunks of RIFF and LIST chunks: */
	    if (!ProcessChunk(f,datapos,filesize,0,
			      RekDepth+1,&subchunksize)) return(FALSE);

	    subchunklen = sizeof(FOURCC) +  /* this is the complete..   */
		sizeof(DWORD)  +  /* .. size of the subchunk  */
		((subchunksize+1) & ~1);

	    datashowed += subchunklen;      /* we showed the subchunk   */
	    datapos    += subchunklen;      /* for the rest of the loop */
	}
    } break;

    /* Feel free to put your extensions here! */

    case avihtag:
	dump_vals(f,sizeof(names_avih)/sizeof(struct VAL),names_avih);
	break;
    case strhtag:
    {
	char   typestr[5];
	fread(&fcc_type,sizeof(FOURCC),1,f);
	FOURCC2Str(fcc_type,typestr);
	printf("\tfcc_type     = %s\n",typestr);
	dump_vals(f,sizeof(names_strh)/sizeof(struct VAL),names_strh);
	break;
    }
    case strftag:
	switch (fcc_type) {
	case vidstag:
	    dump_vals(f,sizeof(names_strf_vids)/sizeof(struct VAL),names_strf_vids);
	    break;
	case audstag:
	    dump_vals(f,sizeof(names_strf_auds)/sizeof(char*),names_strf_auds);
	    break;
	default:
	    printf("unknown\n");
	    break;
	}
	break;
    case fmt_tag:
	if (riff_type == wavetag)
	    dump_vals(f,sizeof(names_strf_auds)/sizeof(char*),names_strf_auds);
	break;
    case dmlhtag:
	dump_vals(f,sizeof(names_dmlh)/sizeof(struct VAL),names_dmlh);
	break;
    case _00dbtag:
    case _00dctag:
	buflen = (*chunksize > BUFSIZE) ? BUFSIZE : *chunksize;
	fread(buf, buflen, 1, f);
	dump_jpeg(buf,buflen);
	break;
    }

    return(TRUE);
}

static void
usage(char *prog)
{
    char *h;

    if (NULL != (h = strrchr(prog,'/')))
	prog = h+1;
    fprintf(stderr,
	    "\n"
	    "%s shows contents of RIFF files (AVI,WAVE...).\n"
	    "(c) 1994 UP-Vision Computergrafik for c't\n"
	    "unix port and some extentions (for avi) by Gerd Knorr\n"
	    "\n"
	    "usage:  %s [ -j ] [ -e ] filename\n"
	    "options:\n"
	    "  -j  try to decode some mjpeg headers\n"
	    "  -e  don't stop on errors\n"
	    "\n",
	    prog,prog);
}

int main (int argc, char **argv)
{
    FILE*  f;            /* the input file              */
    off_t  filesize;     /* its size                    */
    off_t  filepos;
    DWORD  chunksize;    /* size of the RIFF chunk data */
    int c;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "jeh")))
	    break;
	switch (c) {
	case 'j':
	    print_mjpeg = 1;
	    break;
	case 'e':
	    stop_on_errors = 0;
	    break;
	case 'h':
	default:
	    usage(argv[0]);
	    exit(1);
	}
    }

    if (optind == argc) {
	usage(argv[0]);
	exit(1);
    }

    if (!(f=fopen(argv[optind],"rb"))) {
	printf("\n\n *** Error opening file %s. Program aborted!\n",
	       argv[optind]);
	return(1);
    }

    fseeko(f, 0, SEEK_END);
    filesize = ftello(f);
    fseeko(f, 0, SEEK_SET);

    printf("Contents of file %s (%s/0x%s bytes):\n\n",argv[optind],
	   off_t_to_char(filesize,10,1),
	   off_t_to_char(filesize,16,1));

    for (filepos = 0; filepos < filesize;) {
	chunksize = 0;
	if (!ProcessChunk(f,filepos,filesize,RIFFtag,0,&chunksize))
	    break;
	filepos += chunksize + 8;
	printf("\n");
    }

    return(0);
}
