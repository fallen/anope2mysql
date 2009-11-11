#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/param.h>

typedef int16_t int16;
typedef u_int16_t uint16;
typedef int32_t int32;
typedef u_int32_t uint32;

typedef struct nickalias_ NickAlias;
typedef struct nickcore_ NickCore;
typedef struct user_ User;
typedef struct ModuleData_ ModuleData;                  /* ModuleData struct */
typedef struct memo_ Memo;

#define NS_TEMPORARY	0xFF00      /* All temporary status flags */
#define NS_VERBOTEN	0x0002      /* Nick may not be registered or used */
#define NICKMAX		32
#define getc_db(f)		(fgetc((f)->fp))
#define SLISTF_NODUP	0x00000001		/* No duplicates in the list. */
#define SLISTF_SORT 	0x00000002		/* Automatically sort the list. Used with compareitem member. */
#define SLIST_DEFAULT_LIMIT 32767
#define read_db(f,buf,len)      (fread((buf),1,(len),(f)->fp))
#define read_buffer(buf,f)      (read_db((f),(buf),sizeof(buf)) == sizeof(buf))

int CSMaxReg = 10;

struct memo_ {
    uint32 number;      /* Index number -- not necessarily array position! */
    uint16 flags;
    time_t time;        /* When it was sent */
    char sender[NICKMAX];
    char *text;
    ModuleData *moduleData;     /* Module saved data attached to the Memo */
};


char s_NickServ[] = "Themis";
char NickDBName[] = "./dbs/nick.db";

#define NICK_VERSION 		14


typedef struct dbFILE_ dbFILE;
struct dbFILE_ {
    int mode;                   /* 'r' for reading, 'w' for writing */
    FILE *fp;                   /* The normal file descriptor */
    FILE *backupfp;             /* Open file pointer to a backup copy of
                                 *    the database file (if non-NULL) */
    char filename[MAXPATHLEN];  /* Name of the database file */
    char backupname[MAXPATHLEN];        /* Name of the backup file */
};

typedef struct slist_ SList;
typedef struct slistopts_ SListOpts;

struct slist_ {
        void **list;

        int16 count;            /* Total entries of the list */
        int16 capacity;         /* Capacity of the list */
        int16 limit;            /* Maximum possible entries on the list */

        SListOpts *opts;
};

struct slistopts_ {
        int32 flags;            /* Flags for the list. See below. */

        int  (*compareitem)     (SList *slist, void *item1, void *item2);       /* Called to compare two items */
        int  (*isequal)     (SList *slist, void *item1, void *item2);   /* Called by slist_indexof. item1 can be an arbitrary pointer. */
        void (*freeitem)        (SList *slist, void *item);                                     /* Called when an item is removed */
};

struct nickalias_ {
        NickAlias *next, *prev;
        char *nick;                             /* Nickname */
        char *last_quit;                        /* Last quit message */
        char *last_realname;                    /* Last realname */
        char *last_usermask;                    /* Last usermask */
        time_t time_registered;                 /* When the nick was registered */
        time_t last_seen;                       /* When it was seen online for the last time */
        uint16 status;                          /* See NS_* below */
        NickCore *nc;                           /* I'm an alias of this */
        /* Not saved */
        ModuleData *moduleData;                 /* Module saved data attached to the nick alias */
        User *u;                                /* Current online user that has me */
};

typedef struct {
    int16 memocount, memomax;
    Memo *memos;
} MemoInfo;

#define PASSMAX		32

struct nickcore_ {
        NickCore *next, *prev;

        char *display;                          /* How the nick is displayed */
        char pass[PASSMAX];                             /* Password of the nicks */
        char *email;                            /* E-mail associated to the nick */
        char *greet;                            /* Greet associated to the nick */
        uint32 icq;                             /* ICQ # associated to the nick */
        char *url;                              /* URL associated to the nick */
        uint32 flags;                           /* See NI_* below */
        uint16 language;                        /* Language selected by nickname owner (LANG_*) */
        uint16 accesscount;                     /* # of entries */
        char **access;                          /* Array of strings */
        MemoInfo memos;
        uint16 channelcount;                    /* Number of channels currently registered */
        uint16 channelmax;                      /* Maximum number of channels allowed */

        /* Unsaved data */
        ModuleData *moduleData;         /* Module saved data attached to the NickCore */
        time_t lastmail;                        /* Last time this nick record got a mail */
        SList aliases;                          /* List of aliases */
};

NickCore *nclists[1024];
NickAlias *nalists[1024];

static SListOpts slist_defopts = { 0, NULL, NULL, NULL };

