/*
     This file is part of GNUnet.
     (C) 2011-2013 Christian Grothoff (and other contributing authors)

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
 * @file namestore/namestore.h
 * @brief common internal definitions for namestore service
 * @author Matthias Wachs
 * @author Christian Grothoff
 */
#ifndef NAMESTORE_H
#define NAMESTORE_H

/**
 * Maximum length of any name, including 0-termination.
 */
#define MAX_NAME_LEN 256

GNUNET_NETWORK_STRUCT_BEGIN

/**
 * Generic namestore message with op id
 */
struct GNUNET_NAMESTORE_Header
{
  /**
   * header.type will be GNUNET_MESSAGE_TYPE_NAMESTORE_*
   * header.size will be message size
   */
  struct GNUNET_MessageHeader header;

  /**
   * Request ID in NBO
   */
  uint32_t r_id GNUNET_PACKED;
};


/**
 * Lookup a block in the namestore
 */
struct LookupBlockMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_LOOKUP_BLOCK
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * The query.
   */
  struct GNUNET_HashCode query GNUNET_PACKED;

};


/**
 * Lookup response
 */
struct LookupBlockResponseMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_LOOKUP_BLOCK_RESPONSE
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * Expiration time
   */
  struct GNUNET_TIME_AbsoluteNBO expire;

  /**
   * Signature.
   */
  struct GNUNET_CRYPTO_EccSignature signature;

  /**
   * Derived public key.
   */
  struct GNUNET_CRYPTO_EccPublicSignKey derived_key;

  /* follwed by encrypted block data */
};


/**
 * Cache a record in the namestore.
 */
struct BlockCacheMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_BLOCK_CACHE
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * Expiration time
   */
  struct GNUNET_TIME_AbsoluteNBO expire;

  /**
   * Signature.
   */
  struct GNUNET_CRYPTO_EccSignature signature;

  /**
   * Derived public key.
   */
  struct GNUNET_CRYPTO_EccPublicSignKey derived_key;

  /* follwed by encrypted block data */
};


/**
 * Response to a request to cache a block.
 */
struct BlockCacheResponseMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_BLOCK_CACHE_RESPONSE
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * #GNUNET_OK on success, #GNUNET_SYSERR error
   */
  int32_t op_result GNUNET_PACKED;
};


/**
 * Store a record to the namestore (as authority).
 */
struct RecordStoreMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_STORE
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * Expiration time
   */
  struct GNUNET_TIME_AbsoluteNBO expire;

  /**
   * Name length
   */
  uint16_t name_len GNUNET_PACKED;

  /**
   * Length of serialized record data
   */
  uint16_t rd_len GNUNET_PACKED;

  /**
   * Number of records contained
   */
  uint16_t rd_count GNUNET_PACKED;

  /**
   * always zero (for alignment)
   */
  uint16_t reserved GNUNET_PACKED;

  /**
   * The private key of the authority.
   */
  struct GNUNET_CRYPTO_EccPrivateKey private_key;

  /* followed by:
   * name with length name_len
   * serialized record data with rd_count records
   */
};


/**
 * Response to a record storage request.
 */
struct RecordStoreResponseMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_STORE_RESPONSE
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * #GNUNET_SYSERR on failure, #GNUNET_OK on success
   */
  int32_t op_result GNUNET_PACKED;
};



/**
 * Lookup a name for a zone hash
 */
struct ZoneToNameMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_TO_NAME
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * The private key of the zone to look up in
   */
  struct GNUNET_CRYPTO_EccPrivateKey zone;

  /**
   * The public key of the target zone
   */
  struct GNUNET_CRYPTO_EccPublicSignKey value_zone;
};


/**
 * Respone for zone to name lookup
 */
struct ZoneToNameResponseMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_TO_NAME_RESPONSE
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * Length of the name
   */
  uint16_t name_len GNUNET_PACKED;

  /**
   * Length of serialized record data
   */
  uint16_t rd_len GNUNET_PACKED;

  /**
   * Number of records contained
   */
  uint16_t rd_count GNUNET_PACKED;

  /**
   * result in NBO: #GNUNET_OK on success, #GNUNET_NO if there were no
   * results, #GNUNET_SYSERR on error
   */
  int16_t res GNUNET_PACKED;

  /**
   * The private key of the zone that contained the name.
   */
  struct GNUNET_CRYPTO_EccPrivateKey zone;

  /* followed by:
   * name with length name_len
   * serialized record data with rd_count records
   */

};


/**
 * Record is returned from the namestore (as authority).
 */
struct RecordResultMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_RESULT
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * Name length
   */
  uint16_t name_len GNUNET_PACKED;

  /**
   * Length of serialized record data
   */
  uint16_t rd_len GNUNET_PACKED;

  /**
   * Number of records contained
   */
  uint16_t rd_count GNUNET_PACKED;

  /**
   * always zero (for alignment)
   */
  uint16_t reserved GNUNET_PACKED;

  /**
   * The private key of the authority.
   */
  struct GNUNET_CRYPTO_EccPrivateKey private_key;

  /* followed by:
   * name with length name_len
   * serialized record data with rd_count records
   */
};


/**
 * Start monitoring a zone.
 */
struct ZoneMonitorStartMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_MONITOR_START
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * Zone key.
   */
  struct GNUNET_CRYPTO_EccPrivateKey zone;

};


/**
 * Start a zone iteration for the given zone
 */
struct ZoneIterationStartMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_START
   */
  struct GNUNET_NAMESTORE_Header gns_header;

  /**
   * Zone key.  All zeros for "all zones".
   */
  struct GNUNET_CRYPTO_EccPrivateKey zone;

};


/**
 * Ask for next result of zone iteration for the given operation
 */
struct ZoneIterationNextMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_NEXT
   */
  struct GNUNET_NAMESTORE_Header gns_header;
};


/**
 * Stop zone iteration for the given operation
 */
struct ZoneIterationStopMessage
{
  /**
   * Type will be #GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_STOP
   */
  struct GNUNET_NAMESTORE_Header gns_header;
};


GNUNET_NETWORK_STRUCT_END


/* end of namestore.h */
#endif
