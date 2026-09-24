#ifndef UTGARD_SINGBOX_H
#define UTGARD_SINGBOX_H

#include <windows.h>

#define SB_MSG_MAX 512

/* Product layout; <root> is the folder of the executable itself:

     <root>\utgard.exe
     <root>\sing-box\sing-box.exe, libcronet.dll
     <root>\sing-box\config.json               user's base config, read-only
     <root>\list\general.srs
     <root>\list\applications\active\*.json    overlays, read-only
     <root>\logs\sing-box.log

   The config sing-box runs with is built in memory and handed over on
   stdin: it carries the server credentials and never touches the disk.

   sing-box runs with <root> as its working directory, because the paths
   inside the config - logs/sing-box.log, list/general.srs - are relative
   to the process. */

int singbox_root(wchar_t *out, size_t cap);          /* trailing backslash */
int singbox_exe(wchar_t *out, size_t cap);
int singbox_base_utf8(char *out, size_t cap);        /* for the generator */

/* Is our sing-box running? Matched by full image path, so another copy
   elsewhere on the machine is not mistaken for ours. */
int singbox_running(void);

/* Write sing-box\config.json from the built-in default when it is absent. */
int singbox_seed_config(void);

/* Is sing-box.exe in place? */
int singbox_present(void);

/* Present AND exactly the pinned release, by the hashes of both files.
   Slower than singbox_present: it reads about 90 MB. */
int singbox_verified(void);

/* The pinned release, for the prompt: the client is tested against this one
   version and nothing else. */
const wchar_t *singbox_version(void);

/* Fetch the pinned release, verify its SHA-256 and unpack it into sing-box\.
   Blocking and slow: call it off the UI thread. */
int singbox_install(wchar_t *msg, size_t cap);

/* Runs `sing-box check` on config (JSON text, passed on stdin). 1 when it
   passes; otherwise msg carries a sentence for the user, translated from
   sing-box's own output where the wording is recognised. */
int singbox_check(const char *config, wchar_t *msg, size_t cap);

/* Compile lists/general.json into lists/general.srs with sing-box itself.
   The intermediate .json is kept on failure: it is the artefact that broke. */
int singbox_compile_list(wchar_t *msg, size_t cap);

/* Start it and confirm it is still alive a moment later: a config that sing-box
   accepts can still die on startup, typically over the TUN adapter. */
int singbox_start(const char *config, wchar_t *msg, size_t cap);

/* Ask it to close before killing it: a hard kill leaves the TUN adapter and
   its routes behind. */
int singbox_stop(wchar_t *msg, size_t cap);

#endif
