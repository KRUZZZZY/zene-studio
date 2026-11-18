/* Test the namespace handshake's interrupted, failed and short writes. */
#include <QString>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
int interruptions = 0;
int attempts = 0;
bool shortWrite = false;

ssize_t handshakeWrite(int fd, const void* bytes, size_t size)
{
	++attempts;
	if (interruptions > 0)
	{
		--interruptions;
		errno = EINTR;
		return -1;
	}
	if (shortWrite) { return 0; }
	return ::write(fd, bytes, size);
}
}

#define write handshakeWrite
#include "MountNamespaceTestSupport.h"
#undef write

int main()
{
	int descriptors[2];
	if (::pipe(descriptors) != 0) { return 1; }
	interruptions = 1;
	const bool sent = lmms::testsupport::writeNamespaceHandshakeByte(descriptors[1], 'x');
	char received = 0;
	const bool correct = sent && attempts == 2 && ::read(descriptors[0], &received, 1) == 1 && received == 'x';
	::close(descriptors[0]);
	::close(descriptors[1]);
	if (!correct) { return 2; }
	attempts = 0;
	if (lmms::testsupport::writeNamespaceHandshakeByte(-1, 'x') || errno != EBADF || attempts != 1) { return 3; }
	shortWrite = true;
	if (lmms::testsupport::writeNamespaceHandshakeByte(-1, 'x')) { return 4; }
	return 0;
}
