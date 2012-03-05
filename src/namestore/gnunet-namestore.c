/*
     This file is part of GNUnet.
     (C) 2012 Christian Grothoff (and other contributing authors)

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
 * @file gnunet-namestore.c
 * @brief command line tool to manipulate the local zone
 * @author Christian Grothoff
 *
 * TODO:
 * - printing records
 * - allow users to set record options (not just 'RF_AUTHORITY')
 * - test
 * - parsing SOA, PTR and MX value specifications (and define format!)
 * - add options to list/lookup individual records
 */
#include "platform.h"
#include <gnunet_util_lib.h>
#include <gnunet_dnsparser_lib.h>
#include <gnunet_namestore_service.h>

/**
 * Handle to the namestore.
 */
static struct GNUNET_NAMESTORE_Handle *ns;

/**
 * Hash of the public key of our zone.
 */
static GNUNET_HashCode zone;

/**
 * Private key for the our zone.
 */
static struct GNUNET_CRYPTO_RsaPrivateKey *zone_pkey;

/**
 * Keyfile to manipulate.
 */
static char *keyfile;	

/**
 * Desired action is to add a record.
 */
static int add;

/**
 * Queue entry for the 'add' operation.
 */
static struct GNUNET_NAMESTORE_QueueEntry *add_qe;

/**
 * Desired action is to list records.
 */
static int list;

/**
 * List iterator for the 'list' operation.
 */
static struct GNUNET_NAMESTORE_ZoneIterator *list_it;

/**
 * Desired action is to remove a record.
 */
static int del;

/**
 * Queue entry for the 'del' operation.
 */
static struct GNUNET_NAMESTORE_QueueEntry *del_qe;

/**
 * Name of the records to add/list/remove.
 */
static char *name;

/**
 * Value of the record to add/remove.
 */
static char *value;

/**
 * Type of the record to add/remove, NULL to remove all.
 */
static char *typestring;

/**
 * Desired expiration time.
 */
static char *expirationstring;
		

/**
 * Task run on shutdown.  Cleans up everything.
 *
 * @param cls unused
 * @param tc scheduler context
 */
static void
do_shutdown (void *cls,
	     const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if (NULL != ns)
  {
    GNUNET_NAMESTORE_disconnect (ns, GNUNET_NO);
    ns = NULL;
  }
  if (NULL != zone_pkey)
  {
    GNUNET_CRYPTO_rsa_key_free (zone_pkey);
    zone_pkey = NULL;
  }
}


/**
 * Continuation called to notify client about result of the
 * operation.
 *
 * @param cls closure, unused
 * @param success GNUNET_SYSERR on failure (including timeout/queue drop/failure to validate)
 *                GNUNET_NO if content was already there
 *                GNUNET_YES (or other positive value) on success
 * @param emsg NULL on success, otherwise an error message
 */
static void
add_continuation (void *cls,
		  int32_t success,
		  const char *emsg)
{
  add_qe = NULL;
  if (success != GNUNET_YES)
    fprintf (stderr,
	     _("Adding record failed: %s\n"),
	     (success == GNUNET_NO) ? "record exists" : emsg);
  if ( (NULL == del_qe) &&
       (NULL == list_it) )
    GNUNET_SCHEDULER_shutdown ();
}


/**
 * Continuation called to notify client about result of the
 * operation.
 *
 * @param cls closure, unused
 * @param success GNUNET_SYSERR on failure (including timeout/queue drop/failure to validate)
 *                GNUNET_NO if content was already there
 *                GNUNET_YES (or other positive value) on success
 * @param emsg NULL on success, otherwise an error message
 */
static void
del_continuation (void *cls,
		  int32_t success,
		  const char *emsg)
{
  del_qe = NULL;
  if (success != GNUNET_YES)
    fprintf (stderr,
	     _("Deleting record failed: %s\n"),
	     emsg);
  if ( (NULL == add_qe) &&
       (NULL == list_it) )
    GNUNET_SCHEDULER_shutdown ();
}


/**
 * Process a record that was stored in the namestore.
 *
 * @param cls closure
 * @param zone_key public key of the zone
 * @param expire when does the corresponding block in the DHT expire (until
 *               when should we never do a DHT lookup for the same name again)?; 
 *               GNUNET_TIME_UNIT_ZERO_ABS if there are no records of any type in the namestore,
 *               or the expiration time of the block in the namestore (even if there are zero
 *               records matching the desired record type)
 * @param name name that is being mapped (at most 255 characters long)
 * @param rd_count number of entries in 'rd' array
 * @param rd array of records with data to store
 * @param signature signature of the record block, NULL if signature is unavailable (i.e. 
 *        because the user queried for a particular record type only)
 */
