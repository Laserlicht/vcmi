/*
 * net/if.h - compatibility shim for Nintendo Switch (devkitA64 + libnx).
 *
 * libnx ships <net/if.h> but leaves the if_nametoindex()/if_indextoname() family
 * commented out, and does not implement them. boost::asio references them for IPv6
 * scope-id handling. Re-expose the declarations here (the matching link-time stubs
 * live in switch/compat/switch_net_stubs.c) so the headers compile. VCMI only uses
 * IPv4 TCP, so these are never actually called.
 */
#ifndef _VCMI_SWITCH_NET_IF_H
#define _VCMI_SWITCH_NET_IF_H

#include_next <net/if.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned int if_nametoindex(const char * __name);
char *       if_indextoname(unsigned int __ifindex, char * __ifname);

#ifdef __cplusplus
}
#endif

#endif /* _VCMI_SWITCH_NET_IF_H */
