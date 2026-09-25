#ifndef UTGARD_COREMANIFEST_H
#define UTGARD_COREMANIFEST_H

#include <wchar.h>

/* The two cores Utgard downloads, pinned per architecture. Everything that
   says which official release, where it comes from and what its files must
   hash to lives here and only here. */

typedef struct {
    const wchar_t *name;
    const wchar_t *sha256;
    int            required;    /* 0: allowed in the folder, checked if present */
} core_file;

#define CORE_FILES_MAX 3

typedef struct {
    const wchar_t *title;       /* shown to the user */
    const wchar_t *version;
    const wchar_t *dir;         /* folder beside utgard.exe */
    const wchar_t *archive;     /* file name of the official download */
    const wchar_t *url;
    const wchar_t *archive_sha256;
    core_file      files[CORE_FILES_MAX];   /* [0] is the program */
    int            nfiles;
    int            needs_acl;   /* refuse on a volume that cannot protect it */
    const wchar_t *release_page; /* the release of this very version */
} core_desc;

extern const core_desc CORE_SINGBOX;
extern const core_desc CORE_AWG;

#endif
