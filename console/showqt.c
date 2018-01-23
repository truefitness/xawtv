#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include <fcntl.h>
#include <inttypes.h>
#include <time.h>

#ifndef PRId64
# define PRId64 "lld"
# define PRIx64 "llx"
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
# define SWAP2(x) ((((uint16_t)x>>8)  & (uint16_t)0x00ff) |\
		   (((uint16_t)x<<8)  & (uint16_t)0xff00))

# define SWAP4(x) ((((uint32_t)x>>24) & (uint32_t)0x000000ff) |\
		   (((uint32_t)x>>8)  & (uint32_t)0x0000ff00) |\
		   (((uint32_t)x<<8)  & (uint32_t)0x00ff0000) |\
		   (((uint32_t)x<<24) & (uint32_t)0xff000000))

# define SWAP8(x) ((((uint64_t)x>>56) & (uint64_t)0x00000000000000ffULL) |\
		   (((uint64_t)x>>40) & (uint64_t)0x000000000000ff00ULL) |\
		   (((uint64_t)x>>24) & (uint64_t)0x0000000000ff0000ULL) |\
		   (((uint64_t)x>> 8) & (uint64_t)0x00000000ff000000ULL) |\
		   (((uint64_t)x<< 8) & (uint64_t)0x000000ff00000000ULL) |\
		   (((uint64_t)x<<24) & (uint64_t)0x0000ff0000000000ULL) |\
		   (((uint64_t)x<<40) & (uint64_t)0x00ff000000000000ULL) |\
		   (((uint64_t)x<<56) & (uint64_t)0xff00000000000000ULL))
#else
# define SWAP2(a) (a)
# define SWAP4(a) (a)
# define SWAP8(a) (a)
#endif

#define MAKEFOURCC(a,b,c,d) ((((uint32_t)a)<<24) | (((uint32_t)b)<<16) | \
			     (((uint32_t)c)<< 8) | ( (uint32_t)d)      )

#define a_clip MAKEFOURCC('c','l','i','p')
#define a_co64 MAKEFOURCC('c','o','6','4')
#define a_dinf MAKEFOURCC('d','i','n','f')
#define a_dref MAKEFOURCC('d','r','e','f')
#define a_free MAKEFOURCC('f','r','e','e')
#define a_edts MAKEFOURCC('e','d','t','s')
#define a_elst MAKEFOURCC('e','l','s','t')
#define a_hdlr MAKEFOURCC('h','d','l','r')
#define a_mdat MAKEFOURCC('m','d','a','t')
#define a_mdhd MAKEFOURCC('m','d','h','d')
#define a_mdia MAKEFOURCC('m','d','i','a')
#define a_minf MAKEFOURCC('m','i','n','f')
#define a_moov MAKEFOURCC('m','o','o','v')
#define a_mvhd MAKEFOURCC('m','v','h','d')
#define a_skip MAKEFOURCC('s','k','i','p')
#define a_smhd MAKEFOURCC('s','m','h','d')
#define a_stbl MAKEFOURCC('s','t','b','l')
#define a_stco MAKEFOURCC('s','t','c','o')
#define a_stsc MAKEFOURCC('s','t','s','c')
#define a_stsd MAKEFOURCC('s','t','s','d')
#define a_stsh MAKEFOURCC('s','t','s','h')
#define a_stss MAKEFOURCC('s','t','s','s')
#define a_stsz MAKEFOURCC('s','t','s','z')
#define a_stts MAKEFOURCC('s','t','t','s')
#define a_tkhd MAKEFOURCC('t','k','h','d')
#define a_trak MAKEFOURCC('t','r','a','k')
#define a_udta MAKEFOURCC('u','d','t','a')
#define a_vmhd MAKEFOURCC('v','m','h','d')
#define a_wide MAKEFOURCC('w','i','d','e')

/* ------------------------------------------------------------------ */

struct classic_atom {
    uint32_t  size;
    uint32_t  type;
    uint64_t  extsize;
};

struct qt_atom {
    uint32_t  size;
    uint32_t  type;
    uint64_t  extsize;
};

