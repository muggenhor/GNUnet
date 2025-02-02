/*
     This file is part of GNUnet.
     Copyright (C) 2001, 2002, 2003, 2005, 2006 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.
*/

/**
 * @file util/common_allocation.c
 * @brief wrapper around malloc/free
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_crypto_lib.h"
#if HAVE_MALLOC_H
#include <malloc.h>
#endif
#if HAVE_MALLOC_MALLOC_H
#include <malloc/malloc.h>
#endif

#define LOG(kind,...) GNUNET_log_from (kind, "util",__VA_ARGS__)

#define LOG_STRERROR(kind,syscall) GNUNET_log_from_strerror (kind, "util", syscall)

#ifndef INT_MAX
#define INT_MAX 0x7FFFFFFF
#endif

#if 0
#define W32_MEM_LIMIT 200000000
#endif

#ifdef W32_MEM_LIMIT
static LONG mem_used = 0;
#endif

/**
 * Allocate memory. Checks the return value, aborts if no more
 * memory is available.
 *
 * @param size how many bytes of memory to allocate, do NOT use
 *  this function (or GNUNET_malloc()) to allocate more than several MB
 *  of memory, if you are possibly needing a very large chunk use
 *  #GNUNET_xmalloc_unchecked_() instead.
 * @param filename where in the code was the call to GNUNET_malloc()
 * @param linenumber where in the code was the call to GNUNET_malloc()
 * @return pointer to size bytes of memory
 */
void *
GNUNET_xmalloc_ (size_t size,
		 const char *filename,
		 int linenumber)
{
  void *ret;

  /* As a security precaution, we generally do not allow very large
   * allocations using the default 'GNUNET_malloc()' macro */
  GNUNET_assert_at (size <= GNUNET_MAX_MALLOC_CHECKED,
		    filename,
		    linenumber);
  ret = GNUNET_xmalloc_unchecked_ (size,
				   filename,
				   linenumber);
  if (NULL == ret)
  {
    LOG_STRERROR (GNUNET_ERROR_TYPE_ERROR,
		  "malloc");
    GNUNET_assert (0);
  }
  return ret;
}


/**
 * Allocate and initialize memory. Checks the return value, aborts if no more
 * memory is available.  Don't use #GNUNET_xmemdup_() directly. Use the
 * GNUNET_memdup() macro.
 *
 * @param buf buffer to initialize from (must contain size bytes)
 * @param size number of bytes to allocate
 * @param filename where is this call being made (for debugging)
 * @param linenumber line where this call is being made (for debugging)
 * @return allocated memory, never NULL
 */
void *
GNUNET_xmemdup_ (const void *buf,
		 size_t size,
		 const char *filename,
                 int linenumber)
{
  void *ret;

  /* As a security precaution, we generally do not allow very large
   * allocations here */
  GNUNET_assert_at (size <= GNUNET_MAX_MALLOC_CHECKED, filename, linenumber);
#ifdef W32_MEM_LIMIT
  size += sizeof (size_t);
  if (mem_used + size > W32_MEM_LIMIT)
    return NULL;
#endif
  GNUNET_assert_at (size < INT_MAX, filename, linenumber);
  ret = malloc (size);
  if (ret == NULL)
  {
    LOG_STRERROR (GNUNET_ERROR_TYPE_ERROR, "malloc");
    GNUNET_assert (0);
  }
#ifdef W32_MEM_LIMIT
  *((size_t *) ret) = size;
  ret = &((size_t *) ret)[1];
  mem_used += size;
#endif
  memcpy (ret, buf, size);
  return ret;
}


/**
 * Wrapper around malloc(). Allocates size bytes of memory.
 * The memory will be zero'ed out.
 *
 * @param size the number of bytes to allocate
 * @param filename where in the code was the call to GNUNET_malloc_unchecked()
 * @param linenumber where in the code was the call to GNUNET_malloc_unchecked()
 * @return pointer to size bytes of memory, NULL if we do not have enough memory
 */