static void
display_record (void *cls,
		const struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded *zone_key,
		struct GNUNET_TIME_Absolute expire,			    
		const char *name,
		unsigned int rd_len,
		const struct GNUNET_NAMESTORE_RecordData *rd,
		const struct GNUNET_CRYPTO_RsaSignature *signature)
{
  if (NULL == name)
  {
    list_it = NULL;
    if ( (NULL == del_qe) &&
	 (NULL == add_qe) )
      GNUNET_SCHEDULER_shutdown ();
    return;
  }
  // FIXME: display record!
  GNUNET_NAMESTORE_zone_iterator_next (list_it);
}


/**
 * Main function that will be run.
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param cfg configuration
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded pub;
  uint32_t type;
  const void *data;
  size_t data_size;
  struct in_addr value_a;
  struct in6_addr value_aaaa;
  struct GNUNET_TIME_Relative etime;
  struct GNUNET_NAMESTORE_RecordData rd;

  if (NULL == keyfile)
  {
    fprintf (stderr,
	     _("Option `%s' not given, but I need a zone key file!\n"),
	     "z");
    return;
  }
  zone_pkey = GNUNET_CRYPTO_rsa_key_create_from_file (keyfile);
  GNUNET_free (keyfile);
  keyfile = NULL;
  if (! (add|del|list))
  {
    /* nothing more to be done */  
    GNUNET_CRYPTO_rsa_key_free (zone_pkey);
    zone_pkey = NULL;
    return; 
  }
  if (NULL == zone_pkey)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		_("Failed to read or create private zone key\n"));
    return;
  }
  GNUNET_CRYPTO_rsa_key_get_public (zone_pkey,
				    &pub);
  GNUNET_CRYPTO_hash (&pub, sizeof (pub), &zone);

  ns = GNUNET_NAMESTORE_connect (cfg);
  if (NULL == ns)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		_("Failed to connect to namestore\n"));
    return;
  }
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL,
				&do_shutdown, NULL);
  if (NULL == typestring)
    type = 0;
  else
    type = GNUNET_NAMESTORE_typename_to_number (typestring);
  if (UINT32_MAX == type)
  {
    fprintf (stderr, _("Unsupported type `%s'\n"), typestring);
    GNUNET_SCHEDULER_shutdown ();
    return;
  } else if (add | del)
  {
    fprintf (stderr,
	     _("Missing option `%s' for operation `%s'\n"),
	     "-t", _("add/del"));
    GNUNET_SCHEDULER_shutdown ();
    return;     
  }
  if (NULL != value)
  {
    switch (type)
    {
    case 0:
      fprintf (stderr, _("Need a record type to interpret value `%s'\n"), value);
      GNUNET_SCHEDULER_shutdown ();
      break;
    case GNUNET_DNSPARSER_TYPE_A:
      if (1 != inet_pton (AF_INET, value, &value_a))
      {
	fprintf (stderr, _("Value `%s' invalid for record type `%s'\n"), 
		 value,
		 typestring);
	GNUNET_SCHEDULER_shutdown ();
	return;
      }
      data = &value_a;
      data_size = sizeof (value_a);
      break;
    case GNUNET_DNSPARSER_TYPE_NS:
      data = value;
      data_size = strlen (value);
      break;
    case GNUNET_DNSPARSER_TYPE_CNAME:
      data = value;
      data_size = strlen (value);
      break;
    case GNUNET_DNSPARSER_TYPE_SOA:
      // FIXME
      fprintf (stderr, _("Record type `%s' not implemented yet\n"), typestring);
      GNUNET_SCHEDULER_shutdown ();
      return;
    case GNUNET_DNSPARSER_TYPE_PTR:
      // FIXME
      fprintf (stderr, _("Record type `%s' not implemented yet\n"), typestring);
      GNUNET_SCHEDULER_shutdown ();
      return;
    case GNUNET_DNSPARSER_TYPE_MX:
      // FIXME
      fprintf (stderr, _("Record type `%s' not implemented yet\n"), typestring);
      GNUNET_SCHEDULER_shutdown ();
      return;
    case GNUNET_DNSPARSER_TYPE_TXT:
      data = value;
      data_size = strlen (value);
      break;
    case GNUNET_DNSPARSER_TYPE_AAAA:
      if (1 != inet_pton (AF_INET6, value, &value_aaaa))
      {
	fprintf (stderr, _("Value `%s' invalid for record type `%s'\n"), 
		 value,
		 typestring);
	GNUNET_SCHEDULER_shutdown ();
	return;
      }
      data = &value_aaaa;
      data_size = sizeof (value_aaaa);
      break;
    case GNUNET_NAMESTORE_TYPE_PKEY:
      fprintf (stderr, _("Record type `%s' not implemented yet\n"), typestring);
      GNUNET_SCHEDULER_shutdown ();
      return;
    case GNUNET_NAMESTORE_TYPE_PSEU:
      data = value;
      data_size = strlen (value);
      break;
    default:
      GNUNET_assert (0);
    }
  } else if (add | del)
  {
    fprintf (stderr,
	     _("Missing option `%s' for operation `%s'\n"),
	     "-V", _("add/del"));
    GNUNET_SCHEDULER_shutdown ();
    return;     
  }
  if (NULL != expirationstring)
  {
    if (GNUNET_OK !=
	GNUNET_STRINGS_fancy_time_to_relative (expirationstring,
					       &etime))
    {
      fprintf (stderr,
	       _("Invalid time format `%s'\n"),
	       expirationstring);
      GNUNET_SCHEDULER_shutdown ();
      return;     
    }
  } else if (add | del)
  {
    fprintf (stderr,
	     _("Missing option `%s' for operation `%s'\n"),
	     "-e", _("add/del"));
    GNUNET_SCHEDULER_shutdown ();
    return;     
  }
  if (add)
  {
    if (NULL == name)
    {
      fprintf (stderr,
	       _("Missing option `%s' for operation `%s'\n"),
	       "-n", _("add"));
      GNUNET_SCHEDULER_shutdown ();
      return;     
    }
    rd.data = data;
    rd.data_size = data_size;
    rd.record_type = type;
    rd.expiration = GNUNET_TIME_relative_to_absolute (etime);
    rd.flags = GNUNET_NAMESTORE_RF_AUTHORITY; // FIXME: not always...
    add_qe = GNUNET_NAMESTORE_record_create (ns,
					     zone_pkey,
					     name,
					     &rd,
					     &add_continuation,
					     NULL);
  }
  if (del)
  {
    if (NULL == name)
    {
      fprintf (stderr,
	       _("Missing option `%s' for operation `%s'\n"),
	       "-n", _("del"));
      GNUNET_SCHEDULER_shutdown ();
      return;     
    }
    rd.data = data;
    rd.data_size = data_size;
    rd.record_type = type;
    rd.expiration = GNUNET_TIME_relative_to_absolute (etime);
    rd.flags = GNUNET_NAMESTORE_RF_AUTHORITY; // FIXME: not always...
    del_qe = GNUNET_NAMESTORE_record_create (ns,
					     zone_pkey,
					     name,
					     &rd,
					     &del_continuation,
					     NULL);
  }
  if (list)
  {
    list_it = GNUNET_NAMESTORE_zone_iteration_start (ns,
						     &zone,
						     0, 0,
						     &display_record,
						     NULL);
  }
}


