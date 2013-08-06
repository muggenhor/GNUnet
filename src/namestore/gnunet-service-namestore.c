/*
     This file is part of GNUnet.
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
 * @file namestore/gnunet-service-namestore.c
 * @brief namestore for the GNUnet naming system
 * @author Matthias Wachs
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_dnsparser_lib.h"
#include "gnunet_namestore_service.h"
#include "gnunet_namestore_plugin.h"
#include "gnunet_signatures.h"
#include "namestore.h"

#define LOG_STRERROR_FILE(kind,syscall,filename) GNUNET_log_from_strerror_file (kind, "util", syscall, filename)


/**
 * A namestore client
 */
struct NamestoreClient;


/**
 * A namestore iteration operation.
 */
struct ZoneIteration
{
  /**
   * Next element in the DLL
   */
  struct ZoneIteration *next;

  /**
   * Previous element in the DLL
   */
  struct ZoneIteration *prev;

  /**
   * Namestore client which intiated this zone iteration
   */
  struct NamestoreClient *client;

  /**
   * GNUNET_YES if we iterate over a specific zone
   * GNUNET_NO if we iterate over all zones
   */
  int has_zone;

  /**
   * Hash of the specific zone if 'has_zone' is GNUNET_YES,
   * othwerwise set to '\0'
   */
  struct GNUNET_CRYPTO_ShortHashCode zone;

  /**
   * The operation id fot the zone iteration in the response for the client
   */
  uint64_t request_id;

  /**
   * Offset of the zone iteration used to address next result of the zone
   * iteration in the store
   *
   * Initialy set to 0 in handle_iteration_start
   * Incremented with by every call to handle_iteration_next
   */
  uint32_t offset;

  /**
   * Which flags must be included
   */
  uint16_t must_have_flags;

  /**
   * Which flags must not be included
   */
  uint16_t must_not_have_flags;
};


/**
 * A namestore client
 */
struct NamestoreClient
{
  /**
   * Next element in the DLL
   */
  struct NamestoreClient *next;

  /**
   * Previous element in the DLL
   */
  struct NamestoreClient *prev;

  /**
   * The client
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * Head of the DLL of
   * Zone iteration operations in progress initiated by this client
   */
  struct ZoneIteration *op_head;

  /**
   * Tail of the DLL of
   * Zone iteration operations in progress initiated by this client
   */
  struct ZoneIteration *op_tail;
};


/**
 * A container struct to store information belonging to a zone crypto key pair
 */
struct GNUNET_NAMESTORE_CryptoContainer
{
  /**
   * Filename where to store the container
   */
  char *filename;

  /**
   * Short hash of the zone's public key
   */
  struct GNUNET_CRYPTO_ShortHashCode zone;

  /**
   * Zone's private key
   */
  struct GNUNET_CRYPTO_EccPrivateKey *privkey;

};


/**
 * A namestore monitor.
 */
struct ZoneMonitor
{
  /**
   * Next element in the DLL
   */
  struct ZoneMonitor *next;

  /**
   * Previous element in the DLL
   */
  struct ZoneMonitor *prev;

  /**
   * Namestore client which intiated this zone monitor
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * GNUNET_YES if we monitor over a specific zone
   * GNUNET_NO if we monitor all zones
   */
  int has_zone;

  /**
   * Hash of the specific zone if 'has_zone' is GNUNET_YES,
   * othwerwise set to '\0'
   */
  struct GNUNET_CRYPTO_ShortHashCode zone;

  /**
   * The operation id fot the zone iteration in the response for the client
   */
  uint64_t request_id;

  /**
   * Task active during initial iteration.
   */
  GNUNET_SCHEDULER_TaskIdentifier task;

  /**
   * Offset of the zone iteration used to address next result of the zone
   * iteration in the store
   *
   * Initialy set to 0 in handle_iteration_start
   * Incremented with by every call to handle_iteration_next
   */
  uint32_t offset;

};




/**
 * Configuration handle.
 */
static const struct GNUNET_CONFIGURATION_Handle *GSN_cfg;

/**
 * Database handle
 */
static struct GNUNET_NAMESTORE_PluginFunctions *GSN_database;

/**
 * Zonefile directory
 */
static char *zonefile_directory;

/**
 * Name of the database plugin
 */
static char *db_lib_name;

/**
 * Our notification context.
 */
static struct GNUNET_SERVER_NotificationContext *snc;

/**
 * Head of the Client DLL
 */
static struct NamestoreClient *client_head;

/**
 * Tail of the Client DLL
 */
static struct NamestoreClient *client_tail;

/**
 * Hashmap containing the zone keys this namestore has is authoritative for
 *
 * Keys are the GNUNET_CRYPTO_HashCode of the GNUNET_CRYPTO_ShortHashCode
 * The values are 'struct GNUNET_NAMESTORE_CryptoContainer *'
 */
static struct GNUNET_CONTAINER_MultiHashMap *zonekeys;

/**
 * First active zone monitor.
 */
static struct ZoneMonitor *monitor_head;

/**
 * Last active zone monitor.
 */
static struct ZoneMonitor *monitor_tail;

/**
 * Notification context shared by all monitors.
 */
static struct GNUNET_SERVER_NotificationContext *monitor_nc;


/**
 * Writes the encrypted private key of a zone in a file
 *
 * @param filename where to store the zone
 * @param c the crypto container containing private key of the zone
 * @return GNUNET_OK on success, GNUNET_SYSERR on failure
 */
static int
write_key_to_file (const char *filename, 
		   struct GNUNET_NAMESTORE_CryptoContainer *c)
{
  struct GNUNET_CRYPTO_EccPrivateKey *ret = c->privkey;
  struct GNUNET_DISK_FileHandle *fd;
  struct GNUNET_CRYPTO_ShortHashCode zone;
  struct GNUNET_CRYPTO_EccPublicKey pubkey;
  struct GNUNET_CRYPTO_EccPrivateKey *privkey;

  fd = GNUNET_DISK_file_open (filename, 
			      GNUNET_DISK_OPEN_WRITE | GNUNET_DISK_OPEN_CREATE | GNUNET_DISK_OPEN_FAILIFEXISTS, 
			      GNUNET_DISK_PERM_USER_READ | GNUNET_DISK_PERM_USER_WRITE);
  if ( (NULL == fd) && (EEXIST == errno) )
  {
    privkey = GNUNET_CRYPTO_ecc_key_create_from_file (filename);
    if (NULL == privkey)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Failed to write zone key to file `%s': %s\n"),
		  filename,
		  _("file exists but reading key failed"));
      return GNUNET_SYSERR;
    }
    GNUNET_CRYPTO_ecc_key_get_public (privkey, &pubkey);
    GNUNET_CRYPTO_short_hash (&pubkey, 
			      sizeof (struct GNUNET_CRYPTO_EccPublicKey), 
			      &zone);
    GNUNET_CRYPTO_ecc_key_free (privkey);
    if (0 == memcmp (&zone, &c->zone, sizeof(zone)))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "File zone `%s' containing this key already exists\n", 
		  GNUNET_NAMESTORE_short_h2s (&zone));
      return GNUNET_OK;
    }
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		_("Failed to write zone key to file `%s': %s\n"),
		filename,
		_("file exists with different key"));
    return GNUNET_OK;    
  }
  if (NULL == fd)
  {
    LOG_STRERROR_FILE (GNUNET_ERROR_TYPE_ERROR, "open", filename);
    return GNUNET_SYSERR;
  }
  if (GNUNET_YES != 
      GNUNET_DISK_file_lock (fd, 0, 
			     sizeof (struct GNUNET_CRYPTO_EccPrivateKey),
			     GNUNET_YES))
  {
    GNUNET_break (GNUNET_YES == GNUNET_DISK_file_close (fd));
    return GNUNET_SYSERR;
  }
  GNUNET_assert (sizeof (struct GNUNET_CRYPTO_EccPrivateKey) ==
		 GNUNET_DISK_file_write (fd, ret,
					 sizeof (struct GNUNET_CRYPTO_EccPrivateKey)));
  GNUNET_DISK_file_sync (fd);
  if (GNUNET_YES != 
      GNUNET_DISK_file_unlock (fd, 0, 
			       sizeof (struct GNUNET_CRYPTO_EccPrivateKey)))
    LOG_STRERROR_FILE (GNUNET_ERROR_TYPE_WARNING, "fcntl", filename);
  GNUNET_assert (GNUNET_YES == GNUNET_DISK_file_close (fd));
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Stored zonekey for zone `%s' in file `%s'\n",
	      GNUNET_NAMESTORE_short_h2s(&c->zone), c->filename);
  return GNUNET_OK;
}


