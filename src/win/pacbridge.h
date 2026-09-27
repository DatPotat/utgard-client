#ifndef UTGARD_PACBRIDGE_H
#define UTGARD_PACBRIDGE_H
#include "pac.h"
#include "genconf.h"
#include "pacstore.h"

/* Preparing does not change the running policy. Activation takes ownership
   of script. Disconnect invalidates all existing flows. */
int pacbridge_prepare(genconf_input *in, int enabled, int download_proxy,
                      wchar_t *err, size_t cap);
/* Takes ownership of scripts[0..count). Any proxy result wins; DIRECT is
   returned only when every enabled PAC returns DIRECT. */
void pacbridge_activate(pac_script **scripts, int count);
void pacbridge_disconnect(void);
int pacbridge_active(void);
/* Credentials for the loopback inbound which always selects the active VPN
   profile. The port is reserved before sing-box starts; callers must also
   require a running VPN. */
int pacbridge_proxy(unsigned short *port, char password[65]);
/* Number of PAC files currently participating in each decision. */
int pacbridge_count(void);
#endif
