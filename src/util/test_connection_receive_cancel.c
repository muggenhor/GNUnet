/*
     This file is part of GNUnet.
     Copyright (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file util/test_connection_receive_cancel.c
 * @brief tests for connection.c
 */
#include "platform.h"
#include "gnunet_util_lib.h"

#define PORT 12435


static struct GNUNET_CONNECTION_Handle *csock;

static struct GNUNET_CONNECTION_Handle *asock;

static struct GNUNET_CONNECTION_Handle *lsock;

static struct GNUNET_NETWORK_Handle *ls;

static struct GNUNET_CONFIGURATION_Handle *cfg;


/**
 * Create and initialize a listen socket for the server.
 *
 * @return NULL on error, otherwise the listen socket
 */
static struct GNUNET_NETWORK_Handle *
open_listen_socket ()
{
  const static int on = 1;
  struct sockaddr_in sa;
  struct GNUNET_NETWORK_Handle *desc;

  memset (&sa, 0, sizeof (sa));
#if HAVE_SOCKADDR_IN_SIN_LEN
  sa.sin_len = sizeof (sa);
#endif
  sa.sin_family = AF_INET;
  sa.sin_port = htons (PORT);
  desc = GNUNET_NETWORK_socket_create (AF_INET, SOCK_STREAM, 0);
  GNUNET_assert (desc != NULL);
  if (GNUNET_NETWORK_socket_setsockopt
      (desc, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on)) != GNUNET_OK)
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, "setsockopt");
  GNUNET_assert (GNUNET_OK ==
		 GNUNET_NETWORK_socket_bind (desc, (const struct sockaddr *) &sa,
					     sizeof (sa)));
  GNUNET_NETWORK_socket_listen (desc, 5);
  return desc;
}



static void
dead_receive (void *cls, const void *buf, size_t available,
              const struct sockaddr *addr, socklen_t addrlen, int errCode)
{
  GNUNET_assert (0);
}


static void
run_accept_cancel (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  asock = GNUNET_CONNECTION_create_from_accept (NULL, NULL, ls);
  GNUNET_assert (asock != NULL);
  GNUNET_assert (GNUNET_YES == GNUNET_CONNECTION_check (asock));
  GNUNET_CONNECTION_destroy (lsock);
  GNUNET_CONNECTION_receive (asock, 1024,
                             GNUNET_TIME_relative_multiply
                             (GNUNET_TIME_UNIT_SECONDS, 5), &dead_receive, cls);
}


static void
receive_cancel_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  int *ok = cls;

  GNUNET_CONNECTION_receive_cancel (asock);
  GNUNET_CONNECTION_destroy (csock);
  GNUNET_CONNECTION_destroy (asock);
  *ok = 0;
}



static void
task_receive_cancel (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  ls = open_listen_socket ();
  lsock = GNUNET_CONNECTION_create_from_existing (ls);
  GNUNET_assert (lsock != NULL);
  csock = GNUNET_CONNECTION_create_from_connect (cfg, "localhost", PORT);
  GNUNET_assert (csock != NULL);
  GNUNET_SCHEDULER_add_read_net (GNUNET_TIME_UNIT_FOREVER_REL, ls,
                                 &run_accept_cancel, cls);
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_SECONDS, &receive_cancel_task,
                                cls);
}



/**
 * Main method, starts scheduler with task_timeout.
 */
static int
check_receive_cancel ()
{
  int ok;

  ok = 1;
  cfg = GNUNET_CONFIGURATION_create ();
  GNUNET_CONFIGURATION_set_value_string (cfg, "resolver", "HOSTNAME",
                                         "localhost");
  GNUNET_SCHEDULER_run (&task_receive_cancel, &ok);
  GNUNET_CONFIGURATION_destroy (cfg);
  return ok;
}


int
main (int argc, char *argv[])
{
  int ret = 0;

  GNUNET_log_setup ("test_connection_receive_cancel", "WARNING", NULL);
  ret += check_receive_cancel ();

  return ret;
}

/* end of test_connection_receive_cancel.c */
