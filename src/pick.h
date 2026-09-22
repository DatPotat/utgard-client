#ifndef UTGARD_PICK_H
#define UTGARD_PICK_H

#include <windows.h>

#define PICK_MAX   512
#define PICK_PIDS  16

/* One row of the picker: every running instance of the same executable
   folded into one, so twenty browser processes read as one entry. */
typedef struct {
    wchar_t name[MAX_PATH];     /* executable file name */
    wchar_t path[MAX_PATH];     /* full path; empty when even an administrator
                                   cannot read it - such rows cannot be picked */
    DWORD   pids[PICK_PIDS];
    int     npids;
    int     count;              /* instances, may exceed PICK_PIDS */
} pick_proc;

/* Running processes, folded by path and sorted by name. */
int pick_snapshot(pick_proc *out, int max);

/* Case-insensitive substring on the name; a query made of digits also
   matches a process ID. An empty query matches everything. */
int pick_match(const pick_proc *p, const wchar_t *query);

/* Small icon of an executable, cached by path: re-reading icons every two
   seconds for two hundred processes would be pure waste. */
HICON pick_icon(const wchar_t *path);

#endif
