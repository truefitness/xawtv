#ifdef HAVE_ALSA

#ifdef HAVE_ALSA_ASOUNDLIB_H
# include <alsa/asoundlib.h>
#else
# include <sys/asoundlib.h>
#endif

struct midi_handle {
    snd_seq_t *seq;
    int fd;
    int port;
    snd_seq_event_t *ev;
};

int midi_open(struct midi_handle *h, char *name);
int midi_close(struct midi_handle *h);
int midi_connect(struct midi_handle *h, char *arg);
int midi_read(struct midi_handle *h);

void midi_translate(struct midi_handle *h);

#endif