/**
 * Write allthe given zone key to disk and then removes the entry from the
 * 'zonekeys' hash map.
 *
 * @param cls unused
 * @param key zone key
 * @param value 'struct GNUNET_NAMESTORE_CryptoContainer' containing the private
 *        key
 * @return GNUNET_OK to continue iteration
 */
static int
zone_to_disk_it (void *cls,
                 const struct GNUNET_HashCode *key,
                 void *value)
{
  struct GNUNET_NAMESTORE_CryptoContainer *c = value;

  if (NULL == c->filename)
    GNUNET_asprintf(&c->filename, 
		    "%s/%s.zkey", 
		    zonefile_directory, 
		    GNUNET_NAMESTORE_short_h2s (&c->zone));
  (void) write_key_to_file(c->filename, c);
  GNUNET_assert (GNUNET_OK == GNUNET_CONTAINER_multihashmap_remove (zonekeys, key, value));
  GNUNET_CRYPTO_ecc_key_free (c->privkey);
  GNUNET_free (c->filename);
  GNUNET_free (c);
  return GNUNET_OK;
}


/**
 * Add the given private key to the set of private keys
 * this namestore can use to sign records when needed.
 *
 * @param pkey private key to add to our list (reference will
 *        be taken over or freed and should not be used afterwards)
 */
