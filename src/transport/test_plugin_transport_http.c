/*
     This file is part of GNUnet.
     (C) 2010 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
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
 * @file transport/test_plugin_transport_http.c
 * @brief testcase for plugin_transport_http.c
 * @author Matthias Wachs
 */

#include "platform.h"
#include "gnunet_constants.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_hello_lib.h"
#include "gnunet_os_lib.h"
#include "gnunet_peerinfo_service.h"
#include "gnunet_plugin_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_program_lib.h"
#include "gnunet_signatures.h"
#include "gnunet_service_lib.h"
#include "plugin_transport.h"
#include "gnunet_statistics_service.h"
#include "transport.h"
#include <curl/curl.h>

#define VERBOSE GNUNET_YES
#define DEBUG GNUNET_YES

/**
 * How long until we give up on transmitting the message?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 30)

/**
 * How long until we give up on transmitting the message?
 */
#define STAT_INTERVALL GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 1)

/**
 * Our public key.
 */
/* static struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded my_public_key; */

/**
 * Our identity.
 */
static struct GNUNET_PeerIdentity my_identity;

/**
 * Our private key.
 */
static struct GNUNET_CRYPTO_RsaPrivateKey *my_private_key;

/**
 * Our scheduler.
 */
struct GNUNET_SCHEDULER_Handle *sched;

/**
 * Our statistics handle.
 */
struct GNUNET_STATISTICS_Handle *stats;


/**
 * Our configuration.
 */
const struct GNUNET_CONFIGURATION_Handle *cfg;

/**
 * Number of neighbours we'd like to have.
 */
static uint32_t max_connect_per_transport;

/**
 * Environment for this plugin.
 */
static struct GNUNET_TRANSPORT_PluginEnvironment env;

/**
 *handle for the api provided by this plugin
 */
static struct GNUNET_TRANSPORT_PluginFunctions *api;

/**
 * Did the test pass or fail?
 */
static int fail;

pid_t pid;

/**
 * ID of the task controlling the locking between two hostlist tests
 */
static GNUNET_SCHEDULER_TaskIdentifier ti_check_stat;

static unsigned int timeout_count;

/**
 * Initialize Environment for this plugin
 */
static struct GNUNET_TIME_Relative
receive (void *cls,
	 const struct GNUNET_PeerIdentity * peer,
	 const struct GNUNET_MessageHeader * message,
	 uint32_t distance,
	 struct Session *session,
	 const char *sender_address,
	 uint16_t sender_address_len)
{
  /* do nothing */
  return GNUNET_TIME_UNIT_ZERO;
}

void
notify_address (void *cls,
                const char *name,
                const void *addr,
                uint16_t addrlen, 
		struct GNUNET_TIME_Relative expires)
{
}

/**
 * Simple example test that invokes
 * the check_address function of the plugin.
 */
/* FIXME: won't work on IPv6 enabled systems where IPv4 mapping
 * isn't enabled (eg. FreeBSD > 4)
 */
static void
shutdown_clean ()
{
  GNUNET_assert (NULL ==
                 GNUNET_PLUGIN_unload ("libgnunet_plugin_transport_http",
                                       api));
  if (my_private_key != NULL)
    GNUNET_CRYPTO_rsa_key_free (my_private_key);

  if (ti_check_stat != GNUNET_SCHEDULER_NO_TASK)
    GNUNET_SCHEDULER_cancel(sched, ti_check_stat);
  GNUNET_SCHEDULER_shutdown(sched);
  return;
}

static void
setup_plugin_environment ()
{
  env.cfg = cfg;
  env.sched = sched;
  env.stats = stats;
  env.my_identity = &my_identity;
  env.cls = &env;
  env.receive = &receive;
  env.notify_address = &notify_address;
  env.max_connections = max_connect_per_transport;
}

static int
process_stat (void *cls,
              const char *subsystem,
              const char *name,
              uint64_t value,
              int is_persistent)
{
  if (value==1)
    {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Shutdown, plugin failed \n");
    fail = GNUNET_YES;
    shutdown_clean();
    return GNUNET_YES;
    }
  if (value==2)
    {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Shutdown, plugin not failed \n");
    shutdown_clean();
    return GNUNET_YES;
    }
  return GNUNET_YES;
}


/**
 * Task that checks if we should try to download a hostlist.
 * If so, we initiate the download, otherwise we schedule
 * this task again for a later time.
 */
