#ifndef SORT_ALGO_H
#define SORT_ALGO_H 1

#include <stddef.h>

/* Compares A and B, given auxiliary data AUX, and returns a
   strcmp()-type result. */
typedef int algo_compare_func (const void *a, const void *b, void *aux);

/* Tests a predicate on DATA, given auxiliary data AUX, and
   returns nonzero if true or zero if false. */
typedef int algo_predicate_func (const void *data, void *aux);

/* Returns a random number in the range 0 through MAX exclusive,
   given auxiliary data AUX. */
typedef unsigned algo_random_func (unsigned max, void *aux);

/* A generally suitable random function. */
algo_random_func algo_default_random;

/* Sorts ARRAY, which contains COUNT elements of SIZE bytes each,
   using COMPARE for comparisons.  AUX is passed to each
   comparison as auxiliary data. */
void sort (void *array, size_t count, size_t size,
           algo_compare_func *compare, void *aux);

/* Makes the elements in ARRAY unique, by moving up duplicates,
   and returns the new number of elements in the array.  Sorted
   arrays only.  Arguments same as for sort() above. */
size_t unique (void *array, size_t count, size_t size,
               algo_compare_func *compare, void *aux);

/* Helper function that calls sort(), then unique(). */
size_t sort_unique (void *array, size_t count, size_t size,
                    algo_compare_func *compare, void *aux);

/* Reorders ARRAY, which contains COUNT elements of SIZE bytes
   each, so that the elements for which PREDICATE returns nonzero
   precede those for which PREDICATE returns zero.  AUX is passed
   as auxiliary data to PREDICATE.  Returns the number of
   elements for which PREDICATE returns nonzero.  Not stable. */
size_t partition (void *array, size_t count, size_t size,
                  algo_predicate_func *predicate, void *aux);

/* Randomly reorders ARRAY, which contains COUNT elements of SIZE
   bytes each.  Uses RANDOM as a source of random data, passing
   AUX as the auxiliary data.  RANDOM may be null to use a
   default random source. */
void random_shuffle (void *array, size_t count, size_t size,
                     algo_random_func *random, void *aux);

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is false are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t copy_if (const void *array, size_t count, size_t size,
                void *result,
                algo_predicate_func *predicate, void *aux);

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is true are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t remove_copy_if (const void *array, size_t count, size_t size,
                       void *result,
                       algo_predicate_func *predicate, void *aux);

#endif /* sort-algo.h */
