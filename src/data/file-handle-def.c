/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include <config.h>

#include "data/file-handle-def.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "data/dataset.h"
#include "data/file-name.h"
#include "data/variable.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* File handle. */
struct file_handle
  {
    struct hmap_node name_node; /* Element in named_handles hmap. */
    size_t ref_cnt;             /* Number of references. */
    char *id;                   /* Identifier token, NULL if none. */
    char *name;                 /* User-friendly identifying name. */
    enum fh_referent referent;  /* What the file handle refers to. */

    /* FH_REF_FILE only. */
    char *file_name;		/* File name as provided by user. */
    enum fh_mode mode;  	/* File mode. */
    enum fh_line_ends line_ends; /* Line ends for text files. */

    /* FH_REF_FILE and FH_REF_INLINE only. */
    size_t record_width;        /* Length of fixed-format records. */
    size_t tab_width;           /* Tab width, 0=do not expand tabs. */
    char *encoding;             /* Charset for contents. */

    /* FH_REF_DATASET only. */
    struct dataset *ds;         /* Dataset. */
  };

/* All "struct file_handle"s with nonnull 'id' member. */
static struct hmap named_handles = HMAP_INITIALIZER (named_handles);

/* Default file handle for DATA LIST, REREAD, REPEATING DATA
   commands. */
static struct file_handle *default_handle;

/* The "file" that reads from BEGIN DATA...END DATA. */
static struct file_handle *inline_file;

static struct file_handle *create_handle (const char *id,
                                          char *name, enum fh_referent,
                                          const char *encoding);
static void free_handle (struct file_handle *);
static void unname_handle (struct file_handle *);

/* Hash table of all active locks. */
static struct hmap locks = HMAP_INITIALIZER (locks);

/* File handle initialization routine. */
void
fh_init (void)
{
  inline_file = create_handle ("INLINE", xstrdup ("INLINE"), FH_REF_INLINE,
                               "Auto");
  inline_file->record_width = 80;
  inline_file->tab_width = 8;
}

/* Removes all named file handles from the global list. */
void
fh_done (void)
{
  struct file_handle *handle, *next;

  HMAP_FOR_EACH_SAFE (handle, next,
                      struct file_handle, name_node, &named_handles)
    unname_handle (handle);
}

/* Free HANDLE and remove it from the global list. */
static void
free_handle (struct file_handle *handle)
{
  /* Remove handle from global list. */
  if (handle->id != NULL)
    hmap_delete (&named_handles, &handle->name_node);

  /* Free data. */
  free (handle->id);
  free (handle->name);
  free (handle->file_name);
  free (handle->encoding);
  free (handle);
}

/* Make HANDLE unnamed, so that it can no longer be referenced by
   name.  The caller must hold a reference to HANDLE, which is
   not affected by this function. */
static void
unname_handle (struct file_handle *handle)
{
  assert (handle->id != NULL);
  free (handle->id);
  handle->id = NULL;
  hmap_delete (&named_handles, &handle->name_node);

  /* Drop the reference held by the named_handles table. */
  fh_unref (handle);
}

/* Increments HANDLE's reference count and returns HANDLE. */
struct file_handle *
fh_ref (struct file_handle *handle)
{
  assert (handle->ref_cnt > 0);
  handle->ref_cnt++;
  return handle;
}

/* Decrements HANDLE's reference count.
   If the reference count drops to 0, HANDLE is destroyed. */
void
fh_unref (struct file_handle *handle)
{
  if (handle != NULL)
    {
      assert (handle->ref_cnt > 0);
      if (--handle->ref_cnt == 0)
        free_handle (handle);
    }
}

/* Make HANDLE unnamed, so that it can no longer be referenced by
   name.  The caller must hold a reference to HANDLE, which is
   not affected by this function.

   This function ignores a null pointer as input.  It has no
   effect on the inline handle, which is always named INLINE.*/
void
fh_unname (struct file_handle *handle)
{
  assert (handle->ref_cnt > 1);
  if (handle != fh_inline_file () && handle->id != NULL)
    unname_handle (handle);
}

/* Returns the handle with the given ID, or a null pointer if
   there is none. */
struct file_handle *
fh_from_id (const char *id)
{
  struct file_handle *handle;

  HMAP_FOR_EACH_WITH_HASH (handle, struct file_handle, name_node,
                           utf8_hash_case_string (id, 0), &named_handles)
    if (!utf8_strcasecmp (id, handle->id))
      {
	return fh_ref (handle);
      }

  return NULL;
}

