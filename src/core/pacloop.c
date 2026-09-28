#include "pacloop.h"

pacloop_state pacloop_select(int select_result)
{
    if (select_result < 0) return PACLOOP_ERROR;
    return select_result ? PACLOOP_READY : PACLOOP_IDLE;
}