enum field_type {
    END_OF_LIST = 0,
    INT16,
    INT32,
    INT64,
    FIX16,
    FIX32,
    FOURCC,
    VER,
    FLAGS3,
    TIME,
    LANG,
    COLOR,
    COUNT,
    RES2,
    RES4,
    RES6,
    RES8,
    RES10,
    MATRIX,
};

struct field_list {
    enum field_type type;
    char            *name;
};

struct atom_list {
    uint32_t          type;
    struct field_list *fields;
};

struct fcc_names {
    uint32_t          type;
    char              *name;
};

static int handle_classic_atom(int fh, off_t pos, off_t size, int depth);
#if 0
static int handle_qt_atom(int fh, off_t pos, off_t size, int depth);
#endif

/* ------------------------------------------------------------------ */

static struct field_list l_co64[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { COUNT,  "number of entries"      },
    /* FIXME: loop */
    { INT64,  "offset64"               },
    { END_OF_LIST }
};

static struct field_list l_dref[] = {
    { VER,    "version"            },
    { FLAGS3, "flags"              },
    { INT32,  "number of entries"  },
    /* FIXME: loop */
    { INT32,  "size"               },
    { FOURCC, "type"               },
    { VER,    "version"            },
    { FLAGS3, "flags"              },
    { END_OF_LIST }
};

static struct field_list l_elst[] = {
    { VER,    "version"            },
    { FLAGS3, "flags"              },
    { COUNT,  "number of entries"  },
    /* FIXME: loop */
    { INT32,  "track duration"     },
    { INT32,  "media time"         },
    { FIX32,  "media rate"         },
    { END_OF_LIST }
};

static struct field_list l_hdlr[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { FOURCC, "component type"         },
    { FOURCC, "component subtype"      },
    { RES4,   "component manufacturer" },
    { RES4,   "component flags"        },
    { RES4,   "component flags mask"   },
    /* FIXME: name */
    { END_OF_LIST }
};

static struct field_list l_mdhd[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { TIME,   "ctime"                  },
    { TIME,   "mtime"                  },
    { INT32,  "time scale"             },
    { INT32,  "duration"               },
    { LANG,   "language"               },
    { INT16,  "quality"                },
    { END_OF_LIST }
};

static struct field_list l_mvhd[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { TIME,   "ctime"                  },
    { TIME,   "mtime"                  },
    { INT32,  "time scale"             },
    { INT32,  "duration"               },
    { FIX32,  "preferred rate"         },
    { FIX16,  "preferred volume"       },
    { RES10,  "reserved"               },
    { MATRIX, "matrix"                 },
    { INT32,  "preview time"           },
    { INT32,  "preview duration"       },
    { INT32,  "poster time"            },
    { INT32,  "selection time"         },
    { INT32,  "selection duration"     },
    { INT32,  "current time"           },
    { INT32,  "next track id"          },
    { END_OF_LIST }
};

static struct field_list l_smhd[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { INT16,  "balance"                },
    { RES2,   "reserved"               },
    { END_OF_LIST }
};

static struct field_list l_stco[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { COUNT,  "number of entries"      },
    /* FIXME: loop */
    { INT32,  "offset"                 },
    { END_OF_LIST }
};

static struct field_list l_stsc[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { COUNT,  "number of entries"      },
    /* FIXME: loop */
    { INT32,  "first chunk"            },
    { INT32,  "samples per chunk"      },
    { INT32,  "sample description id"  },
    { END_OF_LIST }
};

static struct field_list l_stsd[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { COUNT,  "number of entries"      },
    /* FIXME: loop */
    { INT32,  "size"                   },
    { FOURCC, "format"                 },
    { RES6,   "reserved"               },
    { INT16,  "data reference index"   },
    { END_OF_LIST }
};

static struct field_list l_stts[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { COUNT,  "number of entries"      },
    /* FIXME: loop */
    { INT32,  "sample count"           },
    { INT32,  "sample duration"        },
    { END_OF_LIST }
};

