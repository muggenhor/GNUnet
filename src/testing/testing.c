/*
      This file is part of GNUnet
      (C) 2008, 2009 Christian Grothoff (and other contributing authors)

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
 * @file testing/testing.c
 * @brief convenience API for writing testcases for GNUnet
 *        Many testcases need to start and stop gnunetd,
 *        and this library is supposed to make that easier
 *        for TESTCASES.  Normal programs should always
 *        use functions from gnunet_{util,arm}_lib.h.  This API is
 *        ONLY for writing testcases!
 * @author Christian Grothoff
 *
 */
#include "platform.h"
#include "gnunet_arm_service.h"
#include "gnunet_core_service.h"
#include "gnunet_constants.h"
#include "gnunet_testing_lib.h"
#include "gnunet_transport_service.h"
#include "gnunet_hello_lib.h"

#define DEBUG_TESTING GNUNET_NO
#define DEBUG_TESTING_RECONNECT GNUNET_NO

/**
 * How long do we wait after starting gnunet-service-arm
 * for the core service to be alive?
 */
#define ARM_START_WAIT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 120)

/**
 * How many times are we willing to try to wait for "scp" or
 * "gnunet-service-arm" to complete (waitpid) before giving up?
 */
#define MAX_EXEC_WAIT_RUNS 250

static struct GNUNET_CORE_MessageHandler no_handlers[] = { {NULL, 0, 0} };

/**
 * Receive the HELLO from one peer, give it to the other
 * and ask them to connect.
 *
 * @param cls "struct ConnectContext"
 * @param message HELLO message of peer
 */
static void
process_hello (void *cls, const struct GNUNET_MessageHeader *message)
{
  struct GNUNET_TESTING_Daemon *daemon = cls;
  int msize;
  if (daemon == NULL)
    return;

  if (daemon->server != NULL)
    {
      GNUNET_CORE_disconnect(daemon->server);
      daemon->server = NULL;
    }

  GNUNET_assert (message != NULL);
  msize = ntohs(message->size);
  if (msize < 1)
    {
      return;
    }
  if (daemon->th != NULL)
    {
      GNUNET_TRANSPORT_get_hello_cancel(daemon->th, &process_hello, daemon);
    }
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Received `%s' from transport service of `%4s'\n",
              "HELLO", GNUNET_i2s (&daemon->id));
#endif



    {
      GNUNET_free_non_null(daemon->hello);
      daemon->hello = GNUNET_malloc(msize);
      memcpy(daemon->hello, message, msize);

      if (daemon->th != NULL)
        {
          GNUNET_TRANSPORT_disconnect(daemon->th);
          daemon->th = NULL;
        }
    }

}

/**
 * Function called after GNUNET_CORE_connect has succeeded
 * (or failed for good).  Note that the private key of the
 * peer is intentionally not exposed here; if you need it,
 * your process should try to read the private key file
 * directly (which should work if you are authorized...).
 *
 * @param cls closure
 * @param server handle to the server, NULL if we failed
 * @param my_identity ID of this peer, NULL if we failed
 * @param publicKey public key of this peer, NULL if we failed
 */
static void
testing_init (void *cls,
              struct GNUNET_CORE_Handle *server,
              const struct GNUNET_PeerIdentity *my_identity,
              const struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded *publicKey)
{
  struct GNUNET_TESTING_Daemon *d = cls;
  GNUNET_TESTING_NotifyDaemonRunning cb;

  GNUNET_assert (d->phase == SP_START_CORE);
  d->phase = SP_START_DONE;
  cb = d->cb;
  d->cb = NULL;
  if (server == NULL)
    {
      d->server = NULL;
      if (GNUNET_YES == d->dead)
        GNUNET_TESTING_daemon_stop (d, GNUNET_TIME_absolute_get_remaining(d->max_timeout), d->dead_cb, d->dead_cb_cls, GNUNET_YES, GNUNET_NO);
      else if (NULL != cb)
        cb (d->cb_cls, NULL, d->cfg, d,
            _("Failed to connect to core service\n"));
      return;
    }
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Successfully started peer `%4s'.\n", GNUNET_i2s (my_identity));
#endif
  d->id = *my_identity;
  d->shortname = strdup (GNUNET_i2s (my_identity));
  d->server = server;
  d->running = GNUNET_YES;
  if (GNUNET_YES == d->dead)
    GNUNET_TESTING_daemon_stop (d, GNUNET_TIME_absolute_get_remaining(d->max_timeout), d->dead_cb, d->dead_cb_cls, GNUNET_YES, GNUNET_NO);
  else if (NULL != cb)
    cb (d->cb_cls, my_identity, d->cfg, d, NULL);
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Successfully started peer `%4s'.\n", GNUNET_i2s (my_identity));
#endif


  d->th = GNUNET_TRANSPORT_connect (d->sched,
                                    d->cfg, d, NULL, NULL, NULL);
  if (d->th == NULL)
    {
      if (GNUNET_YES == d->dead)
        GNUNET_TESTING_daemon_stop (d, GNUNET_TIME_absolute_get_remaining(d->max_timeout), d->dead_cb, d->dead_cb_cls, GNUNET_YES, GNUNET_NO);
      else if (NULL != d->cb)
        d->cb (d->cb_cls, &d->id, d->cfg, d,
            _("Failed to connect to transport service!\n"));
      return;
    }

  GNUNET_TRANSPORT_get_hello (d->th, &process_hello, d);
}


/**
 * Finite-state machine for starting GNUnet.
 *
 * @param cls our "struct GNUNET_TESTING_Daemon"
 * @param tc unused
 */
