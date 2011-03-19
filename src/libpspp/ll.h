/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2011 Free Software Foundation, Inc.

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

   This header (ll.h) supplies "embedded" circular doubly linked
   lists.  Its companion header (llx.h) supplies "external"
   circular doubly linked lists.  The two variants are described
   briefly here.  The embedded variant, for which this is the
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

#ifndef LL_H
#define LL_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include "libpspp/cast.h"

/* Embedded, circular doubly linked list.

   Each list contains a single "null" element that separates the
   head and the tail of the list.  The null element is both
   before the head and after the tail of the list.  An empty list
   contains just the null element.

   An embedded linked list is represented as `struct ll_list'.
   Each node in the list, presumably a structure type, must
   include a `struct ll' member.

   Many list functions take ranges of nodes as arguments.  Ranges
   are "half-open"; that is, R0...R1 includes R0 but not R1.  A
   range whose endpoints are the same (e.g. R0...R0) contains no
   nodes at all.

   Here's an example of a structure type that includes a `struct
   ll':

     struct ll_list list;

     struct foo
       {
         struct ll ll;            // List member.
         int x;                   // Another member.
       };

   Here's an example of iteration from head to tail:

     struct ll *ll;
     for (ll = ll_head (&list); ll != ll_null (&list); ll = ll_next (ll))
       {
         struct foo *foo = ll_data (ll, struct foo, ll);
         ...do something with foo->x...
       }

   Here's another way to do it:

     struct ll *ll = ll_null (&list);
     while ((ll = ll_next (ll)) != ll_null (&list))
       {
         struct foo *foo = ll_data (ll, struct foo, ll);
         ...do something with foo->x...
       }

   Here's a third way:

     struct foo *foo;
     ll_for_each (foo, struct foo, ll, &list)
       {
         ...do something with foo->x...
       }
*/

/* Returns the data structure corresponding to the given node LL,
   assuming that LL is embedded as the given MEMBER name in data
   type STRUCT. */
#define ll_data(LL, STRUCT, MEMBER)                     \
        (CHECK_POINTER_HAS_TYPE(LL, struct ll *),       \
         UP_CAST(LL, STRUCT, MEMBER))

/* Linked list node. */
struct ll
  {
    struct ll *next;    /* Next node. */
    struct ll *prev;    /* Previous node. */
  };

/* Linked list. */
struct ll_list
  {
    struct ll null;     /* Null node. */
  };

/* Returns negative if A < B, zero if A == B, positive if A > B. */
typedef int ll_compare_func (const struct ll *a,
                             const struct ll *b, void *aux);

/* Returns true or false depending on properties of LL. */
typedef bool ll_predicate_func (const struct ll *ll, void *aux);

/* Takes some action on LL. */
typedef void ll_action_func (struct ll *ll, void *aux);

/* Suitable for use as the initializer for a `struct ll_list'
   named LIST.  Typical usage:
       struct ll_list list = LL_INITIALIZER (list);
   LL_INITIALIZER() is an alternative to ll_init(). */
#define LL_INITIALIZER(LIST) { { &(LIST).null, &(LIST).null } }

/* Basics. */
static inline void ll_init (struct ll_list *);
static inline bool ll_is_empty (const struct ll_list *);
size_t ll_count (const struct ll_list *);

/* Iteration. */
static inline struct ll *ll_head (const struct ll_list *);
static inline struct ll *ll_tail (const struct ll_list *);
static inline struct ll *ll_null (const struct ll_list *);
static inline struct ll *ll_next (const struct ll *);
static inline struct ll *ll_prev (const struct ll *);

/* Stack- and queue-like behavior. */
static inline void ll_push_head (struct ll_list *, struct ll *);
static inline void ll_push_tail (struct ll_list *, struct ll *);
static inline struct ll *ll_pop_head (struct ll_list *);
static inline struct ll *ll_pop_tail (struct ll_list *);

/* Insertion. */
static inline void ll_insert (struct ll *before, struct ll *new);
void ll_splice (struct ll *before, struct ll *r0, struct ll *r1);
void ll_swap (struct ll *a, struct ll *b);
void ll_swap_range (struct ll *a0, struct ll *a1,
                    struct ll *b0, struct ll *b1);

