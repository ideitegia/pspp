#ifndef LL_H
#define LL_H


struct node
{
  void *entry;
  struct node *next;
};



typedef void ll_free_func (void *, void *aux);

struct linked_list
{
  struct node *head;
  ll_free_func *free;
  void *aux;
};


struct ll_iterator
{
  struct node *p;
};


/* Iteration */

/* Return the first element in LL */
void * ll_first (const struct linked_list *ll, struct ll_iterator *li);

/* Return the next element in LL iterated by LI */
void * ll_next (const struct linked_list *ll, struct ll_iterator *li);

/* Create a linked list.
   Elements will be freed using F and AUX
*/
struct linked_list * ll_create( ll_free_func *F , void *aux);

/* Destroy a linked list LL */
void ll_destroy(struct linked_list *ll);

/* Push a an element ENTRY onto the list LL */
void ll_push_front(struct linked_list *ll, void *entry);

#endif
