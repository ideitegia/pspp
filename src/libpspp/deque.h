/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#ifndef LIBPSPP_DEQUE_H
#define LIBPSPP_DEQUE_H 1

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include <libpspp/compiler.h>

#include "xalloc.h"

/* Declares data and functions for a deque whose elements have
   the given ELEMENT_TYPE.  Instances of the deque are declared
   as "struct NAME", and each function that operates on the deque
   has NAME_ as a prefix. */
#define DEQUE_DECLARE(NAME, ELEMENT_TYPE)                                   \
/* An instance of the deque. */                                             \
struct NAME                                                                 \
  {                                                                         \
    size_t capacity;    /* Capacity, which must be a power of 2. */         \
    size_t front;       /* One past the front of the queue. */              \
    size_t back;        /* The back of the queue. */                        \
    ELEMENT_TYPE *data; /* Pointer to CAPACITY elements. */                 \
  };                                                                        \
                                                                            \
/* Initializes DEQUE as an empty deque that can store at least              \
   CAPACITY elements.  (The actual capacity may be larger and is            \
   always a power of 2.) */                                                 \
static inline void                                                          \
NAME##_init (struct NAME *deque, size_t capacity)                           \
{                                                                           \
  deque->capacity = 1;                                                      \
  while (deque->capacity < capacity)                                        \
    deque->capacity <<= 1;                                                  \
  deque->front = deque->back = 0;                                           \
  deque->data = xnmalloc (deque->capacity, sizeof *deque->data);            \
}                                                                           \
                                                                            \
/* Destroys DEQUE, which must be empty. */                                  \
static inline void                                                          \
NAME##_destroy (struct NAME *deque)                                         \
{                                                                           \
  free (deque->data);                                                       \
}                                                                           \
                                                                            \
/* Returns the number of elements currently in DEQUE. */                    \
static inline size_t                                                        \
NAME##_count (const struct NAME *deque)                                     \
{                                                                           \
  return deque->front - deque->back;                                        \
}                                                                           \
                                                                            \
/* Returns the maximum number of elements that DEQUE can hold at            \
   any time. */                                                             \
static inline size_t                                                        \
NAME##_capacity (const struct NAME *deque)                                  \
{                                                                           \
  return deque->capacity;                                                   \
}                                                                           \
                                                                            \
/* Returns true if DEQUE is currently empty (contains no                    \
   elements), false otherwise. */                                           \
static inline bool                                                          \
NAME##_is_empty (const struct NAME *deque)                                  \
{                                                                           \
  return NAME##_count (deque) == 0;                                         \
}                                                                           \
                                                                            \
/* Returns true if DEQUE is currently full (cannot take any more            \
   elements), false otherwise. */                                           \
static inline bool                                                          \
NAME##_is_full (const struct NAME *deque)                                   \
{                                                                           \
  return NAME##_count (deque) >= NAME##_capacity (deque);                   \
}                                                                           \
                                                                            \
/* Returns the element in DEQUE that is OFFSET elements from its            \
   front.  A value 0 for OFFSET requests the element at the                 \
   front, a value of 1 the element just behind the front, and so            \
   on.  OFFSET must be less than the current number of elements             \
   in DEQUE. */                                                             \
static inline ELEMENT_TYPE *                                                \
NAME##_front (const struct NAME *deque, size_t offset)                      \
{                                                                           \
  assert (NAME##_count (deque) > offset);                                   \
  return &deque->data[(deque->front - offset - 1) & (deque->capacity - 1)]; \
}                                                                           \
                                                                            \
/* Returns the element in DEQUE that is OFFSET elements from its            \
   back.  A value 0 for OFFSET requests the element at the back,            \
   a value of 1 the element just ahead of the back, and so on.              \
   OFFSET must be less than the current number of elements in               \
   DEQUE. */                                                                \
static inline ELEMENT_TYPE *                                                \
NAME##_back (const struct NAME *deque, size_t offset)                       \
{                                                                           \
  assert (NAME##_count (deque) > offset);                                   \
  return &deque->data[(deque->back + offset) & (deque->capacity - 1)];      \
}                                                                           \
                                                                            \
/* Adds and returns the address of a new element at the front of            \
   DEQUE, which must not be full.  The caller is responsible for            \
   assigning a value to the returned element. */                            \
static inline ELEMENT_TYPE *                                                \
NAME##_push_front (struct NAME *deque)                                      \
{                                                                           \
  assert (!NAME##_is_full (deque));                                         \
  return &deque->data[deque->front++ & (deque->capacity - 1)];              \
}                                                                           \
                                                                            \
/* Adds and returns the address of a new element at the back of             \
   DEQUE, which must not be full.  The caller is responsible for            \
   assigning a value to the returned element. */                            \
static inline ELEMENT_TYPE *                                                \
NAME##_push_back (struct NAME *deque)                                       \
{                                                                           \
  assert (!NAME##_is_full (deque));                                         \
  return &deque->data[--deque->back & (deque->capacity - 1)];               \
}                                                                           \
                                                                            \
/* Pops the front element off DEQUE (which must not be empty) and           \
   returns its address.  The element may be reused the next time            \
   an element is pushed into DEQUE or when DEQUE is expanded. */            \
static inline ELEMENT_TYPE *                                                \
NAME##_pop_front (struct NAME *deque)                                       \
{                                                                           \
  assert (!NAME##_is_empty (deque));                                        \
  return &deque->data[--deque->front & (deque->capacity - 1)];              \
}                                                                           \
                                                                            \
/* Pops the back element off DEQUE (which must not be empty) and            \
   returns its address.  The element may be reused the next time            \
   an element is pushed into DEQUE or when DEQUE is expanded. */            \
static inline ELEMENT_TYPE *                                                \
NAME##_pop_back (struct NAME *deque)                                        \
{                                                                           \
  assert (!NAME##_is_empty (deque));                                        \
  return &deque->data[deque->back++ & (deque->capacity - 1)];               \
}                                                                           \
                                                                            \
/* Expands DEQUE, doubling its capacity. */                                 \
static inline void                                                          \
NAME##_expand (struct NAME *deque)                                          \
{                                                                           \
  struct NAME old_deque = *deque;                                           \
  NAME##_init (deque, deque->capacity * 2);                                 \
  while (!NAME##_is_empty (&old_deque))                                     \
    *NAME##_push_front (deque) = *NAME##_pop_back (&old_deque);             \
  free (old_deque.data);                                                    \
}

#endif /* libpspp/deque.h */
