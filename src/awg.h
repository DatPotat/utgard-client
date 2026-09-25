#ifndef UTGARD_AWG_H
#define UTGARD_AWG_H
#include <stddef.h>
/* 1: accepted, 0: unknown key, -1: invalid/duplicate/too long. */
int awg_add(char *config, size_t cap, const char *key, const char *value);
int awg_validate(const char *config);
#endif
