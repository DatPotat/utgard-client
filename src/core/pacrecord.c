#include "pacrecord.h"
int pacrecord_consistent(const pac_record *a, const pac_record *b)
{
    return a && b && a->magic == PAC_STATUS_MAGIC && a->version == PAC_STATUS_VERSION &&
           a->sequence_begin == a->sequence_end && b->sequence_begin == b->sequence_end &&
           a->sequence_begin == b->sequence_begin;
}
