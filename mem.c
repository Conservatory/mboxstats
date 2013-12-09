#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include "main.h"

void * mymalloc(int size, char *what)
{
        void *dummy = malloc(size);
        if (!dummy)
        {
                fprintf(stderr, "failed to allocate %d bytes for %s\n", size, what);
                exit(1);
        }

        return dummy;
}

void * myrealloc(void *oldp, int newsize, char *what)
{
        void *dummy = realloc(oldp, newsize);
        if (!dummy)
        {
                fprintf(stderr, "failed to reallocate to %d bytes for %s\n", newsize, what);
                exit(1);
        }

        return dummy;
}

char * mystrdup(char *in)
{
        char *dummy = strdup(in);
        if (!dummy)
        {
                fprintf(stderr, "failed to copy string '%s': out of memory?\n", in);
                exit(1);
        }

        return dummy;
}

int resize(void **pnt, int n, int *len, int size)
{
        if (unlikely(n == *len))
        {
                int dummylen = (*len) == 0 ? 2 : (*len) * 2;
                void *dummypnt = (void *)myrealloc(*pnt, dummylen * size, "resize()");

                if (!dummypnt)
                {
                        fprintf(stderr, "resize::realloct: Cannot (re-)allocate %d bytes of memory\n", dummylen * size);
                        return -1;
                }

		*len = dummylen;
		*pnt = dummypnt;
        }
	else if (unlikely(n > *len || n<0 || *len<0))
	{
		fprintf(stderr, "resize: fatal memory corruption problem: n > len || n<0 || len<0!\n");
		exit(1);
	}

	return 0;
}

void free_array(void ***array, int *n, int *len)
{
	int loop;

	for(loop=0; loop<*n; loop++)
	{
		free((*array)[loop]);
	}
	free(*array);

	*array = NULL;
	*n = *len = 0;
}
