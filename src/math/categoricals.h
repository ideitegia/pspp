#ifndef _CATEGORICALS__
#define _CATEGORICALS__

#include <stddef.h>

struct categoricals;
struct variable;
struct ccase;

union value ;

struct categoricals *categoricals_create (const struct variable **v, size_t n_vars,
					  const struct variable *wv);

void categoricals_update (struct categoricals *cat, const struct ccase *c);


/* Return the number of categories (distinct values) for variable N */
size_t categoricals_n_count (const struct categoricals *cat, size_t n);


/* Return the total number of categories */
size_t categoricals_total (const struct categoricals *cat);

/* Return the index for variable N */
int categoricals_index (const struct categoricals *cat, size_t n, const union value *val);

#endif