static void
learn_private_key (struct GNUNET_CRYPTO_EccPrivateKey *pkey)
{
  struct GNUNET_CRYPTO_EccPublicKey pub;
  struct GNUNET_HashCode long_hash;
  struct GNUNET_CRYPTO_ShortHashCode pubkey_hash;
  struct GNUNET_NAMESTORE_CryptoContainer *cc;

  GNUNET_CRYPTO_ecc_key_get_public (pkey, &pub);
  GNUNET_CRYPTO_short_hash (&pub,
			    sizeof (struct GNUNET_CRYPTO_EccPublicKey),
			    &pubkey_hash);
  GNUNET_CRYPTO_short_hash_double (&pubkey_hash, &long_hash);

  if (GNUNET_NO != GNUNET_CONTAINER_multihashmap_contains(zonekeys, &long_hash))
  {
    GNUNET_CRYPTO_ecc_key_free (pkey);
    return;
  }  
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Received new private key for zone `%s'\n",
	      GNUNET_NAMESTORE_short_h2s(&pubkey_hash));
  cc = GNUNET_malloc (sizeof (struct GNUNET_NAMESTORE_CryptoContainer));
  cc->privkey = pkey;
  cc->zone = pubkey_hash;
  GNUNET_assert (GNUNET_YES ==
		 GNUNET_CONTAINER_multihashmap_put(zonekeys, &long_hash, cc, 
						   GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_ONLY));  
}


/**
 * Returns the expiration time of the given block of records. The block
 * expiration time is the expiration time of the block with smallest
 * expiration time.
 *
 * @param rd_count number of records given in 'rd'
 * @param rd array of records
 * @return absolute expiration time
 */
static struct GNUNET_TIME_Absolute
get_block_expiration_time (unsigned int rd_count, const struct GNUNET_NAMESTORE_RecordData *rd)
{
  unsigned int c;
  struct GNUNET_TIME_Absolute expire;
  struct GNUNET_TIME_Absolute at;
  struct GNUNET_TIME_Relative rt;

  if (NULL == rd)
    return GNUNET_TIME_UNIT_ZERO_ABS;
  expire = GNUNET_TIME_UNIT_FOREVER_ABS;
  for (c = 0; c < rd_count; c++)  
  {
    if (0 != (rd[c].flags & GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION))
    {
      rt.rel_value = rd[c].expiration_time;
      at = GNUNET_TIME_relative_to_absolute (rt);
    }
    else
    {
      at.abs_value = rd[c].expiration_time;
    }
    expire = GNUNET_TIME_absolute_min (at, expire);  
  }
  return expire;
}


/**
 * Task run during shutdown.
 *
 * @param cls unused
 * @param tc unused
 */
static void
cleanup_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct ZoneIteration *no;
  struct NamestoreClient *nc;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Stopping namestore service\n");
  if (NULL != snc)
  {
    GNUNET_SERVER_notification_context_destroy (snc);
    snc = NULL;
  }
  if (NULL != zonekeys)
  {
    GNUNET_CONTAINER_multihashmap_iterate (zonekeys, &zone_to_disk_it, NULL);
    GNUNET_CONTAINER_multihashmap_destroy (zonekeys);
    zonekeys = NULL;
  }
  while (NULL != (nc = client_head))
  {
    while (NULL != (no = nc->op_head))
    {
      GNUNET_CONTAINER_DLL_remove (nc->op_head, nc->op_tail, no);
      GNUNET_free (no);
    }
    GNUNET_CONTAINER_DLL_remove (client_head, client_tail, nc);
    GNUNET_free (nc);
  }
  GNUNET_break (NULL == GNUNET_PLUGIN_unload (db_lib_name, GSN_database));
  GNUNET_free (db_lib_name);
  db_lib_name = NULL;
  GNUNET_free_non_null (zonefile_directory);
  zonefile_directory = NULL;
  if (NULL != monitor_nc)
  {
    GNUNET_SERVER_notification_context_destroy (monitor_nc);
    monitor_nc = NULL;
  }
}


/**
 * Lookup our internal data structure for a given client.
 *
 * @param client server client handle to use for the lookup
 * @return our internal structure for the client, NULL if
 *         we do not have any yet
 */
static struct NamestoreClient *
client_lookup (struct GNUNET_SERVER_Client *client)
{
  struct NamestoreClient *nc;

  GNUNET_assert (NULL != client);
  for (nc = client_head; NULL != nc; nc = nc->next)  
    if (client == nc->client)
      return nc;  
  return NULL;
}


/**
 * Called whenever a client is disconnected.
 * Frees our resources associated with that client.
 *
 * @param cls closure
 * @param client identification of the client
 */
static void
client_disconnect_notification (void *cls, 
				struct GNUNET_SERVER_Client *client)
{
  struct ZoneIteration *no;
  struct NamestoreClient *nc;
  struct ZoneMonitor *zm;

  if (NULL == client)
    return;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Client %p disconnected\n", 
	      client);
  if (NULL == (nc = client_lookup (client)))
    return;
  while (NULL != (no = nc->op_head))
  {
    GNUNET_CONTAINER_DLL_remove (nc->op_head, nc->op_tail, no);
    GNUNET_free (no);
  }
  GNUNET_CONTAINER_DLL_remove (client_head, client_tail, nc);
  GNUNET_free (nc);
  for (zm = monitor_head; NULL != zm; zm = zm->next)
  {
    if (client == zm->client)
    {
      GNUNET_CONTAINER_DLL_remove (monitor_head,
				   monitor_tail,
				   zm);
      if (GNUNET_SCHEDULER_NO_TASK != zm->task)
      {
	GNUNET_SCHEDULER_cancel (zm->task);
	zm->task = GNUNET_SCHEDULER_NO_TASK;
      }
      GNUNET_free (zm);
      break;
    }
  }
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_START' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message unused
 */
static void
handle_start (void *cls,
              struct GNUNET_SERVER_Client *client,
              const struct GNUNET_MessageHeader *message)
{
  struct NamestoreClient *nc;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Client %p connected\n", client);
  if (NULL != client_lookup (client))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  nc = GNUNET_malloc (sizeof (struct NamestoreClient));
  nc->client = client;
  GNUNET_SERVER_notification_context_add (snc, client);
  GNUNET_CONTAINER_DLL_insert (client_head, client_tail, nc);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Context for name lookups passed from 'handle_lookup_name' to
 * 'handle_lookup_name_it' as closure
 */
struct LookupNameContext
{
  /**
   * The client to send the response to
   */
  struct NamestoreClient *nc;

  /**
   * Requested zone
   */
  const struct GNUNET_CRYPTO_ShortHashCode *zone;

  /**
   * Requested name
   */
  const char *name;

  /**
   * Operation id for the name lookup
   */
  uint32_t request_id;

  /**
   * Requested specific record type
   */
  uint32_t record_type;
};


/**
 * A 'GNUNET_NAMESTORE_RecordIterator' for name lookups in handle_lookup_name
 *
 * @param cls a 'struct LookupNameContext *' with information about the request
 * @param zone_key zone key of the zone
 * @param expire expiration time
 * @param name name
 * @param rd_count number of records
 * @param rd array of records
 * @param signature signature
 */
static void
handle_lookup_name_it (void *cls,
		       const struct GNUNET_CRYPTO_EccPublicKey *zone_key,
		       struct GNUNET_TIME_Absolute expire,
		       const char *name,
		       unsigned int rd_count,
		       const struct GNUNET_NAMESTORE_RecordData *rd,
		       const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct LookupNameContext *lnc = cls;
  struct LookupNameResponseMessage *lnr_msg;
  struct GNUNET_NAMESTORE_RecordData *rd_selected;
  struct GNUNET_NAMESTORE_CryptoContainer *cc;
  struct GNUNET_CRYPTO_EccSignature *signature_new;
  struct GNUNET_TIME_Absolute e;
  struct GNUNET_TIME_Relative re;
  struct GNUNET_CRYPTO_ShortHashCode zone_key_hash;
  struct GNUNET_HashCode long_hash;
  char *rd_tmp;
  char *name_tmp;
  size_t rd_ser_len;
  size_t r_size;
  size_t name_len;
  int copied_elements;
  int contains_signature;
  int authoritative;
  int rd_modified;
  unsigned int c;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Found %u records under name `%s'\n",
	      rd_count,
	      name);
  authoritative = GNUNET_NO;
  signature_new = NULL;
  cc = NULL;
  if (NULL != zone_key) 
  {
    GNUNET_CRYPTO_short_hash (zone_key, 
			      sizeof (struct GNUNET_CRYPTO_EccPublicKey), 
			      &zone_key_hash);
    GNUNET_CRYPTO_short_hash_double (&zone_key_hash, &long_hash);
    if (NULL != (cc = GNUNET_CONTAINER_multihashmap_get (zonekeys, &long_hash)))   
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Am authoritative for zone `%s'\n",
		  GNUNET_NAMESTORE_short_h2s (&zone_key_hash));
      authoritative = GNUNET_YES;    
    }
  }

  copied_elements = 0;
  rd_modified = GNUNET_NO;
  rd_selected = NULL;
  /* count records to copy */
  for (c = 0; c < rd_count; c++)
  {
    if ( (GNUNET_YES == authoritative) &&
	 (GNUNET_YES ==
	  GNUNET_NAMESTORE_is_expired (&rd[c]) ) )
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Skipping expired record\n");
      continue; 
    }
    if ( (GNUNET_NAMESTORE_TYPE_ANY == lnc->record_type) || 
	 (rd[c].record_type == lnc->record_type) )
      copied_elements++; /* found matching record */
    else
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		  "Skipping non-mtaching record\n");
      rd_modified = GNUNET_YES;
    }
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Found %u records with type %u for name `%s' in zone `%s'\n",
	      copied_elements, 
	      lnc->record_type, 
	      lnc->name, 
	      GNUNET_NAMESTORE_short_h2s(lnc->zone));
  if (copied_elements > 0)
  {
    rd_selected = GNUNET_malloc (copied_elements * sizeof (struct GNUNET_NAMESTORE_RecordData));
    copied_elements = 0;
    for (c = 0; c < rd_count; c++)
    {
      if ( (GNUNET_YES == authoritative) &&
	   (GNUNET_YES ==
	    GNUNET_NAMESTORE_is_expired (&rd[c])) )
	continue;
      if ( (GNUNET_NAMESTORE_TYPE_ANY == lnc->record_type) || 
	   (rd[c].record_type == lnc->record_type) )
      {
	if (0 != (rd[c].flags & GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION))
	{
	  GNUNET_break (GNUNET_YES == authoritative);
	  rd_modified = GNUNET_YES;
	  re.rel_value = rd[c].expiration_time;
	  e = GNUNET_TIME_relative_to_absolute (re);
	}
	else
	{
	  e.abs_value = rd[c].expiration_time;
	}
	/* found matching record, copy and convert flags to public format */
	rd_selected[copied_elements] = rd[c]; /* shallow copy! */
	rd_selected[copied_elements].expiration_time = e.abs_value;
	if (0 != (rd_selected[copied_elements].flags &
		  (GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION | GNUNET_NAMESTORE_RF_AUTHORITY)))
	{
	  rd_selected[copied_elements].flags &= ~ (GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION | 
				  GNUNET_NAMESTORE_RF_AUTHORITY);
	  rd_modified = GNUNET_YES;
	}
	copied_elements++;
      }
      else
      {
	rd_modified = GNUNET_YES;
      }
    }
  }
  else
    rd_selected = NULL;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Found %u matching records for name `%s' in zone `%s'\n",
	      copied_elements,
	      lnc->name, 
	      GNUNET_NAMESTORE_short_h2s (lnc->zone));
  contains_signature = GNUNET_NO;
  if (copied_elements > 0)
  {
    if (GNUNET_YES == authoritative)
    {
      GNUNET_assert (NULL != cc);
      e = get_block_expiration_time (rd_count, rd);
      signature_new = GNUNET_NAMESTORE_create_signature (cc->privkey, e, name, rd_selected, copied_elements);
      GNUNET_assert (NULL != signature_new);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		  "Creating signature for name `%s' with %u records in zone `%s'\n",
		  name, 
		  copied_elements,
		  GNUNET_NAMESTORE_short_h2s(&zone_key_hash));
    }
    else
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Not authoritative, records modified is %d, have sig is %d\n",
		  rd_modified,
		  NULL != signature);
      if ((GNUNET_NO == rd_modified) && (NULL != signature))
	contains_signature = GNUNET_YES; /* returning all records, so include signature */
    }
  }

  rd_ser_len = GNUNET_NAMESTORE_records_get_size (copied_elements, rd_selected);
  name_len = (NULL == name) ? 0 : strlen(name) + 1;
  r_size = sizeof (struct LookupNameResponseMessage) +
           name_len +
           rd_ser_len;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Sending `%s' message\n", 
	      "NAMESTORE_LOOKUP_NAME_RESPONSE");
  lnr_msg = GNUNET_malloc (r_size);
  lnr_msg->gns_header.header.type = ntohs (GNUNET_MESSAGE_TYPE_NAMESTORE_LOOKUP_NAME_RESPONSE);
  lnr_msg->gns_header.header.size = ntohs (r_size);
  lnr_msg->gns_header.r_id = htonl (lnc->request_id);
  lnr_msg->rd_count = htons (copied_elements);
  lnr_msg->rd_len = htons (rd_ser_len);
  lnr_msg->name_len = htons (name_len);
  lnr_msg->expire = GNUNET_TIME_absolute_hton (get_block_expiration_time (copied_elements, 
									  rd_selected));
  name_tmp = (char *) &lnr_msg[1];
  memcpy (name_tmp, name, name_len);
  rd_tmp = &name_tmp[name_len];
  GNUNET_NAMESTORE_records_serialize (copied_elements, rd_selected, rd_ser_len, rd_tmp);
  if (rd_selected != rd)
    GNUNET_free_non_null (rd_selected);
  if (NULL != zone_key)
    lnr_msg->public_key = *zone_key;
  if ( (GNUNET_YES == authoritative) &&
       (copied_elements > 0) )
  {
    /* use new created signature */
    lnr_msg->contains_sig = htons (GNUNET_YES);
    GNUNET_assert (NULL != signature_new);
    lnr_msg->signature = *signature_new;
    GNUNET_free (signature_new);
  }
  else if (GNUNET_YES == contains_signature)
  {
    /* use existing signature */
    lnr_msg->contains_sig = htons (GNUNET_YES);
    GNUNET_assert (NULL != signature);
    lnr_msg->signature = *signature;
  }
  GNUNET_SERVER_notification_context_unicast (snc, lnc->nc->client, 
					      &lnr_msg->gns_header.header, 
					      GNUNET_NO);
  GNUNET_free (lnr_msg);
}


/**
 * Send an empty name response to indicate the end of the 
 * set of results to the client.
 *
 * @param nc notification context to use for sending
 * @param client destination of the empty response
 * @param request_id identification for the request
 */
static void
send_empty_response (struct GNUNET_SERVER_NotificationContext *nc,
		     struct GNUNET_SERVER_Client *client,
		     uint32_t request_id)
{
  struct LookupNameResponseMessage zir_end;

  memset (&zir_end, 0, sizeof (zir_end));
  zir_end.gns_header.header.type = htons (GNUNET_MESSAGE_TYPE_NAMESTORE_LOOKUP_NAME_RESPONSE);
  zir_end.gns_header.header.size = htons (sizeof (struct LookupNameResponseMessage));
  zir_end.gns_header.r_id = htonl(request_id);
  GNUNET_SERVER_notification_context_unicast (nc, 
					      client, 
					      &zir_end.gns_header.header, GNUNET_NO);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_LOOKUP_NAME' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct LookupNameMessage'
 */
static void
handle_lookup_name (void *cls,
                    struct GNUNET_SERVER_Client *client,
                    const struct GNUNET_MessageHeader *message)
{
  const struct LookupNameMessage *ln_msg;
  struct LookupNameContext lnc;
  struct NamestoreClient *nc;
  size_t name_len;
  const char *name;
  uint32_t rid;
  uint32_t type;
  char *conv_name;
  int ret;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Received `%s' message\n", 
	      "NAMESTORE_LOOKUP_NAME");
  if (ntohs (message->size) < sizeof (struct LookupNameMessage))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  if (NULL == (nc = client_lookup(client)))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  ln_msg = (const struct LookupNameMessage *) message;
  rid = ntohl (ln_msg->gns_header.r_id);
  name_len = ntohl (ln_msg->name_len);
  type = ntohl (ln_msg->record_type);
  if ((0 == name_len) || (name_len > MAX_NAME_LEN))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  name = (const char *) &ln_msg[1];
  if ('\0' != name[name_len - 1])
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  if (GNUNET_NAMESTORE_TYPE_ANY == type)
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Looking up all records for name `%s' in zone `%s'\n", 
		name, 
		GNUNET_NAMESTORE_short_h2s(&ln_msg->zone));
  else
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Looking up records with type %u for name `%s' in zone `%s'\n", 
		type, name, 
		GNUNET_NAMESTORE_short_h2s(&ln_msg->zone));

  conv_name = GNUNET_NAMESTORE_normalize_string (name);
  if (NULL == conv_name)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		"Error converting name `%s'\n", name);
    return;
  }

  /* do the actual lookup */
  lnc.request_id = rid;
  lnc.nc = nc;
  lnc.record_type = type;
  lnc.name = conv_name;
  lnc.zone = &ln_msg->zone;
  if (GNUNET_SYSERR ==
      (ret = GSN_database->iterate_records (GSN_database->cls, 
					    &ln_msg->zone, conv_name, 0 /* offset */,
					    &handle_lookup_name_it, &lnc)))
  {
    /* internal error (in database plugin); might be best to just hang up on
       plugin rather than to signal that there are 'no' results, which 
       might also be false... */
    GNUNET_break (0); 
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    GNUNET_free (conv_name);
    return;
  }  
  GNUNET_free (conv_name);
  if (0 == ret)
  {
    /* no records match at all, generate empty response */
    send_empty_response (snc, nc->client, rid);
  }
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Generate a 'struct LookupNameResponseMessage' and send it to the
 * given client using the given notification context.
 *
 * @param nc notification context to use
 * @param client client to unicast to
 * @param request_id request ID to use
 * @param zone_key zone key of the zone
 * @param expire expiration time
 * @param name name
 * @param rd_count number of records
 * @param rd array of records
 * @param signature signature
 */
