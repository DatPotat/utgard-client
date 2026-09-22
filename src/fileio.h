#ifndef UTGARD_FILEIO_H
#define UTGARD_FILEIO_H

#include <windows.h>

/* Whole-file reads and writes by UTF-16 path, so folders named in Cyrillic
   work. Reads NUL-terminate what they read; got may be NULL. Writes go to
   path.tmp, are flushed, and replace the file in one move - an interrupted
   write leaves the old file intact instead of an empty one. */
int file_read(const wchar_t *path, void *buf, size_t cap, size_t *got);
int file_write(const wchar_t *path, const void *data, size_t len);

#endif
