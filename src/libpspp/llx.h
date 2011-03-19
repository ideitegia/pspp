/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2011 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* Circular doubly linked lists.

   This header (llx.h) supplies "external" circular doubly linked
   lists.  Its companion header (ll.h) supplies "embedded"
   circular doubly linked lists.  The two variants are described
   briefly here.  The external variant, for which this is the
   header, is described in slightly more detail below.  Each
   function also has a detailed usage comment at its point of
   definition.

   The "ll" embedded linked list implementation puts the linked
   list node within the data structure that the list contains.
   This makes allocation efficient, in space and time.  It also
   makes it easy to find the list node associated with a given
   object.  However, it's difficult to include a given object in
   an arbitrary number of lists, or to include a single object in
   a single list in multiple positions.

   The "llx" external linked list implementation allocates linked
   list nodes separately from the objects in the list.  Adding
   and removing linked list nodes requires dynamic allocation, so
   it is normally slower and takes more memory than the embedded
   implementation.  It also requires searching the list to find
   the list node associated with a given object.  However, it's
   easy to include a given object in an arbitrary number of
   lists, or to include a single object more than once within a
   single list.  It's also possible to create an external linked
   list without adding a member to the data structure that the
   list contains. */

#ifndef LLX_H
#define LLX_H 1

#include <stdbool.h>
#include <stddef.h>
#include "libpspp/ll.h"

/* External, circular doubly linked list.

   Each list contains a single "null" element that separates the
   head and the tail of the list.  The null element is both
   before the head and after the tail of the list.  An empty list
   contains just the null element.

   An external linked list is represented as `struct llx_list'.
   Each node in the list consists of a `struct llx' that contains
   a `void *' pointer to the node's data.  Use the llx_data()
   function to extract the data pointer from a node.

   Many list functions take ranges of nodes as arguments.  Ranges
   are "half-open"; that is, R0...R1 includes R0 but not R1.  A
   range whose endpoints are the same (e.g. R0...R0) contains no
   nodes at all.

   Consider the following declarations:

     struct llx_list list;

     struct foo
       {
         int x;                   // Data member.
       };

   Here's an example of iteration from head to tail:

     struct llx *llx;
     for (llx = llx_head (&list); llx != llx_null (&list);
          llx = llx_next (llx))
       {
         struct foo *foo = llx_data (llx);
         ...do something with foo->x...
       }

   Here's another way to do it:

     struct llx *llx = llx_null (&list);
     while ((llx = llx_next (llx)) != llx_null (&list))
       {
         struct foo *foo = llx_data (llx);
         ...do something with foo->x...
       }
*/

/* External linked list node. */
struct llx
  {
    struct ll ll;               /* Node. */
    void *data;                 /* Member data. */
  };

/* Linked list. */
struct llx_list
  {
    struct ll_list ll_list;     /* The list. */
  };

/* Suitable for use as the initializer for a `struct llx_list'
   named LIST.  Typical usage:
       struct llx_list list = LLX_INITIALIZER (list);
   LLX_INITIALIZER() is an alternative to llx_init(). */
#define LLX_INITIALIZER(LIST) { LL_INITIALIZER ((LIST).ll_list) }

/* Memory manager. */
struct llx_manager
  {
    /* Allocates and returns memory for a new struct llx.
       If space is unavailable, returns a null pointer. */
    struct llx *(*allocate) (void *aux);

    /* Releases a previously allocated struct llx. */
    void (*release) (struct llx *, void *aux);

    /* Auxiliary data passed to allocate and release
       functions. */
    void *aux;
  };

/* Manager that uses the standard malloc and free routines. */
extern const struct llx_manager llx_malloc_mgr;

/* Returns negative if A < B, zero if A == B, positive if A > B. */
typedef int llx_compare_func (const void *a, const void *b, void *aux);

/* Returns true or false depending on properties of DATA. */
typedef bool llx_predicate_func (const void *data, void *aux);

/* Takes some action on DATA. */
typedef void llx_action_func (void *data, void *aux);

/* Basics. */
static inline void llx_init (struct llx_list *);
void llx_destroy (struct llx_list *, llx_action_func *destructor, void *aux,
                  const struct llx_manager *manager);
static inline bool llx_is_empty (const struct llx_list *);
size_t llx_count (const struct llx_list *);

/* Iteration. */
static inline struct llx *llx_head (const struct llx_list *);
static inline struct llx *llx_tail (const struct llx_list *);
static inline struct llx *llx_null (const struct llx_list *);
static inline struct llx *llx_next (const struct llx *);
static inline struct llx *llx_prev (const struct llx *);
static inline void *llx_data (const struct llx *);

/* Stack- and queue-like behavior. */
struct llx *llx_push_head (struct llx_list *, void *,
                           const struct llx_manager *);
struct llx *llx_push_tail (struct llx_list *, void *,
                           const struct llx_manager *);
void *llx_pop_head (struct llx_list *, const struct llx_manager *);
void *llx_pop_tail (struct llx_list *, const struct llx_manager *);

/* Insertion. */
struct llx *llx_insert (struct llx *before, void *,
                        const struct llx_manager *);
void llx_splice (struct llx *before, struct llx *r0, struct llx *r1);
void llx_swap (struct llx *a, struct llx *b);
void llx_swap_range (struct llx *a0, struct llx *a1,
                     struct llx *b0, struct llx *b1);