static void
send_lookup_response (struct GNUNET_SERVER_NotificationContext *nc,			
		      struct GNUNET_SERVER_Client *client,
		      uint32_t request_id,
		      const struct GNUNET_CRYPTO_EccPublicKey *zone_key,
		      struct GNUNET_TIME_Absolute expire,
		      const char *name,
		      unsigned int rd_count,
		      const struct GNUNET_NAMESTORE_RecordData *rd,
		      const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct LookupNameResponseMessage *zir_msg;
  size_t name_len;
  size_t rd_ser_len;
  size_t msg_size;
  char *name_tmp;
  char *rd_ser;

  name_len = strlen (name) + 1;
  rd_ser_len = GNUNET_NAMESTORE_records_get_size (rd_count, rd);  
  msg_size = sizeof (struct LookupNameResponseMessage) + name_len + rd_ser_len;

  zir_msg = GNUNET_malloc (msg_size);
  zir_msg->gns_header.header.type = htons (GNUNET_MESSAGE_TYPE_NAMESTORE_LOOKUP_NAME_RESPONSE);
  zir_msg->gns_header.header.size = htons (msg_size);
  zir_msg->gns_header.r_id = htonl (request_id);
  zir_msg->expire = GNUNET_TIME_absolute_hton (expire);
  zir_msg->contains_sig = htons ((NULL == signature) ? GNUNET_NO : GNUNET_YES);
  zir_msg->name_len = htons (name_len);
  zir_msg->rd_count = htons (rd_count);
  zir_msg->rd_len = htons (rd_ser_len);
  if (NULL != signature)
    zir_msg->signature = *signature;
  zir_msg->public_key = *zone_key;
  name_tmp = (char *) &zir_msg[1];
  memcpy (name_tmp, name, name_len);
  rd_ser = &name_tmp[name_len];
  GNUNET_NAMESTORE_records_serialize (rd_count, rd, rd_ser_len, rd_ser);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Sending `%s' message with size %u\n", 
	      "ZONE_ITERATION_RESPONSE",
	      msg_size);
  GNUNET_SERVER_notification_context_unicast (nc,
					      client, 
					      &zir_msg->gns_header.header,
					      GNUNET_NO);
  GNUNET_free (zir_msg);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_PUT' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct RecordPutMessage'
 */
static void
handle_record_put (void *cls,
                   struct GNUNET_SERVER_Client *client,
                   const struct GNUNET_MessageHeader *message)
{
  struct NamestoreClient *nc;
  const struct RecordPutMessage *rp_msg;
  struct GNUNET_TIME_Absolute expire;
  const struct GNUNET_CRYPTO_EccSignature *signature;
  struct RecordPutResponseMessage rpr_msg;
  struct GNUNET_CRYPTO_ShortHashCode zone_hash;
  size_t name_len;
  size_t msg_size;
  size_t msg_size_exp;
  const char *name;
  const char *rd_ser;
  char * conv_name;
  uint32_t rid;
  uint32_t rd_ser_len;
  uint32_t rd_count;
  int res;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Received `%s' message\n",
	      "NAMESTORE_RECORD_PUT");
  if (ntohs (message->size) < sizeof (struct RecordPutMessage))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  if (NULL == (nc = client_lookup (client)))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  rp_msg = (const struct RecordPutMessage *) message;
  rid = ntohl (rp_msg->gns_header.r_id);
  msg_size = ntohs (rp_msg->gns_header.header.size);
  name_len = ntohs (rp_msg->name_len);
  rd_count = ntohs (rp_msg->rd_count);
  rd_ser_len = ntohs (rp_msg->rd_len);
  if ((rd_count < 1) || (rd_ser_len < 1) || (name_len >= MAX_NAME_LEN) || (0 == name_len))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  msg_size_exp = sizeof (struct RecordPutMessage) + name_len + rd_ser_len;
  if (msg_size != msg_size_exp)
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  name = (const char *) &rp_msg[1];
  if ('\0' != name[name_len -1])
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  expire = GNUNET_TIME_absolute_ntoh (rp_msg->expire);
  signature = &rp_msg->signature;
  rd_ser = &name[name_len];
  struct GNUNET_NAMESTORE_RecordData rd[rd_count];

  if (GNUNET_OK !=
      GNUNET_NAMESTORE_records_deserialize (rd_ser_len, rd_ser, rd_count, rd))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  GNUNET_CRYPTO_short_hash (&rp_msg->public_key,
                            sizeof (rp_msg->public_key),
                            &zone_hash);

  conv_name = GNUNET_NAMESTORE_normalize_string (name);
  if (NULL == conv_name)
  {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Error converting name `%s'\n", name);
      return;
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Putting %u records under name `%s' in zone `%s'\n",
              rd_count, conv_name,
              GNUNET_NAMESTORE_short_h2s (&zone_hash));
  res = GSN_database->put_records (GSN_database->cls,
				   &rp_msg->public_key,
				   expire,
				   conv_name,
				   rd_count, rd,
				   signature);
  if (GNUNET_OK == res)
  {
    struct ZoneMonitor *zm;

    for (zm = monitor_head; NULL != zm; zm = zm->next)    
      if ( (GNUNET_NO == zm->has_zone) ||
	   (0 == memcmp (&zone_hash, &zm->zone, sizeof (struct GNUNET_CRYPTO_ShortHashCode))) )      
	send_lookup_response (monitor_nc,
			      zm->client,
			      zm->request_id,
			      &rp_msg->public_key,
			      expire,
			      conv_name,
			      rd_count, rd,
			      signature);      
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Putting record for name `%s': %s\n",
              conv_name,
              (GNUNET_OK == res) ? "OK" : "FAILED");
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Sending `%s' message\n", 
	      "RECORD_PUT_RESPONSE");
  GNUNET_free (conv_name);
  rpr_msg.gns_header.header.type = htons (GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_PUT_RESPONSE);
  rpr_msg.gns_header.header.size = htons (sizeof (struct RecordPutResponseMessage));
  rpr_msg.gns_header.r_id = htonl (rid);
  rpr_msg.op_result = htonl (res);
  GNUNET_SERVER_notification_context_unicast (snc, 
					      nc->client, 
					      &rpr_msg.gns_header.header, 
					      GNUNET_NO);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_CREATE' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct RecordCreateMessage'
 */
