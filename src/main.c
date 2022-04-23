#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h> 
#include <ctype.h>

#define DWM_PATCHES "/usr/local/src/sites/dwm.suckless.org/patches/"
#define INDEXMD "/index.md"
#define LINEBUF 4096
#define DESCRIPTION_SECTION "Description" 
#define DESCRIPTION_SECTION_LENGTH 12
#define GREP_BIN "/bin/grep"
#define DESCFILE "descfile.XXXXXX"
#define RESULTCACHE "result.XXXXXX"
#define DEVNULL "/dev/null"
#define ASCNULL '\0'
#define OPTTHREAD_COUNT 4
#define OPTWORK_AMOUNT 80
#define MIN_WORKAMOUNT 40
#define ERRPREFIX_LEN 7
#define AVSEARCH_WORD_LEN 5

char *searchstr;
int sstrcnt = 1;

char *
sappend(char *base, char *append) {
    int baselen = strlen(base);
    int pnamelen = strlen(append);
    char *buf = (char *)calloc(baselen + pnamelen + 1, sizeof(char));

    return strncat(buf, append, pnamelen);
}

int
append_patchmd(char **buf, char *patch) {
    char *patchmd = sappend(patch, INDEXMD);
    *buf = sappend(DWM_PATCHES, patchmd);
    free(patchmd);
    return 0;
}

void 
error(const char* err_format, ...) {
    va_list args;
    int errlen;
    char *err;

    va_start(args, err_format);
    errlen = strlen(err_format);
    err = calloc(ERRPREFIX_LEN + errlen + 1, sizeof(char));
    sprintf(err, "error: %s", err_format);

    vfprintf(stderr, err, args);
    vfprintf(stderr, "\n", args);
    free(err);
    va_end(args);
}

void
empty() {}

#define TRY(EXP) (EXP)
#define WITH ? empty() : 
#define EPERROR() error(strerror(errno));
#define OK(RES) RES == 0

void
usage() {
    printf("usage\n");
}

int 
is_line_separator(char *line) {
    return (line[0] & line[1] & line[2]) == '-';
}

struct threadargs {
    char *descfname;
    int descffd;
    int outfd;
    int startpoint;
    int endpoint;
    int result;
    pthread_mutex_t *mutex;
};

typedef struct threadargs lookupthread_args;


char **getsearch_words(){
    char **words;
    char *token;
    char *searchsdup;
    char *delim = " ";
    int sstrlen = strlen(searchstr);
    sstrcnt = !!sstrlen;
    bool prevcharisspace = true;
    searchsdup = strdup(searchstr);
    

    for (int i = 0; i < sstrlen; i++) {
        if (isspace(searchstr[i])) {
            if (!prevcharisspace)
                sstrcnt++;
        } else {
            prevcharisspace = false;
        }
    }

    words = (char **)calloc(sstrcnt, sizeof(char *));
    token = strtok(searchsdup, delim);

    for (int i = 0; token && i < sstrcnt; i++)
    {
        words[i] = strdup(token);
        token = strtok(NULL, delim);
    }

    free(searchsdup);
    return words;
}

int
searchdescr(FILE *descfile) {
    char rch;
    int res = 1;
    char searchbuf[LINEBUF];
    char **swords = NULL;
    int searchlen;
    int matched_toks = 0;

    searchlen = strlen(searchstr);
    if (searchlen <= 0)
        return 0;
    
    if (searchlen > 1) {
        swords = getsearch_words();
    }

    memset(searchbuf, ASCNULL, LINEBUF);

    while ((rch = fgets(searchbuf, LINEBUF, descfile))) {
        if (searchlen == 1) {
            if (strstr(searchbuf, searchstr))
                return 1;
        } else {
            for (int i = 0; i < sstrcnt; i++)
            {
                if (swords[i]) {
                    if (strstr(searchbuf, searchstr)) {
                        matched_toks++;
                        swords[i] = NULL;
                    }
                }
            }
            
        }
    }
    return matched_toks == sstrcnt;
}

char *
tryread_desc(FILE *index, char *buf, bool descrexists) {
    if (descrexists) {
        return fgets(buf, LINEBUF, index);
    }
    return fgets(buf, DESCRIPTION_SECTION_LENGTH, index);
}

