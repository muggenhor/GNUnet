/*
     This file is part of GNUnet
     (C) 2012, 2013 Christian Grothoff (and other contributing authors)

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
 * @file include/gnunet_namestore_plugin.h
 * @brief plugin API for the namestore database backend
 * @author Christian Grothoff
 */
#ifndef GNUNET_NAMESTORE_PLUGIN_H
#define GNUNET_NAMESTORE_PLUGIN_H

#include "gnunet_util_lib.h"
#include "gnunet_namestore_service.h"

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif


/**
 * Function called for matching blocks.
 *
 * @param cls closure
 * @param block lookup result
 */
typedef void (*GNUNET_NAMESTORE_BlockCallback) (void *cls,
						const struct GNUNET_NAMESTORE_Block *block);


/**
 * Function called by for each matching record.
 *
 * @param cls closure
 * @param zone_key private key of the zone
 * @param label name that is being mapped (at most 255 characters long)
 * @param rd_count number of entries in @a rd array
 * @param rd array of records with data to store
 */
typedef void (*GNUNET_NAMESTORE_RecordIterator) (void *cls,
						 const struct GNUNET_CRYPTO_EccPrivateKey *private_key,
						 const char *label,
						 unsigned int rd_count,
						 const struct GNUNET_NAMESTORE_RecordData *rd);


/**
 * @brief struct returned by the initialization function of the plugin
 */
struct GNUNET_NAMESTORE_PluginFunctions
{

  /**
   * Closure to pass to all plugin functions.
   */
  void *cls;

  /**
   * Cache a block in the datastore. Overwrites existing blocks
   * for the same zone and label.
   *
   * @param cls closure (internal context for the plugin)
   * @param block block to cache
   * @return #GNUNET_OK on success, else #GNUNET_SYSERR
   */
  int (*cache_block) (void *cls, 
		      const struct GNUNET_NAMESTORE_Block *block);


  /**
   * Get the block for a particular zone and label in the
   * datastore.  Will return at most one result to the iterator.
   *
   * @param cls closure (internal context for the plugin)
   * @param query hash of public key derived from the zone and the label
   * @param iter function to call with the result
   * @param iter_cls closure for @a iter
   * @return #GNUNET_OK on success, #GNUNET_NO if there were no results, #GNUNET_SYSERR on error
   */
  int (*lookup_block) (void *cls, 
		       const struct GNUNET_HashCode *query,
		       GNUNET_NAMESTORE_BlockCallback iter, void *iter_cls);



  /**
   * Store a record in the datastore for which we are the authority.
   * Removes any existing record in the same zone with the same name.
   *
   * @param cls closure (internal context for the plugin)
   * @param zone private key of the zone
   * @param label name of the record in the zone
   * @param rd_count number of entries in @a rd array, 0 to delete all records
   * @param rd array of records with data to store
   * @return #GNUNET_OK on success, else #GNUNET_SYSERR
   */
  int (*store_records) (void *cls, 
			const struct GNUNET_CRYPTO_EccPrivateKey *zone,
			const char *label,
			unsigned int rd_count,
			const struct GNUNET_NAMESTORE_RecordData *rd);


  /**
   * Iterate over the results for a particular zone in the
   * datastore.  Will return at most one result to the iterator.
   *
   * @param cls closure (internal context for the plugin)
   * @param zone private key of the zone, NULL for all zones
   * @param offset offset in the list of all matching records
   * @param iter function to call with the result
   * @param iter_cls closure for @a iter
   * @return #GNUNET_OK on success, #GNUNET_NO if there were no results, #GNUNET_SYSERR on error
   */
  int (*iterate_records) (void *cls, 
			  const struct GNUNET_CRYPTO_EccPrivateKey *zone,
			  uint64_t offset,
			  GNUNET_NAMESTORE_RecordIterator iter, void *iter_cls);


  /**
   * Look for an existing PKEY delegation record for a given public key.
   * Returns at most one result to the iterator.
   *
   * @param cls closure (internal context for the plugin)
   * @param zone private key of the zone to look up in, never NULL
   * @param value_zone public key of the target zone (value), never NULL
   * @param iter function to call with the result
   * @param iter_cls closure for @a iter
   * @return #GNUNET_OK on success, #GNUNET_NO if there were no results, #GNUNET_SYSERR on error
   */
  int (*zone_to_name) (void *cls, 
		       const struct GNUNET_CRYPTO_EccPrivateKey *zone,
		       const struct GNUNET_CRYPTO_EccPublicSignKey *value_zone,
		       GNUNET_NAMESTORE_RecordIterator iter, void *iter_cls);


};


#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

/* end of gnunet_namestore_plugin.h */
#endif
