/*
 * switch_net_stubs.c - link-time C-library stubs for Nintendo Switch (devkitA64 + libnx).
 *
 * Provides the handful of POSIX functions that boost::asio / boost reference but that
 * newlib/libnx does not implement. VCMI only uses IPv4 TCP, so most of these are either
 * never meaningfully exercised or have a faithful enough emulation.
 */
#include <net/if.h>
#include <errno.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

/* ---- interface-index helpers (declared-but-commented-out in libnx net/if.h) ---- */
unsigned int if_nametoindex(const char * name)
{
	(void)name;
	return 0; /* "no such interface" */
}

char * if_indextoname(unsigned int ifindex, char * ifname)
{
	(void)ifindex;
	(void)ifname;
	errno = ENXIO;
	return NULL;
}

/* ---- pthread_sigmask: libnx has no per-thread signal masks; treat as a no-op ---- */
int pthread_sigmask(int how, const sigset_t * set, sigset_t * oldset)
{
	(void)how;
	(void)set;
	if(oldset)
		memset(oldset, 0, sizeof(*oldset));
	return 0;
}

/* ---- pause(): no POSIX signals to wait for on libnx ---- */
int pause(void)
{
	errno = ENOSYS;
	return -1;
}

/*
 * pipe(): libnx/newlib has no kernel pipe. boost::asio constructs a self-pipe
 * "select interrupter" eagerly when an io_context is created (used to wake a
 * blocked select()), so a failing stub would make io_context construction throw.
 * Emulate a pipe with a connected loopback TCP socket pair - the same trick used
 * on platforms that lack pipes. Requires socketInitializeDefault() to have run
 * (done in EntryPoint.cpp main() before any io_context is built).
 *
 * fildes[0] = read end, fildes[1] = write end (each end is bidirectional, but asio
 * only writes to [1] and reads from [0]).
 */
int pipe(int fildes[2])
{
	int listener = -1, connector = -1, acceptor = -1;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	if(fildes == NULL)
	{
		errno = EFAULT;
		return -1;
	}

	listener = socket(AF_INET, SOCK_STREAM, 0);
	if(listener < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0; /* let the stack pick an ephemeral port */

	if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		goto fail;
	if(getsockname(listener, (struct sockaddr *)&addr, &addrlen) < 0)
		goto fail;
	if(listen(listener, 1) < 0)
		goto fail;

	connector = socket(AF_INET, SOCK_STREAM, 0);
	if(connector < 0)
		goto fail;
	if(connect(connector, (struct sockaddr *)&addr, addrlen) < 0)
		goto fail;

	acceptor = accept(listener, NULL, NULL);
	if(acceptor < 0)
		goto fail;

	/* Disable Nagle so the 1-byte interrupter wakeups are delivered promptly. */
	{
		int one = 1;
		setsockopt(connector, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		setsockopt(acceptor, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	}

	close(listener);
	fildes[0] = acceptor;  /* read end  */
	fildes[1] = connector; /* write end */
	return 0;

fail:
	if(listener >= 0)
		close(listener);
	if(connector >= 0)
		close(connector);
	if(acceptor >= 0)
		close(acceptor);
	return -1;
}
