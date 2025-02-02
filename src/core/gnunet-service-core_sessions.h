/*
     This file is part of GNUnet.
     Copyright (C) 2009-2014 Christian Grothoff (and other contributing authors)

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
 * @file core/gnunet-service-core_neighbours.h
 * @brief code for managing of 'encrypted' sessions (key exchange done)
 * @author Christian Grothoff
 */
#ifndef GNUNET_SERVICE_CORE_SESSIONS_H
#define GNUNET_SERVICE_CORE_SESSIONS_H

#include "gnunet-service-core.h"
#include "gnunet-service-core_kx.h"


/**
 * Create a session, a key exchange was just completed.
 *
 * @param peer peer that is now connected
 * @param kx key exchange that completed
 */
void
GSC_SESSIONS_create (const struct GNUNET_PeerIdentity *peer,
                     struct GSC_KeyExchangeInfo *kx);


/**
 * The other peer has indicated that he 'lost' the session
 * (KX down), reinitialize the session on our end, in particular
 * this means to restart the typemap transmission.
 *
 * @param peer peer that is now connected
 */
void
GSC_SESSIONS_reinit (const struct GNUNET_PeerIdentity *peer);


/**
 * The other peer has confirmed receiving our type map,
 * check if it is current and if so, stop retransmitting it.
 *
 * @param peer peer that confirmed the type map
 * @param msg confirmation message we received
 */
void
GSC_SESSIONS_confirm_typemap (const struct GNUNET_PeerIdentity *peer,
                              const struct GNUNET_MessageHeader *msg);


/**
 * End the session with the given peer (we are no longer
 * connected).
 *
 * @param pid identity of peer to kill session with
 */
void
GSC_SESSIONS_end (const struct GNUNET_PeerIdentity *pid);


/**
 * Traffic is being solicited for the given peer.  This means that the
 * message queue on the transport-level (NEIGHBOURS subsystem) is now
 * empty and it is now OK to transmit another (non-control) message.
 *
 * @param pid identity of peer ready to receive data
 */
void
GSC_SESSIONS_solicit (const struct GNUNET_PeerIdentity *pid);


/**
 * Queue a request from a client for transmission to a particular peer.
 *
 * @param car request to queue; this handle is then shared between
 *         the caller (CLIENTS subsystem) and SESSIONS and must not
 *         be released by either until either 'GNUNET_SESSIONS_dequeue',
 *         or 'GNUNET_CLIENTS_failed'
 *         have been invoked on it
 */
void
GSC_SESSIONS_queue_request (struct GSC_ClientActiveRequest *car);


/**
 * Dequeue a request from a client from transmission to a particular peer.
 *
 * @param car request to dequeue; this handle will then be 'owned' by
 *        the caller (CLIENTS sysbsystem)
 */
void
GSC_SESSIONS_dequeue_request (struct GSC_ClientActiveRequest *car);


/**
 * Transmit a message to a particular peer.
 *
 * @param car original request that was queued and then solicited,
 *            ownership does not change (dequeue will be called soon).
 * @param msg message to transmit
 * @param cork is corking allowed?
 * @param priority how important is this message
 */
void
GSC_SESSIONS_transmit (struct GSC_ClientActiveRequest *car,
                       const struct GNUNET_MessageHeader *msg,
                       int cork,
                       enum GNUNET_CORE_Priority priority);


/**
 * Broadcast an updated typemap message to all neighbours.
 * Restarts the retransmissions until the typemaps are confirmed.
 *
 * @param msg message to transmit
 */
void
GSC_SESSIONS_broadcast_typemap (const struct GNUNET_MessageHeader *msg);


/**
 * We have a new client, notify it about all current sessions.
 *
 * @param client the new client
 */
void
GSC_SESSIONS_notify_client_about_sessions (struct GSC_Client *client);


/**
 * We've received a typemap message from a peer, update ours.
 * Notifies clients about the session.
 *
 * @param peer peer this is about
 * @param msg typemap update message
 */
void
GSC_SESSIONS_set_typemap (const struct GNUNET_PeerIdentity *peer,
                          const struct GNUNET_MessageHeader *msg);


/**
 * The given peer send a message of the specified type.  Make sure the
 * respective bit is set in its type-map and that clients are notified
 * about the session.
 *
 * @param peer peer this is about
 * @param type type of the message
 */
void
GSC_SESSIONS_add_to_typemap (const struct GNUNET_PeerIdentity *peer,
                             uint16_t type);


/**
 * Initialize sessions subsystem.
 */
void
GSC_SESSIONS_init (void);


/**
 * Shutdown sessions subsystem.
 */
void
GSC_SESSIONS_done (void);



#endif
