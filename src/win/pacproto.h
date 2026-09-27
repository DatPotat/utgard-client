#ifndef UTGARD_PACPROTO_H
#define UTGARD_PACPROTO_H

#include <windows.h>

#define PACPROC_MAGIC 0x50414332u

typedef struct {
    DWORD magic;
    DWORD ok;
    unsigned short pac_port;
    unsigned short dns_port;
    unsigned short proxy_port;
    unsigned short reserved;
    char password[65];
    wchar_t error[512];
} pacproc_ready;

typedef struct {
    DWORD magic;
    UINT_PTR process_handle;
} pacproc_command;

#endif
