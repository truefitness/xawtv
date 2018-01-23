#define EVENT_ARGV_SIZE 16

struct event_entry {
    /* the entry */
    char event[128];
    char action[128];

    /* pre-parsed action for do_command */
    char argbuf[128];
    int  argc;
    char *argv[EVENT_ARGV_SIZE];

    /* linked list */
    struct event_entry *next;
};

int event_register(char *event, char *action);
int event_register_list(struct event_entry *entry);

void event_readconfig(void);
void event_writeconfig(FILE *fp);

int event_dispatch(char *event);
