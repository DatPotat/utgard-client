#ifndef UTGARD_ERRMSG_H
#define UTGARD_ERRMSG_H

#include <string.h>

/* A message for the user into err, cut to cap, and 0 - so a failing path
   reads return oops(err, cap, "..."). err may be NULL.
   static, not inline: the code stays what the compiler made of the copies
   this replaced. Include it only where it is used (-Wunused-function). */
static int oops(char *err, size_t cap, const char *msg)
{
    if (err && cap) {
        size_t n = strlen(msg);
        if (n >= cap) n = cap - 1;
        memcpy(err, msg, n);
        err[n] = '\0';
    }
    return 0;
}

#endif
