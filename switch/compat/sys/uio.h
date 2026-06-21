/*
 * sys/uio.h - compatibility shim for Nintendo Switch (devkitA64 + libnx, newlib).
 *
 * newlib/libnx does not ship <sys/uio.h>, but it does define `struct iovec`
 * (in <sys/_iovec.h>, also pulled in by <sys/socket.h>) and provides the
 * scatter-gather socket calls recvmsg()/sendmsg(). boost::asio only includes
 * this header for the iovec definition; for sockets it uses recvmsg/sendmsg,
 * not readv/writev. We still declare readv/writev so any header that references
 * them compiles - VCMI only uses TCP sockets, so they are never actually called.
 */
#ifndef _VCMI_SWITCH_SYS_UIO_H
#define _VCMI_SWITCH_SYS_UIO_H

#include <sys/_iovec.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readv(int __fd, const struct iovec * __iov, int __iovcnt);
ssize_t writev(int __fd, const struct iovec * __iov, int __iovcnt);

#ifdef __cplusplus
}
#endif

#endif /* _VCMI_SWITCH_SYS_UIO_H */