static struct field_list l_stsz[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { INT32,  "sample size"            },
    { COUNT,  "number of entries"      },
    /* FIXME: loop if "sample size is 0" */
#if 0
    { INT32,  "sample size"            },
#endif
    { END_OF_LIST }
};

static struct field_list l_tkhd[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { TIME,   "ctime"                  },
    { TIME,   "mtime"                  },
    { INT32,  "track id"               },
    { RES4,   "reserved"               },
    { INT32,  "duration"               },
    { RES8,   "reserved"               },
    { INT16,  "layer"                  },
    { INT16,  "alternate group"        },
    { INT16,  "volume"                 },
    { RES2,   "reserved"               },
    { MATRIX, "matrix"                 },
    { FIX32,  "width"                  },
    { FIX32,  "height"                 },
    { END_OF_LIST }
};

static struct field_list l_vmhd[] = {
    { VER,    "version"                },
    { FLAGS3, "flags"                  },
    { INT16,  "graphics mode"          },
    { COLOR,  "opcolor"                },
    { END_OF_LIST }
};

static struct atom_list alist[] = {
    { a_co64, l_co64 },
    { a_dref, l_dref },
    { a_elst, l_elst },
    { a_hdlr, l_hdlr },
    { a_mdhd, l_mdhd },
    { a_mvhd, l_mvhd },
    { a_smhd, l_smhd },
    { a_stco, l_stco },
    { a_stsc, l_stsc },
    { a_stsd, l_stsd },
    { a_stsz, l_stsz },
    { a_stts, l_stts },
    { a_tkhd, l_tkhd },
    { a_vmhd, l_vmhd },
    { /* end of list */}
};

/* ------------------------------------------------------------------ */

static struct fcc_names flist[] = {
    { a_co64, "chunk offset64 atom" },
    { a_clip, "movie clipping atom" },
    { a_dinf, "data information atom" },
    { a_dref, "data reference atom" },
    { a_edts, "edit atom" },
    { a_elst, "edit list atom" },
    { a_free, "unused space" },
    { a_hdlr, "handler reference atom" },
    { a_mdat, "movie data atom" },
    { a_mdhd, "media header atom" },
    { a_mdia, "media atom" },
    { a_minf, "media information atom" },
    { a_moov, "movie atom" },
    { a_mvhd, "movie header atom" },
    { a_skip, "unused space" },
    { a_smhd, "sound media information header atom" },
    { a_stbl, "sample table atom" },
    { a_stco, "chunk offset atom" },
    { a_stsc, "sample-to-chunk atom" },
    { a_stsd, "sample description atom" },
    { a_stsz, "sample size atom" },
    { a_stts, "time-to-sample atom" },
    { a_tkhd, "track header atom" },
    { a_trak, "track atom" },
    { a_udta, "user data atom" },
    { a_vmhd, "video media information header atom"  },
    { a_wide, "reserved space for extsize field" },
    { /* end of list */}
};

/* ------------------------------------------------------------------ */

static int verbose=0;

static void swap_classic_atom(struct classic_atom *a)
{
    a->size    = SWAP4(a->size);
    a->type    = SWAP4(a->type);
    a->extsize = SWAP8(a->extsize);
}

#if 0
static void swap_qt_atom(struct qt_atom *a)
{
}
#endif

static int xisprint(int c)
{
    switch (c) {
    case 169: /* copyright */
	return 1;
    default:
	return isprint(c);
    }
}

static char* strfcc(uint32_t type)
{
    static char retval[64];
    int i,l;

    if (xisprint((type >> 24) & 0xff) &&
	xisprint((type >> 16) & 0xff) &&
	xisprint((type >>  8) & 0xff) &&
	xisprint( type        & 0xff)) {
	l = sprintf(retval,"%c%c%c%c",
		(type >> 24) & 0xff,
		(type >> 16) & 0xff,
		(type >>  8) & 0xff,
		type         & 0xff);
    } else {
	l = sprintf(retval,"0x%08x",type);
    }
    for (i = 0; flist[i].type != 0; i++)
	if (flist[i].type == type)
	    break;
    if (flist[i].type != 0)
	sprintf(retval+l," [%s]",flist[i].name);
    return retval;
}

