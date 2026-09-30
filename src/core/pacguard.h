#ifndef UTGARD_PACGUARD_H
#define UTGARD_PACGUARD_H
#include "dnsname.h"
int pacguard_enter(char (*entries)[DNS_NAME_SIZE], int count, const char *name);
void pacguard_leave(char (*entries)[DNS_NAME_SIZE], int count, const char *name);
#endif