/* Creates a new handle with identifier ID (which may be null)
   and name HANDLE_NAME that refers to REFERENT.  Links the new
   handle into the global list.  Returns the new handle.

   The new handle is not fully initialized.  The caller is
   responsible for completing its initialization. */
static struct file_handle *
create_handle (const char *id, char *handle_name, enum fh_referent referent,
               const char *encoding)
{
  struct file_handle *handle = xzalloc (sizeof *handle);

  handle->ref_cnt = 1;
  handle->id = id != NULL ? xstrdup (id) : NULL;
  handle->name = handle_name;
  handle->referent = referent;
  handle->encoding = xstrdup (encoding);

  if (id != NULL)
    {
      hmap_insert (&named_handles, &handle->name_node,
                   utf8_hash_case_string (handle->id, 0));
    }

  return handle;
}

/* Returns the unique handle of referent type FH_REF_INLINE,
   which refers to the "inline file" that represents character
   data in the command file between BEGIN DATA and END DATA. */
struct file_handle *
fh_inline_file (void)
{
  return inline_file;
}

/* Creates and returns a new file handle with the given ID, which may be null.
   If it is non-null, it must be a UTF-8 encoded string that is unique among
   existing file identifiers.  The new handle is associated with file FILE_NAME
   and the given PROPERTIES. */
struct file_handle *
fh_create_file (const char *id, const char *file_name,
                const struct fh_properties *properties)
{
  char *handle_name;
  struct file_handle *handle;

  handle_name = id != NULL ? xstrdup (id) : xasprintf ("`%s'", file_name);
  handle = create_handle (id, handle_name, FH_REF_FILE, properties->encoding);
  handle->file_name = xstrdup (file_name);
  handle->mode = properties->mode;
  handle->line_ends = properties->line_ends;
  handle->record_width = properties->record_width;
  handle->tab_width = properties->tab_width;
  return handle;
}

/* Creates a new file handle with the given ID, which must be
   unique among existing file identifiers.  The new handle is
   associated with a dataset file (initially empty). */
struct file_handle *
fh_create_dataset (struct dataset *ds)
{
  const char *name;
  struct file_handle *handle;

  name = dataset_name (ds);
  if (name[0] == '\0')
    name = _("active dataset");

  handle = create_handle (NULL, xstrdup (name), FH_REF_DATASET, C_ENCODING);
  handle->ds = ds;
  return handle;
}

/* Returns a set of default properties for a file handle. */
const struct fh_properties *
fh_default_properties (void)
{
#if defined _WIN32 || defined __WIN32__
#define DEFAULT_LINE_ENDS FH_END_CRLF
#else
#define DEFAULT_LINE_ENDS FH_END_LF
#endif

  static const struct fh_properties default_properties
    = {FH_MODE_TEXT, DEFAULT_LINE_ENDS, 1024, 4, (char *) "Auto"};
  return &default_properties;
}

/* Returns the identifier that may be used in syntax to name the
   given HANDLE, which takes the form of a PSPP identifier.  If
   HANDLE has no identifier, returns a null pointer.

   Return value is owned by the file handle.*/
const char *
fh_get_id (const struct file_handle *handle)
{
  return handle->id;
}

/* Returns a user-friendly string to identify the given HANDLE.
   If HANDLE was created by referring to a file name, returns the
   file name, enclosed in double quotes.  Return value is owned
   by the file handle.

   Useful for printing error messages about use of file handles.  */
const char *
fh_get_name (const struct file_handle *handle)
{
  return handle->name;
}

/* Returns the type of object that HANDLE refers to. */
enum fh_referent
fh_get_referent (const struct file_handle *handle)
{
  return handle->referent;
}

/* Returns the name of the file associated with HANDLE. */
const char *
fh_get_file_name (const struct file_handle *handle)
{
  assert (handle->referent == FH_REF_FILE);
  return handle->file_name;
}

/* Returns the mode of HANDLE. */
enum fh_mode
fh_get_mode (const struct file_handle *handle)
{
  assert (handle->referent == FH_REF_FILE);
  return handle->mode;
}

/* Returns the line ends of HANDLE, which must be a handle associated with a
   file. */
enum fh_line_ends
fh_get_line_ends (const struct file_handle *handle)
{
  assert (handle->referent == FH_REF_FILE);
  return handle->line_ends;
}

/* Returns the width of a logical record on HANDLE. */
size_t
fh_get_record_width (const struct file_handle *handle)
{
  assert (handle->referent & (FH_REF_FILE | FH_REF_INLINE));
  return handle->record_width;
}

/* Returns the number of characters per tab stop for HANDLE, or
   zero if tabs are not to be expanded.  Applicable only to
   FH_MODE_TEXT files. */
