#ifndef UTGARD_UPDATE_H
#define UTGARD_UPDATE_H

#include <stddef.h>

/* The newest release is read from where github.com/<repo>/releases/latest
   redirects: .../releases/tag/<tag>. No API call: the REST API allows 60
   anonymous requests an hour per address, and every user behind one VPN exit
   shares that address. Pure functions, testable on the host. */

/* Take <tag> out of a redirect target. 0 if the target is not a release tag
   or the tag holds anything besides letters, digits, '.', '-', '+'. */
int update_tag_from_location(const char *location, char *tag, size_t cap);

/* 1 if release tag `latest` is a higher version than `current`. Optional
   leading v, up to four dot-separated numbers; anything after '-' or '+'
   is ignored. A tag that does not parse is never newer. */
int update_is_newer(const char *latest, const char *current);

#endif
