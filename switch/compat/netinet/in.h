/*
 * netinet/in.h - compatibility shim for Nintendo Switch (devkitA64 + libnx).
 *
 * libnx defines struct in6_addr / ip_mreq but not the IPv6 multicast request
 * struct ipv6_mreq, which boost::asio's socket_types.hpp typedefs. Provide it.
 */
#ifndef _VCMI_SWITCH_NETINET_IN_H
#define _VCMI_SWITCH_NETINET_IN_H

#include_next <netinet/in.h>

#ifndef _VCMI_HAVE_IPV6_MREQ
#define _VCMI_HAVE_IPV6_MREQ
struct ipv6_mreq
{
	struct in6_addr ipv6mr_multiaddr; /* IPv6 multicast address */
	unsigned int    ipv6mr_interface; /* interface index */
};
#endif

#endif /* _VCMI_SWITCH_NETINET_IN_H */
