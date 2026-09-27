#ifndef UTGARD_PACGUARD_H
#define UTGARD_PACGUARD_H
int pacguard_enter(char (*entries)[256], int count, const char *name);
void pacguard_leave(char (*entries)[256], int count, const char *name);
#endif
