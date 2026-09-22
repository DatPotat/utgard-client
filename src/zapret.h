#ifndef UTGARD_ZAPRET_H
#define UTGARD_ZAPRET_H

#include <windows.h>

#define ZAPRET_PATH_MAX 1024
#define ZAPRET_NAME_MAX 128
#define ZAPRET_MAX_STRATEGIES 128

typedef struct {
    wchar_t path[ZAPRET_PATH_MAX];
    wchar_t version[32];      /* empty if service.bat has no LOCAL_VERSION */
    wchar_t problem[160];     /* why the folder was rejected; empty if valid */
    int     strategy_count;   /* *.bat except service*.bat */
    int     valid;
} zapret_info;

/* Inspect a folder. Returns info->valid. Never fails destructively:
   an unusable folder comes back with valid == 0 and problem set. */
int zapret_scan(const wchar_t *dir, zapret_info *out);

/* Strategy .bat files in the folder root, extension stripped, sorted the way
   the filesystem returns them. Pass names == NULL to only count them.
   Returns the number found, which may exceed max. */
int zapret_list(const wchar_t *dir, wchar_t names[][ZAPRET_NAME_MAX], int max);

typedef enum {
    ZAPRET_OFF = 0,
    ZAPRET_STANDALONE,   /* winws.exe started by a strategy .bat */
    ZAPRET_SERVICE       /* installed as the Windows service "zapret" */
} zapret_mode;

typedef struct {
    zapret_mode mode;
    wchar_t     strategy[ZAPRET_NAME_MAX];  /* empty when it cannot be told */
} zapret_status;

/* Reads live state. Touches nothing. */
void zapret_status_read(zapret_status *out);

/* Install the selected strategy as the auto-starting Windows service
   "zapret" and start it, so the bypass survives a reboot. Mirrors what
   service.bat's own "Install Service" does: any previous service is removed
   first, the strategy name is recorded under the service key, and the
   command line is taken from the strategy .bat.
   Returns 1 on success; on failure err holds a message for the user. */
int zapret_service_install(const wchar_t *dir, const wchar_t *strategy,
                           wchar_t *err, size_t errcap);

/* Stop and delete the service, then terminate any stray winws.exe.
   After this nothing starts at boot. */
int zapret_service_remove(wchar_t *err, size_t errcap);

/* Stop and start the installed service without reinstalling it. */
int zapret_service_restart(wchar_t *err, size_t errcap);

/* Command line the strategy .bat passes to winws.exe, with %BIN%, %LISTS%,
   %~dp0 and the game-filter variables expanded and caret escapes removed.
   Exposed so the caller can show it. */
int zapret_strategy_args(const wchar_t *dir, const wchar_t *strategy,
                         wchar_t *out, size_t cap);

/* How many of the given addresses are already listed in
   lists\ipset-exclude-user.txt. That is the file zapret means for the user;
   the tracked ipset-exclude.txt holds only private ranges and is overwritten
   by updates, so it is not consulted. */
int zapret_exclude_present(const wchar_t *dir, const char ips[][16], int count);

/* Write the given IPv4 addresses into lists\ipset-exclude-user.txt as /32
   entries, dropping what we added previously and keeping everything else. */
int zapret_exclude_patch(const wchar_t *dir, const char ips[][16], int count,
                         int *added, int *kept, wchar_t *err, size_t errcap);

/* ---- zapret's own switches -----------------------------------------
   Descriptions shown in the UI are the author's own words from the project
   README; the mechanics below were read out of service.bat. */

typedef enum { GAME_OFF = 0, GAME_ALL, GAME_TCP, GAME_UDP } zapret_game_mode;

/* utils\game_filter.enabled: absent means off, otherwise all / tcp / udp. */
zapret_game_mode zapret_game_get(const wchar_t *dir);
int              zapret_game_set(const wchar_t *dir, zapret_game_mode mode);

/* lists\ipset-all.txt decides the mode: empty file means any, a file holding
   the placeholder means none, anything else means loaded. */
typedef enum { IPSET_LOADED = 0, IPSET_NONE, IPSET_ANY } zapret_ipset_mode;

zapret_ipset_mode zapret_ipset_get(const wchar_t *dir);
int zapret_ipset_cycle(const wchar_t *dir, wchar_t *err, size_t errcap);

/* Replace ipset-all.txt with the current list from the project repository. */
int zapret_ipset_update(const wchar_t *dir, wchar_t *err, size_t errcap);

/* Download the project's hosts file and compare it with the system one. The
   system file is never written: service.bat only tells the user to copy it
   across, and so do we. needs_update says whether it differs. */
int zapret_hosts_check(wchar_t *temp_path, size_t temp_cap, int *needs_update,
                       wchar_t *err, size_t errcap);

/* Remembered folder, stored next to the executable. */
int zapret_path_load(wchar_t *buf, size_t cap);   /* 1 if something was read */
int zapret_path_save(const wchar_t *path);        /* 1 on success */

#endif
