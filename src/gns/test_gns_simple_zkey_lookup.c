/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file gns/test_gns_simple_zkey_lookup.c
 * @brief base testcase for testing zkey lookup
 *
 */
#include "platform.h"
#include "gnunet_testing_lib.h"
#include "gnunet_core_service.h"
#include "block_dns.h"
#include "gnunet_signatures.h"
#include "gnunet_namestore_service.h"
#include "../namestore/namestore.h"
#include "gnunet_dnsparser_lib.h"
#include "gnunet_gns_service.h"
#include "gns.h"

/* Timeout for entire testcase */
#define TIMEOUT GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, 40)

/* test records to resolve */
#define TEST_IP "127.0.0.1"
#define TEST_RECORD_NAME "www"

#define TEST_AUTHORITY_NAME "bob"

#define KEYFILE_BOB "../namestore/zonefiles/HGU0A0VCU334DN7F2I9UIUMVQMM7JMSD142LIMNUGTTV9R0CF4EG.zkey"

/* Task handle to use to schedule test failure */
static GNUNET_SCHEDULER_TaskIdentifier die_task;

/* Global return value (0 for success, anything else for failure) */
static int ok;

static struct GNUNET_NAMESTORE_Handle *namestore_handle;

static struct GNUNET_GNS_Handle *gns_handle;

static const struct GNUNET_CONFIGURATION_Handle *cfg;

static struct GNUNET_CRYPTO_ShortHashCode bob_hash;


/**
 * Check if the get_handle is being used, if so stop the request.  Either
 * way, schedule the end_badly_cont function which actually shuts down the
 * test.
 */
static void
end_badly (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  die_task = GNUNET_SCHEDULER_NO_TASK;
  if (NULL != gns_handle)
  {
    GNUNET_GNS_disconnect(gns_handle);
    gns_handle = NULL;
  }

  if (NULL != namestore_handle)
  {
    GNUNET_NAMESTORE_disconnect (namestore_handle);
    namestore_handle = NULL;
  }
  GNUNET_break (0);
  GNUNET_SCHEDULER_shutdown ();
  ok = 1;
}


static void
end_badly_now ()
{
  GNUNET_SCHEDULER_cancel (die_task);
  die_task = GNUNET_SCHEDULER_add_now (&end_badly, NULL);
}


static void 
shutdown_task (void *cls,
	       const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_GNS_disconnect(gns_handle);
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Shutting down peer!\n");
  GNUNET_SCHEDULER_shutdown ();
}


static void
on_lookup_result(void *cls, uint32_t rd_count,
                 const struct GNUNET_NAMESTORE_RecordData *rd)
{
  struct in_addr a;
  int i;
  char* addr;
  
  if (GNUNET_SCHEDULER_NO_TASK != die_task)
  {
      GNUNET_SCHEDULER_cancel (die_task);
      die_task = GNUNET_SCHEDULER_NO_TASK;
  }

  GNUNET_NAMESTORE_disconnect (namestore_handle);
  namestore_handle = NULL;
  if (rd_count == 0)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Lookup failed\n");
    ok = 2;
  }
  else
  {
    ok = 1;
    GNUNET_log (GNUNET_ERROR_TYPE_INFO, "name: %s\n", (char*)cls);
    for (i=0; i<rd_count; i++)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO, "type: %d\n", rd[i].record_type);
      if (rd[i].record_type == GNUNET_GNS_RECORD_A)
      {
        memcpy(&a, rd[i].data, sizeof(a));
        addr = inet_ntoa(a);
        GNUNET_log (GNUNET_ERROR_TYPE_INFO, "address: %s\n", addr);
        if (0 == strcmp(addr, TEST_IP))
        {
          GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                    "ZKEY correctly resolved to %s!\n", addr);
          ok = 0;
        }
      }
      else
      {
        GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "No resolution!\n");
      }
    }
  }
  
  GNUNET_SCHEDULER_add_now (&shutdown_task, NULL);
}


/**
 * Function scheduled to be run on the successful start of services
 * tries to look up the dns record for TEST_DOMAIN
 */
