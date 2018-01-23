#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#ifdef ATHENA
# include <X11/Xaw/AsciiText.h>
#endif
#ifdef MOTIF
# include <Xm/Text.h>
#endif

#include "complete.h"

#ifndef MIN
#define MIN(x,y)   ((x<y)?(x):(y))
#endif
#define PERROR(str)      fprintf(stderr,"%s:%d: %s: %s\n",__FILE__,__LINE__,str,strerror(errno))

#undef DEBUG

extern Display *dpy;

/*-------------------------------------------------------------------------*/

static int
my_cmp(const void *a, const void *b)
{
    char           *aa, *bb;

    aa = *(char **) a;
    bb = *(char **) b;
    return strcmp(aa, bb);
}

static int
my_scandir(char *dir, char *match, char ***namelist)
{
    DIR            *d;
    struct dirent  *e;
    int             n = 0, len = strlen(match);

    *namelist = NULL;
    if (NULL == (d = opendir(dir))) {
	return 0;
    }
    while (NULL != (e = readdir(d))) {
	if (0 != strncmp(e->d_name, match, len))
	    continue;
#if 0
	fprintf(stderr, "%s %s\n", e->d_name, match);
#endif
	if (0 == (n % 16)) {
	    if (0 == n)
		*namelist = malloc(16 * sizeof(**namelist));
	    else
		*namelist = realloc(*namelist, (n + 16) * sizeof(**namelist));
	}
	(*namelist)[n] = malloc(strlen(e->d_name) + 1);
	strcpy((*namelist)[n], e->d_name);
	n++;
    }
    closedir(d);
    qsort(*namelist, n, sizeof(**namelist), my_cmp);
    return n;
}

void
CompleteAction(Widget widget,
	       XEvent * event,
	       String * params,
	       Cardinal * num_params)
{
    static char     thisdir[] = ".", rootdir[] = "/", *file;
    char           *fn, *fn2, *expand, *dir;
    char            filename[513];
    char          **list;
    struct stat     st;
    int             i, n, len;
#ifdef ATHENA
    XawTextPosition pos;
#endif
#ifdef MOTIF
    XmTextPosition  pos;
#endif
    struct passwd  *pw;
    char           *user, pwmatch[32];	/* anybody with more than 32 char uid ? */

#ifdef ATHENA
    XtVaGetValues(widget,
		  XtNstring, &fn,
		  NULL);
    pos = XawTextGetInsertionPoint(widget);
#endif
#ifdef MOTIF
    fn  = XmTextGetString(widget);
    pos = XmTextGetInsertionPosition(widget);
#endif
    fn2 = strdup(fn+pos);
    fn[pos] = 0;
    expand = tilde_expand(fn);
    if (!expand)
	/* failsave */
	return;

    list = NULL;
    memset(filename, 0, 513);

    if (expand[0] == '~') {
	/* try user name complete */
	if (strchr(expand, '/'))
	    /* ...but not if there is a slash */
	    return;
	user = &expand[1];
	len = strlen(user);
	n = 0;
	while ((pw = getpwent()) != NULL) {
	    if (!strncmp(user, pw->pw_name, len)) {
#ifdef DEBUG
		puts(pw->pw_name);
#endif
		if (0 == n) {
		    strcpy(pwmatch, pw->pw_name);
		} else {
		    for (i = len; !strncmp(pw->pw_name, pwmatch, i); i++);
		    pwmatch[i - 1] = 0;
#ifdef DEBUG
		    printf("%i: %s\n", i, pwmatch);
#endif
		}
		n++;
	    }
	}
	endpwent();
	if (0 == n) {
	    /* no match */
	    XBell(dpy, 100);
	    strcpy(filename, expand);
	} else if (1 == n) {
	    sprintf(filename, "~%s/", pwmatch);
	} else {
	    sprintf(filename, "~%s", pwmatch);
	}

    } else {
	/* try file name complete */
	file = strrchr(expand, '/');
	if (file) {
	    if (file == expand) {
		dir = rootdir;
	    } else {
		dir = expand;
	    }
	    *file = '\0';
	    file++;
	} else {
	    file = expand;
	    dir = thisdir;
	}
#ifdef DEBUG
	printf("%s %s\n", dir, file);
#endif

	n = my_scandir(dir, file, &list);
#if 0
	for (i = 0; i < n; i++) {
	    printf("--> %s\n", list[i]);
	}
#endif
	if (-1 == n) {
	    /* Oops, maybe permission denied ??? */
	    PERROR("scandir");
	    strcpy(filename, fn);
	} else if (0 == n) {
	    /* no match */
	    XBell(dpy, 100);
	    strcpy(filename, fn);
	} else if (1 == n) {
	    /* one match */
	    sprintf(filename, "%s/%s", dir, list[0]);
	    stat(filename, &st);
	    if (strchr(fn, '/')) {
		strcpy(filename, fn);
		*(strrchr(filename, '/') + 1) = '\0';
		strcat(filename, list[0]);
	    } else {
		strcpy(filename, list[0]);
	    }
	    if (S_ISDIR(st.st_mode))
		strcat(filename, "/");
	    else
		strcat(filename, " ");
	} else {
	    /* more than one match */
	    len = MIN(strlen(list[0]), strlen(list[n - 1]));
	    for (i = 0; !strncmp(list[0], list[n - 1], i + 1) && i <= len; i++);
	    if (strchr(fn, '/')) {
		strcpy(filename, fn);
		*(strrchr(filename, '/') + 1) = '\0';
		strncat(filename, list[0], i);
	    } else {
		strncpy(filename, list[0], i);
	    }
	}
    }

#ifdef DEBUG
    printf("result: `%s'\n", filename);
#endif
    pos = strlen(filename);
    strcat(filename,fn2);
#ifdef ATHENA
    XtVaSetValues(widget,
		  XtNstring, filename,
		  NULL);
    XawTextSetInsertionPoint(widget,pos);
#endif
#ifdef MOTIF
    XmTextSetString(widget,filename);
    XmTextSetInsertionPosition(widget,pos);
#endif

    if (list) {
	for (i = 0; i < n; i++)
	    free(list[i]);
	free(list);
    }
    free(expand);
    return;
}

/*-------------------------------------------------------------------------*/

char*
tilde_expand(char *file)
{
    char           *ret, *user;
    struct passwd  *pw;
    int             len;

    if (!file)
	return NULL;

#ifdef DEBUG
    printf("tilde_expand: in : `%s'\n", file);
#endif
    if (!(file[0] == '~' && strchr(file, '/'))) {
	ret = strdup(file);
    } else {
	if (file[1] == '/') {
	    pw = getpwuid(getuid());
	} else {
	    user = strdup(&file[1]);
	    *(strchr(user, '/')) = '\0';
	    pw = getpwnam(user);
	    free(user);
	}
	if (pw == NULL) {
	    ret = strdup(file);
	} else {
#ifdef DEBUG
	    printf("tilde_expand: pw : %s=%s\n", pw->pw_name, pw->pw_dir);
#endif
	    ret = malloc(strlen(file) + strlen(pw->pw_dir));
	    sprintf(ret, "%s%s", pw->pw_dir, strchr(file, '/'));
	}
    }
    /* trim */
    len = strlen(ret);
    while (ret[len - 1] == ' ')
	ret[len - 1] = '\0', len--;
#ifdef DEBUG
    printf("tilde_expand: out: `%s'\n", ret);
#endif
    return ret;
}
