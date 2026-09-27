#ifndef UTGARD_PACPROC_H
#define UTGARD_PACPROC_H

#include <windows.h>
#include "genconf.h"
#include "pacstore.h"

typedef struct {
    HANDLE process;
    HANDLE command;
    HANDLE job;
    unsigned short proxy_port;
    char password[65];
    char helper_path[2048];
    int prepared;
} pac_process;

/* Start the restricted PAC worker and fill the PAC-related generator inputs. */
int pacproc_prepare(pac_process *p, genconf_input *in, const pac_store *store,
                    wchar_t *err, size_t cap);
/* Put sing-box in the worker-owned kill-on-close job and hand its wait handle
   to the worker. On success the worker and sing-box no longer depend on UI. */
int pacproc_attach(pac_process *p, HANDLE singbox,
                   wchar_t *err, size_t cap);
void pacproc_cancel(pac_process *p);

/* Proxy used only for an explicit "download PAC through VPN" action. */
int pacproc_proxy(unsigned short *port, char password[65]);
void pacproc_proxy_clear(void);

#endif
