#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "mem.h"

ssize_t READ(int fd, char *whereto, size_t len)
{
	ssize_t cnt=0;

	while(len>0)
	{
		ssize_t rc;

		rc = read(fd, whereto, len);

		if (rc == -1)
		{
			if (errno != EINTR)
			{
				fprintf(stderr, "READ::read: %d\n", errno);
				return -1;
			}
		}
		else if (rc == 0)
		{
			break;
		}
		else
		{
			whereto += rc;
			len -= rc;
			cnt += rc;
		}
	}

	return cnt;
}

char * genlockfilename(char *in)
{
	char *dummy = (char *)mymalloc(strlen(in) + strlen(".lock") + 1, "lock filename");
	if (!dummy)
	{
		fprintf(stderr, "genlockfilename::malloc: failure");
		return NULL;
	}

	sprintf(dummy, "%s.lock", in);

	return dummy;
}

int lockfile(char *file)
{
	char *lfile = genlockfilename(file);
	int fd;

	if (!lfile)
	{
		fprintf(stderr, "lockfile::genlockfilename: problem creating lockfile-filename");
		return -1;
	}

	do
	{
		fd = open(lfile, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
		if (fd == -1)
		{
			fprintf(stderr, "lockfile: waiting for file '%s' to become available (delete file %s if this is an error)", file, lfile);
			sleep(1);
		}
	}
	while(fd == -1);

	free(lfile);

	return fd;
}

int unlockfile(char *file, int fd)
{
	if (close(fd) == -1)
	{
		fprintf(stderr, "unlockfile::close: problem closing file: %m");
		return -1;
	}

	return unlink(genlockfilename(file));
}