static void
start_fsm (void *cls, 
	   const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_TESTING_Daemon *d = cls;
  GNUNET_TESTING_NotifyDaemonRunning cb;
  enum GNUNET_OS_ProcessStatusType type;
  unsigned long code;
  char *dst;
  int bytes_read;

#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Peer FSM is in phase %u.\n", d->phase);
#endif

  d->task = GNUNET_SCHEDULER_NO_TASK;
  switch (d->phase)
    {
    case SP_COPYING:
      /* confirm copying complete */
      if (GNUNET_OK != GNUNET_OS_process_status (d->pid, &type, &code))
        {
          if (GNUNET_TIME_absolute_get_remaining(d->max_timeout).value == 0)
            {
              cb = d->cb;
              d->cb = NULL;
              if (NULL != cb)
                cb (d->cb_cls,
                    NULL,
                    d->cfg, d, _("`scp' does not seem to terminate (timeout copying config).\n"));
              return;
            }
          /* wait some more */
          d->task
            = GNUNET_SCHEDULER_add_delayed (d->sched,
                                            GNUNET_CONSTANTS_EXEC_WAIT,
                                            &start_fsm, d);
          return;
        }
      if ((type != GNUNET_OS_PROCESS_EXITED) || (code != 0))
        {
          cb = d->cb;
          d->cb = NULL;
          if (NULL != cb)
            cb (d->cb_cls,
                NULL, d->cfg, d, _("`scp' did not complete cleanly.\n"));
          return;
        }
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Successfully copied configuration file.\n");
#endif
      d->phase = SP_COPIED;
      /* fall-through */
    case SP_COPIED:
      /* Start create hostkey process */
      d->pipe_stdout = GNUNET_DISK_pipe(GNUNET_NO);
      if (d->pipe_stdout == NULL)
        {
          cb = d->cb;
          d->cb = NULL;
          if (NULL != cb)
            cb (d->cb_cls,
                NULL,
                d->cfg,
                d,
                (NULL == d->hostname)
                ? _("Failed to create pipe for `gnunet-peerinfo' process.\n")
                : _("Failed to create pipe for `ssh' process.\n"));
          return;
        }
      if (NULL == d->hostname)
        {
#if DEBUG_TESTING
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "Starting `%s', with command `%s %s %s %s'.\n",
                      "gnunet-peerinfo", "gnunet-peerinfo", "-c", d->cfgfile,
                      "-sq");
#endif
          d->pid = GNUNET_OS_start_process (NULL, d->pipe_stdout, "gnunet-peerinfo",
                                            "gnunet-peerinfo",
                                            "-c", d->cfgfile,
                                            "-sq", NULL);
          GNUNET_DISK_pipe_close_end(d->pipe_stdout, GNUNET_DISK_PIPE_END_WRITE);
        }
      else
        {
          if (d->username != NULL)
            GNUNET_asprintf (&dst, "%s@%s", d->username, d->hostname);
          else
            dst = GNUNET_strdup (d->hostname);

#if DEBUG_TESTING
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "Starting `%s', with command `%s %s %s %s %s %s'.\n",
                      "gnunet-peerinfo", "ssh", dst, "gnunet-peerinfo", "-c", d->cfgfile,
                      "-sq");
#endif
          d->pid = GNUNET_OS_start_process (NULL, d->pipe_stdout, "ssh",
                                            "ssh",
                                            dst,
                                            "gnunet-peerinfo",
                                            "-c", d->cfgfile, "-sq", NULL);
          GNUNET_DISK_pipe_close_end(d->pipe_stdout, GNUNET_DISK_PIPE_END_WRITE);
          GNUNET_free (dst);
        }
      if (-1 == d->pid)
        {
          GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                      _("Could not start `%s' process to create hostkey.\n"),
                      (NULL == d->hostname) ? "gnunet-peerinfo" : "ssh");
          cb = d->cb;
          d->cb = NULL;
          if (NULL != cb)
            cb (d->cb_cls,
                NULL,
                d->cfg,
                d,
                (NULL == d->hostname)
                ? _("Failed to start `gnunet-peerinfo' process.\n")
                : _("Failed to start `ssh' process.\n"));
          GNUNET_DISK_pipe_close(d->pipe_stdout);
          return;
        }
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Started `%s', waiting for hostkey.\n",
                  "gnunet-peerinfo");
#endif
      d->phase = SP_HOSTKEY_CREATE;
      d->task
	= GNUNET_SCHEDULER_add_read_file (d->sched,
					  GNUNET_TIME_absolute_get_remaining(d->max_timeout),
					  GNUNET_DISK_pipe_handle(d->pipe_stdout, 
								  GNUNET_DISK_PIPE_END_READ),
					  &start_fsm, 
					  d);
      break;
    case SP_HOSTKEY_CREATE:
      bytes_read = GNUNET_DISK_file_read(GNUNET_DISK_pipe_handle(d->pipe_stdout, 
								 GNUNET_DISK_PIPE_END_READ),
					 &d->hostkeybuf[d->hostkeybufpos], 
					 sizeof(d->hostkeybuf) - d->hostkeybufpos);
      if (bytes_read > 0)
	d->hostkeybufpos += bytes_read;      

      if ( (d->hostkeybufpos < 104) &&
	   (bytes_read > 0) )
	{
	  /* keep reading */
          d->task
            = GNUNET_SCHEDULER_add_read_file (d->sched,
					      GNUNET_TIME_absolute_get_remaining(d->max_timeout),
					      GNUNET_DISK_pipe_handle(d->pipe_stdout, 
								      GNUNET_DISK_PIPE_END_READ),
					      &start_fsm, 
					      d);
          return;
	}
      d->hostkeybuf[103] = '\0';
      if ( (bytes_read < 0) ||
	   (GNUNET_OK != GNUNET_CRYPTO_hash_from_string (d->hostkeybuf,
							 &d->id.hashPubKey)) )
	{
	  /* error */
	  if (bytes_read < 0)
	    GNUNET_log(GNUNET_ERROR_TYPE_WARNING, 
		       _("Error reading from gnunet-peerinfo: %s\n"),
		       STRERROR (errno));
	  else
	    GNUNET_log(GNUNET_ERROR_TYPE_WARNING, 
		       _("Malformed output from gnunet-peerinfo!\n"));
          cb = d->cb;
          d->cb = NULL;
          GNUNET_DISK_pipe_close(d->pipe_stdout);
	  d->pipe_stdout = NULL;
	  (void) PLIBC_KILL (d->pid, SIGKILL);
	  GNUNET_break (GNUNET_OK == GNUNET_OS_process_wait (d->pid));
	  d->pid = 0;
          if (NULL != cb)
            cb (d->cb_cls,
                NULL,
                d->cfg,
                d,
                _("`Failed to get hostkey!\n"));
	  return;
	} 
      GNUNET_DISK_pipe_close(d->pipe_stdout);
      d->pipe_stdout = NULL;
      (void) PLIBC_KILL (d->pid, SIGKILL);
      GNUNET_break (GNUNET_OK == GNUNET_OS_process_wait (d->pid));
      d->pid = 0;
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Successfully got hostkey!\n");
#endif
      if (d->hostkey_callback != NULL)
        {
          d->hostkey_callback(d->hostkey_cls, &d->id, d, NULL);
          d->phase = SP_HOSTKEY_CREATED;
        }
      else
        {
          d->phase = SP_TOPOLOGY_SETUP;
        }
      /* Fall through */
    case SP_HOSTKEY_CREATED:
      /* wait for topology finished */
      if ((GNUNET_YES == d->dead) || (GNUNET_TIME_absolute_get_remaining(d->max_timeout).value == 0))
        {
          cb = d->cb;
          d->cb = NULL;
          if (NULL != cb)
            cb (d->cb_cls,
                NULL,
                d->cfg,
                d,
                _("`Failed while waiting for topology setup!\n"));
          return;
        }

      d->task
        = GNUNET_SCHEDULER_add_delayed (d->sched,
                                        GNUNET_CONSTANTS_EXEC_WAIT,
                                        &start_fsm, d);
      break;
    case SP_TOPOLOGY_SETUP:
      /* start GNUnet on remote host */
      if (NULL == d->hostname)
        {
#if DEBUG_TESTING
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "Starting `%s', with command `%s %s %s %s %s %s'.\n",
                      "gnunet-arm", "gnunet-arm", "-c", d->cfgfile,
                      "-L", "DEBUG",
                      "-s");
