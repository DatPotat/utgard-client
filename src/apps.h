#ifndef UTGARD_APPS_H
#define UTGARD_APPS_H

#include <windows.h>

/* Application lists live as one config file each:

     list\applications\active\<name>.json     applied
     list\applications\inactive\<name>.json   kept, not applied

   Moving a file between the two folders is the whole of enabling and
   disabling - no state file, and the layout is obvious in Explorer. */

#define APPS_MAX       128
#define APPS_NAME_MAX  64

/* Create the folders and move any loose file left directly in
   list\applications into active\. Returns 1 when the layout is in place. */
int apps_prepare(void);


typedef struct {
    wchar_t name[APPS_NAME_MAX];   /* file name without .json */
    int     enabled;
} app_entry;

/* Saved lists, alphabetical, enabled ones first identified by the flag.
   Returns how many were written. */
int apps_scan(app_entry *out, int max);

/* Enabling is moving the file between active\ and inactive\. */
int apps_set_enabled(const app_entry *e, int enable);
int apps_delete(const app_entry *e);

/* Is this a usable list name? Latin letters, digits, dash and underscore:
   the name becomes a file name and a tag the user types. */
int apps_name_ok(const wchar_t *name);

/* Does a list with this name exist in either folder? */
int apps_exists(const wchar_t *name);

/* Write a list: one process_name rule and one process_path rule. They are
   separate rules on purpose - within one sing-box rule fields are ANDed,
   so a single rule with both would demand name and path together.
   New lists land in inactive\: saved lists start switched off.
   Paths may be empty strings; names may too, but not both for an entry. */
int apps_write(const wchar_t *name, const wchar_t names[][MAX_PATH],
               int name_count, const wchar_t paths[][MAX_PATH], int path_count,
               int enabled, wchar_t *err, size_t errcap);

/* Read a list back into the two editor sections. Returns 1 only when the file
   holds nothing but what the editor can show - process_name and process_path
   rules routing to the tunnel. Anything else and it returns 0: opening such a
   file in the form and saving it would silently drop the rest. */
int apps_read(const app_entry *e, wchar_t names[][MAX_PATH], int *name_count,
              wchar_t paths[][MAX_PATH], int *path_count, int max);

#endif
