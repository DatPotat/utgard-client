#include "awg.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char *const keys[] = {
    "jc", "jmin", "jmax", "s1", "s2", "s3", "s4",
    "h1", "h2", "h3", "h4", "i1", "i2", "i3", "i4", "i5"
};

static int number(const char *s, size_t n, uint32_t max, uint32_t *v)
{
    size_t i;
    uint32_t a = 0;
    if (!n) return 0;
    for (i = 0; i < n; ++i) {
        unsigned d = (unsigned char)s[i] - (unsigned)'0';
        if (d > 9 || a > (max - d) / 10 || d > max) return 0;
        a = a * 10 + d;
    }
    *v = a;
    return 1;
}

/* AWG 2.0 signature packets: fixed bytes, timestamp, random bytes/chars/digits.
   Bound the expanded size too: upstream accepts negative/huge random lengths. */
static int signature(const char *s)
{
    size_t total = 0;
    if (!*s) return 0;
    while (*s) {
        const char *end, *arg;
        size_t n;
        uint32_t count = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (*s++ != '<' || !(end = strchr(s, '>'))) return 0;
        arg = s;
        while (arg < end && *arg != ' ' && *arg != '\t') arg++;
        n = (size_t)(arg - s);
        while (arg < end && (*arg == ' ' || *arg == '\t')) arg++;
        if (n == 1 && s[0] == 't') {
            if (arg != end) return 0;
            count = 4;
        } else if (n == 1 && s[0] == 'b') {
            const char *p;
            if (end - arg < 4 || arg[0] != '0' || arg[1] != 'x' || (end - arg) % 2) return 0;
            for (p = arg + 2; p < end; p++) if (!isxdigit((unsigned char)*p)) return 0;
            count = (uint32_t)(end - arg - 2) / 2;
        } else if ((n == 1 && s[0] == 'r') ||
                   (n == 2 && s[0] == 'r' && (s[1] == 'c' || s[1] == 'd'))) {
            if (!number(arg, (size_t)(end - arg), 65507, &count)) return 0;
        } else return 0;
        total += count;
        if (total > 65507) return 0;
        s = end + 1;
    }
    return total > 0;
}

int awg_add(char *config, size_t cap, const char *key, const char *value)
{
    char lower[8], prefix[10];
    size_t i, n = strlen(key), used = strlen(config);
    uint32_t a, b;
    const char *dash, *p;
    int index = -1;
    if (n >= sizeof lower) return 0;
    for (i = 0; i <= n; i++) lower[i] = (char)tolower((unsigned char)key[i]);
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (strcmp(lower, keys[i]) == 0) { index = (int)i; break; }
    if (index < 0) return 0;
    if (!*value || strpbrk(value, "\r\n")) return -1;
    if (index < 7) {
        if (!number(value, strlen(value), index == 0 ? 128 : 65507, &a)) return -1;
    } else if (index < 11) {
        dash = strchr(value, '-');
        if (!number(value, dash ? (size_t)(dash - value) : strlen(value), UINT32_MAX, &a)) return -1;
        if (dash && (!number(dash + 1, strlen(dash + 1), UINT32_MAX, &b) || b < a)) return -1;
    } else if (!signature(value)) return -1;
    snprintf(prefix, sizeof prefix, "%s=", lower);
    p = config;
    while (*p) {
        if (strncmp(p, prefix, strlen(prefix)) == 0) return -1;
        p += strcspn(p, "\n");
        if (*p) p++;
    }
    if (used + strlen(prefix) + strlen(value) + 2 > cap) return -1;
    snprintf(config + used, cap - used, "%s%s\n", prefix, value);
    return 1;
}

int awg_validate(const char *config)
{
    char copy[8192] = "", key[8], value[8192];
    const char *p = config;
    uint32_t low[4] = {1,2,3,4}, high[4] = {1,2,3,4};
    unsigned jc = 0, jmin = 0, jmax = 0, pad[4] = {0};
    int i, j;
    while (*p) {
        const char *eq = strchr(p, '='), *end = strchr(p, '\n');
        size_t n;
        if (!eq || !end || eq > end || eq - p >= (int)sizeof key) return 0;
        memcpy(key, p, (size_t)(eq - p)); key[eq - p] = 0;
        n = (size_t)(end - eq - 1);
        if (n >= sizeof value) return 0;
        memcpy(value, eq + 1, n); value[n] = 0;
        if (awg_add(copy, sizeof copy, key, value) != 1) return 0;
        if (!strcmp(key,"jc")) sscanf(value,"%u", &jc);
        if (!strcmp(key,"jmin")) sscanf(value,"%u", &jmin);
        if (!strcmp(key,"jmax")) sscanf(value,"%u", &jmax);
        if (key[0] == 's') sscanf(value,"%u", &pad[key[1]-'1']);
        if (key[0] == 'h') {
            i = key[1]-'1';
            number(value, strcspn(value,"-"), UINT32_MAX, &low[i]);
            high[i] = low[i];
            if (strchr(value,'-')) number(strchr(value,'-')+1, strlen(strchr(value,'-')+1), UINT32_MAX, &high[i]);
        }
        p = end + 1;
    }
    if (jmin > jmax || (jc && (!jmin || !jmax))) return 0;
    if (pad[0]+148 > 65507 || pad[1]+92 > 65507 || pad[2]+64 > 65507 || pad[3]+1500+32 > 65507) return 0;
    for (i = 0; i < 4; i++) for (j = i+1; j < 4; j++)
        if (low[i] <= high[j] && low[j] <= high[i]) return 0;
    return strcmp(copy, config) == 0;
}
