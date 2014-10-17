#define _LARGEFILE64_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>

#ifdef __APPLE__
#include <sys/syslimits.h>
#endif

using namespace std;

#define MAX_TOCC_INDEX 128

//#define _DEBUG
#ifdef _DEBUG
#define DEBUG(x)  	\
	{		\
		x	\
		fflush(stdout);	\
	} while(0);
#else
#define DEBUG(x)
#endif

extern "C" {
#include "io.h"
#include "mem.h"
#include "val.h"
}
#include "array.h"
#include "br.h"
#include "funcs.h"
#include "main.h"

void show_usage(void)
{
	printf("Usage: mboxstats -i inputfile -o outputfile [-a] [-w] [-m] [-h]\n");
	printf("-a     show all data (not just the top 10)\n");
	printf("-n x   show top 'x' (default is 10)\n");
	printf("-l     lock mailbox\n");
	printf("-x     XML output\n");
	printf("-z     omit XML header\n");
	printf("-y     omit empty entries\n");
	printf("-s x   hide e-mail addresses [1=if name is know, 2=always]\n");
	printf("-c     also calculate number of bits information per byte\n");
	printf("-m     parameter for -i is a maildir\n");
	printf("-p     per-user statistics\n");
	printf("-k     abbreviate byte-amounts\n");
	printf("-w     count word-frequency (slows the program considerably down!)\n");
	printf("-Q     only for XML output! adds an url for from, to, cc and subject\n");
	printf("       add '__REPLACE__' in the string which will get the from/etc.\n");
	printf("-V     show version\n");
	printf("-h     this message\n");
}

char * to_xml_replace(char *in)
{
	static char dummy[4096];	/* yes, I know */
	int len = strlen(in);
	int index, oindex=0;

	for(index=0; index<len; index++)
	{
		if (in[index] == '<' ||
		    in[index] == '>' ||
		    in[index] == '&' ||
		    in[index] == ' ')
		{
			sprintf(&dummy[oindex], "&#%d;", in[index]);
			oindex = strlen(dummy);
		}
		else
			dummy[oindex++] = in[index];
	}

	dummy[oindex] = 0x00;

	return dummy;
}

char * hide_email_address(char *in, char hide)
{
	/* really kids: you should not return pointers to static
	 * buffers
	 */
	static char dummy[4096];

	if (hide == 0)
	{
		return in;
	}

	if (in[0] == '"')
		strcpy(dummy, in + 1);
	else
		strcpy(dummy, in);

	char *lt = strchr(dummy, '<');
	/* this will break on systems where NULL != 0
	 * and believe: those systems exist
	 */
	while(lt > dummy && (*lt == '<' || *lt == ' ' || *lt == '"'))
	{
		*lt = 0x00;
		--lt;
	}

	if ((hide == 1 && lt != NULL) || hide == 2)
	{
		char *at = strchr(dummy, '@');
		if (at)
		{
			strcpy(at + 1, "xxx");
		}
	}

	return dummy;
}

char * to_xml_tag(char *in)
{
	static char dummy[4096];
	int len = strlen(in);
	int index, oindex=0;

	if (isdigit(in[0]))
	{
		strcpy(dummy, "address-");
		oindex = strlen(dummy);
	}

	for(index=0; index<len; index++)
	{
		if (isalnum(in[index]) || in[index] == '-' || in[index] == '_' || in[index] == '.')
			dummy[oindex++] = in[index];
		else if (in[index] == ' ')
			dummy[oindex++] = '-';
	}

	dummy[oindex] = 0x00;

	return dummy;
}

char *url_escape(char *in)
{
	int len = strlen(in);
	int loop, index = 0;
	char *out = (char *)mymalloc(len * 3 + 1, "url escape");

	for(loop=0; loop<len; loop++)
	{
		if (isalnum(in[loop]))
			out[index++] = in[loop];
		else
		{
			sprintf(&out[index], "%%%02x", in[loop]);
			index += 3;
		}
	}

	out[index] = 0x00;

	return out;
}

char * emit_url(char *searcher_in, char *string)
{
	char *buffer, *repl, *searcher;

	if (!searcher_in)
		return mystrdup("");

	buffer = (char *)mymalloc(strlen(searcher_in) + 128 + strlen(string) * 5 + 1, "url");

	searcher = mystrdup(searcher_in);
	repl = strstr(searcher, "__REPLACE__");
	*repl = 0x00;

	sprintf(buffer, "<url>%s%s%s</url>\n", searcher, url_escape(string), &repl[11]);

	free(searcher);

	return buffer;
}

char *b2kb(long unsigned int bytes, char abb)
{
	/* FIXME: check for NULL and free() it after use (down there) */
	char *buffer = (char *)mymalloc(64, "b2kb output");
        if (!buffer)
        {
                fprintf(stderr, "out of memory\n");
                exit(1);
        }

	if (abb)
	{
		if (bytes > (1024 * 1024 * 1024))
			sprintf(buffer, "%ldGB", (bytes -1 + (1024 * 1024 * 1024)) / (1024 * 1024 * 1024));
		else if (bytes > (1024 * 1024))
			sprintf(buffer, "%ldMB", (bytes -1 + (1024 * 1024)) / (1024 * 1024));
		else if (bytes > 1024)
			sprintf(buffer, "%ldKB", (bytes -1 + 1024) / 1024);
		else
			sprintf(buffer, "%ld", bytes);
	}
	else
	{
		sprintf(buffer, "%ld", bytes);
	}

	return buffer;
}

