#ifndef UTGARD_COREDIR_H
#define UTGARD_COREDIR_H

#include <windows.h>
#include "coremanifest.h"

/* The folder a core runs from, guarded. sing-box runs elevated and the
   AmneziaWG service as SYSTEM; both load DLLs from their own folder first,
   so a file dropped there is code in that process. The folder therefore is
   writable by SYSTEM and Administrators only, holds exactly the core's files
   and nothing else, and is held open while the core starts. */

typedef struct {
    HANDLE dir;
    HANDLE files[CORE_FILES_MAX];
} coredir_hold;

/* <root>\<core dir>. */
int coredir_path(const core_desc *c, wchar_t *out, size_t cap);

/* 0 on FAT32/exFAT and other volumes that cannot keep permissions. */
int coredir_volume_has_acl(const wchar_t *path);

/* Create the folder if missing and give it, and the core's files already in
   it, our owner and permissions. Never follows a junction or symlink. */
int coredir_protect(const core_desc *c, wchar_t *msg, size_t cap);

/* Check the folder and every file of the core and keep them open, so nothing
   can be swapped until coredir_release. On a volume without permissions the
   ownership check is skipped for a core that allows it. reinstall_helps
   (may be NULL) says whether downloading the core again would fix a
   failure: yes for a missing or changed file, no for the folder itself -
   its permissions, a link in its place, a file that does not belong. */
int coredir_hold_verified(const core_desc *c, coredir_hold *h, wchar_t *msg, size_t cap,
                          int *reinstall_helps);
void coredir_release(coredir_hold *h);

/* 1 when only SYSTEM and Administrators may change the object: the owner is
   one of them and no allow entry grants anyone else a writing right. */
int coredir_sd_safe(PSECURITY_DESCRIPTOR sd);

/* Shared by the installers. */
int  coredir_sha256_handle(HANDLE file, wchar_t *hex, size_t cap);
int  coredir_open_verified(const wchar_t *path, const wchar_t *expect, HANDLE *out);
void coredir_wipe(const wchar_t *dir);
int  coredir_run_wait(const wchar_t *cmdline, DWORD timeout_ms, DWORD *code);

#endif
