/* $Id: funcs.cpp,v 1.0 2003/04/21 17:45:48 folkert Exp folkert $
 * $Log: funcs.cpp,v $
 * Revision 1.0  2003/04/21 17:45:48  folkert
 * small fixes
 *
 * Revision 0.95  2003/03/16 14:12:40  folkert
 * *** empty log message ***
 *
 * Revision 0.9  2003/02/20 19:23:36  folkert
 * *** empty log message ***
 *
 * Revision 0.7  2003/02/18 18:41:23  folkert
 * date-conversion now also handles incorrect date-fields
 *
 * Revision 0.6  2003/02/04 21:26:09  folkert
 * made "re:"-stripper strip even more
 *
 * Revision 0.5  2003/02/03 19:48:55  folkert
 * *** empty log message ***
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "main.h"
extern "C" {
#include "mem.h"
}

char * stripstring(char *in)
{
	int len = strlen(in), index=0, loop=0;

	char *dummy = (char *)mymalloc(len+1, "stripped string");
	if (!dummy)
	{
		fprintf(stderr, "malloc failure\n");
		exit(1);
	}

	while(loop<len && (in[loop] == ' ' || in[loop] == '\t'))
		loop++;

	while(strncasecmp(&in[loop], "Re:", 3) == 0)
	{
		loop += 3;

		while(loop<len && (in[loop] == ' ' || in[loop] == '\t'))
			loop++;
	}

	while(loop<len)
	{
		dummy[index]=in[loop];
		index++;
		loop++;
	}
	dummy[index] = 0x00;

	index--;
	while(index >= 0 && (dummy[index] ==' ' || dummy[index] == '\t'))
	{
		dummy[index] = 0x00;
		index--;
	}

	return dummy;
}

/* Thu, 30 Jan 2003 18:42:31 +0100 */
char datestringtofields(char *string, int &year, int &month, int &day, int &wday, int &hour, int &minute, int &second, char **timezone)
{
	char *curpos;

	for(unsigned int loop=0; loop<strlen(string); loop++)
	{
		if (unlikely(string[loop] == '\t'))
			string[loop] = ' ';
	}

	/* weekday */
	char *dummy = strchr(string, ',');
	if (!dummy)
		return 0;
	*dummy = 0x00;

	while(*string == ' ')
		string++;
	if (strcmp(string, "Sun") == 0)
		wday = 1;
	else if (strcmp(string, "Mon") == 0)
		wday = 2;
	else if (strcmp(string, "Tue") == 0)
		wday = 3;
	else if (strcmp(string, "Wed") == 0)
		wday = 4;
	else if (strcmp(string, "Thu") == 0)
		wday = 5;
	else if (strcmp(string, "Fri") == 0)
		wday = 6;
	else if (strcmp(string, "Sat") == 0)
		wday = 7;

	/* day of month */
	curpos = dummy + 1;
	while (*curpos == ' ')
		curpos++;
	dummy = strchr(curpos, ' ');
	if (!dummy)
		return 0;
	*dummy = 0x00;
	day = atoi(curpos);

	/* month */
	curpos = dummy + 1;
	while (*curpos == ' ')
		curpos++;
	dummy = strchr(curpos, ' ');
	if (!dummy)
		return 0;
	*dummy = 0x00;
	if (strcmp(curpos, "Jan") == 0)
		month = 1;
	else if (strcmp(curpos, "Feb") == 0)
		month = 2;
	else if (strcmp(curpos, "Mar") == 0)
		month = 3;
	else if (strcmp(curpos, "Apr") == 0)
		month = 4;
	else if (strcmp(curpos, "May") == 0)
		month = 5;
	else if (strcmp(curpos, "Jun") == 0)
		month = 6;
	else if (strcmp(curpos, "Jul") == 0)
		month = 7;
	else if (strcmp(curpos, "Aug") == 0)
		month = 8;
	else if (strcmp(curpos, "Sep") == 0)
		month = 9;
	else if (strcmp(curpos, "Oct") == 0)
		month = 10;
	else if (strcmp(curpos, "Nov") == 0)
		month = 11;
	else if (strcmp(curpos, "Dec") == 0)
		month = 12;

	/* year */
	curpos = dummy + 1;
	while (*curpos == ' ')
		curpos++;
	dummy = strchr(curpos, ' ');
	if (!dummy)
		return 0;
	*dummy = 0x00;
	year = atoi(curpos);

	/* hour */
	curpos = dummy + 1;
	dummy = strchr(curpos, ':');
	if (!dummy)
		return 0;
	*dummy = 0x00;
	hour = atoi(curpos);

	/* minute */
	curpos = dummy + 1;
	dummy = strchr(curpos, ':');
	if (!dummy)
		return 0;
	*dummy = 0x00;
	minute = atoi(curpos);

	/* second */
	curpos = dummy + 1;
	dummy = strchr(curpos, ' ');
	if (!dummy)
		return 0;
	*dummy = 0x00;
	second = atoi(curpos);

	/* timezone */
	curpos = dummy + 1;
	while( *curpos == ' ')
		curpos++;
	dummy = strchr(curpos, ' ');
	if (dummy)
		*dummy = 0x00;
	if (strlen(curpos) > 0)
		*timezone = curpos;
	else
		*timezone = NULL;

	return 1;
}
