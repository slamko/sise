#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>

#define DWM_PATCHES "/usr/local/src/sites/dwm.suckless.org/patches/"
#define INDEXMD "/index.md"
#define LINEBUF 4096
#define DESCRIPTION_SECTION "Description" 
#define DESCRIPTION_SECTION_LENGTH 11
#define GREP_BIN "/bin/grep"
#define DESCFILE "descfile.XXXXXX"
#define RESULTCACHE "result.XXXXXX"
#define DEVNULL "/dev/null"
#define ASCNULL '\0'
#define OPTTHREAD_COUNT 4
#define OPTWORK_AMOUNT 80
#define MIN_WORKAMOUNT 40
#define ERRPREFIX_LEN 7

char *searchstr;

char *
concat(char *base, char *append) {
    int pdirlen = strlen(base);
    int pnamelen = strlen(append);
    char *buf = (char *)calloc(pdirlen + pnamelen + 1, sizeof(char));

    if (!buf)
        exit(0);

    strcpy(buf, base);
    strcpy(buf + pdirlen, append);
    return buf; 
}

int
append_patchmd(char **buf, char *patch) {
    char *patchmd = concat(patch, INDEXMD);
    *buf = concat(DWM_PATCHES, patchmd);
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
eperror() {
    error(strerror(errno));
}

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
};

typedef struct threadargs lookupthread_args;

int
read_description(char *indexmd, int descffd) {
    FILE *descfile;
    FILE *index;
    int res = 1;
    int descrlen = 0;
    char linebuf[4096];
    bool description_exists = false;

    index = fopen(indexmd, "r");
    if (!index) {
        return 1;
    }

    memset(linebuf, ASCNULL, LINEBUF);

    while(fgets(linebuf, LINEBUF, index) != NULL) {
        if (description_exists) {
            descrlen++;
            if (is_line_separator(linebuf)) {
                if (descrlen > 1)
                    break;
            } else {
                fwrite(linebuf, sizeof(char), strlen(linebuf), descfile);
            }
        } else {
            char tittle[DESCRIPTION_SECTION_LENGTH + 1];
            memcpy(tittle, linebuf, DESCRIPTION_SECTION_LENGTH);
            tittle[DESCRIPTION_SECTION_LENGTH] = ASCNULL;
            if (strcmp(tittle, DESCRIPTION_SECTION) == 0) {
                description_exists = true;
                descfile = fdopen(descffd, "w");
                if (!descfile) {
                    error("Unable to access cache file");
                    goto cleanup;
                }
            } 
        }
        memset(linebuf, ASCNULL, LINEBUF);
    }

    if (description_exists) {
        fclose(descfile);
        res = 0;
    }

cleanup:
    fclose(index);
    return res;
}

int
lookup_entries_args(char *descfname, int descffd, int startpoint, int endpoint, int outfd) {
    DIR *pd;
    FILE *descfile;
    struct dirent *pdir;

    descfile = fdopen(descffd, "r");

    while ((pdir = readdir(pd)) != NULL) {
        if (pdir->d_type == DT_DIR) {
            char *indexmd = NULL; 
            int grep, grepst, devnull;
            char dch;

            append_patchmd(&indexmd, pdir->d_name);

            if (read_description(indexmd, descffd) == 0) {
                grep = fork();
                fseek(descfile, 0, SEEK_SET);
                devnull = open(DEVNULL, O_WRONLY);
                if (grep == 0) {
                    dup2(devnull, STDOUT_FILENO);
                    execl(GREP_BIN, GREP_BIN, searchstr, descfname, NULL);
                }
                wait(&grepst);

                if (grepst == 0) {
                    printf("\n%s:\n", pdir->d_name);
                    while((dch = fgetc(descfile)) != EOF) {
                        write(outfd, &dch, sizeof(char));
                    }
                }
            }
            free(indexmd);
        }
    }
    fclose(descfile);
    remove(descfile);
    return 0;
}

int 
lookup_entries(lookupthread_args *args) {
    return lookup_entries_args(args->descfname, args->descffd, args->startpoint, args->endpoint, args->outfd);
}

void *
search_entry(void *thread_args) {
    lookupthread_args *args = (lookupthread_args *)thread_args;
    args->result = lookup_entries(args);
}

int
worth_multithread(int entrycount) {
    return entrycount >= ((OPTWORK_AMOUNT * 2) - (OPTWORK_AMOUNT - (OPTWORK_AMOUNT / 4)));
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
setup_threadargs(lookupthread_args *threadpool, int tid, int thcount, int entrycnt, int thoutfd) {
    lookupthread_args *thargs;
    int descffd;
    char descfname[] = DESCFILE;

    if (thcount < 1 || entrycnt < 1 || tid < 0 || tid >= thcount) {
        error("Internal exception");
        return 1;
    }

    thargs = threadpool + tid;
    descffd = mkstemp(descfname);
    
    if (descffd == -1) {
        eperror();
        return 1;
    }

    thargs->descfname = strdup(descfname);
    thargs->descffd = descffd;
    thargs->outfd = thoutfd;

    if (thcount == 1) {
        thargs->startpoint = 0;
        thargs->endpoint = entrycnt;
        return 0;
    }

    if (tid == 0) {
        thargs->startpoint = 0;
        if (thcount < OPTTHREAD_COUNT) {
            thargs->endpoint = thargs->startpoint + OPTWORK_AMOUNT;
        } else {

        }
    } else {
        lookupthread_args *prevthargs = threadpool + (tid - 1);
        thargs->startpoint = prevthargs->endpoint;

        if (tid == thcount - 1) {
            thargs->endpoint = entrycnt;
        } else {
            thargs->endpoint = (prevthargs->endpoint - prevthargs->startpoint) + thargs->startpoint; 
        }
        return 0;
    }
}

void
cleanup_threadargs(lookupthread_args *thargs) {
    free(thargs->descfname);
    free(thargs);
}

int
main(int argc, char **argv) {
    DIR *pd;
    struct dirent *pdir;
    int tentrycnt = 0, entrycnt = 0;
    
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
        lookupthread_args *thargs;
        int thcount, thpoolsize, rescachefd;
        char targetcache[] = RESULTCACHE;
        int res = 1;

        rescachefd = mkstemp(targetcache);
        if (rescachefd == -1) {
            eperror();
            return res;
        }
        
        thcount = calc_threadcount(tentrycnt);
        thpoolsize = sizeof(pthread_t) * thcount;
        threadpool = malloc(thpoolsize);
        thargs = malloc(sizeof(lookupthread_args) * thcount);
        memset(threadpool, 0, thpoolsize);

        for (int tid = 0; tid < thcount; tid++) {
            if (setup_threadargs(threadpool, tid, thcount, tentrycnt, rescachefd)) {
                return res;
            }
            pthread_create(threadpool + tid, NULL, *search_entry, NULL);
        }
        
        for (int tid = 0; tid < thcount; tid++) {
            pthread_join(threadpool[tid], NULL);
            res |= thargs[tid].result;
            cleanup_threadargs(thargs + tid);
        }

        free(threadpool);
        return !!res;
    } else {
        lookupthread_args thargs;
        lookupthread_args *thargsp;
        int res = 1;

        if (setup_threadargs(&thargs, 0, 1, entrycnt, STDOUT_FILENO)) {
            return res;
        }
        thargsp = &thargs;

        lookup_entries(thargsp);
        res = thargsp->result;
        cleanup_threadargs(thargsp);
        return res;
    }

    return 0;
}