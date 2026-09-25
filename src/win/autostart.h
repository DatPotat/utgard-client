#ifndef UTGARD_AUTOSTART_H
#define UTGARD_AUTOSTART_H

#include <windows.h>

/* Start with Windows through a Task Scheduler logon task.

   The HKCU Run key cannot do it: the executable is manifested
   requireAdministrator, and Windows blocks elevated programs started from the
   Run keys and Startup folders at logon. A logon task with the highest run
   level starts it elevated without a UAC prompt.

   The task belongs to the current user only, and its state lives in the
   scheduler itself - no copy in settings.txt that could drift from it.
   COM must already be initialised on the calling thread. */

/* 1: a task for this user exists and starts this very executable.
   0: no task, a task for another path (the folder was moved), or an error. */
int autostart_get(void);

int autostart_set(int on, wchar_t *err, size_t errcap);

#endif
