#ifndef UTGARD_PACLOOP_H
#define UTGARD_PACLOOP_H

/* What a select() result means to a relay or server loop. A socket error
   does not heal, so looping on it would spin a core at 100 %: a relay ends,
   a server that must stay up pauses before the next try. */
typedef enum { PACLOOP_ERROR = -1, PACLOOP_IDLE = 0, PACLOOP_READY = 1 } pacloop_state;

pacloop_state pacloop_select(int select_result);

#endif
