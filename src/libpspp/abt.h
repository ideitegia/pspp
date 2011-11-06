/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_ABT_H
#define LIBPSPP_ABT_H 1

/* Augmented binary tree (ABT) data structure.

   A data structure can be "augmented" by defining new
   information for it to maintain.  One commonly useful way to
   augment a binary search tree-based data structure is to define
   part of its data as a function of its immediate children's
   data.  Furthermore, augmented data defined in this way can be
   efficiently maintained as the tree changes over time.

   For example, suppose we define the "size" of a node as the sum
   of the "size" of its immediate children, plus 1.  In such an
   annotated BST with height H, we can find the node that would
   be Kth in in-order traversal in O(H) time, instead of O(K)
   time, which is a significant saving for balanced trees.

   The ABT data structure partially abstracts augmentation.  The
   client passes in a "reaugmentation" function that accepts a
   node.  This function must recalculate the node's augmentation
   data based on its own contents and the contents of its
   children, and store the new augmentation data in the node.

   The ABT automatically calls the reaugmentation function
   whenever it can tell that a node's augmentation data might
   need to be updated: when the node is inserted or when a node's
   descendants change due to insertion or deletion.  The ABT does
   not know to call the reaugmentation function if a node's data
   is updated while it is in the ABT.  In such a case, call the
   abt_reaugmented or abt_changed function to update the
   augmentation.

   Augmentation is only partially abstracted: we do not provide
   any way to search an ABT based on its augmentations.  The
   tree structure is thus exposed to the client to allow it to
   implement search.

   To allow for optimization, the ABT implementation assumes that
   the augmentation function in use is unaffected by the shape of
   a binary search tree.  That is, if a given subtree within a
   larger tree is rearranged, e.g. via a series of rotations,
   then the implementation will not call the reaugmentation
   function outside of the subtree, because the overall
   augmentation data for the subtree is assumed not to change.
   This optimization is valid for the forms of augmentation
   described in CLR and Knuth (see below), and it is possible
   that it is valid for every efficient binary search tree
   augmentation.

   The client should not need to be aware of the form of
   balancing applied to the ABT, as its operation should be fully
   encapsulated by the reaugmentation function.  The current
   implementation uses an AA (Arne Andersson) tree, but this is
   subject to change.

   The following example illustrates how to use an ABT to build a
   tree that can be searched either by a data value or in-order
   position:

     // Test data element.
     struct element
       {
         struct abt_node node;       // Embedded binary tree element.
         int data;                   // Primary value.
         int count;                  // Number of nodes in subtree,
                                     // including this node.
       };

     // Returns the `struct element' that NODE is embedded within.
     static struct element *
     node_to_element (const struct abt_node *node)
     {
       return abt_data (node, struct element, node);
     }

     // Compares the DATA values in A and B and returns a
     // strcmp-type return value.
     static int
     compare_elements (const struct abt_node *a_, const struct abt_node *b_,
                       const void *aux)
     {
       const struct element *a = node_to_element (a_);
       const struct element *b = node_to_element (b_);

       return a->data < b->data ? -1 : a->data > b->data;
     }

     // Recalculates the count for NODE's subtree by adding up the
     // counts for its left and right child subtrees.
     static void
     reaugment_elements (struct abt_node *node_, const void *aux)
     {
       struct element *node = node_to_element (node_);
       node->count = 1;
       if (node->node.down[0] != NULL)
         node->count += node_to_element (node->node.down[0])->count;
       if (node->node.down[1] != NULL)
         node->count += node_to_element (node->node.down[1])->count;
     }

     // Finds and returns the element in ABT that is in the given
     // 0-based POSITION in in-order.
     static struct element *
     find_by_position (struct abt *abt, int position)
     {
       struct abt_node *p;
       for (p = abt->root; p != NULL; )
         {
           int p_pos = p->down[0] ? node_to_element (p->down[0])->count : 0;
           if (position == p_pos)
             return node_to_element (p);
           else if (position < p_pos)
             p = p->down[0];
           else
             {
               p = p->down[1];
               position -= p_pos + 1;
             }
         }
       return NULL;
     }

   For more information on augmenting binary search tree-based
   data structures, see Cormen-Leiserson-Rivest, chapter 15, or
   Knuth vol. 3, section 6.2.3, under "Linear list
   representation."  For more information on AA trees, see
   <http://en.wikipedia.org/wiki/AA_tree>, which includes source
   code and links to other resources, such as the original AA
   tree paper.  */

#include <stdbool.h>
#include <stddef.h>
#include "libpspp/cast.h"

/* Returns the data structure corresponding to the given NODE,
   assuming that NODE is embedded as the given MEMBER name in
   data type STRUCT. */
#define abt_data(NODE, STRUCT, MEMBER)                          \
        (CHECK_POINTER_HAS_TYPE (NODE, struct abt_node *),      \
         UP_CAST (NODE, STRUCT, MEMBER))

/* Node in an augmented binary tree. */
struct abt_node
  {
    struct abt_node *up;        /* Parent (NULL for root). */
    struct abt_node *down[2];   /* Left child, right child. */
    int level;                  /* AA tree level (not ordinary BST level). */
  };

/* Compares nodes A and B, with the tree's AUX.
   Returns a strcmp-like result. */
typedef int abt_compare_func (const struct abt_node *a,
                              const struct abt_node *b,
                              const void *aux);

/* Recalculates NODE's augmentation based on NODE's data and that of its left
   and right children NODE->down[0] and NODE[1], respectively, with the tree's
   AUX. */
typedef void abt_reaugment_func (struct abt_node *node, const void *aux);

/* An augmented binary tree. */
struct abt
  {
    struct abt_node *root;         /* Tree's root, NULL if empty. */
    abt_compare_func *compare;     /* To compare nodes. */
    abt_reaugment_func *reaugment; /* To augment a node using its children. */
    const void *aux;               /* Auxiliary data. */
  };

void abt_init (struct abt *, abt_compare_func *, abt_reaugment_func *,
               const void *aux);

static inline bool abt_is_empty (const struct abt *);

struct abt_node *abt_insert (struct abt *, struct abt_node *);
void abt_insert_after (struct abt *,
                       const struct abt_node *, struct abt_node *);
void abt_insert_before (struct abt *,
                        const struct abt_node *, struct abt_node *);
void abt_delete (struct abt *, struct abt_node *);

struct abt_node *abt_first (const struct abt *);
struct abt_node *abt_last (const struct abt *);
struct abt_node *abt_find (const struct abt *, const struct abt_node *);
struct abt_node *abt_next (const struct abt *, const struct abt_node *);
struct abt_node *abt_prev (const struct abt *, const struct abt_node *);

void abt_reaugmented (const struct abt *, struct abt_node *);
struct abt_node *abt_changed (struct abt *, struct abt_node *);
void abt_moved (struct abt *, struct abt_node *);

/* Returns true if ABT contains no nodes, false if ABT contains at least one
   node. */
static inline bool
abt_is_empty (const struct abt *abt)
{
  return abt->root == NULL;
}

#endif /* libpspp/abt.h */
