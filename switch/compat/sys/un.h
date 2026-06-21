/*
 * sys/un.h - compatibility shim for Nintendo Switch (devkitA64 + libnx, newlib).
 *
 * newlib/libnx has no Unix-domain sockets and no <sys/un.h>, but boost::asio's
 * socket_types.hpp includes it unconditionally for the sockaddr_un definition.
 * VCMI only uses TCP/IP sockets, so this type is never actually used at runtime;
 * we just provide the declaration so the headers compile.
 */
#ifndef _VCMI_SWITCH_SYS_UN_H
#define _VCMI_SWITCH_SYS_UN_H

#include <sys/socket.h>

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

struct sockaddr_un
{
	sa_family_t sun_family;
	char        sun_path[UNIX_PATH_MAX];
};

#endif /* _VCMI_SWITCH_SYS_UN_H */