static void
handle_record_create (void *cls,
                      struct GNUNET_SERVER_Client *client,
                      const struct GNUNET_MessageHeader *message)
{
  static struct GNUNET_CRYPTO_EccSignature dummy_signature;
  struct NamestoreClient *nc;
  const struct RecordCreateMessage *rp_msg;
  struct GNUNET_CRYPTO_EccPrivateKey *pkey;
  struct RecordCreateResponseMessage rcr_msg;
  size_t name_len;
  size_t msg_size;
  size_t msg_size_exp;
  size_t rd_ser_len;
  uint32_t rid;
  const char *name_tmp;
  char *conv_name;
  const char *rd_ser;
  unsigned int rd_count;
  int res;
  struct GNUNET_CRYPTO_ShortHashCode pubkey_hash;
  struct GNUNET_CRYPTO_EccPublicKey pubkey;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Received `%s' message\n", "NAMESTORE_RECORD_CREATE");
  if (ntohs (message->size) < sizeof (struct RecordCreateMessage))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  if (NULL == (nc = client_lookup (client)))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  rp_msg = (const struct RecordCreateMessage *) message;
  rid = ntohl (rp_msg->gns_header.r_id);
  name_len = ntohs (rp_msg->name_len);
  msg_size = ntohs (message->size);
  rd_count = ntohs (rp_msg->rd_count);
  rd_ser_len = ntohs (rp_msg->rd_len);
  GNUNET_break (0 == ntohs (rp_msg->reserved));
  msg_size_exp = sizeof (struct RecordCreateMessage) + name_len + rd_ser_len;
  if (msg_size != msg_size_exp)
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  if ((0 == name_len) || (name_len > MAX_NAME_LEN))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  name_tmp = (const char *) &rp_msg[1];
  rd_ser = &name_tmp[name_len];
  if ('\0' != name_tmp[name_len -1])
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  pkey = GNUNET_new (struct GNUNET_CRYPTO_EccPrivateKey);
  memcpy (pkey, &rp_msg->private_key, sizeof (struct GNUNET_CRYPTO_EccPrivateKey));
  {
    struct GNUNET_NAMESTORE_RecordData rd[rd_count];

    if (GNUNET_OK !=
	GNUNET_NAMESTORE_records_deserialize (rd_ser_len, rd_ser, rd_count, rd))
    {
      GNUNET_break (0);
      GNUNET_CRYPTO_ecc_key_free (pkey);
      GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
      return;
    }

    /* Extracting and converting private key */
    GNUNET_CRYPTO_ecc_key_get_public (pkey, &pubkey);
    GNUNET_CRYPTO_short_hash (&pubkey,
			      sizeof (struct GNUNET_CRYPTO_EccPublicKey),
			      &pubkey_hash);
    learn_private_key (pkey);    
    conv_name = GNUNET_NAMESTORE_normalize_string (name_tmp);
    if (NULL == conv_name)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Error converting name `%s'\n", name_tmp);
      GNUNET_CRYPTO_ecc_key_free (pkey);
      GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
      return;
    }
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"Creating %u records for name `%s' in zone `%s'\n",
		(unsigned int) rd_count,
		conv_name,
		GNUNET_NAMESTORE_short_h2s (&pubkey_hash));
    if (0 == rd_count)
      res = GSN_database->remove_records (GSN_database->cls,
					  &pubkey_hash,
					  conv_name);
    else
      res = GSN_database->put_records (GSN_database->cls,
				       &pubkey,
				       GNUNET_TIME_absolute_ntoh (rp_msg->expire),
				       conv_name,
				       rd_count, rd,
				       &dummy_signature);
    if (GNUNET_OK == res)
    {
      struct ZoneMonitor *zm;
      
      for (zm = monitor_head; NULL != zm; zm = zm->next)    
	if ( (GNUNET_NO == zm->has_zone) ||
	     (0 == memcmp (&pubkey_hash, &zm->zone, sizeof (struct GNUNET_CRYPTO_ShortHashCode))) )      
	  send_lookup_response (monitor_nc,
				zm->client,
				zm->request_id,
				&pubkey,
				GNUNET_TIME_absolute_ntoh (rp_msg->expire),
				conv_name,
				rd_count, rd,
				&dummy_signature);      
    }    
    GNUNET_free (conv_name);
  }
  
  /* Send response */
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Sending `%s' message\n", "RECORD_CREATE_RESPONSE");
  rcr_msg.gns_header.header.type = htons (GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_CREATE_RESPONSE);
  rcr_msg.gns_header.header.size = htons (sizeof (struct RecordCreateResponseMessage));
  rcr_msg.gns_header.r_id = htonl (rid);
  rcr_msg.op_result = htonl (res);
  GNUNET_SERVER_notification_context_unicast (snc, nc->client,
					      &rcr_msg.gns_header.header,
					      GNUNET_NO);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Context for record remove operations passed from 'handle_zone_to_name' to
 * 'handle_zone_to_name_it' as closure
 */
struct ZoneToNameCtx
{
  /**
   * Namestore client
   */
  struct NamestoreClient *nc;

  /**
   * Request id (to be used in the response to the client).
   */
  uint32_t rid;

  /**
   * Set to GNUNET_OK on success, GNUNET_SYSERR on error.  Note that
   * not finding a name for the zone still counts as a 'success' here,
   * as this field is about the success of executing the IPC protocol.
   */
  int success;
};


/**
 * Zone to name iterator
 *
 * @param cls struct ZoneToNameCtx *
 * @param zone_key the zone key
 * @param expire expiration date
 * @param name name
 * @param rd_count number of records
 * @param rd record data
 * @param signature signature
 */
static void
handle_zone_to_name_it (void *cls,
			const struct GNUNET_CRYPTO_EccPublicKey *zone_key,
			struct GNUNET_TIME_Absolute expire,
			const char *name,
			unsigned int rd_count,
			const struct GNUNET_NAMESTORE_RecordData *rd,
			const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct ZoneToNameCtx *ztn_ctx = cls;
  struct ZoneToNameResponseMessage *ztnr_msg;
  int16_t res;
  size_t name_len;
  size_t rd_ser_len;
  size_t msg_size;
  char *name_tmp;
  char *rd_tmp;
  char *sig_tmp;

  if ((NULL != zone_key) && (NULL != name))
  {
    /* found result */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Found result: name `%s' has %u records\n", 
		name, rd_count);
    res = GNUNET_YES;
    name_len = strlen (name) + 1;
  }
  else
  {
    /* no result found */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Found no results\n");
    res = GNUNET_NO;
    name_len = 0;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Sending `%s' message\n", 
	      "ZONE_TO_NAME_RESPONSE");
  rd_ser_len = GNUNET_NAMESTORE_records_get_size (rd_count, rd);
  msg_size = sizeof (struct ZoneToNameResponseMessage) + name_len + rd_ser_len;
  if (NULL != signature)
    msg_size += sizeof (struct GNUNET_CRYPTO_EccSignature);
  if (msg_size >= GNUNET_SERVER_MAX_MESSAGE_SIZE)
  {
    GNUNET_break (0);
    ztn_ctx->success = GNUNET_SYSERR;
    return;
  }
  ztnr_msg = GNUNET_malloc (msg_size);
  ztnr_msg->gns_header.header.type = htons (GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_TO_NAME_RESPONSE);
  ztnr_msg->gns_header.header.size = htons (msg_size);
  ztnr_msg->gns_header.r_id = htonl (ztn_ctx->rid);
  ztnr_msg->res = htons (res);
  ztnr_msg->rd_len = htons (rd_ser_len);
  ztnr_msg->rd_count = htons (rd_count);
  ztnr_msg->name_len = htons (name_len);
  ztnr_msg->expire = GNUNET_TIME_absolute_hton (expire);
  if (NULL != zone_key)
    ztnr_msg->zone_key = *zone_key;
  name_tmp = (char *) &ztnr_msg[1];
  if (NULL != name)
    memcpy (name_tmp, name, name_len);
  rd_tmp = &name_tmp[name_len];
  GNUNET_NAMESTORE_records_serialize (rd_count, rd, rd_ser_len, rd_tmp);
  sig_tmp = &rd_tmp[rd_ser_len];
  if (NULL != signature)
    memcpy (sig_tmp, signature, sizeof (struct GNUNET_CRYPTO_EccSignature));
  ztn_ctx->success = GNUNET_OK;
  GNUNET_SERVER_notification_context_unicast (snc, ztn_ctx->nc->client,
					      &ztnr_msg->gns_header.header,
					      GNUNET_NO);
  GNUNET_free (ztnr_msg);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_TO_NAME' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct ZoneToNameMessage'
 */
static void
handle_zone_to_name (void *cls,
                     struct GNUNET_SERVER_Client *client,
                     const struct GNUNET_MessageHeader *message)
{
  struct NamestoreClient *nc;
  const struct ZoneToNameMessage *ztn_msg;
  struct ZoneToNameCtx ztn_ctx;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Received `%s' message\n",
	      "ZONE_TO_NAME");
  ztn_msg = (const struct ZoneToNameMessage *) message;
  if (NULL == (nc = client_lookup(client)))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  ztn_ctx.rid = ntohl (ztn_msg->gns_header.r_id);
  ztn_ctx.nc = nc;
  ztn_ctx.success = GNUNET_SYSERR;
  if (GNUNET_SYSERR ==
      GSN_database->zone_to_name (GSN_database->cls, 
				  &ztn_msg->zone,
				  &ztn_msg->value_zone,
				  &handle_zone_to_name_it, &ztn_ctx))
  {
    /* internal error, hang up instead of signalling something
       that might be wrong */
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;    
  }
  GNUNET_SERVER_receive_done (client, ztn_ctx.success);
}