#endif
          d->pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-arm",
                                            "gnunet-arm",
                                            "-c", d->cfgfile,
#if DEBUG_TESTING
                                            "-L", "DEBUG",
#endif
                                            "-s", "-q", NULL);
        }
      else
        {
          if (d->username != NULL)
            GNUNET_asprintf (&dst, "%s@%s", d->username, d->hostname);
          else
            dst = GNUNET_strdup (d->hostname);

#if DEBUG_TESTING
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "Starting `%s', with command `%s %s %s %s %s %s %s %s'.\n",
                      "gnunet-arm", "ssh", dst, "gnunet-arm", "-c", d->cfgfile,
                      "-L", "DEBUG", "-s", "-q");
#endif
          d->pid = GNUNET_OS_start_process (NULL, NULL, "ssh",
                                            "ssh",
                                            dst,
                                            "gnunet-arm",
#if DEBUG_TESTING
                                            "-L", "DEBUG",
#endif
                                            "-c", d->cfgfile, "-s", "-q", NULL);
          GNUNET_free (dst);
        }
      if (-1 == d->pid)
        {
          GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                      _("Could not start `%s' process to start GNUnet.\n"),
                      (NULL == d->hostname) ? "gnunet-arm" : "ssh");
          cb = d->cb;
          d->cb = NULL;
          if (NULL != cb)
            cb (d->cb_cls,
                NULL,
                d->cfg,
                d,
                (NULL == d->hostname)
                ? _("Failed to start `gnunet-arm' process.\n")
                : _("Failed to start `ssh' process.\n"));
          return;
        }
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Started `%s', waiting for `%s' to be up.\n",
                  "gnunet-arm", "gnunet-service-core");