int main(int argc, char *argv[])
{
	/* statistics variables */
	long unsigned int Cyear[3000]; // in the year 3000 this program will fail
	long unsigned int Cyearbytes[3000];
	long unsigned int Cmonth[12+1], Cday[31+1], Cwday[7+1], Chour[24], Ctotal=0, total=0;
	long unsigned int Cmonthbytes[12+1], Cdaybytes[31+1], Cwdaybytes[7+1], Chourbytes[24];
	long unsigned int importance_low = 0, importance_normal = 0, importance_high = 0;
	long unsigned int total_lines = 0, total_header = 0;
	long unsigned int total_bytes = 0, total_header_bytes = 0;
	long unsigned int total_line_length = 0;
	char **MessageIDs = NULL;
	time_t *MessageIDst = NULL;
	int nMessageIDs = 0;

	struct tm first_tm;
	memset(&first_tm, 0x00, sizeof(first_tm));
	first_tm.tm_sec  = 59;
	first_tm.tm_min  = 59;
	first_tm.tm_hour = 23;
	first_tm.tm_mday = 31;
	first_tm.tm_mon  = 12 - 1;
	first_tm.tm_year = 2035 - 1900;
	time_t first_ts = mktime(&first_tm);
	time_t last_ts = 0;

	double total_bits = 0.0;
	char xml=0, xml_header=1;
	char he=0;
	char omit_empty=0;
	char abbr=0;
	int top_n = 10;
	double avg_spam_score = 0.0;
	int n_avg_spam_score = 0;
	char bits_per_byte = 0;
	char lock_file = 0;
	char calc_quote = 0;

	/* misc variables */
	DIR *dir = NULL;
	FILE *fh;
	char empty_line = 0;
	int c;
	char *input = NULL, *output = NULL;
	char all = 0, cnt_words = 0;
	char peruser = 0;
	char mbox = 0; /* input is a mailbox-folder */
	int lockfd = -1;
	time_t start, now;
	double fsize=0.0; /* double to prevent overflows */
	/* msg-storage array */
	char **msg = NULL;
	int  msg_n = 0, msg_len = 0;
	char *searcher = NULL;

	memset(Cyear, 0x00, sizeof(Cyear));
	memset(Cmonth, 0x00, sizeof(Cmonth));
	memset(Cday, 0x00, sizeof(Cday));
	memset(Cwday, 0x00, sizeof(Cwday));
	memset(Chour, 0x00, sizeof(Chour));
	memset(Cyearbytes, 0x00, sizeof(Cyearbytes));
	memset(Cmonthbytes, 0x00, sizeof(Cmonthbytes));
	memset(Cdaybytes, 0x00, sizeof(Cdaybytes));
	memset(Cwdaybytes, 0x00, sizeof(Cwdaybytes));
	memset(Chourbytes, 0x00, sizeof(Chourbytes));

	if (argc == 1)
	{
		show_usage();
		return 1;
	}

	/* parse parameters */
        while((c = getopt(argc, argv, "i:o:l:awmphxzs:ykn:cqQ:")) != -1)
        {
                switch(c)
                {
                case 'i':
			input = optarg;
			break;
		case 'o':
			output = optarg;
			break;
		case 'a':
			all = 1;
			break;
		case 'q':
			calc_quote = 1;
			break;
		case 'l':
			lock_file = 1;
			break;
		case 'c':
			bits_per_byte = 1;
			break;
		case 'w':
			cnt_words = 1;
			break;
		case 'm':
			mbox = 1;
			break;
		case 'k':
			abbr = 1;
			break;
		case 'p':
			peruser = 1;
			break;
		case 'x':
			xml=1;
			break;
		case 'z':
			xml_header=0;
			break;
		case 's':
			he = atoi(optarg);
			if (he < 0 || he > 2)
			{
				fprintf(stderr, "-s parameter expects either 0, 1 or 2\n");
				return 1;
			}
			break;
		case 'y':
			omit_empty=1;
			break;
		case 'n':
			top_n = atoi(optarg);
			if (top_n < 1)
			{
				fprintf(stderr, "number of elements to show must be at least 1 (and not %d)\n", top_n);
				return 1;
			}
			break;
		case 'Q':
			searcher = optarg;
			if (strstr(searcher, "__REPLACE__") == NULL)
			{
				fprintf(stderr, "Search-engine replace string misses '__REPLACE__'\n");
				return 1;
			}
			break;
		case 'V':
			printf("mboxstats v" VERSION ", (C) 2003-2005 by folkert@vanheusden.com\n");
			break;
		case 'h':
			show_usage();
			exit(0);
		default:
			show_usage();
			exit(0);
		}
	}

	/* lock file */
	if (mbox == 0 && input != NULL && lock_file == 1)
	{
		lockfd = lockfile(input);
		if (lockfd == -1)
		{
			fprintf(stderr, "Cannot create lockfile!\n");
			return 1;
		}
	}

	array from(13, 4); /* 0=hits, 1=bytes, 2=time, 3=n_lines/msg, 4=wday, 5=dmonth, 6=month, 7=linelength, 8=spam score, 9=# spam */
			   /* 10=n_replies_to + 11=replytime, 12=quote percentage */
			  /* subarray: 0 = useragent, 1=from (recv most msgs
			   * from), 2=to (sent most msgs to), 3=subject */
	array subject(5); /* 0=hits, 1=bytes, 2=time, 3=firstmsg, 4=lastmsg */
	array to(3); /* 0=hits, 1=bytes, 2=time */
	array cc(3); /* 0=hits, 1=bytes, 2=time */
	array words(1);
	array tld(2); /* top level domains (0=hits, 1=bytes) */
	array org(2); /* organization (0=hits, 1=bytes) */
	array useragent(2); /* user-agent (0=hits, 1=bytes) */
	array tz(2); /* timezone 0=hits, 1=bytes */
	array bd(2); /* bussiest day 0=hits, 1=bytes */
	array urls(1);
	int n_is_reply = 0;
	long int n_resp_time = 0;
	int global_is_reply = 0;
	int pgp_signed = 0;
	long long int total_att_size = 0;
	int n_att = 0;

	time(&start);
	for(;;)
	{
		int fd;
		char *file;
		struct dirent *de = NULL;
		char path[PATH_MAX];

		DEBUG(printf("start\n");)

		/* not a mailbox-dir? then treat as file */
		if (mbox == 0)
		{
			file = input;
		}
		else
		{
			/* mailbox-dir */

			/* open directory */
			if (!dir)
			{
				/* create path */
				if (mbox == 1)
					sprintf(path, "%s/cur", input);
				else
					sprintf(path, "%s/new", input);
				if (output) printf("Processing path: %s\n", path);

				/* open directory */
				dir = opendir(path);
				if (!dir)
				{
					fprintf(stderr, "Could not open directory %s!\n", path);
					if (mbox == 1)
					{
						mbox = 2;
						continue;
					}
					else
					{
						break;
					}
				}
			}

			/* try to fetch a file */
			de = readdir(dir);
			if (!de) /* last file in dir, close dir */
			{
				if (closedir(dir) == -1)
				{
					fprintf(stderr, "error closing director!\n");
					break;
				}

				dir = NULL;

				/* both boxes done? exit */
				if (mbox == 2)
					break;

				/* just did the cur-box, continue with the new-box */
				mbox = 2;

				/* restart the loop */
				continue;
			}

			/* create path */
			if (mbox == 1)
				sprintf(path, "%s/cur/%s", input, de -> d_name);
			else
				sprintf(path, "%s/new/%s", input, de -> d_name);

			file = path;

			/* check for directories (skip them) */
			struct stat64 st;
			if (stat64(file, &st) == -1) /* file probably went away */
			{
				if (errno == ENOENT)
					continue;

				fprintf(stderr, "problem fetching parameters on file %s!\n", file);
				return 1;
			}

			/* is dir? then skip */
			if (S_ISDIR(st.st_mode))
				continue;
		}

		DEBUG(printf("open file %s\n", file);)

		/* open the file */
		if (file == NULL)
			fd = 0;
		else
			fd = open64(file, O_RDONLY);
		if (fd == -1)
		{
			if (errno == ENOENT) /* file deleted? */
			{
				fprintf(stderr, "file %s is not there!\n", file);
				break; /* continue */
			}

			/* otherwhise: error situation */
			fprintf(stderr, "error opening file %s\n", input);
			return 1;
		}

		/* get filesize */
		if (fd != 0)
		{
			struct stat64 buf;
			if (fstat64(fd, &buf) == -1)
			{
				fprintf(stderr, "error getting filesize: %s\n", strerror(errno));
				return 1;
			}
			fsize = (double)buf.st_size;
		}

		/* start up buffered reader */
		buffered_reader bf(fd);

		for(;;)
		{
			char *line;

			DEBUG(printf("read_line\n");)
			line = bf.read_line();

			/* grow array if neccessary */
			DEBUG(printf("resize\n");)
			if (resize((void **)&msg, msg_n, &msg_len, sizeof(char *)) == -1)
			{
				fprintf(stderr, "Memory allocation problem!\n");
				break;
			}


			if (line != NULL && strlen(line) == 0)	/* ignore empty lines */
			{
				DEBUG(printf("ignore empty line\n");)
				empty_line = 1;
				msg[msg_n++] = NULL;
				free(line);
			}
			else if (line == NULL || (strncmp(line, "From ", 5) == 0 && empty_line == 1))	/* finished reading a message? */
			{
				int year=0, month=0, day=0, wday=0, hour=0, minute=0, second=0;
				int from_index = -1, to_index[MAX_TOCC_INDEX], n_to_index=0, subject_index = -1;
				char *to_str[MAX_TOCC_INDEX];
				int cc_index[MAX_TOCC_INDEX], n_cc_index=0;
				int loop, header_len = 0;
				unsigned long int msg_bytes = 0;
				char header = 1;
				char *fromfld = NULL;
				char *ua = NULL;
				char *subj = NULL;
				int add_org = -1;
				double spam_score = 0.0;
				char is_spam = 0;
				int cur_tz = -1;
				int cur_tld = -1;
				int cur_bd = -1;
				int cur_line_length = 0;
				time_t ts_t = (time_t)0;
				char *cur_msgid = NULL;
				char *in_reply_to = NULL;
				char *timezone = NULL;
				char *boundary = NULL;
				char msg_state = MS_TEXT;
				char interesting_attachment = 0;
				int quoted_lines = 0, original_lines = 0;
				char signature = 0;

				free(line);
				empty_line = 0;

				for(loop=0; loop<128; loop++)
					to_index[loop] = -1;

				/* keep track of total number of messages */
				total++;

				/* end of message, process */
				DEBUG(printf("end of msg, process\n");)
				for(loop=0; loop<msg_n; loop++)
				{
					if (likely(msg[loop]))
					{
						msg_bytes += strlen(msg[loop]);
						if (bits_per_byte)
							total_bits += calc_nbits_in_data((unsigned char *)msg[loop], strlen(msg[loop]));
					}

					if (msg[loop] == NULL && header) /* end of header */
					{
						/* keep track of total of lines */
						total_lines += (msg_n - header_len);
						total_header += header_len;
						header = 0;
					}
					else if (header)	/* header */
					{
						char *bound_dum;

						header_len++; /* length of header */

						total_header_bytes += strlen(msg[loop]);

						DEBUG(printf("do header\n");)
						if (strncmp(msg[loop], "From:", 5) == 0)
						{
							fromfld = stripstring(&msg[loop][6]);
							char *dummy = mystrdup(fromfld);
							from_index = from.addstring(dummy, 1);
							char *dot = strrchr(dummy, '.');
							if (dot)
							{
								char *end = strchr(dot, '>');
								if (end)
									*end = 0x00;
								cur_tld = tld.addstring(dot + 1);
							}
							free(dummy);
						}
						else if (strncmp(msg[loop], "Message-ID:", 11) == 0)
						{
							char *dummy = strchr(&msg[loop][12], '<');
							if (dummy)
							{
								cur_msgid = mystrdup(dummy);
								dummy = strchr(cur_msgid, '>');
								if (dummy) *dummy = 0x00;
							}
						}
						else if (strncmp(msg[loop], "In-Reply-To:", 12) == 0)
						{
							char *dummy = strchr(&msg[loop][12], '<');
							if (dummy)
							{
								in_reply_to = mystrdup(dummy);
								dummy = strchr(in_reply_to, '>');
								if (dummy) *dummy = 0x00;
							}

							global_is_reply++;
						}
						else if (strncmp(msg[loop], "Subject:", 8) == 0)
						{
							subj = stripstring(&msg[loop][9]);
							subject_index = subject.addstring(subj);
						}
						else if (strncmp(msg[loop], "To:", 3) == 0)
						{
							char *dummy = stripstring(&msg[loop][4]);
							char *komma = strchr(dummy, ',');
							char *pnt = dummy;
							while (komma)
							{
								*komma = 0x00;
								to_index[n_to_index] = to.addstring(pnt, 1);
								to_str[n_to_index++] = mystrdup(pnt);
								pnt = komma + 1;
								while(*pnt == ' ') pnt++;
								komma = strchr(pnt, ',');
							}
							if (strlen(pnt) > 0)
							{
								to_index[n_to_index] = to.addstring(pnt, 1);
								to_str[n_to_index++] = mystrdup(pnt);
							}
							free(dummy);
						}
						else if (strncasecmp(msg[loop], "CC:", 3) == 0)
						{
							char *dummy = stripstring(&msg[loop][4]);
							char *komma = strchr(dummy, ',');
							char *pnt = dummy;
							while (komma)
							{
								*komma = 0x00;
								cc_index[n_cc_index++] = cc.addstring(pnt, 1);
								pnt = komma + 1;
								while(*pnt == ' ') pnt++;
								komma = strchr(pnt, ',');
							}
							if (strlen(pnt) > 0)
								cc_index[n_cc_index++] = cc.addstring(pnt, 1);
							free(dummy);
						}
						else if (strncmp(msg[loop], "Importance:", 11) == 0)
						{
							char *dummy = stripstring(&msg[loop][12]);
							if (strcasecmp(dummy, "Low") == 0)
								importance_low++;
							else if (strcasecmp(dummy, "Normal") == 0)
								importance_normal++;
							else if (strcasecmp(dummy, "High") == 0)
								importance_high++;
							free(dummy);
						}
						else if (strncmp(msg[loop], "Organization:", 13) == 0)
						{
							char *dummy = stripstring(&msg[loop][14]);
							add_org = org.addstring(dummy, 0);
							free(dummy);
						}
						else if (strncmp(msg[loop], "User-Agent:", 11) == 0)
						{
							char *dummy = stripstring(&msg[loop][12]);
							ua = mystrdup(&msg[loop][12]);
							free(dummy);
						}
						else if (strncmp(msg[loop], "X-Mailer:", 9) == 0)
						{
							char *dummy = stripstring(&msg[loop][10]);
							ua = mystrdup(&msg[loop][10]);
							free(dummy);
						}
						else if (strncmp(msg[loop], "X-Spam-Status:", 14) == 0)
						{
							/* X-Spam-Status: No, score=0.0 required=5.0 tests=HTML_MESSAGE autolearn=failed */
							char *dummy = strstr(&msg[loop][14], "score=");
							if (dummy)
							{
								spam_score = atof(dummy + 6);
								avg_spam_score += spam_score;
								n_avg_spam_score++;

								char *req = strstr(&msg[loop][14], "required=");
								if (req)
								{
									if (spam_score >= atof(req + 9))
										is_spam = 1;
								}
							}
						}
						else if ((bound_dum = strstr(msg[loop], " boundary=")) != NULL)
						{
							char *dummy = strchr(bound_dum, '"');
							if (dummy)
							{
								int len = strlen(dummy + 1);
								boundary = (char *)mymalloc(len + 1, "boundary");
								memcpy(boundary, dummy + 1, len);
								boundary[len] = 0x00;
								dummy = strchr(boundary, '"');
								if (dummy)
									*dummy = 0x00;
							}
							else
							{
								bound_dum += 10;
								int len = strlen(bound_dum);
								boundary = (char *)mymalloc(len + 1, "boundary");
								memcpy(boundary, bound_dum, len);
								boundary[len] = 0x00;
								dummy = strchr(boundary, ' ');
								if (dummy) *dummy = 0x00;
								dummy = strchr(boundary, '\t');
								if (dummy) *dummy = 0x00;
								dummy = strchr(boundary, ';');
								if (dummy) *dummy = 0x00;
							}
						}
						else if (strncmp(msg[loop], "Date:", 5) == 0)
						{
							char *dummy = stripstring(&msg[loop][6]);

							if (datestringtofields(dummy, year, month, day, wday, hour, minute, second, &timezone))
							{
								if (month > 12 || day > 31 || wday > 7 || hour > 23 || year < 0)
								{
									if (output) printf("Invalid date-line: %s\n", &msg[loop][6]);
									month = day = wday = hour = 0;
								}
								else
								{
									struct tm ts;

									Cyear[year]++;
									Cmonth[month]++;
									Cday[day]++;
									Cwday[wday]++;
									Chour[hour]++;
									Ctotal++;

									memset(&ts, 0x00, sizeof(ts));
									ts.tm_sec  = second;
									ts.tm_min  = minute;
									ts.tm_hour = hour;
									ts.tm_mday = day;
									ts.tm_mon  = month - 1;
									ts.tm_year = year - 1900;
									ts_t = mktime(&ts);

									if (ts_t != (time_t)-1 && ts_t < first_ts)
										first_ts = ts_t;

									if (ts_t != (time_t)-1 && ts_t > last_ts)
										last_ts = ts_t;

									if (ts_t == (time_t)-1)
										ts_t = 0;

									/* bussiest day */
									char buffer[128];
									sprintf(buffer, "%04d-%02d-%02d", year, month, day);
									cur_bd = bd.addstring(buffer);
								}

								DEBUG(printf("add tz\n");)
								if (timezone)
									cur_tz = tz.addstring(timezone, 1);
								DEBUG(printf("tz done\n");)
							}

							free(dummy);
						}
					}
					else if (!signature)
					{
						if (msg[loop] != NULL)		/* message-text */
						{
							int len = strlen(msg[loop]);

							DEBUG(printf("do msg-text\n");)

							char *pnt;

							cur_line_length += len;

							pnt = msg[loop];
							while((pnt = strstr(pnt, "http://")))
							{
								char *end = strchr(pnt, ' ');
								if (end)
									*end = 0x00;
								else
									end = &pnt[strlen(pnt)];

								urls.addstring(pnt);

								pnt = end + 1;
							}

							pnt = msg[loop];
							while((pnt = strstr(pnt, "ftp://")))
							{
								char *end = strchr(pnt, ' ');
								if (end)
									*end = 0x00;
								else
									end = &pnt[strlen(pnt)];

								urls.addstring(pnt);

								pnt = end + 1;
							}

							if (strcmp(msg[loop], "--") == 0 || strcmp(msg[loop], "-- ") == 0)
							{
								signature = 1;
							}

							if (strncmp(msg[loop], "-----BEGIN PGP SIGNED MESSAGE-----", 34) == 0 ||
							    strncmp(msg[loop], "-----BEGIN PGP SIGNATURE-----", 29) == 0)
							{
								pgp_signed++;
							}

							/* determine if its quoted text or not */
							pnt = msg[loop];
							while(*pnt == ' ' || *pnt == '\t') pnt++;
							if (*pnt == '>')
								quoted_lines++;
							else
								original_lines++;

							/* start of mime thing? */
							if (boundary != NULL && msg[loop][0] == '-' && msg[loop][1] == '-' && strcmp(&msg[loop][2], boundary) == 0)
							{
								if (msg_state == MS_DATA_LOOK)
								{
									// do anything?
								}

								msg_state = MS_HEADER;
								interesting_attachment = 0;
							}
							else if (msg_state == MS_HEADER)
							{
								// is a file-attachment?
								if (strstr(msg[loop], "filename="))
								{
									interesting_attachment = 1;
									n_att++;
								}
							}
							else if (msg_state == MS_DATA_LOOK)
							{
								total_att_size += len;
							}

							if (cnt_words)
							{
								char *dummy = msg[loop];

								for(;;)
								{
									char *end, stop=0;

									while(toupper(*dummy) < 'A' && toupper(*dummy) > 'Z' && *dummy != 0x00)
										dummy++;

									if (!*dummy)
										break;

									end = dummy;
									while(toupper(*end) >= 'A' && toupper(*end) <= 'Z' && *end != 0x00)
									{
										*end = tolower(*end);
										end++;
									}

									if (*end == 0x00)
										stop = 1;
									else
										*end = 0x00;

									if ((end-dummy) > 1 && (end-dummy) < 11)
										words.addstring(dummy);

									if (stop)
										break;

									dummy = end + 1;
								}
							}
						}
						else
						{
							if (interesting_attachment)
								msg_state = MS_DATA_LOOK;
							else
								msg_state = MS_DATA;
						}
					}
					else /* signature */
					{
					}
				}

				DEBUG(printf("msg processed, overall stats\n");)

				total_bytes += msg_bytes;
				if (year >= 0 && year < 3000)
					Cyearbytes[year] += msg_bytes;
				Cmonthbytes[month] += msg_bytes;
				Cdaybytes[day] += msg_bytes;
				Cwdaybytes[wday] += msg_bytes;
				Chourbytes[hour] += msg_bytes;

				/* keep track of compose-time of each message */
				if (ts_t != (time_t)0 && cur_msgid != NULL)
				{
					MessageIDs = (char **)myrealloc(MessageIDs, (nMessageIDs + 1) * sizeof(char *), "MessageIDs");
					MessageIDs[nMessageIDs] = cur_msgid;
					MessageIDst = (time_t *)myrealloc(MessageIDst, (nMessageIDs + 1) * sizeof(time_t), "MessageIDs time_t");

					if (timezone)
						MessageIDst[nMessageIDs++] = ts_t - (86400 * atoi(timezone) / 100);
					else
						MessageIDst[nMessageIDs++] = ts_t ;
				}

				if (add_org != -1)
					org.addcounter(add_org, 1, msg_bytes);

				if (cur_tz != -1)
					tz.addcounter(cur_tz, 1, msg_bytes);

				if (cur_tld != -1)
					tld.addcounter(cur_tld, 1, msg_bytes);

				if (cur_bd != -1)
					bd.addcounter(cur_bd, 1, msg_bytes);

				total_line_length += cur_line_length;

				/* per user number of bytes & time*/
				DEBUG(printf("per user\n");)
				int time_index = (hour * 60) + minute;
				if (from_index != -1)
				{
					from.addcounter(from_index, 1, msg_bytes);
					from.addcounter(from_index, 2, time_index);
					from.addcounter(from_index, 3, msg_n - header_len);
					/* user-agent */
					if (ua)
					{
						DEBUG(printf("user agent\n");)
						from.getsubcounter(from_index, 0).addstring(ua, 1);
						free(ua);
					}

					/* subject */
					if (subj)
					{
						DEBUG(printf("subject\n");)
						from.getsubcounter(from_index, 3).addstring(subj, 1);
						free(subj);
					}

					from.addcounter(from_index, 4, wday);
					from.addcounter(from_index, 5, day);
					from.addcounter(from_index, 6, month);
					from.addcounter(from_index, 7, cur_line_length);
					from.addcounter(from_index, 8, (int)(spam_score * 1000.0));
					if (is_spam)
						from.addcounter(from_index, 9, 1);

					if (in_reply_to)
					{
						int id_loop;

						if (ts_t != (time_t)0)
						{
							for(id_loop=0; id_loop<nMessageIDs; id_loop++)
							{
								if (strcasecmp(MessageIDs[id_loop], in_reply_to) == 0)
								{
									from.addcounter(from_index, 10, 1);	/* N */
									/* abs: in case someone wrote a reply before the original message */
									from.addcounter(from_index, 11, abs(ts_t - MessageIDst[id_loop])); /* ^T */

									n_is_reply++;
									n_resp_time += abs(ts_t - MessageIDst[id_loop]);
								}
							}
						}

						free(in_reply_to);
					}
					from.addcounter(from_index, 12, (int)(((double)quoted_lines / (double)(quoted_lines + original_lines)) * 1000.0));
				}
				if (n_to_index > 0)
				{
					DEBUG(printf("to stats\n");)
					for(int loop=0; loop<n_to_index; loop++)
					{
						DEBUG(printf("%d [%d] [%s]\n", loop, to_index[loop], to_str[loop]);)
						to.addcounter(to_index[loop], 1, msg_bytes);
						DEBUG(printf("-- 2\n");)
						to.addcounter(to_index[loop], 2, time_index);
						DEBUG(printf("-- 3 -> %d\n", from_index);)
						if (from_index != -1)
						{
							from.getsubcounter(from_index, 2).addstring(to_str[loop], 1);
						}
						if (fromfld)
						{
							DEBUG(printf("%s %s\n", fromfld, to_str[loop]);)
							from.getsubcounter(to_str[loop], 1).addstring(fromfld, 1);
						}
						DEBUG(printf("-- 4\n");)
						free(to_str[loop]);
					}
					DEBUG(printf("-- 5\n");)
					free(fromfld);
					DEBUG(printf("-- 6\n");)
				}
				if (n_cc_index > 0)
				{
					DEBUG(printf("cc stats\n");)
					for(int loop=0; loop<n_cc_index; loop++)
					{
						cc.addcounter(cc_index[loop], 1, msg_bytes);
						cc.addcounter(cc_index[loop], 2, time_index);
					}
				}
				if (subject_index != -1)
				{
					DEBUG(printf("subject stats\n");)
					subject.addcounter(subject_index, 1, msg_bytes);
					subject.addcounter(subject_index, 2, time_index);
					if (ts_t != 0)
					{
						if (ts_t < subject.getcounter(subject_index, 3) || subject.getcounter(subject_index, 3) == 0)
							subject.setcounter(subject_index, 3, ts_t);

						if (ts_t > subject.getcounter(subject_index, 4))
							subject.setcounter(subject_index, 4, ts_t);
					}
				}


				free_array((void ***)&msg, &msg_n, &msg_len);
				free(boundary);

				DEBUG(printf("done\n");)
			}
			else
			{
				empty_line = 0;

				msg[msg_n++] = line;
			}

			if (mbox == 0)
			{
				time(&now);

				if (unlikely((now - start) > 1))
				{
					start = now;

					if (output && fd != 0)
					{
						printf("%3.2f%%\r", ((double)bf.file_offset() * 100.0) / fsize);
						fflush(stdout);
					}
				}
			}
			else
			{
				if (output)
					printf("%s    \r", file);
			}

			if (!line)
				break;
		}

		close(fd);

		if (mbox == 0)
			break;
	}

	if (output) printf("Done parsing input file. Generating statistics...\n");

	if (mbox == 0 && input != NULL && lock_file == 1)
	{
		if (unlockfile(input, lockfd) != 0)
		{
			fprintf(stderr, "Problem unlocking file!\nContinuing...\n");
		}
	}

	if (output)
		fh = fopen(output, "w");
	else
		fh = stdout;
	if (!fh)
	{
		fprintf(stderr, "Cannot create file %s!\n", output);
		return 1;
	}

	char *weekday[7+1] = { "", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
	char *month[12+1] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	time(&now);
	int loop, more_then_1=0;
	for(loop=0; loop<from.getN(); loop++)
	{
		if (from.getcounter(loop, 0) > 1)
			more_then_1++;
	}
	/* for every user, find the most-used mail-client */
	DEBUG(printf("top ua\n");)
	for(loop=0; loop<from.getN(); loop++)
	{
		from.getsubcounter(loop, 0).sort(0);
	}
	/* get em */
	int spammiest=-1;
	double highest_spam_score = -9999999.9;
	int n_spams=0, spammer=-1;
	int qr_total = 0, qr_n = 0;
	for(loop=0; loop<from.getN(); loop++)
	{
		char *string = from.getsubcounter(loop, 0).getstring(0);
		if (string)
		{
			int ua_i = useragent.addstring(string);
			useragent.addcounter(ua_i, 1, from.getcounter(loop, 1));
		}

		if (n_avg_spam_score)
		{
			double cur_score = from.getcounter(loop, 8) / (double)(from.getcounter(loop, 0) * 1000.0);

			if (cur_score > highest_spam_score)
			{
				spammiest = loop;
				highest_spam_score = cur_score;
			}

			if (from.getcounter(loop, 9) > n_spams)
			{
				spammer = loop;
				n_spams = from.getcounter(loop, 9);
			}
		}

		if (from.getcounter(loop, 0))
		{
			int dummy = from.getcounter(loop, 12) / from.getcounter(loop, 0);
			from.setcounter(loop, 12, dummy);
			qr_total += dummy;
			qr_n++;
		}
	}

	char *cur_time = mystrdup(ctime(&now));
	char *lf = strchr(cur_time, '\n');
	if (lf) *lf = 0x00;
	char *first_msg = mystrdup(ctime(&first_ts));
	lf = strchr(first_msg, '\n');
	if (lf) *lf = 0x00;
	char *last_msg = mystrdup(ctime(&last_ts));
	lf = strchr(last_msg, '\n');
	if (lf) *lf = 0x00;
	if (xml)
	{
		if (xml_header)
			fprintf(fh, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
		fprintf(fh, "<mailbox-stats>\n");
		fprintf(fh, "	<global-stats>\n");
		fprintf(fh, "		<generated-at>%s</generated-at>\n", cur_time);
		fprintf(fh, "		<first-message>%s</first-message>\n", first_msg);
		fprintf(fh, "		<last-message>%s</last-message>\n", last_msg);
		fprintf(fh, "		<totals>\n");
		fprintf(fh, "			<n-messages>%ld</n-messages>\n", total);
		fprintf(fh, "			<n-is-reply>%d</n-is-reply>\n", global_is_reply);
		if (n_is_reply)
		{
			int avg_resp = n_resp_time / n_is_reply;
			fprintf(fh, "			<avg-resp-time>%02d:%02d:%02d</avg-resp-time>\n", avg_resp / 3600, (avg_resp / 60) % 60, avg_resp % 60);
		}
		fprintf(fh, "			<n-pgp-signed>%d</n-pgp-signed>\n", pgp_signed);
		fprintf(fh, "			<total-size>%s</total-size>\n", b2kb(total_bytes, abbr));
		fprintf(fh, "			<avg-size>%s</avg-size>\n", b2kb(total_bytes / total, abbr));
		fprintf(fh, "			<n-attachments>%d</n-attachments>\n", n_att);
		fprintf(fh, "			<att-size>%s</att-size>\n", b2kb(total_att_size, abbr));
		if (calc_quote)
			fprintf(fh, "			<avg-quote-percentage>%.2f</avg-quote-percentage>\n", (double)qr_total / (double)(qr_n * 10));
		bd.sort(0);
		fprintf(fh, "			<bussiest-day-in-n day=\"%s\"><n-msgs>%d</n-msgs><n-bytes>%s</n-bytes></bussiest-day-in-n>\n",
									bd.getstring(0), bd.getcounter(0, 0), b2kb(bd.getcounter(0, 1), abbr));
		bd.sort(1);
		fprintf(fh, "			<bussiest-day-in-bytes day=\"%s\"><n-msgs>%d</n-msgs><n-bytes>%s</n-bytes></bussiest-day-in-bytes>\n",
									bd.getstring(0), bd.getcounter(0, 0), b2kb(bd.getcounter(0, 1), abbr));
		fprintf(fh, "			<n-writers>%d</n-writers>\n", from.getN());
		fprintf(fh, "			<wrote-more-then-1-message>%d</wrote-more-then-1-message>\n", more_then_1);
		fprintf(fh, "			<n-lines>%ld</n-lines>\n", total_lines);
		fprintf(fh, "			<header-size>%ld</header-size>\n", total_header);
		fprintf(fh, "			<n-user-agents>%d</n-user-agents>\n", useragent.getN());
		fprintf(fh, "			<n-organisations>%d</n-organisations>\n", org.getN());
		fprintf(fh, "			<n-toplevel-domains>%d</n-toplevel-domains>\n", tld.getN());
		if (n_avg_spam_score)
		{
			fprintf(fh, "			<avg-spam-score>%f</avg-spam-score>\n", avg_spam_score / ((double)n_avg_spam_score));
			if (spammiest != -1)
				fprintf(fh, "				<spammiest-writer><score>%f</score><name>%s</name></spammiest-writer>\n", highest_spam_score, to_xml_replace(hide_email_address(from.getstring(spammiest), he)));
			if (spammer != -1)
				fprintf(fh, "			<most-spam-by n=\"%d\">%s</most-spam-by>\n", n_spams, to_xml_replace(hide_email_address(from.getstring(spammer), he)));
		}
		fprintf(fh, "		</totals>\n");
		fprintf(fh, "		<averages>\n");
		fprintf(fh, "			<lines-per-message>%ld</lines-per-message>\n", total_lines / total);
		fprintf(fh, "			<lines-per-header>%ld</lines-per-header>\n", total_header / total);
		fprintf(fh, "			<header-percent-of-message>%.2f%%</header-percent-of-message>\n", ((double)total_header * 100.0) / (double)total_lines);
		fprintf(fh, "			<header-percent-of-total>%.2f%%</header-percent-of-total>\n", ((double)total_header_bytes * 100.0) / (double)total_bytes);
		fprintf(fh, "			<line-length>%ld</line-length>\n", total_line_length / total_lines);
		if (bits_per_byte)
			fprintf(fh, "			<bits-per-byte>%.4f</bits-per-byte>\n", total_bits / (double)total_bytes);
		fprintf(fh, "		</averages>\n");
		if (total)
		{
			fprintf(fh, "		<importance>\n");
			fprintf(fh, "			<low>%.2f%%</low>\n", ((double)importance_low * 100.0) / (double)total);
			fprintf(fh, "			<normal>%.2f%%</normal>\n", ((double)importance_normal * 100.0) / (double)total);
			fprintf(fh, "			<high>%.2f%%</high>\n", ((double)importance_high * 100.0) / (double)total);
			fprintf(fh, "		</importance>\n");
		}
		fprintf(fh, "\n");
		fprintf(fh, "	</global-stats>\n");
	}
	else
	{
		fprintf(fh, "Overall statistics\n");
		fprintf(fh, "------------------\n");
		fprintf(fh, "Statistics created on: %s\n", cur_time);
		fprintf(fh, "First message was written at: %s\n", first_msg);
		fprintf(fh, "Last message was written at: %s\n", last_msg);
		fprintf(fh, "Total number of messages: %ld\n", total);
		fprintf(fh, "Number of messages that is a reply: %d (%.2f%%)\n", global_is_reply, (double)(global_is_reply * 100) / (double)total);
		fprintf(fh, "Total size: %s\n", b2kb(total_bytes, abbr));
		fprintf(fh, "Average size: %s\n", b2kb(total_bytes / total, abbr));
		fprintf(fh, "Total number of attachments: %d\n", n_att);
		fprintf(fh, "Total size of attachments: %s (%.2f%% of total)\n", b2kb(total_att_size, abbr), (total_att_size * 100.0) / ((double)total_bytes));
		if (calc_quote)
			fprintf(fh, "Average quote percentage: %.2f%%\n", (double)qr_total / (double)(qr_n * 10));
		if (n_is_reply)
		{
			int avg_resp = n_resp_time / n_is_reply;
			fprintf(fh, "Average response time: %02d:%02d:%02d\n", avg_resp / 3600, (avg_resp / 60) % 60, avg_resp % 60);
		}
		fprintf(fh, "Number of PGP signed messages: %d (%.2f%%)\n", pgp_signed, ((double)pgp_signed * 100.0) / total);
		bd.sort(0);
		fprintf(fh, "Most busy day in # msgs: %s (%d msgs, %s bytes)\n", bd.getstring(0), bd.getcounter(0, 0), b2kb(bd.getcounter(0, 1), abbr));
		bd.sort(1);
		fprintf(fh, "Most busy day in # bytes: %s (%d msgs, %s bytes)\n", bd.getstring(0), bd.getcounter(0, 0), b2kb(bd.getcounter(0, 1), abbr));
		fprintf(fh, "Total number of writers: %d\n", from.getN());
		fprintf(fh, "Number of people who wrote >1 message: %d (%.2f%%)\n", more_then_1, (double)(more_then_1 * 100) / (double)from.getN());
		fprintf(fh, "Total number of lines: %ld\n", total_lines);
		if (total) fprintf(fh, "Average lines per message: %ld\n", total_lines / total);
		fprintf(fh, "Total header length (lines): %ld\n", total_header);
		if (total) fprintf(fh, "Average header length (lines): %ld (%.2f%%)\n", total_header / total, (double)(total_header * 100) / (double)total_lines);
		if (total_lines) fprintf(fh, "Average line length: %ld\n", total_line_length / total_lines);
		if (total_bytes) fprintf(fh, "The header is %.2f%% bytes in size of the total.\n", ((double)total_header_bytes * 100.0) / (double)total_bytes);
		if (bits_per_byte)
		{
			if (total_bytes) fprintf(fh, "Average number of bits information per byte: %.4f\n", total_bits / (double)total_bytes);
		}
		fprintf(fh, "Total number of unique user-agents: %d\n", useragent.getN());
		fprintf(fh, "Total number of unique organisations: %d\n", org.getN());
		fprintf(fh, "Total number of unique top-level domains: %d\n", tld.getN());
		if (n_avg_spam_score)
		{
			fprintf(fh, "Average spam score: %.2f\n", avg_spam_score / ((double)n_avg_spam_score));
			if (spammiest != -1)
				fprintf(fh, "Spammiest writer: %s (%.2f)\n", hide_email_address(from.getstring(spammiest), he), highest_spam_score);
			if (spammer != -1)
				fprintf(fh, "Most spam by: %s (%d)\n", hide_email_address(from.getstring(spammer), he), n_spams);
		}
		fprintf(fh, "\n");

		if (total)
		{
			DEBUG(printf("importance\n");)
			fprintf(fh, "Importance\n");
			fprintf(fh, "----------\n");
			fprintf(fh, "Low   : %.2f%%\n", ((double)importance_low * 100.0) / (double)total);
			fprintf(fh, "Normal: %.2f%%\n", ((double)importance_normal * 100.0) / (double)total);
			fprintf(fh, "High  : %.2f%%\n", ((double)importance_high * 100.0) / (double)total);
			fprintf(fh, "(the rest is unspecified)\n");
		}
		fprintf(fh, "\n");
	}

	DEBUG(printf("top writers\n");)
	if (xml)
		fprintf(fh, "	<top-writers>\n");
	else
	{
		fprintf(fh, "Top writers\n");
		fprintf(fh, "   | # msgs|av size| total|time | e-mail address\n");
		fprintf(fh, "---+-------+-------+------+-----+--------------------------------\n");
	}
	from.sort(0);
	for(loop=0; loop<(all?from.getN():top_n); loop++)
	{
		char *string = from.getstring(loop);
		if (!string) break;

		if (from.getcounter(loop, 0))
		{
			int dummy = from.getcounter(loop, 2) / from.getcounter(loop, 0);
			if (xml)
			{
				fprintf(fh, "		<top-writer rank=\"%d\">\n", loop+1);
				fprintf(fh, "			<e-mail-addr>%s</e-mail-addr>\n", to_xml_replace(hide_email_address(string, he)));
				fprintf(fh, "%s", emit_url(searcher, string));
				fprintf(fh, "			<n-messages>%d</n-messages>\n", from.getcounter(loop, 0));
				fprintf(fh, "			<avg-size>%s</avg-size>\n", b2kb(from.getcounter(loop, 1) / from.getcounter(loop, 0), abbr));
				fprintf(fh, "			<total-size>%s</total-size>\n", b2kb(from.getcounter(loop, 1), abbr));
				fprintf(fh, "			<mostly-written-at>%02d:%02d</mostly-written-at>\n", dummy/60, dummy%60);
				fprintf(fh, "		</top-writer>\n");
			}
			else
				fprintf(fh, "%3d] %6d|%7s|%6s|%02d:%02d| %s\n", loop+1, from.getcounter(loop, 0), b2kb(from.getcounter(loop, 1) / from.getcounter(loop, 0), abbr), b2kb(from.getcounter(loop, 1), abbr), (dummy / 60), (dummy % 60), hide_email_address(string, he));
		}
		else if (!xml)
			fprintf(fh, "%3d] %6d|                   | %s\n", loop+1, from.getcounter(loop, 0), hide_email_address(string, he));
	}
	if (xml)
		fprintf(fh, "	</top-writers>\n");
	else
		fprintf(fh, "\n");

	DEBUG(printf("top subjects\n");)
	if (xml)
		fprintf(fh, "	<top-subjects>\n");
	else
	{
		fprintf(fh, "Top subjects\n");
		fprintf(fh, "   | # msgs|av size| total|time | subject\n");
		fprintf(fh, "---+-------+-------+------+-----+--------------------------------\n");
	}
	subject.sort(0);
	for(loop=0; loop<(all?subject.getN():top_n); loop++)
	{
		char *string = subject.getstring(loop);
		if (!string) break;

		if (subject.getcounter(loop, 0))
		{
			int dummy = subject.getcounter(loop, 2) / subject.getcounter(loop, 0);
			if (xml)
			{
				fprintf(fh, "		<top-subject rank=\"%d\">\n", loop+1);
				fprintf(fh, "			<subject>%s</subject>\n", to_xml_replace(string));
				fprintf(fh, "%s", emit_url(searcher, string));
				fprintf(fh, "			<n-messages>%d</n-messages>\n", subject.getcounter(loop, 0));
				fprintf(fh, "			<avg-size>%s</avg-size>\n", b2kb(subject.getcounter(loop, 1) / subject.getcounter(loop, 0), abbr));
				fprintf(fh, "			<total-size>%s</total-size>\n", b2kb(subject.getcounter(loop, 1), abbr));
				fprintf(fh, "			<mostly-written-at>%02d:%02d</mostly-written-at>\n", dummy/60, dummy%60);
				fprintf(fh, "			<first-msg>%d</first-msg>\n", subject.getcounter(loop, 3));
				fprintf(fh, "			<last-msg>%d</last-msg>\n", subject.getcounter(loop, 4));
				fprintf(fh, "		</top-subject>\n");
			}
			else
				fprintf(fh, "%3d] %6d|%7s|%6s|%02d:%02d| %s\n", loop+1, subject.getcounter(loop, 0), b2kb(subject.getcounter(loop, 1) / subject.getcounter(loop, 0), abbr), b2kb(subject.getcounter(loop, 1), abbr), (dummy / 60), (dummy % 60), hide_email_address(string, he));
		}
		else if (!xml)
			fprintf(fh, "%3d] %6d|                   | %s\n", loop+1, subject.getcounter(loop, 0), hide_email_address(string, he));
	}
	if (xml)
		fprintf(fh, "	</top-subjects>\n");
	else
		fprintf(fh, "\n");

	DEBUG(printf("top receivers\n");)
	if (xml)
		fprintf(fh, "	<top-receivers>\n");
	else
	{
		fprintf(fh, "Top receivers\n");
		fprintf(fh, "   | # msgs|av size| total|time | e-mail address\n");
		fprintf(fh, "---+-------+-------+------+-----+--------------------------------\n");
	}
	to.sort(0);
	for(loop=0; loop<(all?to.getN():top_n); loop++)
	{
		char *string = to.getstring(loop);
		if (!string) break;

		if (to.getcounter(loop, 0))
		{
			int dummy = to.getcounter(loop, 2) / to.getcounter(loop, 0);
			if (xml)
			{
				fprintf(fh, "		<top-receiver rank=\"%d\">\n", loop+1);
				fprintf(fh, "			<e-mail-addr>%s</e-mail-addr>\n", to_xml_replace(hide_email_address(string, he)));
				fprintf(fh, "%s", emit_url(searcher, string));
				fprintf(fh, "			<n-messages>%d</n-messages>\n", to.getcounter(loop, 0));
				fprintf(fh, "			<avg-size>%s</avg-size>\n", b2kb(to.getcounter(loop, 1) / to.getcounter(loop, 0), abbr));
				fprintf(fh, "			<total-size>%s</total-size>\n", b2kb(to.getcounter(loop, 1), abbr));
				fprintf(fh, "			<mostly-written-at>%02d:%02d</mostly-written-at>\n", dummy/60, dummy%60);
				fprintf(fh, "		</top-receiver>\n");
			}
			else
				fprintf(fh, "%3d] %6d|%7s|%6s|%02d:%02d| %s\n", loop+1, to.getcounter(loop, 0), b2kb(to.getcounter(loop, 1) / to.getcounter(loop, 0), abbr), b2kb(to.getcounter(loop, 1), abbr), (dummy / 60), (dummy % 60), hide_email_address(string, he));
		}
		else if (!xml)
			fprintf(fh, "%3d] %6d|                   | %s\n", loop+1, to.getcounter(loop, 0), hide_email_address(string, he));
	}
	if (xml)
		fprintf(fh, "	</top-receivers>\n");
	else
		fprintf(fh, "\n");

	if (cc.getN())
	{
		DEBUG(printf("top ccers\n");)
		if (xml)
			fprintf(fh, "	<top-ccers>\n");
		else
		{
			fprintf(fh, "Top CC'ers\n");
			fprintf(fh, "   | # msgs|av size| total|time | e-mail address\n");
			fprintf(fh, "---+-------+-------+------+-----+--------------------------------\n");
		}
		cc.sort(0);
		for(loop=0; loop<(all?cc.getN():top_n); loop++)
		{
			char *string = cc.getstring(loop);
			if (!string) break;

			if (cc.getcounter(loop, 0))
			{
				int dummy = cc.getcounter(loop, 2) / cc.getcounter(loop, 0);
				if (xml)
				{
					fprintf(fh, "		<top-ccers rank=\"%d\">\n", loop+1);
					fprintf(fh, "			<e-mail-addr>%s</e-mail-addr>\n", to_xml_replace(hide_email_address(string, he)));
					fprintf(fh, "%s", emit_url(searcher, string));
					fprintf(fh, "			<n-messages>%d</n-messages>\n", cc.getcounter(loop, 0));
					fprintf(fh, "			<avg-size>%s</avg-size>\n", b2kb(cc.getcounter(loop, 1) / cc.getcounter(loop, 0), abbr));
					fprintf(fh, "			<total-size>%s</total-size>\n", b2kb(cc.getcounter(loop, 1), abbr));
					fprintf(fh, "			<mostly-written-at>%02d:%02d</mostly-written-at>\n", dummy/60, dummy%60);
					fprintf(fh, "		</top-ccers>\n");
				}
				else
					fprintf(fh, "%3d] %6d|%7s|%6s|%02d:%02d| %s\n", loop+1, cc.getcounter(loop, 0), b2kb(cc.getcounter(loop, 1) / cc.getcounter(loop, 0), abbr), b2kb(cc.getcounter(loop, 1), abbr), (dummy / 60), (dummy % 60), hide_email_address(string, he));
			}
			else if (!xml)
				fprintf(fh, "%3d] %6d|                   | %s\n", loop+1, cc.getcounter(loop, 0), hide_email_address(string, he));
		}
		if (xml)
			fprintf(fh, "	</top-ccers>\n");
		else
			fprintf(fh, "\n");
	}

	DEBUG(printf("top tld\n");)
	if (xml)
		fprintf(fh, "	<top-level-domains>\n");
	else
	{
		fprintf(fh, "Top of top-level-domain\n");
		fprintf(fh, "   |tld | KB/MB  | n\n");
		fprintf(fh, "---+----+--------+----------------------------------------\n");
	}
	tld.sort(0);
	for(loop=0; loop<(all?tld.getN():top_n); loop++)
	{
		char *string = tld.getstring(loop);
		if (!string) break;

		if (xml)
		{
			if ((omit_empty && strlen(string) > 0) || omit_empty == 0)
			{
				fprintf(fh, "		<tld rank=\"%d\">\n", loop + 1);
				fprintf(fh, "			<name>%s</name>\n", to_xml_replace(string));
				fprintf(fh, "			<freq>%d</freq>\n", tld.getcounter(loop, 0));
				fprintf(fh, "			<avg-size>%s</avg-size>\n", b2kb(tld.getcounter(loop, 1) / tld.getcounter(loop, 0), abbr));
				fprintf(fh, "			<total-size>%s</total-size>\n", b2kb(tld.getcounter(loop, 1), abbr));
				fprintf(fh, "		</tld>\n");
			}
		}
		else
			fprintf(fh, "%2d] %4s (%6s) %d\n", loop+1, string, b2kb(tld.getcounter(loop, 1), abbr), tld.getcounter(loop, 0));
	}
	if (xml)
		fprintf(fh, "	</top-level-domains>\n");
	else
		fprintf(fh, "\n");

	if (tz.getN())
	{
		DEBUG(printf("top timezone\n");)
		if (xml)
			fprintf(fh, "	<top-timezones>\n");
		else
		{
			fprintf(fh, "Timezones\n");
			fprintf(fh, "   | n  | KB/MB  |tld\n");
			fprintf(fh, "---+----+--------+----------------------------------------\n");
		}
		tz.sort(0);
		for(loop=0; loop<(all?tz.getN():top_n); loop++)
		{
			char *string = tz.getstring(loop);
			if (!string) break;

			if (xml)
			{
				if ((omit_empty && strlen(string) > 0) || omit_empty == 0)
				{
					fprintf(fh, "		<tz rank=\"%d\">\n", loop + 1);
					fprintf(fh, "			<name>%s</name>\n", to_xml_replace(string));
					fprintf(fh, "			<freq>%d</freq>\n", tz.getcounter(loop, 0));
					fprintf(fh, "			<avg-size>%s</avg-size>\n", b2kb(tz.getcounter(loop, 1) / tz.getcounter(loop, 0), abbr));
					fprintf(fh, "			<total-size>%s</total-size>\n", b2kb(tz.getcounter(loop, 1), abbr));
					fprintf(fh, "		</tz>\n");
				}
			}
			else
				fprintf(fh, "%2d] %4d (%6s) %s\n", loop+1, tz.getcounter(loop, 0), b2kb(tz.getcounter(loop, 1), abbr), string);
		}
		if (xml)
			fprintf(fh, "	</top-timezones>\n");
		else
			fprintf(fh, "\n");
	}

	if (org.getN())
	{
		DEBUG(printf("top org\n");)
		if (xml)
			fprintf(fh, "	<top-organisations>\n");
		else
		{
			fprintf(fh, "Top organisations\n");
			fprintf(fh, "   | n  | KB/MB  | Organisation\n");
			fprintf(fh, "---+----+--------+----------------------------------------\n");
		}
		org.sort(0);
		for(loop=0; loop<(all?org.getN():top_n); loop++)
		{
			char *string = org.getstring(loop);
			if (!string) break;

			if (xml)
			{
				if ((omit_empty && strlen(string) > 0) || omit_empty == 0)
				{
					fprintf(fh, "		<org rank=\"%d\">\n", loop + 1);
					fprintf(fh, "			<name>%s</name>\n", to_xml_replace(string));
					fprintf(fh, "			<freq>%d</freq>\n", org.getcounter(loop, 0));
					fprintf(fh, "			<bytes>%s</bytes>\n", b2kb(org.getcounter(loop, 1), abbr));
					fprintf(fh, "		</org>\n");
				}
			}
			else
				fprintf(fh, "%2d] %4d (%6s) %s\n", loop+1, org.getcounter(loop, 0), b2kb(org.getcounter(loop, 1), abbr), string);
		}
		if (xml)
			fprintf(fh, "	</top-organisations>\n");
		else
			fprintf(fh, "\n");
	}

	/* useragents */
	if (useragent.getN())
	{
		if (xml)
			fprintf(fh, "	<top-user-agents>\n");
		else
		{
			fprintf(fh, "Top user agents\n");
			fprintf(fh, "(number of unique user agents, NOT the amount of messages per user agent)\n");
			fprintf(fh, "   | n  | KB/MB  |User agent\n");
			fprintf(fh, "---+----+--------+----------------------------------------\n");
		}
		useragent.sort(0);
		for(loop=0; loop<(all?useragent.getN():top_n); loop++)
		{
			char *string = useragent.getstring(loop);
			if (!string) break;

			if (xml)
			{
				if ((omit_empty && strlen(string) > 0) || omit_empty == 0)
				{
					fprintf(fh, "		<useragent rank=\"%d\">\n", loop + 1);
					fprintf(fh, "			<name>%s</name>\n", to_xml_replace(string));
					fprintf(fh, "			<freq>%d</freq>\n", useragent.getcounter(loop, 0));
					fprintf(fh, "			<bytes>%s</bytes>\n", b2kb(useragent.getcounter(loop, 1), abbr));
					fprintf(fh, "		</useragent>\n");
				}
			}
			else
				fprintf(fh, "%2d] %4d (%6s) %s\n", loop+1, useragent.getcounter(loop, 0), b2kb(useragent.getcounter(loop, 1), abbr), string);
		}
		if (xml)
			fprintf(fh, "	</top-user-agents>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);
	}

	if (Ctotal)
	{
		DEBUG(printf("top mpm\n");)
			long unsigned int max_day = 0;
		for(loop=1; loop<=7; loop++)
			max_day = max(Cwday[loop], max_day);
		DEBUG(printf("max/day: %ld\n", max_day);)
			if (xml)
				fprintf(fh, "	<messages-per-day>\n");
			else
			{
				fprintf(fh, "Messages per day\n");
				fprintf(fh, "----------------------------------------------------------\n");
			}
		for(loop=1; loop<=7; loop++)
		{
			if (xml)
				fprintf(fh, "		<%s><msgs>%ld</msgs><bytes>%s</bytes></%s>\n", weekday[loop], Cwday[loop],  b2kb(Cwdaybytes[loop], abbr), weekday[loop]);
			else
			{
				fprintf(fh, "%9s %5ld (%6s)", weekday[loop], Cwday[loop], b2kb(Cwdaybytes[loop], abbr));

				for(int loop2=0; loop2<(double(Cwday[loop]) * (32.0 / double(max_day))); loop2++)
					fprintf(fh, "*");
				fprintf(fh, "\n");
			}
		}
		if (xml)
			fprintf(fh, "	</messages-per-day>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);

		long unsigned int max_year = 0;
		for(loop=0; loop<3000; loop++)
			max_year = max(Cyear[loop], max_year);
		DEBUG(printf("max/year: %ld\n", max_year);)
			if (xml)
				fprintf(fh, "   <messages-per-year>\n");
			else
			{
				fprintf(fh, "Messages per year\n");
				fprintf(fh, "----------------------------------------------------------\n");
			}
		loop=0;
		while(Cyear[loop] == 0 && loop < 3000) loop++;
		int loop_end=2999;
		while(Cyear[loop_end] == 0 && loop_end > 0) loop_end--;
		loop_end++;
		for(; loop<=loop_end; loop++)
		{
			if (xml)
				fprintf(fh, "           <year%d><msgs>%ld</msgs><bytes>%s</bytes></year%d>\n", loop, Cyear[loop], b2kb(Cyearbytes[loop], abbr), loop);
			else
			{
				fprintf(fh, "%d %5ld (%6s)", loop, Cyear[loop], b2kb(Cyearbytes[loop], abbr));

				for(int loop2=0; loop2<(double(Cyear[loop]) * (39.0 / double(max_year))); loop2++)
					fprintf(fh, "*");
				fprintf(fh, "\n");
			}
		}
		if (xml)
			fprintf(fh, "   </messages-per-year>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);


		long unsigned int max_month = 0;
		for(loop=1; loop<=12; loop++)
			max_month = max(Cmonth[loop], max_month);
		DEBUG(printf("max/month: %ld\n", max_month);)
			if (xml)
				fprintf(fh, "	<messages-per-month>\n");
			else
			{
				fprintf(fh, "Messages per month\n");
				fprintf(fh, "----------------------------------------------------------\n");
			}
		for(loop=1; loop<=12; loop++)
		{
			if (xml)
				fprintf(fh, "		<%s><msgs>%ld</msgs><bytes>%s</bytes></%s>\n", month[loop], Cmonth[loop], b2kb(Cmonthbytes[loop], abbr), month[loop]);
			else
			{
				fprintf(fh, "%s %5ld (%6s)", month[loop], Cmonth[loop], b2kb(Cmonthbytes[loop], abbr));

				for(int loop2=0; loop2<(double(Cmonth[loop]) * (39.0 / double(max_month))); loop2++)
					fprintf(fh, "*");
				fprintf(fh, "\n");
			}
		}
		if (xml)
			fprintf(fh, "	</messages-per-month>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);

		if (xml)
			fprintf(fh, "	<messages-per-day-of-month>\n");
		else
		{
			fprintf(fh, "Messages per day-of-the-month\n");
			fprintf(fh, "----------------------------------------------------------\n");
		}
		long unsigned int max_dmonth = 0;
		for(loop=1; loop<=31; loop++)
			max_dmonth = max(Cday[loop], max_dmonth);
		DEBUG(printf("max/dmonth: %ld\n", max_dmonth);)
			for(loop=1; loop<=31; loop++)
			{
				if (xml)
					fprintf(fh, "		<day-%d><msgs>%ld</msgs><bytes>%s</bytes></day-%d>\n", loop, Cday[loop], b2kb(Cdaybytes[loop], abbr), loop);
				else
				{
					fprintf(fh, "%2d %5ld (%6s)", loop, Cday[loop], b2kb(Cdaybytes[loop], abbr));

					for(int loop2=0; loop2<(double(Cday[loop]) * (39.0 / double(max_day))); loop2++)
						fprintf(fh, "*");
					fprintf(fh, "\n");
				}
			}
		if (xml)
			fprintf(fh, "	</messages-per-day-of-month>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);


		if (xml)
			fprintf(fh, "	<messages-per-hour>\n");
		else
		{
			fprintf(fh, "Messages per hour\n");
			fprintf(fh, "----------------------------------------------------------\n");
		}
		long unsigned int max_hour = 0;
		for(loop=1; loop<=23; loop++)
			max_hour = max(Chour[loop], max_hour);
		DEBUG(printf("max/hour: %ld\n", max_hour);)
			for(loop=1; loop<=23; loop++)
			{
				if (xml)
					fprintf(fh, "		<hour-%d><msgs>%ld</msgs><bytes>%s</bytes></hour-%d>\n", loop, Chour[loop], b2kb(Chourbytes[loop], abbr), loop);
				else
				{
					fprintf(fh, "%2d %5ld (%6s)", loop, Chour[loop], b2kb(Chourbytes[loop], abbr));

					for(int loop2=0; loop2<(double(Chour[loop]) * (40.0 / double(max_hour))); loop2++)
						fprintf(fh, "*");
					fprintf(fh, "\n");
				}
			}
		if (xml)
			fprintf(fh, "	</messages-per-hour>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);
	}

	if (cnt_words)
	{
		DEBUG(printf("top words\n");)
			if (xml)
				fprintf(fh, "	<word-frequency>\n");
			else
			{
				fprintf(fh, "Most used words\n");
				fprintf(fh, "     |count | word\n");
				fprintf(fh, "-----+------+---------------------------\n");
			}
		words.sort(0);
		for(loop=0; loop<(all?words.getN():top_n); loop++)
		{
			char *string = words.getstring(loop);
			if (!string) break;

			if (xml)
				fprintf(fh, "		<%s>%d</%s>\n", string, words.getcounter(loop, 0), string);
			else
				fprintf(fh, "%5d] %5d| %s\n", loop+1, words.getcounter(loop, 0), string);
		}
		if (xml)
			fprintf(fh, "	</word-frequency>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);
	}

	if (urls.getN())
	{
		if (xml)
			fprintf(fh, "	<urls>\n");
		else
		{
			fprintf(fh, "URLs\n");
			fprintf(fh, "   | n   | URL\n");
			fprintf(fh, "---+-----+------------------------------------------------\n");
		}

		urls.sort(0);
		for(loop=0; loop<(all?urls.getN():top_n); loop++)
		{
			char *dummy = urls.getstring(loop);
			if (!dummy) break;

			if (xml)
				fprintf(fh, "		<url-%d><freq>%d</freq><url>%s</url></url-%d>\n", loop+1, urls.getcounter(loop, 0), to_xml_replace(dummy), loop + 1);
			else
				fprintf(fh, "%2d] %5d %s\n", loop+1, urls.getcounter(loop, 0), dummy);
		}

		if (xml)
			fprintf(fh, "	</urls>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);
	}

	/* to quick replyers */
	array avg_resp(2);

	for(loop=0; loop<from.getN(); loop++)
	{
		if (from.getcounter(loop, 10) && from.getstring(loop) != NULL)
		{
			int index = avg_resp.addstring(from.getstring(loop), 0);
			avg_resp.setcounter(index, 0, from.getcounter(loop, 11) / from.getcounter(loop, 10));
			avg_resp.setcounter(index, 1, from.getcounter(loop, 10));
		}
	}

	if (avg_resp.getN())
	{
		int n_done = 1;

		if (xml)
			fprintf(fh, "	<top-avg-resp>\n");
		else
		{
			fprintf(fh, "Top quick replyers\n");
			fprintf(fh, "   |interval| n   | URL\n");
			fprintf(fh, "---+--------+-----+------------------------------------------\n");
		}

		avg_resp.sort(0);
		for(loop=avg_resp.getN()-1; loop>=(all?0:max(0,avg_resp.getN()-(1 + top_n))); loop--)
		{
			int cur_avg_resp = avg_resp.getcounter(loop, 0);
			char *string = avg_resp.getstring(loop);
			if (!string) break;

			if (xml)
			{
				fprintf(fh, "		<resp-pers rank=\"%d\">\n", n_done);
				fprintf(fh, "			<name>%s</name>\n", to_xml_replace(hide_email_address(avg_resp.getstring(loop), he)));
				fprintf(fh, "			<avg-resp-time>%02d:%02d:%02d</avg-resp-time>\n", cur_avg_resp / 3600, (cur_avg_resp / 60) % 60, cur_avg_resp % 60);
				fprintf(fh, "			<n-replies>%d</n-replies>\n", avg_resp.getcounter(loop, 1));
				fprintf(fh, "		</resp-pers>\n");
			}
			else
				fprintf(fh, "%2d] %02d:%02d:%02d (%3d) %s\n", n_done, cur_avg_resp / 3600, (cur_avg_resp / 60) % 60, cur_avg_resp % 60, avg_resp.getcounter(loop, 1), hide_email_address(string, he));

			n_done++;
		}

		if (xml)
			fprintf(fh, "	</top-avg-resp>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);
	}

	if (calc_quote)
	{
		if (xml)
			fprintf(fh, "	<top-quoters>\n");
		else
		{
			fprintf(fh, "Top quoters\n");
			fprintf(fh, "   |Perc. | n   | Who\n");
			fprintf(fh, "---+------+-----+-----------------------------------------\n");
		}
		from.sort(12);
		for(loop=0; loop<(all?from.getN():top_n); loop++)
		{
			char *string = from.getstring(loop);
			if (!string) break;

			if (xml)
			{
				fprintf(fh, "		<quoter rank=\"%d\">\n", loop + 1);
				fprintf(fh, "			<name>%s</name>\n", to_xml_replace(hide_email_address(string, he)));
				fprintf(fh, "			<n-msgs>%d</n-msgs>\n", from.getcounter(loop, 0));
				fprintf(fh, "			<avg-percentage>%.2f%%</avg-percentage>\n", (double)from.getcounter(loop, 12) / 10.0);
				fprintf(fh, "		</quoter>\n");
			}
			else
				fprintf(fh, "%2d] %3.2f%% (%3d) %s\n", loop + 1, (double)from.getcounter(loop, 12) / 10.0, from.getcounter(loop, 0), hide_email_address(string, he));
		}
		if (xml)
			fprintf(fh, "	</top-quoters>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);
	}

	if (peruser)
	{
		DEBUG(printf("top per-user\n");)
			if (xml)
				fprintf(fh, "	<per-user-stats>\n");
			else
			{
				fprintf(fh, "Per-user statistics\n");
				fprintf(fh, "----------------------------------------------------------\n");
			}
		for(loop=0; loop<from.getN(); loop++)
		{
			int div = from.getcounter(loop, 0)?from.getcounter(loop, 0):1;
			int dummy = from.getcounter(loop, 2) / div;
			DEBUG(printf("tpu 1\n");)
				if (xml)
				{
					fprintf(fh, "	<person>\n");
					fprintf(fh, "		<who>%s</who>\n", to_xml_replace(hide_email_address(from.getstring(loop), he)));
				}
				else
					fprintf(fh, "Stats for %s\n", hide_email_address(from.getstring(loop), he));

			if (from.getcounter(loop, 0))
			{
				if (xml)
				{
					fprintf(fh, "		<n-msgs>%d</n-msgs>\n", from.getcounter(loop, 0));
					if (from.getcounter(loop, 10))
						fprintf(fh, "		<avg-response-time>%d</avg-response-time>\n", from.getcounter(loop, 11) / from.getcounter(loop, 10));
					fprintf(fh, "		<total-bytes>%s</total-bytes>\n", b2kb(from.getcounter(loop, 1), abbr));
					fprintf(fh, "		<avg-bytes>%s</avg-bytes>\n", b2kb(from.getcounter(loop, 1) / div, abbr));
					fprintf(fh, "		<avg-timestamp>%02d:%02d</avg-timestamp>\n", dummy / 60, dummy % 60);
					fprintf(fh, "		<most-active-weekday>%s</most-active-weekday>\n", weekday[from.getcounter(loop, 4) / div]);
					fprintf(fh, "		<most-active-dayofmonth>%d</most-active-dayofmonth>\n", from.getcounter(loop, 5) / div);
					fprintf(fh, "		<most-active-month>%s</most-active-month>\n", month[from.getcounter(loop, 6) / div]);
				}
				else
				{
					fprintf(fh, "# msgs: %d, total # bytes: %s, avg # bytes: %s, time: %02d:%02d, # lines/msg: %d\n",
							from.getcounter(loop, 0),
							b2kb(from.getcounter(loop, 1), abbr),
							b2kb(from.getcounter(loop, 1) / div, abbr),
							(dummy / 60), (dummy % 60),
							from.getcounter(loop, 3) / div
					       );
					fprintf(fh, "Most active week-day: %s, day-of-month: %d, month: %s\n",
							weekday[from.getcounter(loop, 4) / div],
							from.getcounter(loop, 5) / div,
							month[from.getcounter(loop, 6) / div]);
					if (from.getcounter(loop, 10))
					{
						int avg_resp = from.getcounter(loop, 11) / from.getcounter(loop, 10);

						fprintf(fh, "Average response time: %02d:%02d:%02d\n", avg_resp / 3600, (avg_resp / 60) % 60, avg_resp % 60);
					}
				}
				if (from.getcounter(loop, 3))
				{
					if (xml)
						fprintf(fh, "		<avg-line-length>%d</avg-line-length>\n", from.getcounter(loop, 7) / from.getcounter(loop, 3));
					else
						fprintf(fh, "Avg. line length: %d\n", from.getcounter(loop, 7) / from.getcounter(loop, 3));
				}
				if (n_avg_spam_score)
				{
					if (xml)
					{
						fprintf(fh, "			<avg-spam-score>%f</avg-spam-score>\n", from.getcounter(loop, 8) / (double)(from.getcounter(loop, 0) * 1000));
						fprintf(fh, "			<n-spam-messages>%d</n-spam-messages>\n", from.getcounter(loop, 9));
					}
					else
					{
						fprintf(fh, "Avg. spam score: %.3f, # spam messages: %d\n", from.getcounter(loop, 8) / (double)(from.getcounter(loop, 0) * 1000), from.getcounter(loop, 9));
					}
				}
			}
			if (from.getsubcounter(loop, 0).getN())
			{
				from.getsubcounter(loop, 0).sort(0);	/* useragent */
				if (xml)
					fprintf(fh, "		<most-used-user-agent>\n			<user-agent>%s</user-agent>\n			<n>%d</n>\n		</most-used-user-agent>\n", to_xml_replace(from.getsubcounter(loop, 0).getstring(0)), from.getsubcounter(loop, 0).getcounter(0, 0));
				else
					fprintf(fh, "Most used useragent    : %s (%d)\n", from.getsubcounter(loop, 0).getstring(0), from.getsubcounter(loop, 0).getcounter(0, 0));
			}
			if (from.getsubcounter(loop, 1).getN())
			{
				from.getsubcounter(loop, 1).sort(0);	/* from (most recv msgs from) */
				if (xml)
					fprintf(fh, "		<most-msg-recv-from>\n			<from>%s</from>\n			<n>%d</n>\n		</most-msg-recv-from>\n", to_xml_replace(hide_email_address(from.getsubcounter(loop, 1).getstring(0), he)), from.getsubcounter(loop, 1).getcounter(0, 0));
				else
					fprintf(fh, "Most msgs received from: %s (%d)\n", hide_email_address(from.getsubcounter(loop, 1).getstring(0), he), from.getsubcounter(loop, 1).getcounter(0, 0));
			}
			if (from.getsubcounter(loop, 2).getN())
			{
				from.getsubcounter(loop, 2).sort(0);	/* to (most msgs sent to) */
				if (xml)
					fprintf(fh, "		<most-msg-send-to>\n			<to>%s</to>\n			<n>%d</n>\n		</most-msg-send-to>\n", to_xml_replace(hide_email_address(from.getsubcounter(loop, 2).getstring(0), he)), from.getsubcounter(loop, 2).getcounter(0, 0));
				else
					fprintf(fh, "Most msgs sent to      : %s (%d)\n", hide_email_address(from.getsubcounter(loop, 2).getstring(0), he), from.getsubcounter(loop, 2).getcounter(0, 0));
			}
			if (from.getsubcounter(loop, 3).getN())
			{
				from.getsubcounter(loop, 3).sort(0);	/* most used subject */
				if (xml)
					fprintf(fh, "		<most-used-subject>\n			<subject>%s</subject>\n			<n>%d</n>\n		</most-used-subject>\n", to_xml_replace(from.getsubcounter(loop, 3).getstring(0)), from.getsubcounter(loop, 3).getcounter(0, 0));
				else
					fprintf(fh, "Most used subject      : %s (%d)\n", from.getsubcounter(loop, 3).getstring(0), from.getsubcounter(loop, 3).getcounter(0, 0));
			}

			if (xml)
				fprintf(fh, "	</person>\n");
			else
				fprintf(fh, "----------------------------------------------------------\n");
		}

		if (xml)
			fprintf(fh, "	</per-user-stats>\n");
		else
			fprintf(fh, "\n");
		fflush(fh);
	}

	if (xml)
	{
		fprintf(fh, "	<created-with><name>mboxstats</name><version>" VERSION "</version><developer>folkert@vanheusden.com</developer><url>http://www.vanheusden.com/mboxstats/</url></created-with>\n");
		fprintf(fh, "</mailbox-stats>\n");
	}
	else
	{
		fprintf(fh, "Created with mboxstats v" VERSION "; written by folkert@vanheusden.com\n");
		fprintf(fh, "http://www.vanheusden.com/mboxstats/\n");
	}

	DEBUG(printf("finished\n");)
		fclose(fh);

	if (output) printf("Done\n");

	return 0;
}
