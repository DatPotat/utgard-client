#ifndef UTGARD_PACSTATUS_H
#define UTGARD_PACSTATUS_H

#include <windows.h>
#include "pacrecord.h"
typedef pac_record pac_status_record;

/* The status file exists exactly while the helper holds it: Utgard creates
   it delete-on-close and hands the helper the only handle. */
void pacstatus_writer(HANDLE file, unsigned active_count);
void pacstatus_active(unsigned active_count);
void pacstatus_evaluation_error(DWORD error);
void pacstatus_worker_cap(void);
void pacstatus_dns_cap(void);
void pacstatus_udp_evict(void);
int pacstatus_read(pac_status_record *record);

/* Pure validation used by the host test and the Windows reader. */

#endif