#define FIELD_NAME "\t%s%-20s = "
static void dump_fields(int fh, off_t pos, struct field_list *list)
{
    char dummy[64],si[8];
    int  i,loop,cpos;
    int8_t   int8;
    int16_t  int16;
    int32_t  int32, fcc, count;
    int64_t  int64;
    uint16_t color[3];
    uint32_t uint32;
    time_t   t;

    if (0 == verbose)
	return;
    if (-1 == lseek(fh,pos,SEEK_SET)) {
	perror("lseek");
	exit(1);
    }
    si[0] = 0;
    count = 0;
    cpos  = 0;
    loop  = 0;
    for (i = 0; list[i].type != END_OF_LIST || loop < count-1; i++) {
	switch (list[i].type) {
	case FOURCC:
	    read(fh,&fcc,sizeof(fcc));
	    printf(FIELD_NAME "%s\n",si,list[i].name,strfcc(SWAP4(fcc)));
	    break;
	case VER:
	    read(fh,&int8,sizeof(int8));
	    if (verbose > 1 || int8 > 0)
		printf(FIELD_NAME "%d\n",si,list[i].name,(int)int8);
	    break;
	case LANG:
	case INT16:
	    read(fh,&int16,sizeof(int16));
	    printf(FIELD_NAME "%d\n",si,list[i].name,(int)SWAP2(int16));
	    break;
	case INT32:
	    read(fh,&int32,sizeof(int32));
	    printf(FIELD_NAME "%d\n",si,list[i].name,SWAP4(int32));
	    break;
	case INT64:
	    read(fh,&int64,sizeof(int64));
	    printf(FIELD_NAME "%" PRId64 "\n",si,list[i].name,SWAP8(int64));
	    break;
	case FIX16:
	    read(fh,&int16,sizeof(int16));
	    printf(FIELD_NAME "%f\n",si,list[i].name,
		   SWAP2(int16) / 256.0);
	    break;
	case FIX32:
	    read(fh,&int32,sizeof(int32));
	    printf(FIELD_NAME "%f\n",si,list[i].name,
		   SWAP4(int32) / 65536.0);
	    break;
	case FLAGS3:
	    read(fh,dummy,3);
	    int32 = dummy[0] << 16 | dummy[1] << 8 | dummy[2];
	    if (verbose > 1 || int32 > 0)
		printf(FIELD_NAME "0x%06x\n",si,list[i].name,int32);
	    break;
	case TIME:
	    read(fh,&uint32,sizeof(uint32));
	    t = SWAP4(uint32) - 2082848400;
	    strftime(dummy,sizeof(dummy),"%d. %b %Y - %H:%M:%S",localtime(&t));
	    printf(FIELD_NAME "%s\n",si,list[i].name,dummy);
	    break;
	case COLOR:
	    read(fh,&color,sizeof(color));
	    printf(FIELD_NAME "%d/%d/%d (rgb)\n",si,list[i].name,
		   (int)SWAP2(color[0]),
		   (int)SWAP2(color[0]),
		   (int)SWAP2(color[0]));
	    break;
	case RES2:
	    read(fh,dummy,2);
	    break;
	case RES4:
	    read(fh,dummy,4);
	    break;
	case RES6:
	    read(fh,dummy,6);
	    break;
	case RES8:
	    read(fh,dummy,8);
	    break;
	case RES10:
	    read(fh,dummy,10);
	    break;
	case MATRIX:
	    read(fh,dummy,36);
	    break;
	case COUNT:
	    read(fh,&count,sizeof(count));
	    count = SWAP4(count);
	    cpos  = i;
	    if (verbose < 2) {
		printf("\t[list follows]\n");
		return;
	    }
	    printf(FIELD_NAME "%d\n",si,list[i].name,count);
	    sprintf(si,"[%d] ",loop);
	    break;
	case END_OF_LIST:
	    i = cpos;
	    loop++;
	    sprintf(si,"[%d] ",loop);
	    break;
	}
    }
}