size_t
fh_get_tab_width (const struct file_handle *handle)
{
  assert (handle->referent & (FH_REF_FILE | FH_REF_INLINE));
  return handle->tab_width;
}

/* Returns the encoding of characters read from HANDLE. */
const char *
fh_get_encoding (const struct file_handle *handle)
{
  return handle->encoding;
}

/* Returns the dataset handle associated with HANDLE.
   Applicable to only FH_REF_DATASET files. */
struct dataset *
fh_get_dataset (const struct file_handle *handle)
{
  assert (handle->referent == FH_REF_DATASET);
  return handle->ds;
}

/* Returns the current default handle. */
struct file_handle *
fh_get_default_handle (void)
{
  return default_handle ? default_handle : fh_inline_file ();
}

/* Sets NEW_DEFAULT_HANDLE as the default handle. */
void
fh_set_default_handle (struct file_handle *new_default_handle)
{
  assert (new_default_handle == NULL
          || (new_default_handle->referent & (FH_REF_INLINE | FH_REF_FILE)));
  if (default_handle != NULL && default_handle != inline_file)
    fh_unref (default_handle);
  default_handle = new_default_handle;
  if (default_handle != NULL)
    fh_ref (default_handle);
}

/* Information about a file handle's readers or writers. */
struct fh_lock
  {
    struct hmap_node node;      /* hmap_node member. */

    /* Hash key. */
    enum fh_referent referent;  /* Type of underlying file. */
    union
      {
        struct file_identity *file; /* FH_REF_FILE only. */
        unsigned int unique_id;    /* FH_REF_DATASET only. */
      }
    u;
    enum fh_access access;      /* Type of file access. */

    /* Number of openers. */
    size_t open_cnt;

    /* Applicable only when open_cnt > 0. */
    bool exclusive;             /* No other openers allowed? */
    const char *type;           /* Human-readable type of file. */
    void *aux;                  /* Owner's auxiliary data. */
  };


static void make_key (struct fh_lock *, const struct file_handle *,
                      enum fh_access);
static void free_key (struct fh_lock *);
static int compare_fh_locks (const struct fh_lock *a, const struct fh_lock *b);
static unsigned int hash_fh_lock (const struct fh_lock *lock);

/* Tries to lock handle H for the given kind of ACCESS and TYPE
   of file.  Returns a pointer to a struct fh_lock if successful,
   otherwise a null pointer.

   H's referent type must be one of the bits in MASK.  The caller
   must verify this ahead of time; we simply assert it here.

   TYPE is the sort of file, e.g. "system file".  Only one type
   of access is allowed on a given file at a time for reading,
   and similarly for writing.  If successful, a reference to TYPE
   is retained, so it should probably be a string literal.

   TYPE should be marked with N_() in the caller: that is, the
   caller should not translate it with gettext, but fh_lock will
   do so.

   ACCESS specifies whether the lock is for reading or writing.
   EXCLUSIVE is true to require exclusive access, false to allow
   sharing with other accessors.  Exclusive read access precludes
   other readers, but not writers; exclusive write access
   precludes other writers, but not readers.  A sharable read or
   write lock precludes reader or writers, respectively, of a
   different TYPE.

   A lock may be associated with auxiliary data.  See
   fh_lock_get_aux and fh_lock_set_aux for more details. */
struct fh_lock *
fh_lock (struct file_handle *h, enum fh_referent mask UNUSED,
         const char *type, enum fh_access access, bool exclusive)
{
  struct fh_lock *key = NULL;
  size_t hash ;
  struct fh_lock *lock = NULL;
  bool found_lock = false;

  assert ((fh_get_referent (h) & mask) != 0);
  assert (access == FH_ACC_READ || access == FH_ACC_WRITE);

  key = xmalloc (sizeof *key);

  make_key (key, h, access);

  key->open_cnt = 1;
  key->exclusive = exclusive;
  key->type = type;
  key->aux = NULL;

  hash = hash_fh_lock (key);

  HMAP_FOR_EACH_WITH_HASH (lock, struct fh_lock, node, hash, &locks)
    {
      if ( 0 == compare_fh_locks (lock, key))
	{
	  found_lock = true;
	  break;
	}
    }

  if ( found_lock )
    {
      if (strcmp (lock->type, type))
        {
          if (access == FH_ACC_READ)
            msg (SE, _("Can't read from %s as a %s because it is "
                       "already being read as a %s."),
                 fh_get_name (h), gettext (type), gettext (lock->type));
          else
            msg (SE, _("Can't write to %s as a %s because it is "
                       "already being written as a %s."),
                 fh_get_name (h), gettext (type), gettext (lock->type));
          return NULL;
        }
      else if (exclusive || lock->exclusive)
        {
          msg (SE, _("Can't re-open %s as a %s."),
               fh_get_name (h), gettext (type));
          return NULL;
        }
      lock->open_cnt++;
      
      free_key (key);
      free (key);

      return lock;
    }

  hmap_insert (&locks, &key->node, hash);
  found_lock = false;
  HMAP_FOR_EACH_WITH_HASH (lock, struct fh_lock, node, hash, &locks)
    {
      if ( 0 == compare_fh_locks (lock, key))
	{
	  found_lock = true;
	  break;
	}
    }

  assert (found_lock);

  return key;
}

