#ifndef UTGARD_PACLOGIC_H
#define UTGARD_PACLOGIC_H
#include <stddef.h>
#include "dnsname.h"

typedef int (*paclogic_evaluator)(const char *host, int domain, void *context);
typedef struct { int vpn; int rewrite; const char *rewrite_host; } paclogic_result;

int paclogic_any(size_t count, int (*evaluate)(size_t index, void *context),
                 void *context, int *had_error);
paclogic_result paclogic_route(int received_domain, const char *original,
                               const char (*names)[DNS_NAME_SIZE], int name_count,
                               paclogic_evaluator evaluate, void *context);
#endif