/**
 * Zone iteration processor result
 */
enum ZoneIterationResult
{
  /**
   * Found records, but all records were filtered
   * Continue to iterate
   */
  IT_ALL_RECORDS_FILTERED = -1,

  /**
   * Found records,
   * Continue to iterate with next iteration_next call
   */
  IT_SUCCESS_MORE_AVAILABLE = 0,

  /**
   * Iteration complete
   */
  IT_SUCCESS_NOT_MORE_RESULTS_AVAILABLE = 1
};


/**
 * Context for record remove operations passed from
 * 'run_zone_iteration_round' to 'zone_iteraterate_proc' as closure
 */
struct ZoneIterationProcResult
{
  /**
   * The zone iteration handle
   */
  struct ZoneIteration *zi;

  /**
   * Iteration result: iteration done?
   * IT_SUCCESS_MORE_AVAILABLE:  if there may be more results overall but
   * we got one for now and have sent it to the client
   * IT_SUCCESS_NOT_MORE_RESULTS_AVAILABLE: if there are no further results,
   * IT_ALL_RECORDS_FILTERED: if all results were filtered so far.
   */
  int res_iteration_finished;

};


/**
 * Process results for zone iteration from database
 *
 * @param cls struct ZoneIterationProcResult *proc
 * @param zone_key the zone key
 * @param expire expiration time
 * @param name name
 * @param rd_count number of records for this name
 * @param rd record data
 * @param signature block signature
 */
