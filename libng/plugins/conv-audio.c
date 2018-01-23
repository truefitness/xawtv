#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

#include "grab-ng.h"

/* ---------------------------------------------------------------------- */
/* stuff we need from lame.h                                              */

struct lame_global_struct;
typedef struct lame_global_struct lame_global_flags;

static lame_global_flags* (*lame_init)(void);
static int (*lame_close)(lame_global_flags *);

static int (*lame_set_in_samplerate)(lame_global_flags *, int);
static int (*lame_set_num_channels)(lame_global_flags *, int);
static int (*lame_set_quality)(lame_global_flags *, int);
static int (*lame_init_params)(lame_global_flags * const );

/*
 * num_samples = number of samples in the L (or R)
 * channel, not the total number of samples in pcm[]
 * returns # of output bytes
 * mp3buffer_size_max = 1.25*num_samples + 7200
 */
static int (*lame_encode_buffer_interleaved)(
    lame_global_flags*  gfp,           /* global context handlei          */
    short int           pcm[],         /* PCM data for left and right
					  channel, interleaved            */
    int                 num_samples,   /* number of samples per channel,
					  _not_ number of samples in
					  pcm[]                           */
    unsigned char*      mp3buf,        /* pointer to encoded MP3 stream   */
    int                 mp3buf_size ); /* number of valid octets in this
					  stream                          */
static int (*lame_encode_flush)(
    lame_global_flags *  gfp,    /* global context handle                 */
    unsigned char*       mp3buf, /* pointer to encoded MP3 stream         */
    int                  size);  /* number of valid octets in this stream */

/* ---------------------------------------------------------------------- */
/* simple, portable dynamic linking (call stuff indirectly using          */
/* function pointers)                                                     */

#define SYM(symbol) { .func = (void*)(&symbol), .name = #symbol }
static struct {
    void   **func;
    char   *name;
} symtab[] = {
    SYM(lame_init),
    SYM(lame_close),
    SYM(lame_set_in_samplerate),
    SYM(lame_set_num_channels),
    SYM(lame_set_quality),
    SYM(lame_init_params),
    SYM(lame_encode_buffer_interleaved),
    SYM(lame_encode_flush),
};

static int link_lame(void)
{
    void *handle;
    void *symbol;
    unsigned int i;

    handle = dlopen("libmp3lame.so.0",RTLD_NOW);
    if (NULL == handle)
	return -1;
    for (i = 0; i < sizeof(symtab)/sizeof(symtab[0]); i++) {
	symbol = dlsym(handle,symtab[i].name);
	if (NULL == symbol) {
	    fprintf(stderr,"dlsym(mp3lame,%s): %s\n",
		    symtab[i].name, dlerror());
	    dlclose(handle);
	    return -1;
	}
	*(symtab[i].func) = symbol;
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

struct mp3_enc_state {
    lame_global_flags *gf;
    int first;
};

static void* mp3_enc_init(void *priv)
{
    struct mp3_enc_state *h;

    h = malloc(sizeof(*h));
    if (NULL == h)
	return NULL;
    memset(h,0,sizeof(*h));
    h->gf    = lame_init();
    h->first = 1;
    return h;
}

static struct ng_audio_buf*
mp3_enc_data(void *handle, struct ng_audio_buf *in)
{
    static struct ng_audio_fmt fmt = {
	.fmtid = AUDIO_MP3,
	.rate  = 0,
    };
    struct mp3_enc_state *h = handle;
    struct ng_audio_buf *out;
    int samples, size;

    if (h->first) {
	lame_set_in_samplerate(h->gf, in->fmt.rate);
	lame_set_num_channels(h->gf, ng_afmt_to_channels[in->fmt.fmtid]);
	lame_set_quality(h->gf, 5 /* FIXME */);
	lame_init_params(h->gf);
	h->first = 0;
    }
    samples = in->size >> 2;
    size = 7200 + samples * 5 / 4; /* worst case */
    out = ng_malloc_audio_buf(&fmt, size);

    out->size = lame_encode_buffer_interleaved
	(h->gf, (short int*) in->data, samples, out->data, size);
    free(in);
    return out;
}

static void mp3_enc_fini(void *handle)
{
    struct mp3_enc_state *h = handle;

    lame_close(h->gf);
    free(h);
}

/* ---------------------------------------------------------------------- */

static struct ng_audio_conv mp3_list[] = {
    {
	/* --- compress --- */
	init:           mp3_enc_init,
	frame:          mp3_enc_data,
	fini:           mp3_enc_fini,
	fmtid_in:	AUDIO_S16_NATIVE_STEREO,
	fmtid_out:	AUDIO_MP3,
	priv:		NULL,
    }
};
static const int nconv = sizeof(mp3_list)/sizeof(mp3_list[0]);

/* ---------------------------------------------------------------------- */
/* init stuff                                                             */

extern void ng_plugin_init(void);
void ng_plugin_init(void)
{
    if (0 != link_lame())
	return;
    ng_aconv_register(NG_PLUGIN_MAGIC,__FILE__,mp3_list,nconv);
}
