/*
 * uio.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#ifndef _VCMI_SWITCH_SYS_UIO_H
#define _VCMI_SWITCH_SYS_UIO_H

#include <sys/_iovec.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// newlib/libnx has no <sys/uio.h>; boost::asio only includes it for the iovec
// definition (its socket code uses recvmsg/sendmsg, not readv/writev). Declared
// here so any header referencing them compiles - never actually called.
ssize_t readv(int __fd, const struct iovec * __iov, int __iovcnt);
ssize_t writev(int __fd, const struct iovec * __iov, int __iovcnt);

#ifdef __cplusplus
}
#endif

#endif /* _VCMI_SWITCH_SYS_UIO_H */
