#include <math.h>
#include <string.h>
#include "val.h"
#include "main.h"

double calc_nbits_in_data(unsigned char *data, int nbytestoprocess)
{
        int cnts[256], loop;
        double ent=0.0, log2=log(2.0);
        memset(cnts, 0x00, sizeof(cnts));

        for(loop=0; loop<nbytestoprocess; loop++)
        {
                cnts[data[loop]]++;
        }

        for(loop=0; loop<256;loop++)
        {
                double prob = (double)cnts[loop] / (double)nbytestoprocess;

                if (unlikely(prob > 0.0))
                {
                        ent += prob * (log(1.0/prob)/log2);
                }
        }

        ent *= (double)nbytestoprocess;

        if (ent < 0.0) ent=0.0;

        ent = min((double)(nbytestoprocess*8), ent);

        return ent;
}