#endif
      d->phase = SP_START_ARMING;
      d->task
        = GNUNET_SCHEDULER_add_delayed (d->sched,
                                        GNUNET_CONSTANTS_EXEC_WAIT,
                                        &start_fsm, d);
      break;
    case SP_START_ARMING:
      if (GNUNET_OK != GNUNET_OS_process_status (d->pid, &type, &code))
        {
          if (GNUNET_TIME_absolute_get_remaining(d->max_timeout).value == 0)
            {
              cb = d->cb;
              d->cb = NULL;
              if (NULL != cb)
                cb (d->cb_cls,
                    NULL,
                    d->cfg,
                    d,
                    (NULL == d->hostname)
                    ? _("`gnunet-arm' does not seem to terminate.\n")
                    : _("`ssh' does not seem to terminate.\n"));
              return;
            }
          /* wait some more */
          d->task
            = GNUNET_SCHEDULER_add_delayed (d->sched,
                                            GNUNET_CONSTANTS_EXEC_WAIT,
                                            &start_fsm, d);
          return;
        }
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Successfully started `%s'.\n", "gnunet-arm");
#endif
      d->phase = SP_START_CORE;
      d->server = GNUNET_CORE_connect (d->sched,
                                       d->cfg,
                                       ARM_START_WAIT,
                                       d,
                                       &testing_init,
                                       NULL, NULL,
                                       NULL, GNUNET_NO,
                                       NULL, GNUNET_NO, no_handlers);
      break;
    case SP_START_CORE:
      GNUNET_break (0);
      break;
    case SP_START_DONE:
      GNUNET_break (0);
      break;
    case SP_SHUTDOWN_START:
      /* confirm copying complete */
      if (GNUNET_OK != GNUNET_OS_process_status (d->pid, &type, &code))
        {
          if (GNUNET_TIME_absolute_get_remaining(d->max_timeout).value == 0)
            {
              if (NULL != d->dead_cb)
                d->dead_cb (d->dead_cb_cls,
                            _("either `gnunet-arm' or `ssh' does not seem to terminate.\n"));
              if (d->th != NULL)
                {
                  GNUNET_TRANSPORT_get_hello_cancel(d->th, &process_hello, d);
                  GNUNET_TRANSPORT_disconnect(d->th);
                  d->th = NULL;
                }
              GNUNET_CONFIGURATION_destroy (d->cfg);
              GNUNET_free (d->cfgfile);
              GNUNET_free_non_null(d->hello);
              GNUNET_free_non_null (d->hostname);
              GNUNET_free_non_null (d->username);
              GNUNET_free_non_null (d->shortname);
              GNUNET_free (d);
              return;
            }
          /* wait some more */
          d->task
            = GNUNET_SCHEDULER_add_delayed (d->sched,
                                            GNUNET_CONSTANTS_EXEC_WAIT,
                                            &start_fsm, d);
          return;
        }
      if ((type != GNUNET_OS_PROCESS_EXITED) || (code != 0))
        {
          if (NULL != d->dead_cb)
            d->dead_cb (d->dead_cb_cls,
                        _("shutdown (either `gnunet-arm' or `ssh') did not complete cleanly.\n"));
          if (d->th != NULL)
            {
              GNUNET_TRANSPORT_get_hello_cancel(d->th, &process_hello, d);
              GNUNET_TRANSPORT_disconnect(d->th);
              d->th = NULL;
            }
          GNUNET_CONFIGURATION_destroy (d->cfg);
          GNUNET_free (d->cfgfile);
          GNUNET_free_non_null(d->hello);
          GNUNET_free_non_null (d->hostname);
          GNUNET_free_non_null (d->username);
          GNUNET_free_non_null (d->shortname);
          GNUNET_free (d);
          return;
        }
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Peer shutdown complete.\n");
#endif
      if (d->th != NULL)
        {
          GNUNET_TRANSPORT_get_hello_cancel(d->th, &process_hello, d);
          GNUNET_TRANSPORT_disconnect(d->th);
          d->th = NULL;
        }
      /* state clean up and notifications */
      if (d->churn == GNUNET_NO)
        {
          GNUNET_CONFIGURATION_destroy (d->cfg);
          GNUNET_free (d->cfgfile);
          GNUNET_free_non_null (d->hostname);
          GNUNET_free_non_null (d->username);
        }

      GNUNET_free_non_null(d->hello);
      d->hello = NULL;
      GNUNET_free_non_null (d->shortname);
      d->shortname = NULL;
      if (NULL != d->dead_cb)
        d->dead_cb (d->dead_cb_cls, NULL);

      if (d->churn == GNUNET_NO)
        GNUNET_free (d);

      break;
    case SP_CONFIG_UPDATE:
      /* confirm copying complete */
      if (GNUNET_OK != GNUNET_OS_process_status (d->pid, &type, &code))
        {
          if (GNUNET_TIME_absolute_get_remaining(d->max_timeout).value == 0) /* FIXME: config update should take timeout parameter! */
            {
              cb = d->cb;
              d->cb = NULL;
              if (NULL != cb)
                cb (d->cb_cls,
                    NULL,
                    d->cfg, d, _("`scp' does not seem to terminate.\n"));
              return;
            }
          /* wait some more */
          d->task
            = GNUNET_SCHEDULER_add_delayed (d->sched,
                                            GNUNET_CONSTANTS_EXEC_WAIT,
                                            &start_fsm, d);
          return;
        }
      if ((type != GNUNET_OS_PROCESS_EXITED) || (code != 0))
        {
          if (NULL != d->update_cb)
            d->update_cb (d->update_cb_cls,
                          _("`scp' did not complete cleanly.\n"));
          return;
        }
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Successfully copied configuration file.\n");
#endif
      if (NULL != d->update_cb)
        d->update_cb (d->update_cb_cls, NULL);
      d->phase = SP_START_DONE;
      break;
    }
}

/**
 * Continues GNUnet daemon startup when user wanted to be notified
 * once a hostkey was generated (for creating friends files, blacklists,
 * etc.).
 *
 * @param daemon the daemon to finish starting
 */
void
GNUNET_TESTING_daemon_continue_startup(struct GNUNET_TESTING_Daemon *daemon)
{
  GNUNET_assert(daemon->phase == SP_HOSTKEY_CREATED);
  daemon->phase = SP_TOPOLOGY_SETUP;
}


/**
 * Start a peer that has previously been stopped using the daemon_stop
 * call (and files weren't deleted and the allow restart flag)
 *
 * @param daemon the daemon to start (has been previously stopped)
 * @param timeout how long to wait for restart
 * @param cb the callback for notification when the peer is running
 * @param cb_cls closure for the callback
 */
void
GNUNET_TESTING_daemon_start_stopped (struct GNUNET_TESTING_Daemon *daemon,
                                     struct GNUNET_TIME_Relative timeout,
                                     GNUNET_TESTING_NotifyDaemonRunning cb,
                                     void *cb_cls)
{
  if (daemon->running == GNUNET_YES)
  {
    cb(cb_cls, &daemon->id, daemon->cfg, daemon, "Daemon already running, can't restart!");
    return;
  }

  daemon->cb = cb;
  daemon->cb_cls = cb_cls;
  daemon->phase = SP_TOPOLOGY_SETUP;
  daemon->max_timeout = GNUNET_TIME_relative_to_absolute(timeout);

  GNUNET_SCHEDULER_add_continuation (daemon->sched,
                                     &start_fsm,
                                     daemon,
                                     GNUNET_SCHEDULER_REASON_PREREQ_DONE);
}

/**
 * Starts a GNUnet daemon.  GNUnet must be installed on the target
 * system and available in the PATH.  The machine must furthermore be
 * reachable via "ssh" (unless the hostname is "NULL") without the
 * need to enter a password.
 *
 * @param sched scheduler to use
 * @param cfg configuration to use
 * @param timeout how long to wait starting up peers
 * @param hostname name of the machine where to run GNUnet
 *        (use NULL for localhost).
 * @param hostkey_callback function to call once the hostkey has been
 *        generated for this peer, but it hasn't yet been started
 *        (NULL to start immediately, otherwise waits on GNUNET_TESTING_daemon_continue_start)
 * @param hostkey_cls closure for hostkey callback
 * @param cb function to call once peer is up, or failed to start
 * @param cb_cls closure for cb
 * @return handle to the daemon (actual start will be completed asynchronously)
 */
