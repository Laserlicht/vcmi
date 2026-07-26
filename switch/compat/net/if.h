/*
 * if.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#ifndef _VCMI_SWITCH_NET_IF_H
#define _VCMI_SWITCH_NET_IF_H

#include_next <net/if.h>

#ifdef __cplusplus
extern "C" {
#endif

// libnx leaves these commented out and unimplemented; boost::asio references them
// for IPv6 scope-id handling. Stubs live in switch/compat/switch_net_stubs.c -
// VCMI only uses IPv4 TCP, so they are never actually called.
unsigned int if_nametoindex(const char * __name);
char *       if_indextoname(unsigned int __ifindex, char * __ifname);

#ifdef __cplusplus
}
#endif

#endif /* _VCMI_SWITCH_NET_IF_H */
