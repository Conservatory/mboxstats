/* $Id: funcs.h,v 0.95 2003/03/16 14:12:40 folkert Exp $
 * $Log: funcs.h,v $
 * Revision 0.95  2003/03/16 14:12:40  folkert
 * *** empty log message ***
 *
 * Revision 0.5  2003/02/03 19:48:55  folkert
 * *** empty log message ***
 *
 */

char * stripstring(char *in);
char datestringtofields(char *string, int &year, int &month, int &day, int &wday, int &hour, int &minute, int &second, char **timezone);