static void dump_string(int fh, off_t pos, off_t size)
{
    off_t off;
    uint16_t ssize,stype;
    char *str;

    if (0 == verbose)
	return;
    if (-1 == lseek(fh,pos,SEEK_SET)) {
	perror("lseek");
	exit(1);
    }
    /* FIXME: specs say size is _including_ size+type */
    for (off = 0; off < size; off += ssize+4) {
	read(fh,&ssize,sizeof(ssize));
	read(fh,&stype,sizeof(stype));
	ssize = SWAP2(ssize);
	stype = SWAP2(stype);
	str = malloc(ssize+1);
	read(fh,str,ssize);
	str[ssize] = 0;
	printf("\t%d[%d] = %s\n",(int)stype,(int)ssize,str);
	free(str);
    }
    if (off != size) {
	fprintf(stderr,"Huh?  string size mismatch!\n");
	exit(1);
    }
}

static int handle_classic_atom(int fh, off_t pos, off_t size, int depth)
{
    struct classic_atom a;
    uint64_t asize;
    size_t off;
    int i;

    if (-1 == lseek(fh,pos,SEEK_SET)) {
	perror("lseek");
	exit(1);
    }
    if (sizeof(a) != read(fh,&a,sizeof(a))) {
	perror("read");
	exit(1);
    }
    swap_classic_atom(&a);
    switch (a.size) {
    case 0:
	asize = size;
	off   = 8;
	break;
    case 1:
	asize = a.extsize;
	off   = 16;
	break;
    default:
	asize = a.size;
	off   = 8;
    }
    printf("0x%08" PRIx64 " 0x%08" PRIx64 " %*s%s\n",
	   (int64_t)pos,(int64_t)asize,depth,"",strfcc(a.type));
    switch (a.type) {
    case a_dinf:
    case a_edts:
    case a_mdia:
    case a_minf:
    case a_moov:
    case a_stbl:
    case a_trak:
    case a_udta:
	while (off < asize)
	    off += handle_classic_atom(fh,pos+off,asize-off,depth+3);
	if (off != asize) {
	    fprintf(stderr,"Huh?  atom size mismatch!\n");
	    exit(1);
	}
	break;
    default:
	if (169 == ((a.type >> 24) & 0xff)) {
	    dump_string(fh,pos+off,asize-off);
	} else {
	    for (i = 0; alist[i].type != 0; i++)
		if (alist[i].type == a.type)
		    break;
	    if (alist[i].type != 0)
		dump_fields(fh,pos+off,alist[i].fields);
	}
    }
    return asize;
}

#if 0
static int handle_qt_atom(int fh, off_t pos, off_t size, int depth)
{
    return 0;
}
#endif

/* ------------------------------------------------------------------ */

static void
usage(char *prog)
{
    char *h;

    if (NULL != (h = strrchr(prog,'/')))
	prog = h+1;
    fprintf(stderr,
	    "%s - dump structure of quicktime files\n"
	    "\n"
	    "usage:  %s [ -j ] [ -e ] filename\n"
	    "options:\n"
	    "  -h  this text\n"
	    "  -v  increase verbose level\n"
	    "\n",
	    prog,prog);
}

int main(int argc, char *argv[])
{
    int   fh;
    off_t off,size;

    int c;

    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hv")))
	    break;
	switch (c) {
	case 'v':
	    verbose++;
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

    setlocale(LC_ALL,NULL);
    fh = open(argv[optind],O_RDONLY);
    if (-1 == fh) {
	fprintf(stderr,"open %s: %s\n",argv[optind],strerror(errno));
	exit(1);
    }
    size = lseek(fh,0,SEEK_END);
    for (off = 0; off < size;)
	off += handle_classic_atom(fh,off,size,0);
    if (off != size) {
	fprintf(stderr,"Huh?  File size mismatch!\n");
	exit(1);
    }
    return 0;
}
