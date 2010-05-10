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

#define PLUGIN libgnunet_plugin_transport_template

/**
 * How long until we give up on transmitting the message?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 30)

/**
 * How long until we give up on transmitting the message?
 */
#define STAT_INTERVALL GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 3)

/**
 * Our public key.
 */
/* static struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded my_public_key; */

/**
 * Our identity.
 */
static struct GNUNET_PeerIdentity my_identity;

#if 0
/**
 * Our private key.
 */
static struct GNUNET_CRYPTO_RsaPrivateKey *my_private_key;
#endif

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
 * ID of the task controlling the locking between two hostlist tests
 */
static GNUNET_SCHEDULER_TaskIdentifier ti_check_stat;

static unsigned int timeout_count;

/**
 * Did the test pass or fail?
 */
static int fail;

pid_t pid;

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
/*  if (stat_get_handle != NULL)
  {
    GNUNET_STATISTICS_get_cancel(stat_get_handle);
  }*/

  /* if ( NULL!=stats )GNUNET_STATISTICS_destroy (stats, GNUNET_YES); */
  if (ti_check_stat != GNUNET_SCHEDULER_NO_TASK)
    GNUNET_SCHEDULER_cancel(sched,ti_check_stat);
  ti_check_stat = GNUNET_SCHEDULER_NO_TASK;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Unloading http plugin\n");
  GNUNET_assert (NULL == GNUNET_PLUGIN_unload ("libgnunet_plugin_transport_http", api));
  
  GNUNET_SCHEDULER_shutdown(sched);
  /* FIXME: */ fail = GNUNET_NO;
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

#if 0
static int
process_stat (void *cls,
              const char *subsystem,
              const char *name,
              uint64_t value,
              int is_persistent)
{
  stat_get_handle = NULL;
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
#endif

#if 0
static void
cont_func (void *cls, int success)
{
  stat_get_handle = NULL;
}
#endif

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

  if ( timeout_count > 5 )
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Testcase timeout\n",  timeout_count);
    fail = GNUNET_YES;
    shutdown_clean();
    return;
  }
  timeout_count++;

/*  stat_get_handle = GNUNET_STATISTICS_get (stats,
                                           "http-transport",
                                           gettext_noop("shutdown"),
                                           GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 2),
                                           &cont_func,
                                           &process_stat,
                                           NULL);*/

  ti_check_stat = GNUNET_SCHEDULER_add_delayed (sched, STAT_INTERVALL, &task_check_stat, NULL);
  return;
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
  size_t retcode;

  /* in real-world cases, this would probably get this data differently
     as this fread() stuff is exactly what the library already would do
     by default internally */
  retcode = fread(ptr, size, nmemb, stream);

  fprintf(stderr, "*** We read %d bytes from file\n", (int) retcode);

  return retcode;
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
  char * libname;
  sched = s;
  cfg = c;

  /* settings up statistics */
/*  stats = GNUNET_STATISTICS_create (sched, "http-transport", cfg);
  if (NULL == stats)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                _("Failed to retrieve statistics handle\n"));
    fail = GNUNET_YES;
    shutdown_clean();
    return ;
  }*/

  /* load plugins... */
  setup_plugin_environment ();
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, _("Loading HTTP transport plugin `%s'\n"),"libgnunet_plugin_transport_http");
  GNUNET_asprintf (&libname, "libgnunet_plugin_transport_http");
  api = GNUNET_PLUGIN_load (libname, &env);
  GNUNET_free (libname);
  if (api == NULL)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                _("Failed to load transport plugin for http\n"));
    fail = GNUNET_YES;
    return;
  }

  ti_check_stat = GNUNET_SCHEDULER_add_now (sched, &task_check_stat, NULL);
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
/*  GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Starting statistics service\n");
  pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-service-statistics",
                                 "gnunet-service-statistics",
                                 "-L", "DEBUG",
                                 "-c", "test_plugin_transport_data_http.conf", NULL);

  fail = GNUNET_NO;
  if (pid==-1 )
  {
    GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Failed to start statistics service\n");
    fail = GNUNET_YES;
    return fail;
  }*/

  ret = (GNUNET_OK ==
         GNUNET_PROGRAM_run (5,
                             argv_prog,
                             "test_plugin_transport_http",
                             "testcase", options, &run, NULL)) ? GNUNET_NO : GNUNET_YES;
  GNUNET_DISK_directory_remove ("/tmp/test_plugin_transport_http");

/*  if (0 != PLIBC_KILL (pid, SIGTERM))
  {
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_DEBUG, "Failed to kill statistics service");
    fail = GNUNET_YES;
  }
  if (GNUNET_OS_process_wait(pid) != GNUNET_OK)
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "waitpid");
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Killed statistics service\n");*/
  return fail;
}

/* end of test_plugin_transport_http.c */