void *srealloc(void *oldptr, long newsize)
{
    void *buf;

    if (!newsize) {
        newsize = 1;
    }
    buf = realloc(oldptr, newsize);
    if (!buf)
#ifndef _WIN32
        raise(SIGUSR1);
#else
        abort();
#endif
    return buf;
}

void *scalloc(long elsize, long els)
{
    void *buf;

    if (!elsize || !els) {
        elsize = els = 1;
    }
    buf = calloc(elsize, els);
    if (!buf)
#ifndef _WIN32
        raise(SIGUSR1);
#else
        abort();
#endif
    return buf;
}

char *strscpy(char *d, const char *s, size_t len)
{
    char *d_orig = d;

    if (!len) {
        return d;
    }
    while (--len && (*d++ = *s++));
    *d = '\0';
    return d_orig;
}

int read_int16(uint16 * ret, dbFILE * f)
{
    int c1, c2;

    c1 = fgetc(f->fp);
    c2 = fgetc(f->fp);
    if (c1 == EOF || c2 == EOF)
        return -1;
    *ret = c1 << 8 | c2;
    return 0;
}

int read_int32(uint32 * ret, dbFILE * f)
{
    int c1, c2, c3, c4;

    c1 = fgetc(f->fp);
    c2 = fgetc(f->fp);
    c3 = fgetc(f->fp);
    c4 = fgetc(f->fp);
    if (c1 == EOF || c2 == EOF || c3 == EOF || c4 == EOF)
        return -1;
    *ret = c1 << 24 | c2 << 16 | c3 << 8 | c4;
    return 0;
}

int read_string(char **ret, dbFILE * f)
{
    char *s;
    uint16 len;

    if (read_int16(&len, f) < 0)
        return -1;
    if (len == 0) {
        *ret = NULL;
        return 0;
    }
    s = scalloc(len, 1);
    if (len != fread(s, 1, len, f->fp)) {
        free(s);
        return -1;
    }
    *ret = s;
    return 0;
}




static dbFILE *open_db_read(const char *service, const char *filename)
{
    dbFILE *f;
    FILE *fp;

    f = scalloc(sizeof(*f), 1);
    if (!f) {
        return NULL;
    }
    strscpy(f->filename, filename, sizeof(f->filename));
    f->mode = 'r';
    fp = fopen(f->filename, "rb");
    if (!fp) {
        int errno_save = errno;

        free(f);
        errno = errno_save;
        return NULL;
    }
    f->fp = fp;
    f->backupfp = NULL;
    return f;
}

dbFILE *open_db(const char *service, const char *filename,
                uint32 version)
{
        return open_db_read(service, filename);
}

int NSAllowKillImmed = 0;
#define NI_KILL_IMMED		0x00000800  /* Kill immediately instead of in 60 sec */
#define NI_SERVICES_ADMIN	0x00002000  /* User is a Services admin */
#define NI_SERVICES_ROOT        0x00008000  /* User is a Services root */

#define SAFE(x) do {					\
    if ((x) < 0) {					\
	printf("Read error on %s", NickDBName);	\
	failed = 1;					\
	break;						\
    }							\
} while (0)

int get_file_version(dbFILE * f)
{
    FILE *fp = f->fp;
    int version =
        fgetc(fp) << 24 | fgetc(fp) << 16 | fgetc(fp) << 8 | fgetc(fp);
    if (ferror(fp)) {
#ifndef NOT_MAIN
        printf("Error reading version number on %s", f->filename);
#endif
        return 0;
    } else if (feof(fp)) {
#ifndef NOT_MAIN
        printf("Error reading version number on %s: End of file detected",
             f->filename);
#endif
        return 0;
    } else if (version < 1) {
#ifndef NOT_MAIN
        printf("Invalid version number (%d) on %s", version, f->filename);
#endif
        return 0;
    }
    return version;
}

int slist_setcapacity(SList * slist, int16 capacity)
{
    if (slist->capacity == capacity)
        return 1;
    slist->capacity = capacity;
    if (slist->capacity)
        slist->list =
            srealloc(slist->list, sizeof(void *) * slist->capacity);
    else {
        free(slist->list);
        slist->list = NULL;
    }
    if (slist->capacity < slist->count)
        slist->count = slist->capacity;
    return 1;
}

int slist_indexof(SList * slist, void *item)
{
    int16 i;
    void *entry;

    if (slist->count == 0)
        return -1;

    for (i = 0, entry = slist->list[0]; i < slist->count;
         i++, entry = slist->list[i]) {
        if ((slist->opts
             && slist->opts->isequal) ? (slist->opts->isequal(slist, item,
                                                              entry))
            : (item == entry))
            return i;
    }

    return -1;
}

