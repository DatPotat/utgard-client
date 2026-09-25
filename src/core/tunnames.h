#ifndef UTGARD_TUNNAMES_H
#define UTGARD_TUNNAMES_H

/* The two network adapters Utgard brings up, by fixed names, so that they
   can be waited for: a new one is created only once the old one is gone. */

/* sing-box's TUN (interface_name in the generated config). */
#define UTGARD_SB_TUN     "utgard-sing-box-tun"
#define UTGARD_SB_TUN_W  L"utgard-sing-box-tun"

/* The AmneziaWG tunnel. amneziawg.exe names the adapter after the tunnel,
   and the tunnel after its config file: [a-zA-Z0-9_=+.-]{1,32}. */
#define UTGARD_AWG_TUN_W L"utgard-awg-tun"

#endif