int
read_description(char *indexmd, FILE *descfile) {
    FILE *index;
    int res = 1;
    char tempbuf[LINEBUF];
    char linebuf[LINEBUF];
    bool description_exists = false;

    index = fopen(indexmd, "r");
    if (!index)
        return 1;

    memset(linebuf, ASCNULL, LINEBUF);
    memset(tempbuf, ASCNULL, LINEBUF);

    for(int descrlen = 0; 
        tryread_desc(index, linebuf, description_exists) != NULL; 
        descrlen++) {
        if (description_exists) {
            if (is_line_separator(linebuf) && descrlen > 1) {
                break;
            } else {
                if (descrlen > 0)
                    fputs(tempbuf, descfile);
                
                memcpy(tempbuf, linebuf, LINEBUF);
            }
        } else {
            description_exists = !strcmp(linebuf, DESCRIPTION_SECTION);
        }
        memset(linebuf, ASCNULL, LINEBUF);
    }

    if (description_exists) {
        fflush(descfile);
        res = 0;
    }

    fclose(index);
    return res;
}

int isdir(struct dirent *dir) {
    struct stat dst;
    if (!dir || !dir->d_name)
        return 0;

    if (dir->d_name[0] == '.') 
        return 0;

    switch (dir->d_type)
    {
    case DT_DIR:
        return 1;
    case DT_UNKNOWN: 
        if (OK(stat(dir->d_name, &dst)))
            return S_ISDIR(dst.st_mode);
        
        return 0;  
    default:
        return 0;
    }
}

void 
lock_if_multithreaded(pthread_mutex_t *mutex) {
    if (mutex)
        pthread_mutex_lock(mutex);
}

void 
unlock_if_multithreaded(pthread_mutex_t *mutex) {
    if (mutex)
        pthread_mutex_unlock(mutex);
}

int
lookup_entries_args(char *descfname, int descffd, int startpoint, int endpoint, 
                        int outfd, pthread_mutex_t *fmutex) {
    DIR *pd;
    FILE *descfile, *rescache;
    struct dirent *pdir;

    printf("\nStart point: %d", startpoint);
    printf("\nEnd point: %d", endpoint);
    printf("\ndescfd: %d", descffd);

    TRY(rescache = fdopen(outfd, "w")) 
        WITH error("Failed to open result cache file");
    TRY(pd = opendir(DWM_PATCHES)) 
        WITH error("Failed to open patch dir");
    
    while ((pdir = readdir(pd)) != NULL) {
        if (isdir(pdir)) {
            char *indexmd = NULL; 
            int grep, grepst;
            char dch;

            append_patchmd(&indexmd, pdir->d_name);
            TRY(descfile = fopen(descfname, "w+")) 
                WITH error("Failed to open descfile");
            
            if (OK(read_description(indexmd, descfile))) {
                int searchres;

                searchres = searchdescr(descfile);
                lock_if_multithreaded(fmutex);
                fseek(descfile, 0, SEEK_SET);

                if (OK(searchres)) {
                    fprintf(rescache, "\n%s:\n", pdir->d_name);
                    while((dch = fgetc(descfile)) != EOF) {
                        fputc(dch, rescache);
                    }
                }

                unlock_if_multithreaded(fmutex);
            }
            free(indexmd);
            fclose(descfile);
        }
    }

    fclose(rescache);
    closedir(pd);
    remove(descfname);
    return 0;
}

int 
lookup_entries(lookupthread_args *args) {
    return lookup_entries_args(args->descfname, args->descffd, args->startpoint, 
        args->endpoint, args->outfd, args->mutex);
}

void *
search_entry(void *thread_args) {
    lookupthread_args *args = (lookupthread_args *)thread_args;
    args->result = lookup_entries(args);
    return NULL;
}

int
worth_multithread(int entrycount) {
    printf("\nEntrycount: %d\n", entrycount);
    //return entrycount >= ((OPTWORK_AMOUNT * 2) - (OPTWORK_AMOUNT - (OPTWORK_AMOUNT / 4)));
    return 0;
}

int
calc_threadcount(int entrycnt) {
    int optentrycnt = OPTWORK_AMOUNT * OPTTHREAD_COUNT;
    if (entrycnt > optentrycnt) {
        return OPTTHREAD_COUNT;
    } else {
        float actualthrcount = entrycnt / OPTWORK_AMOUNT;
        int nearest_thrcount = floor((double)actualthrcount);
        int workdiff = (actualthrcount * 100) - (nearest_thrcount * 100);
        
        if (workdiff < MIN_WORKAMOUNT)
            return nearest_thrcount;

        return nearest_thrcount + 1;
    }
}

