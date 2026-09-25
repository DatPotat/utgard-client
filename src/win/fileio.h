#ifndef UTGARD_FILEIO_H
#define UTGARD_FILEIO_H

#include <windows.h>

/* Whole-file reads and writes by UTF-16 path, so folders named in Cyrillic
   work. Reads NUL-terminate what they read; got may be NULL. Writes go to
   path.tmp, are flushed, and replace the file in one move - an interrupted
   write leaves the old file intact instead of an empty one.

   file_read returns 0 on failure, 1 when the whole file was read, and
   FILE_READ_PARTIAL when it is larger than buf: buf then holds its start.
   Code that writes the file back must refuse the partial case, or it
   silently cuts the file down to the buffer. */
#define FILE_READ_PARTIAL 2
int file_read(const wchar_t *path, void *buf, size_t cap, size_t *got);
int file_write(const wchar_t *path, const void *data, size_t len);

#endif
