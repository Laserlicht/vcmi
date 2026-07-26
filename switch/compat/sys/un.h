/*
 * un.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#ifndef _VCMI_SWITCH_SYS_UN_H
#define _VCMI_SWITCH_SYS_UN_H

#include <sys/socket.h>

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

// newlib/libnx has no Unix-domain sockets and no <sys/un.h>, but boost::asio's
// socket_types.hpp includes it unconditionally for this definition. VCMI only
// uses TCP/IP sockets, so this type is never actually used at runtime.
struct sockaddr_un
{
	sa_family_t sun_family;
	char        sun_path[UNIX_PATH_MAX];
};

#endif /* _VCMI_SWITCH_SYS_UN_H */
