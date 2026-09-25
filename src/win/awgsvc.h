#ifndef UTGARD_AWGSVC_H
#define UTGARD_AWGSVC_H

#include <stddef.h>
#include <wchar.h>

/* The AmneziaWG tunnel as a Windows service we create for the session and
   delete afterwards - the Windows side of the AmneziaWG backend. The service
   is the official amneziawg.exe in "/tunnelservice" mode; it reads its config
   from a named pipe we serve, so the private key never touches the disk,
   and raises the adapter below with Table = off: no routes of its own. */

/* The adapter the tunnel appears as; sing-box binds to it by this name. */
const wchar_t *awgsvc_interface(void);

/* Left from a crash: ours and stopped is deleted. 1 if ours is running. */
int awgsvc_cleanup(void);

/* Blocking, for a worker thread. conf is the text from awgconf_build. */
int awgsvc_start(const char *conf, wchar_t *msg, size_t cap);
int awgsvc_stop(wchar_t *msg, size_t cap);

/* 1 running, 0 not there or stopped. */
int awgsvc_running(void);

#endif
