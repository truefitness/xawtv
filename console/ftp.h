extern int ftp_debug;

struct ftp_state* ftp_init(char *name, int autologin, int passive, int debug);
void ftp_send(struct ftp_state* s, int argc, ...);
int  ftp_recv(struct ftp_state* s);
void ftp_connect(struct ftp_state* s, char *host,
		 char *user, char *pass, char *dir);
int  ftp_connected(struct ftp_state *s);
void ftp_upload(struct ftp_state* s, char *local, char *remote, char *tmp);
void ftp_fini(struct ftp_state *s);