static void
zone_iteraterate_proc (void *cls,
                       const struct GNUNET_CRYPTO_EccPublicKey *zone_key,
                       struct GNUNET_TIME_Absolute expire,
                       const char *name,
                       unsigned int rd_count,
                       const struct GNUNET_NAMESTORE_RecordData *rd,
                       const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct ZoneIterationProcResult *proc = cls;
  struct GNUNET_NAMESTORE_RecordData rd_filtered[rd_count];
  struct GNUNET_CRYPTO_EccSignature *new_signature = NULL;
  struct GNUNET_NAMESTORE_CryptoContainer *cc;
  struct GNUNET_HashCode long_hash;
  struct GNUNET_CRYPTO_ShortHashCode zone_hash;
  struct GNUNET_TIME_Relative rt;
  unsigned int rd_count_filtered;
  unsigned int c;

  proc->res_iteration_finished = IT_SUCCESS_MORE_AVAILABLE;
  if ((NULL == zone_key) && (NULL == name))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Iteration done\n");
    proc->res_iteration_finished = IT_SUCCESS_NOT_MORE_RESULTS_AVAILABLE;
    return;
  }
  if ((NULL == zone_key) || (NULL == name)) 
  {
    /* what is this!? should never happen */
    GNUNET_break (0);
    return;    
  }
  rd_count_filtered  = 0;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Received result for zone iteration: `%s'\n", 
	      name);
  for (c = 0; c < rd_count; c++)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"Record %u has flags: %x must have flags are %x, must not have flags are %x\n",
		c, rd[c].flags, 
		proc->zi->must_have_flags,
		proc->zi->must_not_have_flags);
    /* Checking must have flags, except 'relative-expiration' which is a special flag */
    if ((rd[c].flags & proc->zi->must_have_flags & (~GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION))
	!= (proc->zi->must_have_flags & (~ GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION)))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Record %u lacks 'must-have' flags: Not included\n", c);
      continue;
    }
    /* Checking must-not-have flags */
    if (0 != (rd[c].flags & proc->zi->must_not_have_flags))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		  "Record %u has 'must-not-have' flags: Not included\n", c);
      continue;
    }
    rd_filtered[rd_count_filtered] = rd[c];
    /* convert relative to absolute expiration time unless explicitly requested otherwise */
    if ( (0 == (proc->zi->must_have_flags & GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION)) &&
	 (0 != (rd[c].flags & GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION)) )
    {
      /* should convert relative-to-absolute expiration time */
      rt.rel_value = rd[c].expiration_time;
      rd_filtered[c].expiration_time = GNUNET_TIME_relative_to_absolute (rt).abs_value;
      rd_filtered[c].flags &= ~ GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION;
    }
    /* we NEVER keep the 'authority' flag */
    rd_filtered[c].flags &= ~ GNUNET_NAMESTORE_RF_AUTHORITY;
    rd_count_filtered++;    
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Included %u of %u records\n", 
	      rd_count_filtered, rd_count);

  signature = NULL;    
  if ( (rd_count_filtered > 0) &&
       (0 == (proc->zi->must_have_flags & GNUNET_NAMESTORE_RF_RELATIVE_EXPIRATION)) )
  {
    /* compute / obtain signature, but only if we (a) have records and (b) expiration times were 
       converted to absolute expiration times */
    GNUNET_CRYPTO_short_hash (zone_key, 
			      sizeof (struct GNUNET_CRYPTO_EccPublicKey),
			      &zone_hash);
    GNUNET_CRYPTO_short_hash_double (&zone_hash, &long_hash);
    if (NULL != (cc = GNUNET_CONTAINER_multihashmap_get (zonekeys, &long_hash)))
    {
      expire = get_block_expiration_time (rd_count_filtered, rd_filtered);


      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Creating signature for `%s' in zone `%s' with %u records and expiration %llu\n",
		  name, GNUNET_NAMESTORE_short_h2s(&zone_hash), 
		  rd_count_filtered,
		  (unsigned long long) expire.abs_value);
      /* TODO 1) AB: New publishing
       * - Create HDKF(Q,i)
       * - Encrypt record block R with HKDF: HDKF(Q,i) == E(R)
       * - Create block |e,E(R)|
       * - Create d: h * x mod n == hash (name, zone)  * c->privkey mod n
       * - Create ECC signature S_d (e, E_HKDF(Q,i))
       *
       * Return: zone_key , expire, name, rd_count_filtered, new signature S_d
       *
       * Q: zone's public key
       * x: zone's private key
       * i: name
       * d: derived secret
       *
       * - how do I get n:
       * Extract from private key s_expression
       * Question
       * - how do I multiply h * x?
       */

      new_signature = GNUNET_NAMESTORE_create_signature (cc->privkey, expire, name, 
							 rd_filtered, rd_count_filtered);
      GNUNET_assert (NULL != new_signature);
      signature = new_signature;
    }
    else if (rd_count_filtered == rd_count)
    {
      if (NULL != signature)
	{
	  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		      "Using provided signature for `%s' in zone `%s' with %u records and expiration %llu\n",
		      name, GNUNET_NAMESTORE_short_h2s (&zone_hash), rd_count_filtered, 
		      (unsigned long long) expire.abs_value);
	  return;
	}    
    }
  }
  if (0 == rd_count_filtered)
  {
    /* After filtering records there are no records left to return */
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "No records to transmit\n");
    proc->res_iteration_finished = IT_ALL_RECORDS_FILTERED;
    return;
  }

  if (GNUNET_YES == proc->zi->has_zone)
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Sending name `%s' for iteration over zone `%s'\n",
		name, GNUNET_NAMESTORE_short_h2s(&proc->zi->zone));
  else
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Sending name `%s' for iteration over all zones\n",
		name);
  send_lookup_response (snc,
			proc->zi->client->client,
			proc->zi->request_id,
			zone_key,
			expire,
			name,
			rd_count_filtered,
			rd_filtered,
			signature);
  proc->res_iteration_finished = IT_SUCCESS_MORE_AVAILABLE;
  GNUNET_free_non_null (new_signature);
}


/**
 * Perform the next round of the zone iteration.
 *
 * @param zi zone iterator to process
 */
static void
run_zone_iteration_round (struct ZoneIteration *zi)
{
  struct ZoneIterationProcResult proc;
  struct GNUNET_CRYPTO_ShortHashCode *zone;
  int ret;

  memset (&proc, 0, sizeof (proc));
  proc.zi = zi;
  if (GNUNET_YES == zi->has_zone)
    zone = &zi->zone;
  else
    zone = NULL;
  proc.res_iteration_finished = IT_ALL_RECORDS_FILTERED;
  while (IT_ALL_RECORDS_FILTERED == proc.res_iteration_finished)
  {
    if (GNUNET_SYSERR ==
	(ret = GSN_database->iterate_records (GSN_database->cls, zone, NULL, 
					      zi->offset, 
					      &zone_iteraterate_proc, &proc)))
    {
      GNUNET_break (0);
      break;
    }
    if (GNUNET_NO == ret)
      proc.res_iteration_finished = IT_SUCCESS_NOT_MORE_RESULTS_AVAILABLE;
    zi->offset++;
  }
  if (IT_SUCCESS_MORE_AVAILABLE == proc.res_iteration_finished)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "More results available\n");
    return; /* more results later */
  }
  if (GNUNET_YES == zi->has_zone)
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"No more results for zone `%s'\n", 
		GNUNET_NAMESTORE_short_h2s(&zi->zone));
  else
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"No more results for all zones\n");
  send_empty_response (snc, zi->client->client, zi->request_id);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Removing zone iterator\n");
  GNUNET_CONTAINER_DLL_remove (zi->client->op_head, 
			       zi->client->op_tail,
			       zi);
  GNUNET_free (zi);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_START' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct ZoneIterationStartMessage'
 */
static void
handle_iteration_start (void *cls,
                        struct GNUNET_SERVER_Client *client,
                        const struct GNUNET_MessageHeader *message)
{
  static struct GNUNET_CRYPTO_ShortHashCode zeros;
  const struct ZoneIterationStartMessage *zis_msg;
  struct NamestoreClient *nc;
  struct ZoneIteration *zi;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Received `%s' message\n", "ZONE_ITERATION_START");
  if (NULL == (nc = client_lookup (client)))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  zis_msg = (const struct ZoneIterationStartMessage *) message;
  zi = GNUNET_new (struct ZoneIteration);
  zi->request_id = ntohl (zis_msg->gns_header.r_id);
  zi->offset = 0;
  zi->client = nc;
  zi->must_have_flags = ntohs (zis_msg->must_have_flags);
  zi->must_not_have_flags = ntohs (zis_msg->must_not_have_flags);
  if (0 == memcmp (&zeros, &zis_msg->zone, sizeof (zeros)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Starting to iterate over all zones\n");
    zi->zone = zis_msg->zone;
    zi->has_zone = GNUNET_NO;
  }
  else
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Starting to iterate over zone `%s'\n", GNUNET_NAMESTORE_short_h2s (&zis_msg->zone));
    zi->zone = zis_msg->zone;
    zi->has_zone = GNUNET_YES;
  }
  GNUNET_CONTAINER_DLL_insert (nc->op_head, nc->op_tail, zi);
  run_zone_iteration_round (zi);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_STOP' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct ZoneIterationStopMessage'
 */
static void
handle_iteration_stop (void *cls,
                       struct GNUNET_SERVER_Client *client,
                       const struct GNUNET_MessageHeader *message)
{
  struct NamestoreClient *nc;
  struct ZoneIteration *zi;
  const struct ZoneIterationStopMessage *zis_msg;
  uint32_t rid;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Received `%s' message\n",
	      "ZONE_ITERATION_STOP");
  if (NULL == (nc = client_lookup(client)))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  zis_msg = (const struct ZoneIterationStopMessage *) message;
  rid = ntohl (zis_msg->gns_header.r_id);
  for (zi = nc->op_head; NULL != zi; zi = zi->next)
    if (zi->request_id == rid)
      break;
  if (NULL == zi)
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  GNUNET_CONTAINER_DLL_remove (nc->op_head, nc->op_tail, zi);
  if (GNUNET_YES == zi->has_zone)
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Stopped zone iteration for zone `%s'\n",
		GNUNET_NAMESTORE_short_h2s (&zi->zone));
  else
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Stopped zone iteration over all zones\n");
  GNUNET_free (zi);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_NEXT' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct ZoneIterationNextMessage'
 */
