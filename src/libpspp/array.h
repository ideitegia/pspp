#ifndef ALGORITHM_H
#define ALGORITHM_H 1

#include <stddef.h>
#include <stdbool.h>

/* Compares A and B, given auxiliary data AUX, and returns a
   strcmp()-type result. */

typedef int algo_compare_func (const void *a, const void *b, const void *aux);

/* Tests a predicate on DATA, given auxiliary data AUX */
typedef bool algo_predicate_func (const void *data, const void *aux);

/* Returns a random number in the range 0 through MAX exclusive,
   given auxiliary data AUX. */
typedef unsigned algo_random_func (unsigned max, const void *aux);

/* A generally suitable random function. */
algo_random_func algo_default_random;

/* Finds an element in ARRAY, which contains COUNT elements of
   SIZE bytes each, using COMPARE for comparisons.  Returns the
   first element in ARRAY that matches TARGET, or a null pointer
   on failure.  AUX is passed to each comparison as auxiliary
   data. */
void *find (const void *array, size_t count, size_t size,
            const void *target,
            algo_compare_func *compare, const void *aux);

/* Counts and return the number of elements in ARRAY, which
   contains COUNT elements of SIZE bytes each, which are equal to
   ELEMENT as compared with COMPARE.  AUX is passed as auxiliary
   data to COMPARE. */
size_t count_equal (const void *array, size_t count, size_t size,
                    const void *element,
                    algo_compare_func *compare, const void *aux);

/* Counts and return the number of elements in ARRAY, which
   contains COUNT elements of SIZE bytes each, for which
   PREDICATE returns true.  AUX is passed as auxiliary data to
   PREDICATE. */
size_t count_if (const void *array, size_t count, size_t size,
                 algo_predicate_func *predicate, const void *aux);

/* Sorts ARRAY, which contains COUNT elements of SIZE bytes each,
   using COMPARE for comparisons.  AUX is passed to each
   comparison as auxiliary data. */
void sort (void *array, size_t count, size_t size,
           algo_compare_func *compare, const void *aux);

/* Tests whether ARRAY, which contains COUNT elements of SIZE
   bytes each, is sorted in order according to COMPARE.  AUX is
   passed to COMPARE as auxiliary data. */
bool is_sorted (const void *array, size_t count, size_t size,
               algo_compare_func *compare, const void *aux);

/* Makes the elements in ARRAY unique, by moving up duplicates,
   and returns the new number of elements in the array.  Sorted
   arrays only.  Arguments same as for sort() above. */
size_t unique (void *array, size_t count, size_t size,
               algo_compare_func *compare, const void *aux);

/* Helper function that calls sort(), then unique(). */
size_t sort_unique (void *array, size_t count, size_t size,
                    algo_compare_func *compare, const void *aux);

/* Reorders ARRAY, which contains COUNT elements of SIZE bytes
   each, so that the elements for which PREDICATE returns true
   precede those for which PREDICATE returns false.  AUX is passed
   as auxiliary data to PREDICATE.  Returns the number of
   elements for which PREDICATE returns true.  Not stable. */
size_t partition (void *array, size_t count, size_t size,
                  algo_predicate_func *predicate, const void *aux);

/* Checks whether ARRAY, which contains COUNT elements of SIZE
   bytes each, is partitioned such that PREDICATE returns true
   for the first TRUE_CNT elements and zero for the remaining
   elements.  AUX is passed as auxiliary data to PREDICATE. */
bool is_partitioned (const void *array, size_t count, size_t size,
                    size_t true_cnt,
                    algo_predicate_func *predicate, const void *aux);

/* Randomly reorders ARRAY, which contains COUNT elements of SIZE
   bytes each.  Uses RANDOM as a source of random data, passing
   AUX as the auxiliary data.  RANDOM may be null to use a
   default random source. */
void random_shuffle (void *array, size_t count, size_t size,
                     algo_random_func *random, const void *aux);

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is false are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t copy_if (const void *array, size_t count, size_t size,
                void *result,
                algo_predicate_func *predicate, const void *aux);

/* Removes N elements starting at IDX from ARRAY, which consists
   of COUNT elements of SIZE bytes each, by shifting the elements
   following them, if any, into its position. */
void remove_range (void *array, size_t count, size_t size,
                   size_t idx, size_t n);

/* Removes element IDX from ARRAY, which consists of COUNT
   elements of SIZE bytes each, by shifting the elements
   following it, if any, into its position. */
void remove_element (void *array, size_t count, size_t size,
                     size_t idx);

/* Makes room for N elements starting at IDX in ARRAY, which
   initially consists of COUNT elements of SIZE bytes each, by
   shifting elements IDX...COUNT (exclusive) to the right by N
   positions. */
void insert_range (void *array, size_t count, size_t size,
                   size_t idx, size_t n);

/* Makes room for a new element at IDX in ARRAY, which initially
   consists of COUNT elements of SIZE bytes each, by shifting
   elements IDX...COUNT (exclusive) to the right by one
   position. */