/* Removal. */
static inline struct ll *ll_remove (struct ll *);
static inline void ll_remove_range (struct ll *r0, struct ll *r1);
size_t ll_remove_equal (struct ll *r0, struct ll *r1, struct ll *target,
                        ll_compare_func *, void *aux);
size_t ll_remove_if (struct ll *r0, struct ll *r1,
                     ll_predicate_func *, void *aux);
static inline void ll_moved (struct ll *);

/* Non-mutating algorithms. */
struct ll *ll_find_equal (const struct ll *r0, const struct ll *r1,
                          const struct ll *target,
                          ll_compare_func *, void *aux);
struct ll *ll_find_if (const struct ll *r0, const struct ll *r1,
                       ll_predicate_func *, void *aux);
struct ll *ll_find_adjacent_equal (const struct ll *r0, const struct ll *r1,
                                   ll_compare_func *, void *aux);
size_t ll_count_range (const struct ll *r0, const struct ll *r1);
size_t ll_count_equal (const struct ll *r0, const struct ll *r1,
                       const struct ll *target,
                       ll_compare_func *, void *aux);
size_t ll_count_if (const struct ll *r0, const struct ll *r1,
                    ll_predicate_func *, void *aux);
struct ll *ll_max (const struct ll *r0, const struct ll *r1,
                   ll_compare_func *, void *aux);
struct ll *ll_min (const struct ll *r0, const struct ll *r1,
                   ll_compare_func *, void *aux);
int ll_lexicographical_compare_3way (const struct ll *a0, const struct ll *a1,
                                     const struct ll *b0, const struct ll *b1,
                                     ll_compare_func *, void *aux);

/* Mutating algorithms. */
void ll_apply (struct ll *r0, struct ll *r1, ll_action_func *, void *aux);
void ll_reverse (struct ll *r0, struct ll *r1);
bool ll_next_permutation (struct ll *r0, struct ll *r1,
                          ll_compare_func *, void *aux);
bool ll_prev_permutation (struct ll *r0, struct ll *r1,
                          ll_compare_func *, void *aux);

/* Sorted list functions. */
void ll_sort (struct ll *r0, struct ll *r1, ll_compare_func *, void *aux);
struct ll *ll_find_run (const struct ll *r0, const struct ll *r1,
                        ll_compare_func *, void *aux);
struct ll *ll_merge (struct ll *a0, struct ll *a1,
                     struct ll *b0, struct ll *b1,
                     ll_compare_func *, void *aux);
bool ll_is_sorted (const struct ll *r0, const struct ll *r1,
                   ll_compare_func *, void *aux);
size_t ll_unique (struct ll *r0, struct ll *r1, struct ll *dups,
                  ll_compare_func *, void *aux);
void ll_sort_unique (struct ll *r0, struct ll *r1, struct ll *dups,
                     ll_compare_func *, void *aux);
void ll_insert_ordered (struct ll *r0, struct ll *r1, struct ll *new_elem,
                        ll_compare_func *, void *aux);
struct ll *ll_partition (struct ll *r0, struct ll *r1,
                         ll_predicate_func *, void *aux);
struct ll *ll_find_partition (const struct ll *r0, const struct ll *r1,
                              ll_predicate_func *, void *aux);

/* Iteration helper macros. */

/* Sets DATA to each object in LIST in turn, in forward or
   reverse order, assuming that each
   `struct ll' in LIST is embedded as the given MEMBER name in
   data type STRUCT.

   Behavior is undefined if DATA is removed from the list between
   loop iterations. */
#define ll_for_each(DATA, STRUCT, MEMBER, LIST)                 \
        for (DATA = ll_head__ (STRUCT, MEMBER, LIST);           \
             DATA != NULL;                                      \
             DATA = ll_next__ (DATA, STRUCT, MEMBER, LIST))
#define ll_for_each_reverse(DATA, STRUCT, MEMBER, LIST)         \
        for (DATA = ll_tail__ (STRUCT, MEMBER, LIST);           \
             DATA != NULL;                                      \
             DATA = ll_prev__ (DATA, STRUCT, MEMBER, LIST))

/* Continues a iteration of LIST, starting from the object
   currently in DATA and continuing, in forward or reverse order,
   through the remainder of the list, assuming that each `struct
   ll' in LIST is embedded as the given MEMBER name in data type
   STRUCT.

   Behavior is undefined if DATA is removed from the list between
   loop iterations. */
