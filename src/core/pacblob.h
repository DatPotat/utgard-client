#ifndef UTGARD_PACBLOB_H
#define UTGARD_PACBLOB_H
#include <stddef.h>
#include <stdint.h>

#define PAC_BLOB_FORMAT 1u
#define PAC_BLOB_SOURCE_MAX 2048u

size_t pacblob_pack(const char *script, size_t script_length,
                    const uint16_t *source, size_t source_units,
                    unsigned char *out, size_t cap);
int pacblob_unpack(const unsigned char *blob, size_t blob_length,
                   const char *script, size_t script_length,
                   uint16_t *source, size_t source_cap, size_t *source_units);

/* One PAC per file (list\pac\NN.pac, from 2.3.2): the whole item goes into
   one DPAPI blob, whose own integrity check replaces the hash above.
   Layout: format (2), enabled (0/1), source units (u16 LE, NUL included),
   source (UTF-16LE), script length (u32 LE), script. Exact length only. */
#define PAC_ITEM_FORMAT 2u
size_t pacitem_pack(int enabled, const uint16_t *source, size_t source_units,
                    const char *script, size_t script_length,
                    unsigned char *out, size_t cap);
/* *script points into blob; 0 when anything is off. */
int pacitem_unpack(const unsigned char *blob, size_t blob_length, size_t script_max,
                   int *enabled, uint16_t *source, size_t source_cap, size_t *source_units,
                   const unsigned char **script, size_t *script_length);

#endif
