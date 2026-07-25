// PS Vita build: force-included (via -include, see root CMakeLists.txt's `if(VITA)`
// block) ahead of every translation unit, so these defines land before boost::asio or
// any other dependency header is parsed.
//
// This file intentionally starts small. VitaSDK's newlib is a more complete libc than
// some other homebrew newlib targets, so it is NOT assumed to be missing the same
// POSIX pieces Nintendo Switch's libnx does - guessing wrong here would silently mask
// real problems instead of surfacing them as compiler errors. Entries were added only
// once the Docker cross-compile actually demanded them; see vita/README.md for the
// running list of what was needed and why.
#pragma once

#if defined(__vita__)

// PS Vita has no AF_UNIX domain sockets and no serial ports; disabling these in
// boost::asio avoids pulling in code paths that would need POSIX APIs vitasdk doesn't
// provide (sys/un.h and friends).
#define BOOST_ASIO_DISABLE_LOCAL_SOCKETS
#define BOOST_ASIO_DISABLE_SERIAL_PORT

// backtrace()/backtrace_symbols() are not available on vitasdk's newlib.
#define BOOST_STACKTRACE_USE_NOOP

#endif // __vita__