void *
GNUNET_xmalloc_unchecked_ (size_t size,
			   const char *filename,
			   int linenumber)
{
  void *result;

#ifdef W32_MEM_LIMIT
  size += sizeof (size_t);
  if (mem_used + size > W32_MEM_LIMIT)
    return NULL;
#endif

  result = malloc (size);
  if (NULL == result)
    return NULL;
  memset (result, 0, size);

#ifdef W32_MEM_LIMIT
  *((size_t *) result) = size;
  result = &((size_t *) result)[1];
  mem_used += size;
#endif

  return result;
}


/**
 * Reallocate memory. Checks the return value, aborts if no more
 * memory is available.
 *
 * @param ptr the pointer to reallocate
 * @param n how many bytes of memory to allocate
 * @param filename where in the code was the call to GNUNET_realloc()
 * @param linenumber where in the code was the call to GNUNET_realloc()
 * @return pointer to size bytes of memory
 */
void *
GNUNET_xrealloc_ (void *ptr,
		  size_t n,
		  const char *filename,
		  int linenumber)
{
#ifdef W32_MEM_LIMIT
  n += sizeof (size_t);
  ptr = &((size_t *) ptr)[-1];
  mem_used = mem_used - *((size_t *) ptr) + n;
#endif
  ptr = realloc (ptr, n);
  if ((NULL == ptr) && (n > 0))
  {
    LOG_STRERROR (GNUNET_ERROR_TYPE_ERROR, "realloc");
    GNUNET_assert (0);
  }
#ifdef W32_MEM_LIMIT
  ptr = &((size_t *) ptr)[1];
#endif
  return ptr;
}


# if __BYTE_ORDER == __LITTLE_ENDIAN
#define BAADFOOD_STR "\x0D\xF0\xAD\xBA"
#endif
# if __BYTE_ORDER == __BIG_ENDIAN
#define BAADFOOD_STR "\xBA\xAD\xF0\x0D"
#endif

#if WINDOWS
#define M_SIZE(p) _msize (p)
#endif
#if HAVE_MALLOC_NP_H
#include <malloc_np.h>
#endif
#if HAVE_MALLOC_USABLE_SIZE
#define M_SIZE(p) malloc_usable_size (p)
#elif HAVE_MALLOC_SIZE
#define M_SIZE(p) malloc_size (p)
#endif

/**
 * Free memory. Merely a wrapper for the case that we
 * want to keep track of allocations.
 *
 * @param ptr the pointer to free
 * @param filename where in the code was the call to GNUNET_free
 * @param linenumber where in the code was the call to GNUNET_free
 */
void
GNUNET_xfree_ (void *ptr,
	       const char *filename,
	       int linenumber)
{
  GNUNET_assert_at (NULL != ptr,
		    filename,
		    linenumber);
#ifdef W32_MEM_LIMIT
  ptr = &((size_t *) ptr)[-1];
  mem_used -= *((size_t *) ptr);
#endif
#if defined(M_SIZE)
#if ENABLE_POISONING
  {
    const uint64_t baadfood = GNUNET_ntohll (0xBAADF00DBAADF00DLL);
    uint64_t *base = ptr;
    size_t s = M_SIZE (ptr);
    size_t i;

    for (i=0;i<s/8;i++)
      base[i] = baadfood;
    memcpy (&base[s/8], &baadfood, s % 8);
  }
#endif
#endif
  free (ptr);
}


/**
 * Dup a string (same semantics as strdup).
 *
 * @param str the string to dup
 * @param filename where in the code was the call to GNUNET_strdup()
 * @param linenumber where in the code was the call to GNUNET_strdup()
 * @return `strdup(@a str)`
 */
char *
GNUNET_xstrdup_ (const char *str,
		 const char *filename,
		 int linenumber)
{
  char *res;
  size_t slen;

  GNUNET_assert_at (str != NULL,
		    filename,
		    linenumber);
  slen = strlen (str) + 1;
  res = GNUNET_xmalloc_ (slen,
			 filename,
			 linenumber);
  memcpy (res,
	  str,
	  slen);
  return res;
}


#if ! HAVE_STRNLEN
static size_t
strnlen (const char *s,
	 size_t n)
{
  const char *e;

  e = memchr (s, '\0', n);
  if (NULL == e)
    return n;
  return e - s;
}
#endif


