int resize(void **pnt, int n, int *len, int size);
void free_array(void ***array, int *n, int *len);
void * mymalloc(int size, char *what);
void * myrealloc(void *oldp, int newsize, char *what);
char * mystrdup(char *in);
