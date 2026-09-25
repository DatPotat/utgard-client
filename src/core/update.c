#include "update.h"

#include <string.h>

int update_tag_from_location(const char *location, char *tag, size_t cap)
{
    static const char mark[] = "/releases/tag/";
    const char *p = location ? strstr(location, mark) : NULL;
    size_t      n = 0;

    if (!p || !cap) return 0;
    p += sizeof mark - 1;
    for (; p[n]; n++) {
        char c = p[n];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '+'))
            return 0;
    }
    if (n == 0 || n >= cap) return 0;
    memcpy(tag, p, n);
    tag[n] = '\0';
    return 1;
}

/* Up to four numbers; missing ones are 0, so 2.1 == 2.1.0. */
static int parse(const char *s, unsigned long v[4])
{
    int i;

    memset(v, 0, 4 * sizeof v[0]);
    if (*s == 'v' || *s == 'V') s++;
    for (i = 0; i < 4; i++) {
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (v[i] > 100000000UL) return 0;
            v[i] = v[i] * 10 + (unsigned long)(*s++ - '0');
            digits++;
        }
        if (!digits) return 0;
        if (*s == '\0' || *s == '-' || *s == '+') return 1;
        if (*s++ != '.') return 0;
    }
    return 0;
}

int update_is_newer(const char *latest, const char *current)
{
    unsigned long a[4], b[4];
    int           i;

    if (!parse(latest, a) || !parse(current, b)) return 0;
    for (i = 0; i < 4; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return 0;
}
