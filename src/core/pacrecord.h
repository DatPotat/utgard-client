#ifndef UTGARD_PACRECORD_H
#define UTGARD_PACRECORD_H
#include <stdint.h>
#define PAC_STATUS_MAGIC 0x53544350u
#define PAC_STATUS_VERSION 2u
typedef struct {
    uint32_t magic, version;
    uint64_t sequence_begin;
    uint32_t active_count, helper_pid, last_error, reserved;
    uint64_t evaluation_errors, last_error_time;
    uint64_t worker_cap_hits, dns_cap_hits, udp_evictions;
    uint64_t sequence_end;
} pac_record;
int pacrecord_consistent(const pac_record *first, const pac_record *second);
#endif
