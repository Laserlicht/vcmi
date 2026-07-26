/*
 * switch_compat.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// Force-included (-include) into every translation unit of the Switch build -
// see CMakeLists.txt. Papers over gaps between newlib/libnx and the POSIX
// surface VCMI's dependencies (notably boost::asio) expect.
#ifndef VCMI_SWITCH_COMPAT_H
#define VCMI_SWITCH_COMPAT_H

/* boost::asio pulls in <termios.h> for serial-port support, but newlib's
 * <termios.h> references a non-existent <sys/termios.h> on this target.
 * VCMI never uses serial ports, so disable that part of asio entirely. */
#ifndef BOOST_ASIO_DISABLE_SERIAL_PORT
#define BOOST_ASIO_DISABLE_SERIAL_PORT 1
#endif

/* boost::stacktrace's default backends need <dlfcn.h> / _Unwind_Backtrace with
 * _GNU_SOURCE, neither of which is available on libnx. VCMI only uses stacktrace
 * for crash diagnostics, so select the no-op backend (compiles, returns empty). */
#ifndef BOOST_STACKTRACE_USE_NOOP
#define BOOST_STACKTRACE_USE_NOOP 1
#endif

/* newlib/libnx is missing the BSD ESHUTDOWN errno that boost::asio maps in its
 * error category. Assign a value that does not collide with newlib's range
 * (its highest is ENOTRECOVERABLE = 141; user errors start at __ELASTERROR = 2000). */
#include <errno.h>
#ifndef ESHUTDOWN
#define ESHUTDOWN 142
#endif

/* newlib/libnx <signal.h> lacks some sigaction flags that boost::asio references
 * in its signal handling. Define them as no-ops (0) - VCMI does not rely on the
 * associated behaviour (child-process reaping / restartable syscalls). */
#include <signal.h>
#ifndef SA_RESTART
#define SA_RESTART 0
#endif
#ifndef SA_NOCLDWAIT
#define SA_NOCLDWAIT 0
#endif

#endif /* VCMI_SWITCH_COMPAT_H */
