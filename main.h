#define MS_TEXT		0
#define MS_HEADER	1
#define MS_DATA		2
#define MS_DATA_LOOK	3

/* code taken from linux kernel */
#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif
#ifndef __builtin_expect
#define __builtin_expect(x, expected_value) (x)
#endif
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

void set_logging(char ls);
void fvhlib_log(int loglevel, char *format, ...);