static void
task_check_stat (void *cls,
            const struct GNUNET_SCHEDULER_TaskContext *tc)
{

  ti_check_stat = GNUNET_SCHEDULER_NO_TASK;
  if (0 != (tc->reason & GNUNET_SCHEDULER_REASON_SHUTDOWN))
    return;

  if ( timeout_count > 10 )
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Testcase timeout\n",  timeout_count);
    fail = GNUNET_YES;
    shutdown_clean();
    return;
  }
  timeout_count++;

  GNUNET_STATISTICS_get (stats,
                         "http-transport",
                         gettext_noop("shutdown"),
                         GNUNET_TIME_UNIT_MINUTES,
                         NULL,
                         &process_stat,
                         NULL);

  ti_check_stat = GNUNET_SCHEDULER_add_delayed (sched, STAT_INTERVALL, &task_check_stat, NULL);
  return;
}

/**
 * Runs the test.
 *
 * @param cls closure
 * @param s scheduler to use
 * @param c configuration to use
 */
static void
run (void *cls,
     struct GNUNET_SCHEDULER_Handle *s,
     char *const *args,
     const char *cfgfile, const struct GNUNET_CONFIGURATION_Handle *c)
{
  unsigned long long tneigh;
  char *keyfile;
  char *libname;

  sched = s;
  cfg = c;

  /* parse configuration */
  if ((GNUNET_OK !=
       GNUNET_CONFIGURATION_get_value_number (c,
                                              "TRANSPORT",
                                              "NEIGHBOUR_LIMIT",
                                              &tneigh)) ||
      (GNUNET_OK !=
       GNUNET_CONFIGURATION_get_value_filename (c,
                                                "GNUNETD",
                                                "HOSTKEY", &keyfile)))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _("Transport service is lacking key configuration settings.  Exiting.\n"));
      GNUNET_SCHEDULER_shutdown (s);
      return;
    }

  pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-service-statistics",
                                 "gnunet-service-statistics",
                                 "-L", "DEBUG",
                                 "-c", "test_plugin_transport_data_http.conf", NULL);


  if ( pid == -1)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                     _("Failed to start service for `%s' http transport plugin test.\n"),
                     "statistics");
    GNUNET_SCHEDULER_shutdown (s);
    return;
  }

  stats = GNUNET_STATISTICS_create (sched, "http-transport", cfg);
  env.stats = stats;
  /*
  max_connect_per_transport = (uint32_t) tneigh;
  my_private_key = GNUNET_CRYPTO_rsa_key_create_from_file (keyfile);
  GNUNET_free (keyfile);

  if (my_private_key == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _
                  ("Transport service could not access hostkey.  Exiting.\n"));
      GNUNET_SCHEDULER_shutdown (s);
      return;
    }
  GNUNET_CRYPTO_rsa_key_get_public (my_private_key, &my_public_key);
  GNUNET_CRYPTO_hash (&my_public_key,
                      sizeof (my_public_key), &my_identity.hashPubKey);
  */
  /* load plugins... */
  setup_plugin_environment ();

  GNUNET_asprintf (&libname, "libgnunet_plugin_transport_http");

  api = GNUNET_PLUGIN_load (libname, &env);
  if (api != NULL )
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Loading http transport plugin `%s' was successful\n",libname);

  GNUNET_free (libname);
  if (api == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _("Failed to load http transport plugin\n"));
      fail = GNUNET_YES;
      shutdown_clean ();
      return;

    }
  fail = GNUNET_NO;

  char * test_message  = "Hello World!";
  size_t bs = 0;
  size_t size = strlen(test_message) +1;

  /* Testing to send */
  bs = api->send(NULL, &my_identity,test_message,size,0, TIMEOUT, NULL, NULL, 0, GNUNET_NO, NULL, NULL);
  GNUNET_assert ( bs == size);

  /* check statistics */
  ti_check_stat = GNUNET_SCHEDULER_add_now(sched, &task_check_stat, NULL);

  return;
}


/**
 * The main function for the transport service.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  static struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  int ret;
  char *const argv_prog[] = {
    "test_plugin_transport_http",
    "-c",
    "test_plugin_transport_data_http.conf",
    "-L",
#if VERBOSE
    "DEBUG",
#else
    "WARNING",
#endif
    NULL
  };
  GNUNET_log_setup ("test_plugin_transport_http",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  fail = GNUNET_YES;
  ret = (GNUNET_OK ==
         GNUNET_PROGRAM_run (5,
                             argv_prog,
                             "test_plugin_transport_http",
                             "testcase", options, &run, NULL)) ? fail : 1;
  GNUNET_DISK_directory_remove ("/tmp/test_plugin_transport_http");

  if (0 != PLIBC_KILL (pid, SIGTERM))
  {
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "kill");
    fail = 1;
  }
  return fail;
}

/* end of test_plugin_transport_http.c */