#define ll_for_each_continue(DATA, STRUCT, MEMBER, LIST)        \
        for (;                                                  \
             DATA != NULL;                                      \
             DATA = ll_next__ (DATA, STRUCT, MEMBER, LIST))
#define ll_for_each_reverse_continue(DATA, STRUCT, MEMBER, LIST)        \
        for (;                                                          \
             DATA != NULL;                                              \
             DATA = ll_prev__ (DATA, STRUCT, MEMBER, LIST))

/* Sets DATA to each object in LIST in turn, in forward or
   reverse order, assuming that each `struct ll' in LIST is
   embedded as the given MEMBER name in data type STRUCT.  NEXT
   (or PREV) must be another variable of the same type as DATA.

   Behavior is well-defined even if DATA is removed from the list
   between iterations. */
#define ll_for_each_safe(DATA, NEXT, STRUCT, MEMBER, LIST)              \
        for (DATA = ll_head__ (STRUCT, MEMBER, LIST);                   \
             (DATA != NULL                                              \
              ? (NEXT = ll_next__ (DATA, STRUCT, MEMBER, LIST), 1)      \
              : 0);                                                     \
             DATA = NEXT)
#define ll_for_each_reverse_safe(DATA, PREV, STRUCT, MEMBER, LIST)      \
        for (DATA = ll_tail__ (STRUCT, MEMBER, LIST);                   \
             (DATA != NULL                                              \
              ? (PREV = ll_prev__ (DATA, STRUCT, MEMBER, LIST), 1)      \
              : 0);                                                     \
             DATA = PREV)

/* Continues a iteration of LIST, in forward or reverse order,
   starting from the object currently in DATA and continuing
   forward through the remainder of the list, assuming that each
   `struct ll' in LIST is embedded as the given MEMBER name in
   data type STRUCT.  NEXT (or PREV) must be another variable of
   the same type as DATA.

   Behavior is well-defined even if DATA is removed from the list
   between iterations. */
#define ll_for_each_safe_continue(DATA, NEXT, STRUCT, MEMBER, LIST)     \
        for (;                                                          \
             (DATA != NULL                                              \
              ? (NEXT = ll_next__ (DATA, STRUCT, MEMBER, LIST), 1)      \
              : 0);                                                     \
             DATA = NEXT)
#define ll_for_each_safe_reverse_continue(DATA, PREV, STRUCT, MEMBER, LIST) \
        for (;                                                          \
             (DATA != NULL                                              \
              ? (PREV = ll_prev__ (DATA, STRUCT, MEMBER, LIST), 1)      \
              : 0);                                                     \
             DATA = PREV)

/* Sets DATA to each object in LIST in turn, in forward or
   reverse order, assuming that each `struct ll' in LIST is
   embedded as the given MEMBER name in data type STRUCT.
   Each object is removed from LIST before its loop iteration. */
#define ll_for_each_preremove(DATA, STRUCT, MEMBER, LIST)                 \
        while (!ll_is_empty (LIST)                                        \
               ? (DATA = ll_data (ll_pop_head (LIST), STRUCT, MEMBER), 1) \
               : 0)
#define ll_for_each_reverse_preremove(DATA, STRUCT, MEMBER, LIST)         \
        while (!ll_is_empty (LIST)                                        \
               ? (DATA = ll_data (ll_pop_tail (LIST), STRUCT, MEMBER), 1) \
               : 0)

/* Sets DATA to each object in LIST in turn, in forward or
   reverse order, assuming that each `struct ll' in LIST is
   embedded as the given MEMBER name in data type STRUCT.
   At the end of each loop iteration, DATA is removed from the
   list. */
#define ll_for_each_postremove(DATA, STRUCT, MEMBER, LIST)      \
        for (;                                                  \
             (DATA = ll_head__ (STRUCT, MEMBER, LIST)) != NULL; \
             ll_remove (&DATA->MEMBER))
#define ll_for_each_reverse_postremove(DATA, STRUCT, MEMBER, LIST)      \
        for (;                                                          \
             (DATA = ll_tail__ (STRUCT, MEMBER, LIST)) != NULL;         \
             ll_remove (&DATA->MEMBER))

/* Macros for internal use only. */
#define ll_data__(LL, STRUCT, MEMBER, LIST)                             \
        ((LL) != ll_null (LIST) ? ll_data (LL, STRUCT, MEMBER) : NULL)