static void
handle_iteration_next (void *cls,
                       struct GNUNET_SERVER_Client *client,
                       const struct GNUNET_MessageHeader *message)
{
  struct NamestoreClient *nc;
  struct ZoneIteration *zi;
  const struct ZoneIterationNextMessage *zis_msg;
  uint32_t rid;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Received `%s' message\n", "ZONE_ITERATION_NEXT");
  if (NULL == (nc = client_lookup(client)))
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  zis_msg = (const struct ZoneIterationNextMessage *) message;
  rid = ntohl (zis_msg->gns_header.r_id);
  for (zi = nc->op_head; NULL != zi; zi = zi->next)
    if (zi->request_id == rid)
      break;
  if (NULL == zi)
  {
    GNUNET_break (0);
    GNUNET_SERVER_receive_done (client, GNUNET_SYSERR);
    return;
  }
  run_zone_iteration_round (zi);
  GNUNET_SERVER_receive_done (client, GNUNET_OK);
}


/**
 * Load zone keys from directory by reading all .zkey files in this directory
 *
 * @param cls int * 'counter' to store the number of files found
 * @param filename directory to scan
 * @return GNUNET_OK to continue
 */
static int
zonekey_file_it (void *cls, const char *filename)
{
  unsigned int *counter = cls;
  struct GNUNET_CRYPTO_EccPrivateKey *pk;
  

  if ((NULL == filename) ||
      (NULL == strstr (filename, ".zkey")))
    return GNUNET_OK;
  pk = GNUNET_CRYPTO_ecc_key_create_from_file (filename);
  learn_private_key (pk);
  (*counter)++;
  return GNUNET_OK;
}


/**
 * Send 'sync' message to zone monitor, we're now in sync.
 *
 * @param zm monitor that is now in sync
 */ 
static void
monitor_sync (struct ZoneMonitor *zm)
{
  struct GNUNET_MessageHeader sync;

  sync.size = htons (sizeof (struct GNUNET_MessageHeader));
  sync.type = htons (GNUNET_MESSAGE_TYPE_NAMESTORE_MONITOR_SYNC);
  GNUNET_SERVER_notification_context_unicast (monitor_nc,
					      zm->client,
					      &sync,
					      GNUNET_NO);
}


/**
 * Obtain the next datum during the zone monitor's zone intiial iteration.
 *
 * @param cls zone monitor that does its initial iteration
 * @param tc scheduler context
 */
static void
monitor_next (void *cls,
	      const struct GNUNET_SCHEDULER_TaskContext *tc);


/**
 * A 'GNUNET_NAMESTORE_RecordIterator' for monitors.
 *
 * @param cls a 'struct ZoneMonitor *' with information about the monitor
 * @param zone_key zone key of the zone
 * @param expire expiration time
 * @param name name
 * @param rd_count number of records
 * @param rd array of records
 * @param signature signature
 */
static void
monitor_iterate_cb (void *cls,
		    const struct GNUNET_CRYPTO_EccPublicKey *zone_key,
		    struct GNUNET_TIME_Absolute expire,
		    const char *name,
		    unsigned int rd_count,
		    const struct GNUNET_NAMESTORE_RecordData *rd,
		    const struct GNUNET_CRYPTO_EccSignature *signature)
{
  struct ZoneMonitor *zm = cls;

  if (NULL == name)
  {
    /* finished with iteration */
    monitor_sync (zm);
    return;
  }
  send_lookup_response (monitor_nc,
			zm->client,
			zm->request_id,
			zone_key,
			expire,
			name,
			rd_count,
			rd,
			signature);
  zm->task = GNUNET_SCHEDULER_add_now (&monitor_next, zm);
}


/**
 * Handles a 'GNUNET_MESSAGE_TYPE_NAMESTORE_MONITOR_START' message
 *
 * @param cls unused
 * @param client GNUNET_SERVER_Client sending the message
 * @param message message of type 'struct ZoneMonitorStartMessage'
 */
static void
handle_monitor_start (void *cls,
		      struct GNUNET_SERVER_Client *client,
		      const struct GNUNET_MessageHeader *message)
{
  static struct GNUNET_CRYPTO_ShortHashCode zeros;
  const struct ZoneMonitorStartMessage *zis_msg;
  struct ZoneMonitor *zm;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Received `%s' message\n",
	      "ZONE_MONITOR_START");
  zis_msg = (const struct ZoneMonitorStartMessage *) message;
  zm = GNUNET_new (struct ZoneMonitor);
  zm->request_id = ntohl (zis_msg->gns_header.r_id);
  zm->offset = 0;
  zm->client = client; // FIXME: notify handler for disconnects, check monitors!
  if (0 == memcmp (&zeros, &zis_msg->zone, sizeof (zeros)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"Starting to monitor all zones\n");
    zm->zone = zis_msg->zone;
    zm->has_zone = GNUNET_NO;
  }
  else
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Starting to monitor zone `%s'\n",
		GNUNET_NAMESTORE_short_h2s (&zis_msg->zone));
    zm->zone = zis_msg->zone;
    zm->has_zone = GNUNET_YES;
  }
  GNUNET_CONTAINER_DLL_insert (monitor_head, monitor_tail, zm);
  GNUNET_SERVER_client_mark_monitor (client);
  GNUNET_SERVER_disable_receive_done_warning (client);
  GNUNET_SERVER_notification_context_add (monitor_nc,
					  client);
  zm->task = GNUNET_SCHEDULER_add_now (&monitor_next, zm);  
}


/**
 * Obtain the next datum during the zone monitor's zone intiial iteration.
 *
 * @param cls zone monitor that does its initial iteration
 * @param tc scheduler context
 */
static void
monitor_next (void *cls,
	      const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct ZoneMonitor *zm = cls;
  int ret;
  
  zm->task = GNUNET_SCHEDULER_NO_TASK;
  ret = GSN_database->iterate_records (GSN_database->cls,
				       (GNUNET_YES == zm->has_zone) ? &zm->zone : NULL,
				       NULL, zm->offset++,
				       &monitor_iterate_cb, zm);
  if (GNUNET_SYSERR == ret)
  {
    GNUNET_SERVER_client_disconnect (zm->client);
    return;
  }
  if (GNUNET_NO == ret)
  {
    /* empty zone */
    monitor_sync (zm);
    return;
  }
}


/**
 * Process namestore requests.
 *
 * @param cls closure
 * @param server the initialized server
 * @param cfg configuration to use
 */
static void
run (void *cls, struct GNUNET_SERVER_Handle *server,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  static const struct GNUNET_SERVER_MessageHandler handlers[] = {
    {&handle_start, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_START, sizeof (struct StartMessage)},
    {&handle_lookup_name, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_LOOKUP_NAME, 0},
    {&handle_record_put, NULL,
    GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_PUT, 0},
    {&handle_record_create, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_RECORD_CREATE, 0},
    {&handle_zone_to_name, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_TO_NAME, sizeof (struct ZoneToNameMessage) },
    {&handle_iteration_start, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_START, sizeof (struct ZoneIterationStartMessage) },
    {&handle_iteration_next, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_NEXT, sizeof (struct ZoneIterationNextMessage) },
    {&handle_iteration_stop, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_ZONE_ITERATION_STOP, sizeof (struct ZoneIterationStopMessage) },
    {&handle_monitor_start, NULL,
     GNUNET_MESSAGE_TYPE_NAMESTORE_MONITOR_START, sizeof (struct ZoneMonitorStartMessage) },
    {NULL, NULL, 0, 0}
  };
  char *database;
  unsigned int counter;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Starting namestore service\n");
  GSN_cfg = cfg;
  monitor_nc = GNUNET_SERVER_notification_context_create (server, 1);
  /* Load private keys from disk */
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_filename (cfg, "namestore", 
					       "zonefile_directory",
					       &zonefile_directory))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, 
		_("No directory to load zonefiles specified in configuration\n"));
    GNUNET_SCHEDULER_add_now (&cleanup_task, NULL);
    return;
  }

  if (GNUNET_NO == GNUNET_DISK_file_test (zonefile_directory))
  {
    if (GNUNET_SYSERR == GNUNET_DISK_directory_create (zonefile_directory))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR, 
		  _("Creating directory `%s' for zone files failed!\n"),
		  zonefile_directory);
      GNUNET_SCHEDULER_add_now (&cleanup_task, NULL);
      return;
    }
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
		"Created directory `%s' for zone files\n", 
		zonefile_directory);
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Scanning directory `%s' for zone files\n", 
	      zonefile_directory);
  zonekeys = GNUNET_CONTAINER_multihashmap_create (16, GNUNET_NO);
  counter = 0;
  GNUNET_DISK_directory_scan (zonefile_directory, 
			      &zonekey_file_it, &counter);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Found %u zone files\n", 
	      counter);

  /* Loading database plugin */
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (cfg, "namestore", "database",
                                             &database))
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "No database backend configured\n");

  GNUNET_asprintf (&db_lib_name, "libgnunet_plugin_namestore_%s", database);
  GSN_database = GNUNET_PLUGIN_load (db_lib_name, (void *) GSN_cfg);
  GNUNET_free (database);
  if (NULL == GSN_database)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, 
		"Could not load database backend `%s'\n",
		db_lib_name);
    GNUNET_SCHEDULER_add_now (&cleanup_task, NULL);
    return;
  }

  /* Configuring server handles */
  GNUNET_SERVER_add_handlers (server, handlers);
  snc = GNUNET_SERVER_notification_context_create (server, 16);
  GNUNET_SERVER_disconnect_notify (server,
                                   &client_disconnect_notification,
                                   NULL);
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL, &cleanup_task,
                                NULL);
}


/**
 * The main function for the template service.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  return (GNUNET_OK ==
          GNUNET_SERVICE_run (argc, argv, "namestore",
                              GNUNET_SERVICE_OPTION_NONE, &run, NULL)) ? 0 : 1;
}

/* end of gnunet-service-namestore.c */