static void
commence_testing (void *cls, int32_t success, const char *emsg)
{
  char name[GNUNET_DNSPARSER_MAX_NAME_LENGTH];
  char* pos;
  struct GNUNET_CRYPTO_ShortHashAsciiEncoded hash_str;
  
  gns_handle = GNUNET_GNS_connect(cfg);
  if (NULL == gns_handle)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Failed to connect to GNS!\n");
    end_badly_now ();
    return;
  }

  pos = name;
  strcpy(pos, TEST_RECORD_NAME);
  pos += strlen(TEST_RECORD_NAME);
  strcpy(pos, ".");
  pos++;
  GNUNET_CRYPTO_short_hash_to_enc(&bob_hash, &hash_str);
  strcpy(pos, (char*)&hash_str);
  pos += strlen((char*)&hash_str);
  strcpy(pos, ".");
  pos++;
  strcpy(pos, GNUNET_GNS_TLD_ZKEY);

  GNUNET_GNS_lookup(gns_handle, name, GNUNET_GNS_RECORD_A,
                    GNUNET_NO,
                    NULL,
                    &on_lookup_result, NULL);
}


static void
do_check (void *cls,
          const struct GNUNET_CONFIGURATION_Handle *ccfg,
          struct GNUNET_TESTING_Peer *peer)
{
  struct GNUNET_CRYPTO_EccPublicKey alice_pkey;
  struct GNUNET_CRYPTO_EccPublicKey bob_pkey;
  struct GNUNET_CRYPTO_EccPrivateKey *alice_key;
  struct GNUNET_CRYPTO_EccPrivateKey *bob_key;
  struct GNUNET_CRYPTO_EccSignature *sig;
  char* alice_keyfile;

  cfg = ccfg;
  die_task = GNUNET_SCHEDULER_add_delayed (TIMEOUT, &end_badly, NULL);

  /* put records into namestore */
  namestore_handle = GNUNET_NAMESTORE_connect(cfg);
  if (NULL == namestore_handle)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Failed to connect to namestore\n");
    end_badly_now ();
    return;
  }

  if (GNUNET_OK != GNUNET_CONFIGURATION_get_value_filename (cfg, "gns",
                                                          "ZONEKEY",
                                                          &alice_keyfile))
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Failed to get key from cfg\n");
    end_badly_now ();
    return;
  }

  alice_key = GNUNET_CRYPTO_ecc_key_create_from_file (alice_keyfile);
  bob_key = GNUNET_CRYPTO_ecc_key_create_from_file (KEYFILE_BOB);

  GNUNET_CRYPTO_ecc_key_get_public (alice_key, &alice_pkey);
  GNUNET_CRYPTO_ecc_key_get_public (bob_key, &bob_pkey);

  struct GNUNET_NAMESTORE_RecordData rd;
  char* ip = TEST_IP;
  struct in_addr *web = GNUNET_malloc(sizeof(struct in_addr));
  rd.expiration_time = UINT64_MAX;
  GNUNET_assert(1 == inet_pton (AF_INET, ip, web));

  GNUNET_CRYPTO_short_hash(&bob_pkey, sizeof(bob_pkey), &bob_hash);

  rd.data_size = sizeof(struct GNUNET_CRYPTO_ShortHashCode);
  rd.data = &bob_hash;
  rd.record_type = GNUNET_GNS_RECORD_PKEY;
  rd.flags = GNUNET_NAMESTORE_RF_AUTHORITY;

  GNUNET_NAMESTORE_record_put_by_authority (namestore_handle,
					    alice_key,
					    TEST_AUTHORITY_NAME,
					    1, &rd,
					    NULL,
					    NULL);

  rd.data_size = sizeof(struct in_addr);
  rd.data = web;
  rd.record_type = GNUNET_DNSPARSER_TYPE_A;
  sig = GNUNET_NAMESTORE_create_signature(bob_key,
                                          GNUNET_TIME_UNIT_FOREVER_ABS,
                                          TEST_RECORD_NAME,
                                          &rd, 1);

  GNUNET_NAMESTORE_record_put (namestore_handle,
                               &bob_pkey,
                               TEST_RECORD_NAME,
                               GNUNET_TIME_UNIT_FOREVER_ABS,
                               1,
                               &rd,
                               sig,
                               &commence_testing,
                               NULL);
  GNUNET_free (alice_keyfile);
  GNUNET_free (web);
  GNUNET_free (sig);
  GNUNET_CRYPTO_ecc_key_free (bob_key);
  GNUNET_CRYPTO_ecc_key_free (alice_key);
}


int
main (int argc, char *argv[])
{
  ok = 1;

  GNUNET_log_setup ("test-gns-simple-zkey-lookup",
                    "WARNING",
                    NULL);
  GNUNET_TESTING_peer_run ("test-gns-simple-zkey-lookup", 
			   "test_gns_simple_lookup.conf", 
			   &do_check, NULL);
  return ok;
}


/* end of test_gns_simple_zkey_lookup.c */
