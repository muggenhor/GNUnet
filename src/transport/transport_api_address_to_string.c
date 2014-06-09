/*
     This file is part of GNUnet.
     (C) 2009, 2010 Christian Grothoff (and other contributing authors)

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
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_arm_service.h"
#include "gnunet_hello_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_transport_service.h"
#include "transport.h"

/**
 * Context for the address lookup.
 */
struct GNUNET_TRANSPORT_AddressToStringContext
{
  /**
   * Function to call with the human-readable address.
   */
  GNUNET_TRANSPORT_AddressToStringCallback cb;

  /**
   * Closure for cb.
   */
  void *cb_cls;

  /**
   * Connection to the service.
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * When should this operation time out?
   */
  struct GNUNET_TIME_Absolute timeout;
};


/**
 * Function called with responses from the service.
 *
 * @param cls our `struct GNUNET_TRANSPORT_AddressToStringContext *`
 * @param msg NULL on timeout or error, otherwise presumably a
 *        message with the human-readable address
 */
static void
address_response_processor (void *cls,
                            const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_TRANSPORT_AddressToStringContext *alucb = cls;
  struct AddressToStringResultMessage *atsm;
  const char *address;
  uint16_t size;
  uint32_t result;
  uint32_t addr_len;
  char *empty_str = "";

  if (NULL == msg)
  {
    alucb->cb (alucb->cb_cls,
               NULL,
               GNUNET_OK);
    GNUNET_CLIENT_disconnect (alucb->client);
    GNUNET_free (alucb);
    return;
  }
  GNUNET_break (ntohs (msg->type) ==
                GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_TO_STRING_REPLY);

  size = ntohs (msg->size);
  if (size < sizeof (struct AddressToStringResultMessage))
  {
    alucb->cb (alucb->cb_cls, NULL, GNUNET_OK);
    GNUNET_CLIENT_disconnect (alucb->client);
    GNUNET_free (alucb);
    return;
  }
  atsm = (struct AddressToStringResultMessage *) msg;

  result = ntohl (atsm->res);
  addr_len = ntohl (atsm->addr_len);

  if (size == (sizeof (struct AddressToStringResultMessage)))
  {
    /* done, success depends on result */
    alucb->cb (alucb->cb_cls, NULL, result);
    GNUNET_CLIENT_disconnect (alucb->client);
    GNUNET_free (alucb);
    return;
  }

  if (GNUNET_NO == result)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Client %p failed to resolve address \n",
        alucb->client);
    GNUNET_break (0);
    alucb->cb (alucb->cb_cls, empty_str, GNUNET_SYSERR);

    /* expect more replies */
    GNUNET_CLIENT_receive (alucb->client, &address_response_processor, alucb,
                           GNUNET_TIME_absolute_get_remaining (alucb->timeout));
    return;
  }

  address = (const char *) &atsm[1];
  if ( (addr_len > (size - (sizeof (struct AddressToStringResultMessage)))) ||
       (address[addr_len -1] != '\0') )
  {
    /* invalid reply */
    GNUNET_break (0);
    alucb->cb (alucb->cb_cls, NULL, GNUNET_SYSERR);
    GNUNET_CLIENT_disconnect (alucb->client);
    GNUNET_free (alucb);
    return;
  }

  /* expect more replies */
  GNUNET_CLIENT_receive (alucb->client, &address_response_processor, alucb,
                         GNUNET_TIME_absolute_get_remaining (alucb->timeout));
  alucb->cb (alucb->cb_cls, address, GNUNET_OK);
}


/**
 * Convert a binary address into a human readable address.
 *
 * @param cfg configuration to use
 * @param address address to convert (binary format)
 * @param numeric should (IP) addresses be displayed in numeric form
 *                (otherwise do reverse DNS lookup)
 * @param timeout how long is the lookup allowed to take at most
 * @param aluc function to call with the results
 * @param aluc_cls closure for aluc
 * @return handle to cancel the operation, NULL on error
 */
struct GNUNET_TRANSPORT_AddressToStringContext *
GNUNET_TRANSPORT_address_to_string (const struct GNUNET_CONFIGURATION_Handle
                                    *cfg,
                                    const struct GNUNET_HELLO_Address *address,
                                    int numeric,
                                    struct GNUNET_TIME_Relative timeout,
                                    GNUNET_TRANSPORT_AddressToStringCallback
                                    aluc, void *aluc_cls)
{
  size_t len;
  size_t alen;
  size_t slen;
  struct AddressLookupMessage *msg;
  struct GNUNET_TRANSPORT_AddressToStringContext *alc;
  struct GNUNET_CLIENT_Connection *client;
  char *addrbuf;

  GNUNET_assert (address != NULL);
  alen = address->address_length;
  slen = strlen (address->transport_name) + 1;
  len = sizeof (struct AddressLookupMessage) + alen + slen;
  if (len >= GNUNET_SERVER_MAX_MESSAGE_SIZE)
  {
    GNUNET_break (0);
    return NULL;
  }
  client = GNUNET_CLIENT_connect ("transport", cfg);
  if (NULL == client)
    return NULL;
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Client %p tries to resolve for peer `%s'address len %u \n",
      client, GNUNET_i2s (&address->peer), address->address_length);

  msg = GNUNET_malloc (len);
  msg->header.size = htons (len);
  msg->header.type = htons (GNUNET_MESSAGE_TYPE_TRANSPORT_ADDRESS_TO_STRING);
  msg->numeric_only = htons ((int16_t) numeric);
  msg->addrlen = htons ((uint16_t) alen);
  msg->timeout = GNUNET_TIME_relative_hton (timeout);
  addrbuf = (char *) &msg[1];
  memcpy (addrbuf, address->address, alen);
  memcpy (&addrbuf[alen], address->transport_name, slen);

  alc = GNUNET_new (struct GNUNET_TRANSPORT_AddressToStringContext);
  alc->cb = aluc;
  alc->cb_cls = aluc_cls;
  alc->timeout = GNUNET_TIME_relative_to_absolute (timeout);
  alc->client = client;
  GNUNET_assert (GNUNET_OK ==
                 GNUNET_CLIENT_transmit_and_get_response (client, &msg->header,
                                                          timeout, GNUNET_YES,
                                                          &address_response_processor,
                                                          alc));
  GNUNET_free (msg);
  return alc;
}


/**
 * Cancel request for address conversion.
 *
 * @param pic the context handle
 */
void
GNUNET_TRANSPORT_address_to_string_cancel (struct
                                           GNUNET_TRANSPORT_AddressToStringContext
                                           *pic)
{
  GNUNET_CLIENT_disconnect (pic->client);
  GNUNET_free (pic);
}



/* end of transport_api_address_to_string.c */
