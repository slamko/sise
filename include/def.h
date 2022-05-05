
#define BASEREPO "/.cache/sise/sites/"
#define PATCHESDIR ".suckless.org/patches/"
#define PATCHESP "/patches/"
#define DWM "dwm"
#define ST "st"
#define SURF "surf"
#define DWM_PATCHESDIR "dwm.suckless.org/patches/"
#define ST_PATCHESDIR "st.suckless.org/patches/"
#define SURF_PATCHESDIR "surf.suckless.org/patches/"
#define TOOLSDIR "tools.suckless.org/"
#define INDEXMD "/index.md"

#define LINEBUF 4096
#define PATHBUF LINEBUF
#define DESCRIPTION_SECTION "Description" 
#define DESCRIPTION_SECTION_LENGTH sizeof(DESCRIPTION_SECTION)
#define GREP_BIN "/bin/grep"
#define DESCFILE "descfile.XXXXXX"
#define DESCFILE_LEN sizeof(DESCFILE)
#define RESULTCACHE "result.XXXXXX"
#define DEVNULL "/dev/null"
#define ASCNULL '\0'

#define OPTTHREAD_COUNT 4
#define OPTWORK_AMOUNT 80
#define MIN_WORKAMOUNT 40
#define ERR_PREFIX "error: "
#define BUG_PREFIX "bug: %s:%d: %s"
#define BUG_PREFIX_LEN sizeof(BUG_PREFIX)
#define FATALERR_PREFIX "fatal error: "
#define ERR_PREFIX_LEN sizeof(ERR_PREFIX)
#define AVSEARCH_WORD_LEN 5
#define MAXSEARCH_LEN 512
#define ENTRYLEN 256
#define TOOLNAME_ARGPOS 1
#define KEYWORD_ARGPOS 2
#define PRINTABLE_DESCRIPTION_MAXLEN 5

#define SEARCH_CMD "search"
#define SYNC_CMD "sync"
#define DOWNLOAD_CMD "load"
#define OPEN_CMD "open"
#define CMD_LEN 8
#define CMD_ARGPOS 1

//#define USE_MULTITHREADED

#define TRY(EXP) if (!(EXP))
#define TRYP(EXP) if (!(EXP))  
#define WITH(HANDLE) { HANDLE; return EXIT_FAILURE; }
#define EPERROR() error(strerror(errno));
#define OK(RES) RES == 0