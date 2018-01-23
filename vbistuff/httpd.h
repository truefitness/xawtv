#include <sys/stat.h>

#define STATE_READ_HEADER  1
#define STATE_PARSE_HEADER 2
#define STATE_WRITE_HEADER 3
#define STATE_WRITE_BODY   4
#define STATE_FINISHED     7

#define STATE_KEEPALIVE    8
#define STATE_CLOSE        9

#define MAX_HEADER 4096
#define BR_HEADER   512

struct REQUEST {
    int		fd;		     /* socket handle */
    int		state;		     /* what to to ??? */
    time_t      ping;                /* last read/write (for timeouts) */
    int         keep_alive;

#ifdef HAVE_SOCKADDR_STORAGE
    struct sockaddr_storage peer;    /* client (log) */
#else
    struct sockaddr peer;
#endif
    char        peerhost[65];
    char        peerserv[9];

    /* request */
    char	hreq[MAX_HEADER+1];  /* request header */
    int 	lreq;		     /* request length */
    int         hdata;               /* data in hreq */
    char        type[16];            /* req type */
    char	uri[1024];           /* req uri */
    char        hostname[65];        /* hostname */
    char	path[1024];          /* file path */
    int         major,minor;         /* http version */

    /* response */
    int         status;              /* status code (log) */
    int         bc;                  /* byte counter (log) */
    char	hres[MAX_HEADER+1];  /* response header */
    int		lres;		     /* header length */
    char        *mime;               /* mime type */
    char	*body;
    int         lbody;
    int         free_the_mallocs;
    int         head_only;
    off_t       written;

    /* linked list */
    struct REQUEST *next;
};

/* --- alevtd.c ------------------------------------------------- */

extern int    debug;
extern int    tcp_port;
extern char   *server_name;
extern int    canonicalhost;
extern char   server_host[];
extern time_t now,start;

extern struct vbi_state *vbi;

void xperror(int loglevel, char *txt, char *peerhost);
void xerror(int loglevel, char *txt, char *peerhost);

/* --- request.c ------------------------------------------------ */

void read_request(struct REQUEST *req, int pipelined);
void parse_request(struct REQUEST *req);

/* --- response.c ----------------------------------------------- */

void mkerror(struct REQUEST *req, int status, int ka);
void mkredirect(struct REQUEST *req);
void mkheader(struct REQUEST *req, int status, time_t mtime);
void write_request(struct REQUEST *req);

/* --- page.c ----------------------------------------------- */

void buildpage(struct REQUEST *req);