/**
 * The main function for gnunet-namestore.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  static const struct GNUNET_GETOPT_CommandLineOption options[] = {
    {'a', "add", NULL,
     gettext_noop ("add record"), 0,
     &GNUNET_GETOPT_set_one, &add},   
    {'d', "delete", NULL,
     gettext_noop ("delete record"), 0,
     &GNUNET_GETOPT_set_one, &del},   
    {'D', "display", NULL,
     gettext_noop ("display records"), 0,
     &GNUNET_GETOPT_set_one, &list},   
    {'e', "expiration", "TIME",
     gettext_noop ("expiration time to use (for adding only)"), 1,
     &GNUNET_GETOPT_set_string, &expirationstring},   
    {'n', "name", "NAME",
     gettext_noop ("name of the record to add/delete/display"), 1,
     &GNUNET_GETOPT_set_string, &name},   
    {'t', "type", "TYPE",
     gettext_noop ("type of the record to add/delete/display"), 1,
     &GNUNET_GETOPT_set_string, &typestring},   
    {'V', "value", "VALUE",
     gettext_noop ("value of the record to add/delete"), 1,
     &GNUNET_GETOPT_set_string, &value},   
    {'z', "zonekey", "FILENAME",
     gettext_noop ("filename with the zone key"), 1,
     &GNUNET_GETOPT_set_string, &keyfile},   
    GNUNET_GETOPT_OPTION_END
  };

  int ret;

  GNUNET_log_setup ("gnunet-namestore", "WARNING", NULL);
  ret =
      (GNUNET_OK ==
       GNUNET_PROGRAM_run (argc, argv, "gnunet-namestore",
                           _("GNUnet zone manipulation tool"), 
			   options,
                           &run, NULL)) ? 0 : 1;

  return ret;
}

/* end of gnunet-namestore.c */
