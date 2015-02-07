/*
      This file is part of GNUnet
      Copyright (C) 2012 Christian Grothoff (and other contributing authors)

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
      Free Software Foundation, Inc., 59 Temple Place - Suite 330,
      Boston, MA 02111-1307, USA.
*/
/**
 * @file set/gnunet-service-set_union_strata_estimator.c
 * @brief invertible bloom filter
 * @author Florian Dold
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "ibf.h"
#include "gnunet-service-set_union_strata_estimator.h"


/**
 * Write the given strata estimator to the buffer.
 *
 * @param se strata estimator to serialize
 * @param buf buffer to write to, must be of appropriate size
 */
void
strata_estimator_write (const struct StrataEstimator *se,
                        void *buf)
{
  unsigned int i;

  GNUNET_assert (NULL != se);
  for (i = 0; i < se->strata_count; i++)
  {
    ibf_write_slice (se->strata[i], 0, se->ibf_size, buf);
    buf += se->ibf_size * IBF_BUCKET_SIZE;
  }
}


/**
 * Read strata from the buffer into the given strata
 * estimator.  The strata estimator must already be allocated.
 *
 * @param buf buffer to read from
 * @param se strata estimator to write to
 */
void
strata_estimator_read (const void *buf,
                       struct StrataEstimator *se)
{
  unsigned int i;

  for (i = 0; i < se->strata_count; i++)
  {
    ibf_read_slice (buf, 0, se->ibf_size, se->strata[i]);
    buf += se->ibf_size * IBF_BUCKET_SIZE;
  }
}


/**
 * Add a key to the strata estimator.
 *
 * @param se strata estimator to add the key to
 * @param key key to add
 */
void
strata_estimator_insert (struct StrataEstimator *se,
                         struct IBF_Key key)
{
  uint64_t v;
  unsigned int i;

  v = key.key_val;
  /* count trailing '1'-bits of v */
  for (i = 0; v & 1; v>>=1, i++)
    /* empty */;
  ibf_insert (se->strata[i], key);
}


/**
 * Remove a key from the strata estimator.
 *
 * @param se strata estimator to remove the key from
 * @param key key to remove
 */
void
strata_estimator_remove (struct StrataEstimator *se,
                         struct IBF_Key key)
{
  uint64_t v;
  unsigned int i;

  v = key.key_val;
  /* count trailing '1'-bits of v */
  for (i = 0; v & 1; v>>=1, i++)
    /* empty */;
  ibf_remove (se->strata[i], key);
}


/**
 * Create a new strata estimator with the given parameters.
 *
 * @param strata_count number of stratas, that is, number of ibfs in the estimator
 * @param ibf_size size of each ibf stratum
 * @param ibf_hashnum hashnum parameter of each ibf
 * @return a freshly allocated, empty strata estimator
 */
struct StrataEstimator *
strata_estimator_create (unsigned int strata_count,
                         uint32_t ibf_size,
                         uint8_t ibf_hashnum)
{
  struct StrataEstimator *se;
  unsigned int i;

  /* fixme: allocate everything in one chunk */
  se = GNUNET_new (struct StrataEstimator);
  se->strata_count = strata_count;
  se->ibf_size = ibf_size;
  se->strata = GNUNET_malloc (sizeof (struct InvertibleBloomFilter *) * strata_count);
  for (i = 0; i < strata_count; i++)
    se->strata[i] = ibf_create (ibf_size, ibf_hashnum);
  return se;
}


/**
 * Estimate set difference with two strata estimators,
 * i.e. arrays of IBFs.
 * Does not not modify its arguments.
 *
 * @param se1 first strata estimator
 * @param se2 second strata estimator
 * @return the estimated difference
 */
unsigned int
strata_estimator_difference (const struct StrataEstimator *se1,
                             const struct StrataEstimator *se2)
{
  int i;
  unsigned int count;

  GNUNET_assert (se1->strata_count == se2->strata_count);
  count = 0;
  for (i = se1->strata_count - 1; i >= 0; i--)
  {
    struct InvertibleBloomFilter *diff;
    /* number of keys decoded from the ibf */
    int ibf_count;

    /* FIXME: implement this without always allocating new IBFs */
    diff = ibf_dup (se1->strata[i]);
    ibf_subtract (diff, se2->strata[i]);
    for (ibf_count = 0; GNUNET_YES; ibf_count++)
    {
      int more;

      more = ibf_decode (diff, NULL, NULL);
      if (GNUNET_NO == more)
      {
        count += ibf_count;
        break;
      }
      /* Estimate if decoding fails or would not terminate */
      if ((GNUNET_SYSERR == more) || (ibf_count > diff->size))
      {
        ibf_destroy (diff);
        return count * (1 << (i + 1));
      }
    }
    ibf_destroy (diff);
  }
  return count;
}


/**
 * Make a copy of a strata estimator.
 *
 * @param se the strata estimator to copy
 * @return the copy
 */
struct StrataEstimator *
strata_estimator_dup (struct StrataEstimator *se)
{
  struct StrataEstimator *c;
  unsigned int i;

  c = GNUNET_new (struct StrataEstimator);
  c->strata_count = se->strata_count;
  c->ibf_size = se->ibf_size;
  c->strata = GNUNET_malloc (sizeof (struct InvertibleBloomFilter *) * se->strata_count);
  for (i = 0; i < se->strata_count; i++)
    c->strata[i] = ibf_dup (se->strata[i]);
  return c;
}


/**
 * Destroy a strata estimator, free all of its resources.
 *
 * @param se strata estimator to destroy.
 */
void
strata_estimator_destroy (struct StrataEstimator *se)
{
  unsigned int i;

  for (i = 0; i < se->strata_count; i++)
    ibf_destroy (se->strata[i]);
  GNUNET_free (se->strata);
  GNUNET_free (se);
}

