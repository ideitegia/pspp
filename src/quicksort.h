#ifndef QUICKSORT_H
#define QUICKSORT_H 1

/* Equivalent to standard C qsort(), but allows passing an extra
   parameter to the comparison function. */
void
quicksort (void *pbase, size_t total_elems, size_t size,
           int (*cmp) (const void *, const void *, void *),
           void *param);

#endif /* quicksort.h */