int
setup_threadargs(lookupthread_args *threadargpool, int tid, int thcount, int entrycnt,
                    int thoutfd, pthread_mutex_t *fmutex) {
    lookupthread_args *thargs;
    int descffd;
    char descfname[] = DESCFILE;

    if (thcount < 1 || entrycnt < 1 || tid < 0 || tid >= thcount) {
        error("Internal exception");
        return 1;
    }

    thargs = threadargpool + tid;
    descffd = mkstemp(descfname);
    
    if (descffd == -1) {
        EPERROR();
        return 1;
    }

    thargs->descfname = strdup(descfname);
    thargs->descffd = descffd;
    thargs->outfd = thoutfd;
    thargs->mutex = fmutex;

    if (thcount == 1) {
        thargs->startpoint = 0;
        thargs->endpoint = entrycnt;
        return 0;
    }

    if (OK(tid )) {
        thargs->startpoint = 0;
        if (thcount < OPTTHREAD_COUNT) {
            thargs->endpoint = thargs->startpoint + OPTWORK_AMOUNT;
        } else {
            double actworkamount = entrycnt / OPTTHREAD_COUNT;
            int approxstartval = (int)(floor(actworkamount / 100)) + 2;
            thargs->endpoint = thargs->startpoint + approxstartval;
        }
    } else {
        lookupthread_args *prevthargs = threadargpool + (tid - 1);
        thargs->startpoint = prevthargs->endpoint;

        if (tid == thcount - 1) {
            thargs->endpoint = entrycnt;
        } else {
            thargs->endpoint = (prevthargs->endpoint - prevthargs->startpoint) + thargs->startpoint; 
        }
    }
    return 0;
}

void
cleanup_descfname(lookupthread_args *thargs) {
    free(thargs->descfname);
}

void
cleanup_threadargs(lookupthread_args *thargs) {
    cleanup_descfname(thargs);
    free(thargs);
}

int
main(int argc, char **argv) {
    DIR *pd;
    struct dirent *pdir;
    int tentrycnt = 0;
    
    if (argc < 2) {
        usage();
        return 1;
    }

    searchstr = argv[1];
    pd = opendir(DWM_PATCHES);

    while ((pdir = readdir(pd)) != NULL) {
        if (pdir->d_type == DT_DIR) 
            tentrycnt++;
    }
    closedir(pd);

    if (worth_multithread(tentrycnt)) {
        pthread_t *threadpool;
        pthread_mutex_t fmutex;
        lookupthread_args *thargs;
        FILE *rescache;
        int thcount, thpoolsize, rescachefd;
        char rescachename[] = RESULTCACHE, resc;
        int res = 1;

        rescachefd = mkstemp(rescachename);
        if (rescachefd == -1) {
            EPERROR();
            return res;
        }
        
        thcount = calc_threadcount(tentrycnt);
        thpoolsize = sizeof(pthread_t) * thcount;
        threadpool = malloc(thpoolsize);
        thargs = malloc(sizeof(lookupthread_args) * thcount);
        memset(threadpool, 0, thpoolsize);
        pthread_mutex_init(&fmutex, NULL);

        for (int tid = 0; tid < thcount; tid++) {
            if (setup_threadargs(thargs, tid, thcount, tentrycnt, rescachefd, &fmutex)) {
                return res;
            }
            pthread_create(threadpool + tid, NULL, *search_entry, thargs + tid);
        }
        
        for (int tid = 0; tid < thcount; tid++) {
            pthread_join(threadpool[tid], NULL);
            res |= thargs[tid].result;
            cleanup_threadargs(thargs + tid);
        }
        
        pthread_mutex_destroy(&fmutex);
        free(threadpool);

        TRY(rescache = fdopen(rescachefd, "r")) 
            WITH error("Failed to copy rescache");

        while ((resc = fgetc(rescache)) != EOF) {
            fputc(resc, stdout);
        }
        fclose(rescache);
        remove(rescachename);
        return !!res;
    } else {
        lookupthread_args thargs;
        lookupthread_args *thargsp;
        int res = 1;

        if (setup_threadargs(&thargs, 0, 1, tentrycnt, STDOUT_FILENO, NULL)) {
            return res;
        }
        thargsp = &thargs;

        lookup_entries(thargsp);
        res = thargsp->result;
        cleanup_descfname(thargsp);
        return res;
    }

    return 0;
}