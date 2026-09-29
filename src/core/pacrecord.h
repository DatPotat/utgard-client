#ifndef UTGARD_PACRECORD_H
#define UTGARD_PACRECORD_H
#include <stdint.h>
#define PAC_STATUS_MAGIC 0x53544350u
#define PAC_STATUS_VERSION 4u
typedef struct {
    uint32_t magic, version;
    uint64_t sequence_begin;
    uint32_t active_count, last_error;
    uint64_t evaluation_errors, last_error_time;
    uint64_t worker_cap_hits, dns_cap_hits, udp_evictions;
    uint64_t udp_owner_verified, udp_owner_no_table, udp_owner_not_listed, udp_owner_foreign;
    uint64_t sequence_end;
} pac_record;
int pacrecord_consistent(const pac_record *first, const pac_record *second);
#endif