int slist_add(SList * slist, void *item)
{
    if (slist->limit != 0 && slist->count >= slist->limit)
        return -2;
    if (slist->opts && (slist->opts->flags & SLISTF_NODUP)
        && slist_indexof(slist, item) != -1)
        return -3;
    if (slist->capacity == slist->count)
        slist_setcapacity(slist, slist->capacity + 1);

    if (slist->opts && (slist->opts->flags & SLISTF_SORT)
        && slist->opts->compareitem) {
        int i;

        for (i = 0; i < slist->count; i++) {
            if (slist->opts->compareitem(slist, item, slist->list[i]) <= 0) {
                memmove(&slist->list[i + 1], &slist->list[i],
                        sizeof(void *) * (slist->count - i));
                slist->list[i] = item;
                break;
            }
        }

        if (i == slist->count)
            slist->list[slist->count] = item;
    } else {
        slist->list[slist->count] = item;
    }

    return slist->count++;
}

void slist_init(SList * slist)
{
    memset(slist, 0, sizeof(SList));
    slist->limit = SLIST_DEFAULT_LIMIT;
    slist->opts = &slist_defopts;
}
#define HASH(nick)	((tolower((nick)[0])&31)<<5 | (tolower((nick)[1])&31))

char *sstrdup(const char *src)
{
    char *ret = NULL;
    if (src) {
#ifdef __STRICT_ANSI__
        if ((ret = (char *) malloc(strlen(src) + 1))) {;
            strcpy(ret, src);
        }
#else
        ret = strdup(src);
#endif
        if (!ret)
#ifndef _WIN32
            raise(SIGUSR1);
#else
            abort();
#endif
    } else {
        printf("sstrdup() called with NULL-arg");
    }

    return ret;
}

int stricmp(const char *s1, const char *s2)
{
    register int c;

    while ((c = tolower(*s1)) == tolower(*s2)) {
        if (c == 0)
            return 0;
        s1++;
        s2++;
    }
    if (c < tolower(*s2))
        return -1;
    return 1;
}

NickCore *findcore(const char *nick)
{
    NickCore *nc;

    if (!nick || !*nick) {
        printf("debug: findcore() called with NULL values");
        return NULL;
    }

    for (nc = nclists[HASH(nick)]; nc; nc = nc->next) {
        if (stricmp(nc->display, nick) == 0)
            return nc;
    }

    return NULL;
}