#define ll_head__(STRUCT, MEMBER, LIST)                         \
        ll_data__ (ll_head (LIST), STRUCT, MEMBER, LIST)
#define ll_tail__(STRUCT, MEMBER, LIST)                         \
        ll_data__ (ll_tail (LIST), STRUCT, MEMBER, LIST)
#define ll_next__(DATA, STRUCT, MEMBER, LIST)                           \
        ll_data__ (ll_next (&(DATA)->MEMBER), STRUCT, MEMBER, LIST)
#define ll_prev__(DATA, STRUCT, MEMBER, LIST)                           \
        ll_data__ (ll_prev (&(DATA)->MEMBER), STRUCT, MEMBER, LIST)

/* Inline functions. */

/* Initializes LIST as an empty list. */
static inline void
ll_init (struct ll_list *list)
{
  list->null.next = list->null.prev = &list->null;
}

/* Returns true if LIST is empty (contains just the null node),
   false if LIST is not empty (has at least one other node).
   Executes in O(1) time. */
static inline bool
ll_is_empty (const struct ll_list *list)
{
  return ll_head (list) == ll_null (list);
}

/* Returns the first node in LIST,
   or the null node if LIST is empty. */
static inline struct ll *
ll_head (const struct ll_list *list)
{
  return ll_next (ll_null (list));
}

/* Returns the last node in LIST,
   or the null node if LIST is empty. */
static inline struct ll *
ll_tail (const struct ll_list *list)
{
  return ll_prev (ll_null (list));
}

/* Returns LIST's null node. */
static inline struct ll *
ll_null (const struct ll_list *list)
{
  return CONST_CAST (struct ll *, &list->null);
}

/* Returns the node following LL in its list,
   or the null node if LL is at the end of its list.
   (In an empty list, the null node follows itself.) */
static inline struct ll *
ll_next (const struct ll *ll)
{
  return ll->next;
}

/* Returns the node preceding LL in its list,
   or the null node if LL is the first node in its list.
   (In an empty list, the null node precedes itself.) */
static inline struct ll *
ll_prev (const struct ll *ll)
{
  return ll->prev;
}

/* Inserts LL at the head of LIST. */
static inline void
ll_push_head (struct ll_list *list, struct ll *ll)
{
  ll_insert (ll_head (list), ll);
}

/* Inserts LL at the tail of LIST. */
static inline void
ll_push_tail (struct ll_list *list, struct ll *ll)
{
  ll_insert (ll_null (list), ll);
}

/* Removes and returns the first node in LIST,
   which must not be empty. */
static inline struct ll *
ll_pop_head (struct ll_list *list)
{
  struct ll *head;
  assert (!ll_is_empty (list));
  head = ll_head (list);
  ll_remove (head);
  return head;
}

/* Removes and returns the last node in LIST,
   which must not be empty. */
static inline struct ll *
ll_pop_tail (struct ll_list *list)
{
  struct ll *tail;
  assert (!ll_is_empty (list));
  tail = ll_tail (list);
  ll_remove (tail);
  return tail;
}

/* Inserts NEW_ELEM just before BEFORE.
   (NEW_ELEM must not already be in a list.) */
static inline void
ll_insert (struct ll *before, struct ll *new_elem)
{
  struct ll *before_prev = ll_prev (before);
  new_elem->next = before;
  new_elem->prev = before_prev;
  before_prev->next = before->prev = new_elem;
}

/* Removes LL from its list
   and returns the node that formerly followed it. */
static inline struct ll *
ll_remove (struct ll *ll)
{
  struct ll *next = ll_next (ll);
  ll->prev->next = next;
  ll->next->prev = ll->prev;
  return next;
}

/* Removes R0...R1 from their list. */
static inline void
ll_remove_range (struct ll *r0, struct ll *r1)
{
  if (r0 != r1)
    {
      r1 = r1->prev;
      r0->prev->next = r1->next;
      r1->next->prev = r0->prev;
    }
}

/* Adjusts the nodes around LL to compensate for LL having
   changed address, e.g. due to LL being inside a block of memory
   that was realloc()'d.  Equivalent to calling ll_remove()
   before moving LL, then ll_insert() afterward, but more
   efficient. */
static inline void
ll_moved (struct ll *ll)
{
  ll->prev->next = ll->next->prev = ll;
}

#endif /* ll.h */
