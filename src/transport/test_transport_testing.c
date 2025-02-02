/*
     This file is part of GNUnet.
     Copyright (C) 2009, 2010 Christian Grothoff (and other contributing authors)

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
 * @file transport/test_transport_testing.c
 * @brief base test case for transport implementations
 *
 * This test case serves as a base for tcp, udp, and udp-nat
 * transport test cases.  Based on the executable being run
 * the correct test case will be performed.  Conservation of
 * C code apparently.
 */
#include "platform.h"
#include "gnunet_transport_service.h"
#include "transport-testing.h"
/**
 * How long until we give up on transmitting the message?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 30)

static struct GNUNET_SCHEDULER_Task * timeout_task;

static struct PeerContext *p1;

static struct PeerContext *p2;

static struct GNUNET_TRANSPORT_TESTING_ConnectRequest * cc;

struct GNUNET_TRANSPORT_TESTING_handle *tth;

static int connected = GNUNET_NO;

static int ret = 0;


static void
end ()
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Stopping peers\n");

  if (timeout_task != NULL)
    GNUNET_SCHEDULER_cancel (timeout_task);

  GNUNET_TRANSPORT_TESTING_stop_peer (tth, p1);
  GNUNET_TRANSPORT_TESTING_stop_peer (tth, p2);

  GNUNET_TRANSPORT_TESTING_done (tth);
}


static void
end_badly ()
{
  timeout_task = NULL;
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, 
	      "Timeout! Stopping peers\n");

  if (NULL != cc)
  {
    GNUNET_TRANSPORT_TESTING_connect_peers_cancel (tth, cc);
    cc = NULL;
  }

  if (p1 != NULL)
    GNUNET_TRANSPORT_TESTING_stop_peer (tth, p1);
  if (p2 != NULL)
    GNUNET_TRANSPORT_TESTING_stop_peer (tth, p2);

  if (NULL != tth)
    GNUNET_TRANSPORT_TESTING_done (tth);

  ret = GNUNET_SYSERR;
}


static void
testing_connect_cb (struct PeerContext *p1, 
		    struct PeerContext *p2, 
		    void *cls)
{
  char *ps = GNUNET_strdup (GNUNET_i2s (&p1->id));

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Peer %u (`%4s') connected to peer %u (`%s')!\n", p1->no, ps,
              p2->no, GNUNET_i2s (&p2->id));
  GNUNET_free (ps);
  GNUNET_SCHEDULER_add_now (&end, NULL);
}


static void
notify_connect (void *cls,
		const struct GNUNET_PeerIdentity *peer)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Peer `%s' connected \n",
              GNUNET_i2s (peer));
  connected++;
}


static void
notify_disconnect (void *cls, 
		   const struct GNUNET_PeerIdentity *peer)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Peer `%s' disconnected \n",
              GNUNET_i2s (peer));
}


static void
notify_receive (void *cls, 
		const struct GNUNET_PeerIdentity *peer,
                const struct GNUNET_MessageHeader *message)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Receiving\n");
}


static void
start_cb (struct PeerContext *p, void *cls)
{
  static int started;

  started++;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, 
	      "Peer %u (`%s') started\n", p->no,
              GNUNET_i2s (&p->id));

  if (started != 2)
    return;

  char *sender_c = GNUNET_strdup (GNUNET_i2s (&p1->id));

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Test tries to connect peer %u (`%s') -> peer %u (`%s')\n",
              p1->no, sender_c, p2->no, GNUNET_i2s (&p2->id));
  GNUNET_free (sender_c);

  cc = GNUNET_TRANSPORT_TESTING_connect_peers (tth, p1, p2, 
					       &testing_connect_cb,
                                               NULL);
}


static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  tth = GNUNET_TRANSPORT_TESTING_init ();

  timeout_task =
      GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_MINUTES, &end_badly, NULL);

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Starting peer\n");
  p1 = GNUNET_TRANSPORT_TESTING_start_peer (tth,
                                            "test_transport_api_tcp_peer1.conf",
                                            1, &notify_receive, &notify_connect,
                                            &notify_disconnect, &start_cb, p1);

  p2 = GNUNET_TRANSPORT_TESTING_start_peer (tth,
                                            "test_transport_api_tcp_peer2.conf",
                                            2, &notify_receive, &notify_connect,
                                            &notify_disconnect, &start_cb, p2);

  if (p1 == NULL)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Peer1 was not started successfully\n");
    GNUNET_SCHEDULER_shutdown ();
  }
  if (p2 == NULL)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Peer2 was not started successfully\n");
    GNUNET_SCHEDULER_shutdown ();
  }
}


int
main (int argc, char *argv[])
{
  char *const argv_1[] = { 
    "test_transport_testing",
    "-c",
    "test_transport_api_data.conf",
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };

  GNUNET_log_setup ("test_transport_testing",
                    "WARNING",
                    NULL);
  GNUNET_PROGRAM_run ((sizeof (argv_1) / sizeof (char *)) - 1, argv_1,
                      "test_transport_testing", "nohelp", options, 
		      &run, &ret);

  return ret;
}

/* end of test_transport_testing.c */