struct GNUNET_TESTING_Daemon *
GNUNET_TESTING_daemon_start (struct GNUNET_SCHEDULER_Handle *sched,
                             const struct GNUNET_CONFIGURATION_Handle *cfg,
                             struct GNUNET_TIME_Relative timeout,
                             const char *hostname,
                             GNUNET_TESTING_NotifyHostkeyCreated hostkey_callback,
                             void *hostkey_cls,
                             GNUNET_TESTING_NotifyDaemonRunning cb,
                             void *cb_cls)
{
  struct GNUNET_TESTING_Daemon *ret;
  char *arg;
  char *username;

  ret = GNUNET_malloc (sizeof (struct GNUNET_TESTING_Daemon));
  ret->sched = sched;
  ret->hostname = (hostname == NULL) ? NULL : GNUNET_strdup (hostname);
  ret->cfgfile = GNUNET_DISK_mktemp ("gnunet-testing-config");
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Setting up peer with configuration file `%s'.\n",
              ret->cfgfile);
#endif
  if (NULL == ret->cfgfile)
    {
      GNUNET_free_non_null (ret->hostname);
      GNUNET_free (ret);
      return NULL;
    }
  ret->hostkey_callback = hostkey_callback;
  ret->hostkey_cls = hostkey_cls;
  ret->cb = cb;
  ret->cb_cls = cb_cls;
  ret->max_timeout = GNUNET_TIME_relative_to_absolute(timeout);
  ret->cfg = GNUNET_CONFIGURATION_dup (cfg);
  GNUNET_CONFIGURATION_set_value_string (ret->cfg,
                                         "PATHS",
                                         "DEFAULTCONFIG", ret->cfgfile);
  /* 1) write configuration to temporary file */
  if (GNUNET_OK != GNUNET_CONFIGURATION_write (ret->cfg, ret->cfgfile))
    {
      if (0 != UNLINK (ret->cfgfile))
        GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
                                  "unlink", ret->cfgfile);
      GNUNET_CONFIGURATION_destroy (ret->cfg);
      GNUNET_free_non_null (ret->hostname);
      GNUNET_free (ret->cfgfile);
      GNUNET_free (ret);
      return NULL;
    }
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (cfg,
                                             "TESTING",
                                             "USERNAME", &username))
    {
      if (NULL != getenv ("USER"))
        username = GNUNET_strdup (getenv ("USER"));
      else
        username = NULL;
    }
  ret->username = username;

  /* 2) copy file to remote host */
  if (NULL != hostname)
    {
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Copying configuration file to host `%s'.\n", hostname);
#endif
      ret->phase = SP_COPYING;
      if (NULL != username)
        GNUNET_asprintf (&arg, "%s@%s:%s", username, hostname, ret->cfgfile);
      else
        GNUNET_asprintf (&arg, "%s:%s", hostname, ret->cfgfile);
      ret->pid = GNUNET_OS_start_process (NULL, NULL, "scp",
                                          "scp", ret->cfgfile, arg, NULL);
      GNUNET_free (arg);
      if (-1 == ret->pid)
        {
          GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                      _
                      ("Could not start `%s' process to copy configuration file.\n"),
                      "scp");
          if (0 != UNLINK (ret->cfgfile))
            GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
                                      "unlink", ret->cfgfile);
          GNUNET_CONFIGURATION_destroy (ret->cfg);
          GNUNET_free_non_null (ret->hostname);
          GNUNET_free_non_null (ret->username);
          GNUNET_free (ret->cfgfile);
          GNUNET_free (ret);
          return NULL;
        }
      ret->task
        = GNUNET_SCHEDULER_add_delayed (sched,
                                        GNUNET_CONSTANTS_EXEC_WAIT,
                                        &start_fsm, ret);
      return ret;
    }
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "No need to copy configuration file since we are running locally.\n");
#endif
  ret->phase = SP_COPIED;
  GNUNET_SCHEDULER_add_continuation (sched,
                                     &start_fsm,
                                     ret,
                                     GNUNET_SCHEDULER_REASON_PREREQ_DONE);
  return ret;
}


/**
 * Restart (stop and start) a GNUnet daemon.
 *
 * @param d the daemon that should be restarted
 * @param cb function called once the daemon is (re)started
 * @param cb_cls closure for cb
 */
