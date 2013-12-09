/* $Id: array.cpp,v 1.0 2003/04/21 17:45:48 folkert Exp folkert $
 * $Log: array.cpp,v $
 * Revision 1.0  2003/04/21 17:45:48  folkert
 * small fixes
 *
 * Revision 0.95  2003/03/16 14:12:40  folkert
 * *** empty log message ***
 *
 * Revision 0.8  2003/02/20 19:23:36  folkert
 * *** empty log message ***
 *
 * Revision 0.6  2003/02/04 21:26:09  folkert
 * fixed some bugs
 * made strings case-insenstive
 *
 * Revision 0.5  2003/02/03 19:48:55  folkert
 * *** empty log message ***
 *
 */

#include "array.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "main.h"
extern "C" {
#include "mem.h"
}

array::array(int numberofcounters, int n_subs)
{
	assert(numberofcounters >= 1);

	subarrays = NULL;
	nsubarrays = n_subs;

	counters = NULL;
	ncounters = numberofcounters;
	strings = NULL;
	nin = 0;
}

array::~array()
{
	if (counters)
		free(counters);

	if (strings)
		free(strings);
}

void array::setcounter(int index, int subindex, int value)
{
	counters[(index * ncounters) + subindex] = value;
}

int array::addcounter(int index, int subindex, int value)
{
	return counters[(index * ncounters) + subindex] += value;
}

int array::getcounter(int index, int subindex)
{
	return counters[(index * ncounters) + subindex];
}

char * get_email_address(char *in)
{
	char *out;
	char *l = strchr(in, '<');
	if (!l)
	{
		char *space = strchr(in, ' ');
		if (space)
			*space = 0x00;

		return strdup(in);
	}

	out = strdup(l + 1);

	char *r = strchr(out, '>');
	if (r)
		*r = 0x00;

	return out;
}

int array::addstring(char *string, char isemail)
{
	int loop, len=strlen(string);

	/* make lowercase */
	for(loop=0; loop<len; loop++)
		string[loop] = tolower(string[loop]);

	if (isemail)
	{
		char *search_string = get_email_address(string);

		for(loop=0; loop<nin; loop++)
		{
			char * dummy = get_email_address(strings[loop]);

			if (unlikely(strcmp(dummy, search_string) == 0))
			{
				(void)addcounter(loop, 0, 1);
				free(dummy);
				free(search_string);
				return loop;
			}

			free(dummy);
		}

		free(search_string);
	}
	else
	{
                for(loop=0; loop<nin; loop++)
                {
			if (unlikely(strcmp(strings[loop], string) == 0))
			{
				(void)addcounter(loop, 0, 1);
				return loop;
			}
                }
	}

	int index = addelement(string);
	setcounter(index, 0, 1);

	return index;
}

int array::addelement(char *string)
{
	strings = (char **)myrealloc(strings, sizeof(char *) * (nin + 1), "list of strings");
	counters = (long int *)myrealloc(counters, sizeof(long int) * ncounters * (nin + 1), "list of counters");

	if (nsubarrays)
	{
		subarrays = (array **)myrealloc(subarrays, sizeof(array *) * nsubarrays * (nin + 1), "list of subarrays");
		if (!subarrays)
		{
			fprintf(stderr, "cannot allocate sub-arrays\n");
			exit(1);
		}

		for(int loop=0; loop<nsubarrays; loop++)
		{
			int saindex = (nin * nsubarrays) + loop;
			subarrays[saindex] = new array(1, 0);
			if (unlikely(!subarrays[saindex]))
			{
				fprintf(stderr, "cannot allocate subarray\n");
				exit(1);
			}
		}
	}

	strings[nin] = strdup(string);
	for(int loop=0; loop<ncounters; loop++)
		setcounter(nin, loop, 0);

	nin++;

	return nin-1;
}

void array::sort(int subindex)
{
	assert(subindex < ncounters);

	quicksort(subindex, 0, nin);
}

void array::quicksort(int subindex, int begin, int end)
{
	if (end > begin)
	{
		int pivot = getcounter(begin, subindex);
		int l = begin + 1;
		int r = end;

		while(l < r)
		{
			if (getcounter(l, subindex) > pivot)
				l++;
			else
			{
				r--;
				swap_entry(l, r);
			}
		}

		l--;
		swap_entry(begin, l);
		quicksort(subindex, begin, l);
		quicksort(subindex, r, end);
	}
}

void array::swap_entry(int index1, int index2)
{
	char *dummy = strings[index1];
	strings[index1] = strings[index2];
	strings[index2] = dummy;

	if (index1 < 0 || index2 < 0)
	{
		fprintf(stderr, "\nswap_entry: %d %d\n", index1, index2);
		exit(1);
	}

	for(int loop=0; loop<ncounters; loop++)
	{
		int dummy = getcounter(index1, loop);
		setcounter(index1, loop, getcounter(index2, loop));
		setcounter(index2, loop, dummy);
	}
}

array & array::getsubcounter(int index, int subarrayindex)
{
	return *subarrays[(index * nsubarrays) + subarrayindex];
}

array & array::getsubcounter(char *string, int subarrayindex)
{
	int index = -1;

	for(int loop=0; loop<nin; loop++)
	{
		if (unlikely(strcasecmp(strings[loop], string) == 0))
		{
			index = loop;
			break;
		}
	}

	if (index == -1)
	{
		index = addelement(string);
	}

	return *subarrays[(index * nsubarrays) + subarrayindex];
}

char * array::getstring(int index)
{
    if (index>=nin) return NULL;
    else
        return strings[index];
}
