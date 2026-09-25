#include "awgconf.h"

#include <stdio.h>
#include <string.h>

static int oops(char *err, size_t cap, const char *msg)
{
    if (err && cap) snprintf(err, cap, "%s", msg);
    return 0;
}

/* Nothing that could end the line and start another key. */
static int one_line(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == 0x7F) return 0;
    return 1;
}

/* An IP literal and nothing else: digits and dots, or hex digits and colons. */
static int ip_literal(const char *s, int *v6)
{
    size_t i, n = strlen(s);
    int    dots = 0, colons = 0;

    if (n == 0 || n > 45) return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '.') dots++;
        else if (c == ':') colons++;
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    *v6 = colons > 0;
    return colons ? colons >= 2 : dots == 3;
}

static int add(char *out, size_t cap, size_t *n, const char *fmt, const char *a, const char *b)
{
    int w = snprintf(out + *n, cap - *n, fmt, a, b);
    if (w < 0 || (size_t)w >= cap - *n) return 0;
    *n += (size_t)w;
    return 1;
}

int awgconf_build(const link_profile *p, const char *endpoint_ip,
                  char *out, size_t cap, char *err, size_t errcap)
{
    char        num[32], ep[64], range[32] = "";
    const char *line, *nl;
    size_t      n = 0;
    int         v6 = 0;

    if (cap) out[0] = '\0';
    if (!p || p->proto != LINK_WG) return oops(err, errcap, "профиль не WireGuard");
    if (!p->wg_private_key[0] || !p->wg_peer_key[0] || !p->wg_address[0])
        return oops(err, errcap, "в профиле нет ключей или адреса интерфейса");
    if (!one_line(p->wg_private_key) || !one_line(p->wg_peer_key) ||
        !one_line(p->wg_psk) || !one_line(p->wg_address))
        return oops(err, errcap, "профиль AmneziaWG повреждён");
    if (p->awg[0] && !link_awg_valid(p->awg))
        return oops(err, errcap, "параметры AmneziaWG в профиле повреждены — добавьте профиль заново");
    if (!endpoint_ip || !ip_literal(endpoint_ip, &v6))
        return oops(err, errcap, "адрес сервера AmneziaWG не разрешён в IP");
    if (p->port <= 0 || p->port > 65535) return oops(err, errcap, "неверный порт сервера");

    snprintf(ep, sizeof ep, v6 ? "[%s]:%d" : "%s:%d", endpoint_ip, p->port);

    /* IPv4 only (see genconf append_list4): IPv6 addresses are dropped. */
    {
        const char *q = p->wg_address;
        char        v4[512] = "";
        while (*q) {
            size_t k = strcspn(q, ",");
            if (k && !memchr(q, ':', k) && strlen(v4) + k + 2 < sizeof v4) {
                if (v4[0]) strcat(v4, ", ");
                strncat(v4, q, k);
            }
            q += k;
            if (*q == ',') q++;
        }
        if (!v4[0]) return oops(err, errcap, "в профиле нет IPv4-адреса интерфейса — IPv6 Utgard не поддерживает");
        if (!add(out, cap, &n, "[Interface]\nPrivateKey = %s\nAddress = %s\n", p->wg_private_key, v4))
            goto small;
    }
    if (p->mtu) {
        snprintf(num, sizeof num, "%d", p->mtu);
        if (!add(out, cap, &n, "MTU = %s\n%s", num, "")) goto small;
    }
    if (!add(out, cap, &n, "Table = off\n%s%s", "", "")) goto small;
    /* The AWG block holds [Interface] keys, and one [Peer] key: a
       PersistentKeepalive range. Each goes to its own section. */
    for (line = p->awg; *line; line = nl + 1) {
        nl = strchr(line, '\n');
        if (!nl) break;                               /* link_awg_valid made sure */
        if (strncmp(line, "persistentkeepalive = ", 22) == 0) {
            snprintf(range, sizeof range, "%.*s", (int)(nl - line - 22), line + 22);
            continue;
        }
        if (n + (size_t)(nl - line) + 2 > cap) goto small;
        memcpy(out + n, line, (size_t)(nl - line) + 1);
        n += (size_t)(nl - line) + 1;
        out[n] = '\0';
    }
    if (!add(out, cap, &n, "\n[Peer]\nPublicKey = %s\n%s", p->wg_peer_key, "")) goto small;
    if (p->wg_psk[0] && !add(out, cap, &n, "PresharedKey = %s\n%s", p->wg_psk, "")) goto small;
    if (!add(out, cap, &n, "AllowedIPs = 0.0.0.0/0\nEndpoint = %s\n%s", ep, "")) goto small;
    if (range[0]) {
        if (!add(out, cap, &n, "PersistentKeepalive = %s\n%s", range, "")) goto small;
    } else if (p->keepalive > 0) {
        snprintf(num, sizeof num, "%d", p->keepalive);
        if (!add(out, cap, &n, "PersistentKeepalive = %s\n%s", num, "")) goto small;
    }
    return 1;

small:
    awgconf_wipe(out, cap);
    return oops(err, errcap, "конфигурация AmneziaWG не поместилась");
}

void awgconf_wipe(char *text, size_t cap)
{
    volatile char *p = text;
    while (cap--) *p++ = 0;
}