void
GNUNET_TESTING_daemon_restart (struct GNUNET_TESTING_Daemon *d,
                               GNUNET_TESTING_NotifyDaemonRunning cb, void *cb_cls)
{
  char *arg;
  char *del_arg;

  del_arg = NULL;
  if (NULL != d->cb)
    {
      d->dead = GNUNET_YES;
      return;
    }

  d->cb = cb;
  d->cb_cls = cb_cls;

  if (d->phase == SP_CONFIG_UPDATE)
    {
      GNUNET_SCHEDULER_cancel (d->sched, d->task);
      d->phase = SP_START_DONE;
    }
  if (d->server != NULL)
    {
      GNUNET_CORE_disconnect (d->server);
      d->server = NULL;
    }

  if (d->th != NULL)
    {
      GNUNET_TRANSPORT_get_hello_cancel(d->th, &process_hello, d);
      GNUNET_TRANSPORT_disconnect(d->th);
      d->th = NULL;
    }
  /* state clean up and notifications */
  GNUNET_free_non_null(d->hello);

#if DEBUG_TESTING
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                _("Terminating peer `%4s'\n"), GNUNET_i2s (&d->id));
#endif

   d->phase = SP_START_ARMING;

    /* Check if this is a local or remote process */
  if (NULL != d->hostname)
    {
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Stopping gnunet-arm with config `%s' on host `%s'.\n", d->cfgfile, d->hostname);
#endif

      if (d->username != NULL)
        GNUNET_asprintf (&arg, "%s@%s", d->username, d->hostname);
      else
        arg = GNUNET_strdup (d->hostname);

      d->pid = GNUNET_OS_start_process (NULL, NULL, "ssh", "ssh",
                                        arg, "gnunet-arm",
#if DEBUG_TESTING
                                        "-L", "DEBUG",
#endif
                                        "-c", d->cfgfile, "-e", "-r", NULL);
      /* Use -r to restart arm and all services */

      GNUNET_free (arg);
    }
  else
    {
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Stopping gnunet-arm with config `%s' locally.\n", d->cfgfile);
#endif
      d->pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-arm",
                                        "gnunet-arm",
#if DEBUG_TESTING
                                        "-L", "DEBUG",
#endif
                                        "-c", d->cfgfile, "-e", "-r", NULL);
    }

    GNUNET_free_non_null(del_arg);
    d->task
      = GNUNET_SCHEDULER_add_delayed (d->sched,
                                      GNUNET_CONSTANTS_EXEC_WAIT,
                                      &start_fsm, d);

}


/**
 * Stops a GNUnet daemon.
 *
 * @param d the daemon that should be stopped
 * @param timeout how long to wait for process for shutdown to complete
 * @param cb function called once the daemon was stopped
 * @param cb_cls closure for cb
 * @param delete_files GNUNET_YES to remove files, GNUNET_NO
 *        to leave them
 * @param allow_restart GNUNET_YES to restart peer later (using this API)
 *        GNUNET_NO to kill off and clean up for good
 */
void
GNUNET_TESTING_daemon_stop (struct GNUNET_TESTING_Daemon *d,
                            struct GNUNET_TIME_Relative timeout,
                            GNUNET_TESTING_NotifyCompletion cb, void *cb_cls,
                            int delete_files,
			    int allow_restart)
{
  char *arg;
  char *del_arg;
  d->dead_cb = cb;
  d->dead_cb_cls = cb_cls;

  if (NULL != d->cb)
    {
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                 _("Setting d->dead on peer `%4s'\n"), GNUNET_i2s (&d->id));
#endif
      d->dead = GNUNET_YES;
      return;
    }

  if ((d->running == GNUNET_NO) && (d->churn == GNUNET_YES)) /* Peer has already been stopped in churn context! */
    {
      /* Free what was left from churning! */
      GNUNET_assert(d->cfg != NULL);
      GNUNET_CONFIGURATION_destroy (d->cfg);
      if (delete_files == GNUNET_YES)
        {
          if (0 != UNLINK(d->cfgfile))
            {
              GNUNET_log_strerror(GNUNET_ERROR_TYPE_WARNING, "unlink");
            }
        }
      GNUNET_free (d->cfgfile);
      GNUNET_free_non_null (d->hostname);
      GNUNET_free_non_null (d->username);
      if (NULL != d->dead_cb)
        d->dead_cb (d->dead_cb_cls, NULL);
      return;
    }

  del_arg = NULL;
  if (delete_files == GNUNET_YES)
    {
      GNUNET_asprintf(&del_arg, "-d");
    }

  if (d->phase == SP_CONFIG_UPDATE)
    {
      GNUNET_SCHEDULER_cancel (d->sched, d->task);
      d->phase = SP_START_DONE;
    }
  if (d->server != NULL)
    {
      GNUNET_CORE_disconnect (d->server);
      d->server = NULL;
    }
  /* shutdown ARM process (will terminate others) */
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              _("Terminating peer `%4s'\n"), GNUNET_i2s (&d->id));
#endif
  d->phase = SP_SHUTDOWN_START;
  d->running = GNUNET_NO;
  if (allow_restart == GNUNET_YES)
    d->churn = GNUNET_YES;
  if (d->th != NULL)
    {
      GNUNET_TRANSPORT_get_hello_cancel(d->th, &process_hello, d);
      GNUNET_TRANSPORT_disconnect(d->th);
      d->th = NULL;
    }
  /* Check if this is a local or remote process */
  if (NULL != d->hostname)
    {
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Stopping gnunet-arm with config `%s' on host `%s'.\n", d->cfgfile, d->hostname);
#endif

      if (d->username != NULL)
        GNUNET_asprintf (&arg, "%s@%s", d->username, d->hostname);
      else
        arg = GNUNET_strdup (d->hostname);

      d->pid = GNUNET_OS_start_process (NULL, NULL, "ssh", "ssh",
                                        arg, "gnunet-arm",
#if DEBUG_TESTING
                                        "-L", "DEBUG",
#endif
                                        "-c", d->cfgfile, "-e", "-q", del_arg, NULL);
      /* Use -e to end arm, and -d to remove temp files */
      GNUNET_free (arg);
    }
  else
    {
#if DEBUG_TESTING
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Stopping gnunet-arm with config `%s' locally.\n", d->cfgfile);
#endif
      d->pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-arm",
                                        "gnunet-arm",
#if DEBUG_TESTING
                                        "-L", "DEBUG",
#endif
                                        "-c", d->cfgfile, "-e", "-q", del_arg, NULL);
    }

  GNUNET_free_non_null(del_arg);
  d->max_timeout = GNUNET_TIME_relative_to_absolute(timeout);
  d->task
    = GNUNET_SCHEDULER_add_now (d->sched,
                                &start_fsm, d);
}


/**
 * Changes the configuration of a GNUnet daemon.
 *
 * @param d the daemon that should be modified
 * @param cfg the new configuration for the daemon
 * @param cb function called once the configuration was changed
 * @param cb_cls closure for cb
 */
void
GNUNET_TESTING_daemon_reconfigure (struct GNUNET_TESTING_Daemon *d,
                                   struct GNUNET_CONFIGURATION_Handle *cfg,
                                   GNUNET_TESTING_NotifyCompletion cb,
                                   void *cb_cls)
{
  char *arg;

  if (d->phase != SP_START_DONE)
    {
      if (NULL != cb)
        cb (cb_cls,
            _
            ("Peer not yet running, can not change configuration at this point."));
      return;
    }

  /* 1) write configuration to temporary file */
  if (GNUNET_OK != GNUNET_CONFIGURATION_write (cfg, d->cfgfile))
    {
      if (NULL != cb)
        cb (cb_cls, _("Failed to write new configuration to disk."));
      return;
    }

  /* 2) copy file to remote host (if necessary) */
  if (NULL == d->hostname)
    {
      /* signal success */
      if (NULL != cb)
        cb (cb_cls, NULL);
      return;
    }
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Copying updated configuration file to remote host `%s'.\n",
              d->hostname);
#endif
  d->phase = SP_CONFIG_UPDATE;
  if (NULL != d->username)
    GNUNET_asprintf (&arg, "%s@%s:%s", d->username, d->hostname, d->cfgfile);
  else
    GNUNET_asprintf (&arg, "%s:%s", d->hostname, d->cfgfile);
  d->pid = GNUNET_OS_start_process (NULL, NULL, "scp", "scp", d->cfgfile, arg, NULL);
  GNUNET_free (arg);
  if (-1 == d->pid)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _
                  ("Could not start `%s' process to copy configuration file.\n"),
                  "scp");
      if (NULL != cb)
        cb (cb_cls, _("Failed to copy new configuration to remote machine."));
      d->phase = SP_START_DONE;
      return;
    }
  d->update_cb = cb;
  d->update_cb_cls = cb_cls;
  d->task
    = GNUNET_SCHEDULER_add_delayed (d->sched,
                                    GNUNET_CONSTANTS_EXEC_WAIT,
                                    &start_fsm, d);
}


