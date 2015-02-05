/*
     This file is part of GNUnet.
     (C) 2011-2015 Christian Grothoff (and other contributing authors)

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
 * @file ats/gnunet-service-ats_connectivity.h
 * @brief ats service, interaction with 'connecivity' API
 * @author Matthias Wachs
 * @author Christian Grothoff
 *
 * TODO: will need API to query connectivity requests!
 */
#ifndef GNUNET_SERVICE_ATS_CONNECTIVITY_H
#define GNUNET_SERVICE_ATS_CONNECTIVITY_H


/**
 * Handle 'request address' messages from clients.
 *
 * @param cls unused, NULL
 * @param client client that sent the request
 * @param message the request message
 */
void
GAS_handle_request_address (void *cls,
                            struct GNUNET_SERVER_Client *client,
                            const struct GNUNET_MessageHeader *message);


/**
 * Cancel 'request address' messages from clients.
 *
 * @param cls unused, NULL
 * @param client client that sent the request
 * @param message the request message
 */
void
GAS_handle_request_address_cancel (void *cls,
                                   struct GNUNET_SERVER_Client *client,
                                   const struct GNUNET_MessageHeader *message);


/**
 * Unregister a client (which may have been a connectivity client,
 * but this is not assured).
 *
 * @param client handle of the (now dead) client
 */
void
GAS_connectivity_remove_client (struct GNUNET_SERVER_Client *client);


/**
 * Initialize connectivity subsystem.
 */
void
GAS_connectivity_init (void);


/**
 * Shutdown connectivity subsystem.
 */
void
GAS_connectivity_done (void);


#endif
/* end of gnunet-service-ats_connectivity.h */
