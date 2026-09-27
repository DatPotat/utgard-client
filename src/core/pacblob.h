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
int pacblob_store_version_supported(int version);

#endif