void insert_element (void *array, size_t count, size_t size,
                     size_t idx);

/* Moves an element in ARRAY, which consists of COUNT elements of
   SIZE bytes each, from OLD_IDX to NEW_IDX, shifting around
   other elements as needed.  Runs in O(abs(OLD_IDX - NEW_IDX))
   time. */
void move_element (void *array, size_t count, size_t size,
                   size_t old_idx, size_t new_idx);

/* Moves N elements in ARRAY starting at OLD_IDX, which consists
   of COUNT elements of SIZE bytes each, so that they now start
   at NEW_IDX, shifting around other elements as needed. */
void move_range (void *array, size_t count, size_t size,
                 size_t old_idx, size_t new_idx, size_t n);

/* Removes elements equal to ELEMENT from ARRAY, which consists
   of COUNT elements of SIZE bytes each.  Returns the number of
   remaining elements.  AUX is passed to COMPARE as auxiliary
   data. */
size_t remove_equal (void *array, size_t count, size_t size,
                     void *element,
                     algo_compare_func *compare, const void *aux);

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is true are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t remove_copy_if (const void *array, size_t count, size_t size,
                       void *result,
                       algo_predicate_func *predicate, const void *aux);

/* Searches ARRAY, which contains COUNT elements of SIZE bytes
   each, for VALUE, using a binary search.  ARRAY must ordered
   according to COMPARE.  AUX is passed to COMPARE as auxiliary
   data. */
void *binary_search (const void *array, size_t count, size_t size,
                     void *value,
                     algo_compare_func *compare, const void *aux);

/* Lexicographically compares ARRAY1, which contains COUNT1
   elements of SIZE bytes each, to ARRAY2, which contains COUNT2
   elements of SIZE bytes, according to COMPARE.  Returns a
   strcmp()-type result.  AUX is passed to COMPARE as auxiliary
   data. */
int lexicographical_compare_3way (const void *array1, size_t count1,
                                  const void *array2, size_t count2,
                                  size_t size,
                                  algo_compare_func *compare, const void *aux);

/* Computes the generalized set difference, ARRAY1 minus ARRAY2,
   into RESULT, and returns the number of elements written to
   RESULT.  If a value appears M times in ARRAY1 and N times in
   ARRAY2, then it will appear max(M - N, 0) in RESULT.  ARRAY1
   and ARRAY2 must be sorted, and RESULT is sorted and stable.
   ARRAY1 consists of COUNT1 elements, ARRAY2 of COUNT2 elements,
   each SIZE bytes.  AUX is passed to COMPARE as auxiliary
   data. */
size_t set_difference (const void *array1, size_t count1,
                       const void *array2, size_t count2,
                       size_t size,
                       void *result,
                       algo_compare_func *compare, const void *aux);

/* Finds the first pair of adjacent equal elements in ARRAY,
   which has COUNT elements of SIZE bytes.  Returns the first
   element in ARRAY such that COMPARE returns true when it and
   its successor element are compared.  AUX is passed to COMPARE
   as auxiliary data. */
void *adjacent_find_equal (const void *array, size_t count, size_t size,
                           algo_compare_func *compare, const void *aux);

/* ARRAY contains COUNT elements of SIZE bytes each.  Initially
   the first COUNT - 1 elements of these form a heap, followed by
   a single element not part of the heap.  This function adds the
   final element, forming a heap of COUNT elements in ARRAY.
   Uses COMPARE to compare elements, passing AUX as auxiliary
   data. */
void push_heap (void *array, size_t count, size_t size,
                algo_compare_func *compare, const void *aux);

/* ARRAY contains COUNT elements of SIZE bytes each.  Initially
   all COUNT elements form a heap.  This function moves the
   largest element in the heap to the final position in ARRAY and
   reforms a heap of the remaining COUNT - 1 elements at the
   beginning of ARRAY.  Uses COMPARE to compare elements, passing
   AUX as auxiliary data. */
void pop_heap (void *array, size_t count, size_t size,
               algo_compare_func *compare, const void *aux);

/* Turns ARRAY, which contains COUNT elements of SIZE bytes, into
   a heap.  Uses COMPARE to compare elements, passing AUX as
   auxiliary data. */
void make_heap (void *array, size_t count, size_t size,
                algo_compare_func *compare, const void *aux);

/* ARRAY contains COUNT elements of SIZE bytes each.  Initially
   all COUNT elements form a heap.  This function turns the heap
   into a fully sorted array.  Uses COMPARE to compare elements,
   passing AUX as auxiliary data. */
void sort_heap (void *array, size_t count, size_t size,
                algo_compare_func *compare, const void *aux);

/* ARRAY contains COUNT elements of SIZE bytes each.  This
   function tests whether ARRAY is a heap and returns true if so,
   false otherwise.  Uses COMPARE to compare elements, passing
   AUX as auxiliary data. */
bool is_heap (const void *array, size_t count, size_t size,
             algo_compare_func *compare, const void *aux);


#endif /* algorithm.h */
