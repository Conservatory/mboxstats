/*
 * Copyright (C) 2006 Folkert van Heusden <folkert@vanheusden.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define _LARGEFILE64_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <syslog.h>
#include "br.h"

buffered_reader::buffered_reader(int cur_fd, int cur_block_size)
{
#ifdef USE_MMAP
	struct stat64 finfo;
#endif

	fd = cur_fd;
	block_size = cur_block_size;
	buffer = NULL;
	buffer_length = buffer_pointer = 0;
	mmap_addr = NULL;

	/* try do mmap */
#ifdef USE_MMAP
	if (fstat64(cur_fd, &finfo) == 0)
	{
		if (!S_ISFIFO(finfo.st_mode))
		{
			/* mmap */
			size_of_file = finfo.st_size;
			cur_offset = mmap_addr = (char *)mmap64(NULL, size_of_file, PROT_READ, MAP_SHARED, cur_fd, 0);
			if (!mmap_addr)
			{
				fprintf(stderr, "mmap64 failed: %d/%s\n", errno, strerror(errno));
			}

			/* advise the kernel how to treat the mmaped region */
			/* FIXME: change to madvise64 as soon as it comes available */
			(void)madvise(mmap_addr, size_of_file, MADV_SEQUENTIAL);

			// fprintf(stderr, "*using mmap*\n");
		}
	}
	else
	{
		fprintf(stderr, "Error obtaining information on fd %d: %d/%s\n", cur_fd, errno, strerror(errno));
	}
#endif

	if (!mmap_addr)
	{
#if (_XOPEN_VERSION >= 600)
		(void)posix_fadvise(cur_fd, 0, 0, POSIX_FADV_SEQUENTIAL); // or POSIX_FADV_NOREUSE?
#endif
	}
}

buffered_reader::~buffered_reader()
{
	free(buffer);

	if (mmap_addr)
		munmap(mmap_addr, size_of_file);
}

int buffered_reader::number_of_bytes_in_buffer(void)
{
	return buffer_length - buffer_pointer;
}

int buffered_reader::garbage_collect(char shrink_buffer)
{
	if (buffer_pointer)
	{
		int n_to_move = number_of_bytes_in_buffer();

		if (n_to_move > 0)
		{
			memmove(&buffer[0], &buffer[buffer_pointer], n_to_move);
			buffer_length -= buffer_pointer;
			buffer_pointer = 0;

			if (shrink_buffer)
			{
				char *dummy = (char *)realloc(buffer, buffer_length + 1);
				if (!dummy)
				{
					fprintf(stderr, "buffered_reader::garbage_collect: realloc failed\n");
					syslog(LOG_EMERG, "buffered_reader::garbage_collect: realloc failed");
					exit(1);
				}

				buffer = dummy;
			}
			buffer[buffer_length] = 0x00;
		}
	}

	return 0;
}

int buffered_reader::read_into_buffer(void)
{
	char *dummy;
	int n_read = 0;

	garbage_collect();

	dummy = (char *)realloc(buffer, buffer_length + block_size + 1);
	if (!dummy)
	{
		fprintf(stderr, "buffered_reader::read_into_buffer: realloc failed\n");
		syslog(LOG_EMERG, "buffered_reader::read_into_buffer: realloc failed");
		exit(1);
	}
	buffer = dummy;

	for(;;)
	{
		n_read = read(fd, &buffer[buffer_length], block_size);

		if (n_read == -1)
		{
			if (errno == EINTR || errno == EAGAIN)
				continue;

			fprintf(stderr, "buffered_reader::read_into_buffer: read failed (%s)\n", strerror(errno));
			syslog(LOG_EMERG, "buffered_reader::read_into_buffer: read failed: %m");
			exit(1);
		}

		buffer_length += n_read;

		break;
	}
	buffer[buffer_length] = 0x00;

	return n_read;
}

char * buffered_reader::read_line(void)
{
	char *out = NULL;

#ifdef USE_MMAP
	if (mmap_addr)
	{
		long long int n_bytes;
		char *lf;
		char *virtual_0x00 = &mmap_addr[size_of_file];

		/* EOF reached? */
		if (!cur_offset)
			return NULL;

		/* determine length of current line */
		lf = (char *)memchr(cur_offset, '\n', (virtual_0x00 - cur_offset));

		if (lf)
			n_bytes = lf - cur_offset;
		else
			n_bytes = virtual_0x00 - cur_offset;

		/* allocate memory & copy string */
		out = (char *)malloc(n_bytes + 1);
		if (!out)
		{
			fprintf(stderr, "buffered_reader::read_line: malloc(%lld) failed\n", n_bytes + 1);
			syslog(LOG_EMERG, "buffered_reader::read_line: malloc(%lld) failed", n_bytes + 1);
			exit(1);
		}

		memcpy(out, cur_offset, n_bytes);
		out[n_bytes] = 0x00;

		if (lf)
			cur_offset = lf + 1;
		else
			cur_offset = NULL;
	}
	else
#endif
	{
		long long int lf_offset = -1;
		long long int n_bytes, search_start;

		if (number_of_bytes_in_buffer() <= 0)
		{
			garbage_collect();

			if (read_into_buffer() == 0)
			{
				// EOF
				return NULL;
			}
		}

		search_start = buffer_pointer;

		for(;;)
		{
			char *dummy = strchr(&buffer[buffer_pointer], '\n');
			if (dummy)
				lf_offset = (long long int)(dummy - buffer);

			if (lf_offset != -1)
				break;

			if (read_into_buffer() == 0)
			{
				lf_offset = buffer_length;
				break;
			}
		}

		n_bytes = lf_offset - buffer_pointer;

		out = strndup(&buffer[buffer_pointer], n_bytes);
		if (!out)
		{
			fprintf(stderr, "buffered_reader::read_line: malloc(%lld) failed\n", n_bytes + 1);
			syslog(LOG_EMERG, "buffered_reader::read_line: malloc(%lld) failed", n_bytes + 1);
			exit(1);
		}

		buffer_pointer = lf_offset + 1;
	}

	return out;
}

off64_t buffered_reader::file_offset(void)
{
	if (mmap_addr)
		return cur_offset - mmap_addr;
	else
		return lseek64(fd, 0, SEEK_CUR);
}