/* Releases LOCK that was acquired with fh_lock.
   Returns true if LOCK is still locked, because other clients
   also had it locked.

   Returns false if LOCK has now been destroyed.  In this case
   the caller must ensure that any auxiliary data associated with
   LOCK is destroyed, to avoid a memory leak.  The caller must
   obtain a pointer to the auxiliary data, e.g. via
   fh_lock_get_aux *before* calling fh_unlock (because it yields
   undefined behavior to call fh_lock_get_aux on a destroyed
   lock).  */
bool
fh_unlock (struct fh_lock *lock)
{
  if (lock != NULL)
    {
      assert (lock->open_cnt > 0);
      if (--lock->open_cnt == 0)
        {
	  hmap_delete (&locks, &lock->node);
          free_key (lock);
          free (lock);
          return false;
        }
    }
  return true;
}

/* Returns auxiliary data for LOCK.

   Auxiliary data is shared by every client that holds LOCK (for
   an exclusive lock, this is a single client).  To avoid leaks,
   auxiliary data must be released before LOCK is destroyed. */
void *
fh_lock_get_aux (const struct fh_lock *lock)
{
  return lock->aux;
}

/* Sets the auxiliary data for LOCK to AUX. */
void
fh_lock_set_aux (struct fh_lock *lock, void *aux)
{
  lock->aux = aux;
}

/* Returns true if HANDLE is locked for the given type of ACCESS,
   false otherwise. */
bool
fh_is_locked (const struct file_handle *handle, enum fh_access access)
{
  struct fh_lock key;
  const struct fh_lock *k = NULL;
  bool is_locked = false;
  size_t hash ;

  make_key (&key, handle, access);

  hash = hash_fh_lock (&key);


  HMAP_FOR_EACH_WITH_HASH (k, struct fh_lock, node, hash, &locks)
    {
      if ( 0 == compare_fh_locks (k, &key))
	{
	  is_locked = true;
	  break;
	}
    }

  free_key (&key);

  return is_locked;
}

/* Initializes the key fields in LOCK for looking up or inserting
   handle H for the given kind of ACCESS. */
static void
make_key (struct fh_lock *lock, const struct file_handle *h,
          enum fh_access access)
{
  lock->referent = fh_get_referent (h);
  lock->access = access;
  if (lock->referent == FH_REF_FILE)
    lock->u.file = fn_get_identity (fh_get_file_name (h));
  else if (lock->referent == FH_REF_DATASET)
    lock->u.unique_id = dataset_seqno (fh_get_dataset (h));
}

/* Frees the key fields in LOCK. */
static void
free_key (struct fh_lock *lock)
{
  if (lock->referent == FH_REF_FILE)
    fn_free_identity (lock->u.file);
}

/* Compares the key fields in struct fh_lock objects A and B and
   returns a strcmp()-type result. */
static int
compare_fh_locks (const struct fh_lock *a, const struct fh_lock *b)
{
  if (a->referent != b->referent)
    return a->referent < b->referent ? -1 : 1;
  else if (a->access != b->access)
    return a->access < b->access ? -1 : 1;
  else if (a->referent == FH_REF_FILE)
    return fn_compare_file_identities (a->u.file, b->u.file);
  else if (a->referent == FH_REF_DATASET)
    return (a->u.unique_id < b->u.unique_id ? -1
            : a->u.unique_id > b->u.unique_id);
  else
    return 0;
}

/* Returns a hash value for LOCK. */
static unsigned int
hash_fh_lock (const struct fh_lock *lock)
{
  unsigned int basis;
  if (lock->referent == FH_REF_FILE)
    basis = fn_hash_identity (lock->u.file);
  else if (lock->referent == FH_REF_DATASET)
    basis = lock->u.unique_id;
  else
    basis = 0;
  return hash_int ((lock->referent << 3) | lock->access, basis);
}