/**
 * Data kept for each pair of peers that we try
 * to connect.
 */
struct ConnectContext
{
  /**
   * Testing handle to the first daemon.
   */
  struct GNUNET_TESTING_Daemon *d1;

  /**
   * Handle to core of first daemon (to check connect)
   */
  struct GNUNET_CORE_Handle * d1core;

  /**
   * Testing handle to the second daemon.
   */
  struct GNUNET_TESTING_Daemon *d2;

  /**
   * Transport handle to the second daemon.
   */
  struct GNUNET_TRANSPORT_Handle *d2th;

  /**
   * Function to call once we are done (or have timed out).
   */
  GNUNET_TESTING_NotifyConnection cb;

  /**
   * Closure for "nb".
   */
  void *cb_cls;

  /**
   * When should this operation be complete (or we must trigger
   * a timeout).
   */
  struct GNUNET_TIME_Absolute timeout;

  /**
   * The relative timeout from whence this connect attempt was
   * started.  Allows for reconnect attempts.
   */
  struct GNUNET_TIME_Relative relative_timeout;

  /**
   * Maximum number of connect attempts, will retry connection
   * this number of times on failures.
   */
  unsigned int max_connect_attempts;

  /**
   * Hello timeout task
   */
  GNUNET_SCHEDULER_TaskIdentifier hello_send_task;

  /**
   * Connect timeout task
   */
  GNUNET_SCHEDULER_TaskIdentifier timeout_task;

  /**
   * When should this operation be complete (or we must trigger
   * a timeout).
   */
  struct GNUNET_TIME_Relative timeout_hello;

  /**
   * Was the connection attempt successful?
   */
  int connected;

  /**
   * The distance between the two connected peers
   */
  uint32_t distance;
};


/** Forward declaration **/
static void
reattempt_daemons_connect(void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc);


/**
 * Notify callback about success or failure of the attempt
 * to connect the two peers
 *
 * @param cls our "struct ConnectContext" (freed)
 * @param tc reason tells us if we succeeded or failed
 */
static void
notify_connect_result (void *cls,
                       const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct ConnectContext *ctx = cls;
  struct GNUNET_TIME_Relative remaining;

  ctx->timeout_task = GNUNET_SCHEDULER_NO_TASK;
  if (ctx->hello_send_task != GNUNET_SCHEDULER_NO_TASK)
    {
      GNUNET_SCHEDULER_cancel(ctx->d1->sched, ctx->hello_send_task);
      ctx->hello_send_task = GNUNET_SCHEDULER_NO_TASK;
    }
  if (tc->reason == GNUNET_SCHEDULER_REASON_SHUTDOWN)
    {
      if (ctx->d2th != NULL)
        GNUNET_TRANSPORT_disconnect (ctx->d2th);
      ctx->d2th = NULL;
      if (ctx->d1core != NULL)
        GNUNET_CORE_disconnect (ctx->d1core);

      ctx->d1core = NULL;
      GNUNET_free (ctx);
      return;
    }

  remaining = GNUNET_TIME_absolute_get_remaining(ctx->timeout);

  if (ctx->connected == GNUNET_YES)
    {
      if (ctx->cb != NULL)
        {
          ctx->cb (ctx->cb_cls, &ctx->d1->id, &ctx->d2->id, ctx->distance, ctx->d1->cfg,
                   ctx->d2->cfg, ctx->d1, ctx->d2, NULL);
        }
    }
  else if (remaining.value > 0)
    {
      if (ctx->d1core != NULL)
        {
          GNUNET_CORE_disconnect(ctx->d1core);
          ctx->d1core = NULL;
        }

      if (ctx->d2th != NULL)
        {
          GNUNET_TRANSPORT_disconnect(ctx->d2th);
          ctx->d2th = NULL;
        }
      GNUNET_SCHEDULER_add_now(ctx->d1->sched, &reattempt_daemons_connect, ctx);
      return;
    }
  else
    {
      if (ctx->cb != NULL)
        {
          ctx->cb (ctx->cb_cls, &ctx->d1->id, &ctx->d2->id, 0, ctx->d1->cfg,
                   ctx->d2->cfg, ctx->d1, ctx->d2,
                   _("Peers failed to connect"));
        }
    }

  GNUNET_TRANSPORT_disconnect (ctx->d2th);
  ctx->d2th = NULL;
  GNUNET_CORE_disconnect (ctx->d1core);
  ctx->d1core = NULL;
  GNUNET_free (ctx);
}


/**
 * Success, connection is up.  Signal client our success.
 *
 * @param cls our "struct ConnectContext"
 * @param peer identity of the peer that has connected
 * @param latency the round trip latency of the connection to this peer
 * @param distance distance the transport level distance to this peer
 *
 */
static void
connect_notify (void *cls, const struct GNUNET_PeerIdentity * peer, struct GNUNET_TIME_Relative latency,
                uint32_t distance)
{
  struct ConnectContext *ctx = cls;

  if (memcmp(&ctx->d2->id, peer, sizeof(struct GNUNET_PeerIdentity)) == 0)
    {
      ctx->connected = GNUNET_YES;
      ctx->distance = distance;
      GNUNET_SCHEDULER_cancel(ctx->d1->sched, ctx->timeout_task);
      ctx->timeout_task = GNUNET_SCHEDULER_add_now (ctx->d1->sched,
						    &notify_connect_result,
						    ctx);
    }

}

