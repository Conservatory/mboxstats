/* $Id: array.h,v 0.95 2003/03/16 14:12:40 folkert Exp $
 * $Log: array.h,v $
 * Revision 0.95  2003/03/16 14:12:40  folkert
 * *** empty log message ***
 *
 * Revision 0.8  2003/02/20 19:23:36  folkert
 * *** empty log message ***
 *
 * Revision 0.6  2003/02/04 21:26:09  folkert
 * *** empty log message ***
 *
 * Revision 0.5  2003/02/03 19:48:55  folkert
 * *** empty log message ***
 *
 */

#include <stdlib.h>

class array
{
private:
	int nin;	// number of elements
	long int *counters;// counters
	int ncounters;
	char **strings;// strings
	array **subarrays;
	int nsubarrays;

	void	quicksort(int subindex, int start, int end);

public:
	array(int numberofcounters, int n_subs=0);
	~array();

	int	addstring(char *string, char isemail=0);
	int	addelement(char *string);
	int	getN(void) { return nin; }
	void	setcounter(int index, int subindex, int value);
	array & getsubcounter(int index, int subarrayindex);
	array & getsubcounter(char *string, int subarrayindex);
	int	getcounter(int index, int subindex);
	int	addcounter(int index, int subindex, int value);
	char *	getstring(int index);
	void	sort(int subindex);
	void	swap_entry(int index1, int index2);
};