/**
 * Dup partially a string (same semantics as strndup).
 *
 * @param str the string to dup
 * @param len the length of the string to dup
 * @param filename where in the code was the call to GNUNET_strndup()
 * @param linenumber where in the code was the call to GNUNET_strndup()
 * @return `strndup(@a str,@a len)`
 */
char *
GNUNET_xstrndup_ (const char *str,
		  size_t len,
		  const char *filename,
                  int linenumber)
{
  char *res;

  if (0 == len)
    return GNUNET_strdup ("");
  GNUNET_assert_at (NULL != str,
		    filename,
		    linenumber);
  len = strnlen (str,
		 len);
  res = GNUNET_xmalloc_ (len + 1,
			 filename,
			 linenumber);
  memcpy (res, str, len);
  /* res[len] = '\0'; 'malloc' zeros out anyway */
  return res;
}


/**
 * Grow an array.  Grows old by (*oldCount-newCount)*elementSize bytes
 * and sets *oldCount to newCount.
 *
 * @param old address of the pointer to the array
 *        *old may be NULL
 * @param elementSize the size of the elements of the array
 * @param oldCount address of the number of elements in the *old array
 * @param newCount number of elements in the new array, may be 0
 * @param filename where in the code was the call to GNUNET_array_grow()
 * @param linenumber where in the code was the call to GNUNET_array_grow()
 */
void
GNUNET_xgrow_ (void **old,
	       size_t elementSize,
	       unsigned int *oldCount,
               unsigned int newCount,
	       const char *filename,
	       int linenumber)
{
  void *tmp;
  size_t size;

  GNUNET_assert_at (INT_MAX / elementSize > newCount, filename, linenumber);
  size = newCount * elementSize;
  if (size == 0)
  {
    tmp = NULL;
  }
  else
  {
    tmp = GNUNET_xmalloc_ (size, filename, linenumber);
    memset (tmp, 0, size);      /* client code should not rely on this, though... */
    if (*oldCount > newCount)
      *oldCount = newCount;     /* shrink is also allowed! */
    if (NULL != *old)
      memcpy (tmp, *old, elementSize * (*oldCount));
  }

  if (*old != NULL)
  {
    GNUNET_xfree_ (*old, filename, linenumber);
  }
  *old = tmp;
  *oldCount = newCount;
}


/**
 * Like asprintf(), just portable.
 *
 * @param buf set to a buffer of sufficient size (allocated, caller must free)
 * @param format format string (see printf(), fprintf(), etc.)
 * @param ... data for format string
 * @return number of bytes in `*@a buf`, excluding 0-termination
 */
int
GNUNET_asprintf (char **buf,
		 const char *format,
		 ...)
{
  int ret;
  va_list args;

  va_start (args, format);
  ret = VSNPRINTF (NULL, 0, format, args);
  va_end (args);
  *buf = GNUNET_malloc (ret + 1);
  va_start (args, format);
  ret = VSPRINTF (*buf, format, args);
  va_end (args);
  return ret;
}


/**
 * Like snprintf(), just aborts if the buffer is of insufficient size.
 *
 * @param buf pointer to buffer that is written to
 * @param size number of bytes in buf
 * @param format format strings
 * @param ... data for format string
 * @return number of bytes written to buf or negative value on error
 */
int
GNUNET_snprintf (char *buf,
		 size_t size,
		 const char *format, ...)
{
  int ret;
  va_list args;

  va_start (args, format);
  ret = VSNPRINTF (buf, size, format, args);
  va_end (args);
  GNUNET_assert (ret < size);
  return ret;
}


/**
 * Create a copy of the given message.
 *
 * @param msg message to copy
 * @return duplicate of the message
 */
struct GNUNET_MessageHeader *
GNUNET_copy_message (const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_MessageHeader *ret;
  uint16_t msize;

  msize = ntohs (msg->size);
  GNUNET_assert (msize >= sizeof (struct GNUNET_MessageHeader));
  ret = GNUNET_malloc (msize);
  memcpy (ret, msg, msize);
  return ret;
}


/* end of common_allocation.c */