static void
send_hello(void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct ConnectContext *ctx = cls;

  ctx->hello_send_task = GNUNET_SCHEDULER_NO_TASK;
  if (tc->reason == GNUNET_SCHEDULER_REASON_SHUTDOWN)
    return;
  if (ctx->d1->hello != NULL)
    {
      GNUNET_TRANSPORT_offer_hello (ctx->d2th, GNUNET_HELLO_get_header(ctx->d1->hello));
      ctx->timeout_hello = GNUNET_TIME_relative_add(ctx->timeout_hello,
						    GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_MILLISECONDS,
										  500));
    }
  ctx->hello_send_task = GNUNET_SCHEDULER_add_delayed(ctx->d1->sched,
						      ctx->timeout_hello,
						      &send_hello, ctx);
}

/**
 * Establish a connection between two GNUnet daemons.
 *
 * @param d1 handle for the first daemon
 * @param d2 handle for the second daemon
 * @param timeout how long is the connection attempt
 *        allowed to take?
 * @param max_connect_attempts how many times should we try to reconnect
 *        (within timeout)
 * @param cb function to call at the end
 * @param cb_cls closure for cb
 */
void
GNUNET_TESTING_daemons_connect (struct GNUNET_TESTING_Daemon *d1,
                                struct GNUNET_TESTING_Daemon *d2,
                                struct GNUNET_TIME_Relative timeout,
                                unsigned int max_connect_attempts,
                                GNUNET_TESTING_NotifyConnection cb,
                                void *cb_cls)
{
  struct ConnectContext *ctx;

  if ((d1->running == GNUNET_NO) || (d2->running == GNUNET_NO))
    {
      if (NULL != cb)
        cb (cb_cls, &d1->id, &d2->id, 0, d1->cfg, d2->cfg, d1, d2,
            _("Peers are not fully running yet, can not connect!\n"));
      return;
    }
  ctx = GNUNET_malloc (sizeof (struct ConnectContext));
  ctx->d1 = d1;
  ctx->d2 = d2;
  ctx->timeout = GNUNET_TIME_relative_to_absolute (timeout);
  ctx->timeout_hello = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_MILLISECONDS, 500);
  ctx->relative_timeout = timeout;
  ctx->cb = cb;
  ctx->cb_cls = cb_cls;
  ctx->max_connect_attempts = max_connect_attempts;
  ctx->connected = GNUNET_NO;
#if DEBUG_TESTING
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Asked to connect peer %s to peer %s\n",
              d1->shortname, d2->shortname);
#endif

  ctx->d1core = GNUNET_CORE_connect (d1->sched,
                                     d1->cfg,
                                     timeout,
                                     ctx,
                                     NULL,
                                     &connect_notify, NULL,
                                     NULL, GNUNET_NO,
                                     NULL, GNUNET_NO, no_handlers);
  if (ctx->d1core == NULL)
    {
      GNUNET_free (ctx);
      if (NULL != cb)
        cb (cb_cls, &d1->id, &d2->id, 0, d1->cfg, d2->cfg, d1, d2,
            _("Failed to connect to core service of first peer!\n"));
      return;
    }

#if DEBUG_TESTING > 2
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Asked to connect peer %s to peer %s\n",
              d1->shortname, d2->shortname);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Connecting to transport service of peer %s\n", d2->shortname);

#endif

  ctx->d2th = GNUNET_TRANSPORT_connect (d2->sched,
                                        d2->cfg, d2, NULL, NULL, NULL);
  if (ctx->d2th == NULL)
    {
      GNUNET_CORE_disconnect(ctx->d1core);
      GNUNET_free (ctx);
      if (NULL != cb)
        cb (cb_cls, &d1->id, &d2->id, 0, d1->cfg, d2->cfg, d1, d2,
            _("Failed to connect to transport service!\n"));
      return;
    }

  ctx->timeout_task = GNUNET_SCHEDULER_add_delayed (d1->sched,
                                                    GNUNET_TIME_relative_divide(ctx->relative_timeout, 
										max_connect_attempts), 
                                                    &notify_connect_result, ctx);

  ctx->hello_send_task = GNUNET_SCHEDULER_add_now(ctx->d1->sched, &send_hello, ctx);
}

static void
reattempt_daemons_connect (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{

  struct ConnectContext *ctx = cls;
  if (tc->reason == GNUNET_SCHEDULER_REASON_SHUTDOWN)
    {
      return;
    }
#if DEBUG_TESTING_RECONNECT
  GNUNET_log(GNUNET_ERROR_TYPE_WARNING, "re-attempting connect of peer %s to peer %s\n",
              ctx->d1->shortname, ctx->d2->shortname);
#endif

  GNUNET_assert(ctx->d1core == NULL);

  ctx->d1core = GNUNET_CORE_connect (ctx->d1->sched,
                                     ctx->d1->cfg,
                                     GNUNET_TIME_absolute_get_remaining(ctx->timeout),
                                     ctx,
                                     NULL,
                                     &connect_notify, NULL,
                                     NULL, GNUNET_NO,
                                     NULL, GNUNET_NO, no_handlers);
  if (ctx->d1core == NULL)
    {
      if (NULL != ctx->cb)
        ctx->cb (ctx->cb_cls, &ctx->d1->id, &ctx->d2->id, 0, ctx->d1->cfg, ctx->d2->cfg, ctx->d1, ctx->d2,
                 _("Failed to connect to core service of first peer!\n"));
      GNUNET_free (ctx);
      return;
    }

  ctx->d2th = GNUNET_TRANSPORT_connect (ctx->d2->sched,
                                        ctx->d2->cfg, ctx->d2, NULL, NULL, NULL);
  if (ctx->d2th == NULL)
    {
      GNUNET_CORE_disconnect(ctx->d1core);
      GNUNET_free (ctx);
      if (NULL != ctx->cb)
        ctx->cb (ctx->cb_cls, &ctx->d1->id, &ctx->d2->id, 0, ctx->d1->cfg, ctx->d2->cfg, ctx->d1, ctx->d2,
            _("Failed to connect to transport service!\n"));
      return;
    }

  ctx->timeout_task = GNUNET_SCHEDULER_add_delayed (ctx->d1->sched,
                                                    GNUNET_TIME_relative_divide(ctx->relative_timeout, ctx->max_connect_attempts),
                                                    &notify_connect_result, ctx);

  ctx->hello_send_task = GNUNET_SCHEDULER_add_now(ctx->d1->sched, &send_hello, ctx);
}

/* end of testing.c */