void load_ns_dbase(void)
{
    dbFILE *f;
    int ver, i, j, c;
    NickAlias *na, **nalast, *naprev;
    NickCore *nc, **nclast, *ncprev;
    int failed = 0;
    uint16 tmp16;
    uint32 tmp32;
    char *s, *pass;

    if (!(f = open_db(s_NickServ, NickDBName, NICK_VERSION)))
        return;

    ver = get_file_version(f);

    if (ver <= 11) {
//        close_db(f);
//        load_old_ns_dbase();
        printf("old database gtfo !\n");
        return;
    }

    /* First we load nick cores */
    for (i = 0; i < 1024 && !failed; i++) {
    
    
        nclast = &nclists[i];
        ncprev = NULL;

        while ((c = getc_db(f)) == 1) {
            if (c != 1)
                printf("Invalid format in %s", NickDBName);

            nc = scalloc(1, sizeof(NickCore));
            *nclast = nc;
            nclast = &nc->next;
            nc->prev = ncprev;
            ncprev = nc;

            slist_init(&nc->aliases);

            SAFE(read_string(&nc->display, f));
            printf("%s", nc->display);
            if (ver < 14) {
                SAFE(read_string(&pass, f));
                if (pass) {
                    memset(nc->pass, 0, PASSMAX);
                    memcpy(nc->pass, pass, strlen(pass));
                } else
                    memset(nc->pass, 0, PASSMAX);
            } else
                SAFE(read_buffer(nc->pass, f));
//            printf(" %s", nc->pass);
            SAFE(read_string(&nc->email, f));
//            printf(" %s", nc->email);
            SAFE(read_string(&nc->greet, f));
//            printf(" %s", nc->greet);
            SAFE(read_int32(&nc->icq, f));
//            printf(" %d", nc->icq);
            SAFE(read_string(&nc->url, f));
//	    printf(" %s\n", nc->url);
            SAFE(read_int32(&nc->flags, f));
            if (!NSAllowKillImmed)
                nc->flags &= ~NI_KILL_IMMED;
            SAFE(read_int16(&nc->language, f));

            /* Add services opers and admins to the appropriate list, but
               only if the database version is more than 10. */
/*            if (nc->flags & NI_SERVICES_ADMIN)
                slist_add(&servadmins, nc);
            if (nc->flags & NI_SERVICES_OPER)
                slist_add(&servopers, nc); */ 
                
// OSEF des axx Sop et Sadmin !

            SAFE(read_int16(&nc->accesscount, f));
            if (nc->accesscount) {
                char **access;
                access = scalloc(sizeof(char *) * nc->accesscount, 1);
                nc->access = access;
                for (j = 0; j < nc->accesscount; j++, access++)
                    SAFE(read_string(access, f));
            }

            SAFE(read_int16(&tmp16, f));
            nc->memos.memocount = (int16) tmp16;
            SAFE(read_int16(&tmp16, f));
            nc->memos.memomax = (int16) tmp16;
            if (nc->memos.memocount) {
                Memo *memos;
                memos = scalloc(sizeof(Memo) * nc->memos.memocount, 1);
                nc->memos.memos = memos;
                for (j = 0; j < nc->memos.memocount; j++, memos++) {
                    SAFE(read_int32(&memos->number, f));
                    SAFE(read_int16(&memos->flags, f));
                    SAFE(read_int32(&tmp32, f));
                    memos->time = tmp32;
                    SAFE(read_buffer(memos->sender, f));
                    SAFE(read_string(&memos->text, f));
                    memos->moduleData = NULL;
                }
            }

            SAFE(read_int16(&nc->channelcount, f));
            SAFE(read_int16(&tmp16, f));
            nc->channelmax = CSMaxReg;

            if (ver < 13) {
                /* Used to be dead authentication system */
                SAFE(read_int16(&tmp16, f));
                SAFE(read_int32(&tmp32, f));
                SAFE(read_int16(&tmp16, f));
                SAFE(read_string(&s, f));
            }

        }                       /* while (getc_db(f) != 0) */
        *nclast = NULL;
    }                           /* for (i) */

    for (i = 0; i < 1024 && !failed; i++) {
        nalast = &nalists[i];
        naprev = NULL;
        while ((c = getc_db(f)) == 1) {
            if (c != 1)
                printf("Invalid format in %s", NickDBName);

            na = scalloc(1, sizeof(NickAlias));

            SAFE(read_string(&na->nick, f));

            SAFE(read_string(&na->last_usermask, f));
            SAFE(read_string(&na->last_realname, f));
            SAFE(read_string(&na->last_quit, f));

            SAFE(read_int32(&tmp32, f));
            na->time_registered = tmp32;
            SAFE(read_int32(&tmp32, f));
            na->last_seen = tmp32;
            SAFE(read_int16(&na->status, f));
            na->status &= ~NS_TEMPORARY;

            SAFE(read_string(&s, f));
            na->nc = findcore(s);
            free(s);

            slist_add(&na->nc->aliases, na);

            if (!(na->status & NS_VERBOTEN)) {
                if (!na->last_usermask)
                    na->last_usermask = sstrdup("");
                if (!na->last_realname)
                    na->last_realname = sstrdup("");
            }

            na->nc->flags &= ~NI_SERVICES_ROOT;

            *nalast = na;
            nalast = &na->next;
            na->prev = naprev;
            naprev = na;

        }                       /* while (getc_db(f) != 0) */

        *nalast = NULL;
    }                           /* for (i) */

//    close_db(f);
// nevermind wasting memory

    for (i = 0; i < 1024; i++) {
        NickAlias *next;

        for (na = nalists[i]; na; na = next) {
            next = na->next;
            /* We check for coreless nicks (although it should never happen) */
            if (!na->nc) {
                printf("%s: while loading database: %s has no core! We delete it (here just ignore it !).", s_NickServ, na->nick);
//                delnick(na);
                continue;
            }

            /* Add the Services root flag if needed. */
/*            for (j = 0; j < RootNumber; j++)
                if (!stricmp(ServicesRoots[j], na->nick))
                    na->nc->flags |= NI_SERVICES_ROOT; */
// OSEF de savoir si Paul Pierre ou Jacques est Services Root !                    
                    
        }
    }
}

#undef SAFE

int main(void) {

	printf("Anope2MySQL\n");
	printf("by Fallen for EpiKnet IRC Network\n\n");

	printf("Loading Themis (nicknames) database ...");
	load_ns_dbase();
	printf(" [DONE]\n");
	
	

	return 0;
}