/* Removal. */
struct llx *llx_remove (struct llx *, const struct llx_manager *);
void llx_remove_range (struct llx *r0, struct llx *r1,
                       const struct llx_manager *);
size_t llx_remove_equal (struct llx *r0, struct llx *r1, const void *target,
                         llx_compare_func *, void *aux,
                         const struct llx_manager *);
size_t llx_remove_if (struct llx *r0, struct llx *r1,
                      llx_predicate_func *, void *aux,
                      const struct llx_manager *);

/* Non-mutating algorithms. */
struct llx *llx_find (const struct llx *r0, const struct llx *r1,
                      const void *target);
struct llx *llx_find_equal (const struct llx *r0, const struct llx *r1,
                            const void *target,
                            llx_compare_func *, void *aux);
struct llx *llx_find_if (const struct llx *r0, const struct llx *r1,
                         llx_predicate_func *, void *aux);
struct llx *llx_find_adjacent_equal (const struct llx *r0,
                                     const struct llx *r1,
                                     llx_compare_func *, void *aux);
size_t llx_count_range (const struct llx *r0, const struct llx *r1);
size_t llx_count_equal (const struct llx *r0, const struct llx *r1,
                        const void *target,
                        llx_compare_func *, void *aux);
size_t llx_count_if (const struct llx *r0, const struct llx *r1,
                     llx_predicate_func *, void *aux);
struct llx *llx_max (const struct llx *r0, const struct llx *r1,
                     llx_compare_func *, void *aux);
struct llx *llx_min (const struct llx *r0, const struct llx *r1,
                     llx_compare_func *, void *aux);
int llx_lexicographical_compare_3way (const struct llx *a0,
                                      const struct llx *a1,
                                      const struct llx *b0,
                                      const struct llx *b1,
                                      llx_compare_func *, void *aux);

/* Mutating algorithms. */
void llx_apply (struct llx *r0, struct llx *r1, llx_action_func *, void *aux);
void llx_reverse (struct llx *r0, struct llx *r1);
bool llx_next_permutation (struct llx *r0, struct llx *r1,
                           llx_compare_func *, void *aux);
bool llx_prev_permutation (struct llx *r0, struct llx *r1,
                           llx_compare_func *, void *aux);

/* Sorted list functions. */
void llx_sort (struct llx *r0, struct llx *r1, llx_compare_func *, void *aux);
struct llx *llx_find_run (const struct llx *r0, const struct llx *r1,
                          llx_compare_func *, void *aux);
bool llx_is_sorted (const struct llx *r0, const struct llx *r1,
                    llx_compare_func *, void *aux);
struct llx *llx_merge (struct llx *a0, struct llx *a1,
                       struct llx *b0, struct llx *b1,
                       llx_compare_func *, void *aux);
size_t llx_unique (struct llx *r0, struct llx *r1, struct llx *dups,
                   llx_compare_func *, void *aux,
                   const struct llx_manager *);
void llx_sort_unique (struct llx *r0, struct llx *r1, struct llx *dups,
                      llx_compare_func *, void *aux,
                      const struct llx_manager *);
struct llx *llx_insert_ordered (struct llx *r0, struct llx *r1, void *data,
                                llx_compare_func *, void *aux,
                                const struct llx_manager *);
struct llx *llx_partition (struct llx *r0, struct llx *r1,
                           llx_predicate_func *, void *aux);
struct llx *llx_find_partition (const struct llx *r0, const struct llx *r1,
                                llx_predicate_func *, void *aux);

/* Returns the llx within which LL is embedded. */
static struct llx *
llx_from_ll (struct ll *ll)
{
  return ll_data (ll, struct llx, ll);
}

/* Initializes LIST as an empty list. */
static inline void
llx_init (struct llx_list *list)
{
  ll_init (&list->ll_list);
}

/* Returns true if LIST is empty (contains just the null node),
   false if LIST is not empty (has at least one other node).
   Executes in O(1) time. */
static inline bool
llx_is_empty (const struct llx_list *list)
{
  return ll_is_empty (&list->ll_list);
}

/* Returns the first node in LIST,
   or the null node if LIST is empty. */
static inline struct llx *
llx_head (const struct llx_list *list)
{
  return llx_from_ll (ll_head (&list->ll_list));
}

/* Returns the last node in LIST,
   or the null node if LIST is empty. */
static inline struct llx *
llx_tail (const struct llx_list *list)
{
  return llx_from_ll (ll_tail (&list->ll_list));
}

/* Returns LIST's null node. */
static inline struct llx *
llx_null (const struct llx_list *list)
{
  return llx_from_ll (ll_null (&list->ll_list));
}

/* Returns the node following LLX in its list,
   or the null node if LLX is at the end of its list.
   (In an empty list, the null node follows itself.) */
static inline struct llx *
llx_next (const struct llx *llx)
{
  return llx_from_ll (ll_next (&llx->ll));
}

/* Returns the node preceding LLX in its list,
   or the null node if LLX is the first node in its list.
   (In an empty list, the null node precedes itself.) */
static inline struct llx *
llx_prev (const struct llx *llx)
{
  return llx_from_ll (ll_prev (&llx->ll));
}

/* Returns the data in node LLX. */
static inline void *
llx_data (const struct llx *llx)
{
  return llx->data;
}

#endif /* llx.h */
