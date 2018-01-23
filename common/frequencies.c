#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#include "grab-ng.h"
#include "commands.h"
#include "frequencies.h"

/* --------------------------------------------------------------------- */

int               chantab = -1;
struct CHANLISTS  *chanlists;
struct STRTAB     *chanlist_names;


/* --------------------------------------------------------------------- */

void freq_init(void)
{
    char line[256],value[256];
    FILE *fp;
    int nr,i,j;

    if (NULL == (fp = fopen(DATADIR "/Index.map","r"))) {
	perror("open " DATADIR "/Index.map");
	exit(1);
    }
    if (debug)
	fprintf(stderr,"freq: reading " DATADIR "/Index.map\n");

    nr = 0;
    i  = 0;
    while (NULL != fgets(line,255,fp)) {
	nr++;
	if (line[0] == '\n' || line[0] == '#' || line[0] == '%')
	    continue;
	if (1 == sscanf(line,"[%255[^]]]",value)) {
	    /* [section] */
	    chanlists = realloc(chanlists, (i+2) * sizeof(struct CHANLISTS));
	    memset(chanlists+i, 0, 2*sizeof(struct CHANLISTS));
	    chanlists[i].name = strdup(value);
	    i++;
	    continue;
	}
	if (NULL == chanlists) {
	    fprintf(stderr,"%s:%d: error: no section\n",
		    DATADIR "/Index.map",nr);
	    continue;
	}

	if (1 == sscanf(line," file = %255[^\n]",value)) {
	    /* file = <filename> */
	    chanlists[i-1].filename = strdup(value);
	    continue;
	}

	/* Huh ? */
	fprintf(stderr,"%s:%d: syntax error\n",
		DATADIR "/Index.map",nr);
    }
    fclose(fp);

    chanlist_names = malloc((i+1) * sizeof(struct STRTAB));
    for (j = 0; j < i; j++) {
	chanlist_names[j].nr  = j;
	chanlist_names[j].str = chanlists[j].name;
    }
    chanlist_names[j].nr  = -1;
    chanlist_names[j].str = NULL;
}

/* --------------------------------------------------------------------- */

static int freq_readlist(struct CHANLIST **list, int n, char *name)
{
    char line[256],value[256];
    char filename[256];
    FILE *fp;
    int nr;

    sprintf(filename,"%s/%s",DATADIR,name);
    if (NULL == (fp = fopen(filename,"r"))) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	exit(1);
    }
    if (debug)
	fprintf(stderr,"freq: reading %s\n",filename);

    nr = 0;
    while (NULL != fgets(line,255,fp)) {
	nr++;
	if (1 == sscanf(line,"# include \"%[^\"]\"",value)) {
	    /* includes */
	    n = freq_readlist(list,n,value);
	    continue;
	}
	if (line[0] == '\n' || line[0] == '#' || line[0] == '%') {
	    /* ignore */
	    continue;
	}
	if (1 == sscanf(line,"[%255[^]]]",value)) {
	    /* [section] */
	    if (0 == (n % 16)) {
		*list = realloc(*list, (n+16) * sizeof(struct CHANLIST));
		memset((*list)+n, 0, 16*sizeof(struct CHANLIST));
	    }
	    (*list)[n].name = strdup(value);
	    n++;
	    continue;
	}
	if (0 == n) {
	    fprintf(stderr,"%s:%d: error: no section\n",filename,nr);
	    continue;
	}

	if (1 == sscanf(line," freq = %255[^\n]", value)) {
	    /* freq =  */
	    (*list)[n-1].freq = atoi(value);
	    continue;
	}

	/* Huh ? */
	fprintf(stderr,"%s:%d: syntax error\n", filename, nr);
    }
    fclose(fp);
    return n;
}

void freq_newtab(int n)
{
    if (debug)
	fprintf(stderr,"freq: newtab %d\n",n);

    if (NULL == chanlists[n].list)
	chanlists[n].count =
	    freq_readlist(&chanlists[n].list,0,chanlists[n].filename);
    chantab = n;
}
