#ifndef UTGARD_WINMSG_H
#define UTGARD_WINMSG_H

#include <windows.h>
#include <strsafe.h>

/* A message for the user into msg, cut to cap, and 0 - so a failing path
   reads return say(msg, cap, L"..."). msg may be NULL.
   static, not inline: the code stays what the compiler made of the copies
   this replaced. Include it only where it is used (-Wunused-function). */
static int say(wchar_t *msg, size_t cap, const wchar_t *text)
{
    if (msg && cap) StringCchCopyW(msg, cap, text);
    return 0;
}

#endif
