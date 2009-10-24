/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file util/resolver_api.c
 * @brief resolver for writing a tool
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_client_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_resolver_service.h"
#include "gnunet_server_lib.h"
#include "resolver.h"


/**
 * FIXME.
 */
struct GetAddressContext
{

  /**
   * FIXME.
   */
  GNUNET_RESOLVER_AddressCallback callback;

  /**
   * Closure for "callback".
   */
  void *cls;

  /**
   * FIXME.
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * FIXME.
   */
  struct GNUNET_TIME_Absolute timeout;
};


/**
 * Possible hostnames for "loopback".
 */
static const char *loopback[] = {
  "localhost",
  "ip6-localnet",
  NULL
};


/**
 * Check that the resolver service runs on localhost
 * (or equivalent).
 */
static void
check_config (const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  char *hostname;
  unsigned int i;
  struct in_addr v4;
  struct in6_addr v6;

  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (cfg,
					     "resolver",
					     "HOSTNAME",
					     &hostname))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Must specify `%s' for `%s' in configuration!\n"),
		  "HOSTNAME",
		  "resolver");
      GNUNET_assert (0);
    }
  if ( (0 == inet_pton (AF_INET,
			hostname,
		 	&v4)) ||
       (0 == inet_pton (AF_INET6,
			hostname,
			&v6)) )
    {
      GNUNET_free (hostname);
      return;
    }
  i = 0;
  while (loopback[i] != NULL)
    if (0 == strcmp (loopback[i++], hostname))
      {
	GNUNET_free (hostname); 
	return;
      }
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
	      _("Must specify `%s' for `%s' in configuration!\n"),
	      "localhost",
	      "resolver");
  GNUNET_free (hostname); 
  GNUNET_assert (0); 
}


/**
 * Convert IP address to string without DNS resolution.
 *
 * @param sa the address 
 * @param salen number of bytes in sa
 * @return address as a string, NULL on error
 */
static char *
no_resolve (const struct sockaddr *sa, socklen_t salen)
{
  char *ret;
  char inet4[INET_ADDRSTRLEN];
  char inet6[INET6_ADDRSTRLEN];

  if (salen < sizeof (struct sockaddr))
    return NULL;
  switch (sa->sa_family)
    {
    case AF_INET:
      if (salen != sizeof (struct sockaddr_in))
        return NULL;
      inet_ntop (AF_INET,
                 &((struct sockaddr_in *) sa)->sin_addr,
                 inet4, INET_ADDRSTRLEN);
      ret = GNUNET_strdup (inet4);
      break;
    case AF_INET6:
      if (salen != sizeof (struct sockaddr_in6))
        return NULL;
      inet_ntop (AF_INET6,
                 &((struct sockaddr_in6 *) sa)->sin6_addr,
                 inet6, INET6_ADDRSTRLEN);
      ret = GNUNET_strdup (inet6);
      break;
    default:
      ret = NULL;
      break;
    }
  return ret;
}


/**
 * FIXME
 *
 * @param cls FIXME
 * @param msg FIXME
 */
static void
handle_address_response (void *cls, const struct GNUNET_MessageHeader *msg)
{
  struct GetAddressContext *gac = cls;
  uint16_t size;
  const struct sockaddr *sa;
  socklen_t salen;


  if (msg == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  _("Timeout trying to resolve hostname.\n"));
      gac->callback (gac->cls, NULL, 0);
      GNUNET_CLIENT_disconnect (gac->client);
      GNUNET_free (gac);
      return;
    }
  if (GNUNET_MESSAGE_TYPE_RESOLVER_RESPONSE != ntohs (msg->type))
    {
      GNUNET_break (0);
      gac->callback (gac->cls, NULL, 0);
      GNUNET_CLIENT_disconnect (gac->client);
      GNUNET_free (gac);
      return;
    }

  size = ntohs (msg->size);
  if (size == sizeof (struct GNUNET_MessageHeader))
    {
#if DEBUG_RESOLVER
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  _("Received end message resolving hostname.\n"));
#endif
      gac->callback (gac->cls, NULL, 0);
      GNUNET_CLIENT_disconnect (gac->client);
      GNUNET_free (gac);
      return;
    }
  sa = (const struct sockaddr *) &msg[1];
  salen = size - sizeof (struct GNUNET_MessageHeader);
  if (salen < sizeof (struct sockaddr))
    {
      GNUNET_break (0);
      gac->callback (gac->cls, NULL, 0);
      GNUNET_CLIENT_disconnect (gac->client);
      GNUNET_free (gac);
      return;
    }
#if DEBUG_RESOLVER
  {
    char *ips = no_resolve (sa, salen);
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, _("Resolver returns `%s'.\n"), ips);
    GNUNET_free (ips);
  }
#endif
  gac->callback (gac->cls, sa, salen);
  GNUNET_CLIENT_receive (gac->client,
                         &handle_address_response,
                         gac,
                         GNUNET_TIME_absolute_get_remaining (gac->timeout));
}


/**
 * Convert a string to one or more IP addresses.
 *
 * @param sched scheduler to use
 * @param cfg configuration to use
 * @param hostname the hostname to resolve
 * @param domain AF_INET or AF_INET6; use AF_UNSPEC for "any"
 * @param callback function to call with addresses
 * @param callback_cls closure for callback
 * @param timeout how long to try resolving
 */
void
GNUNET_RESOLVER_ip_get (struct GNUNET_SCHEDULER_Handle *sched,
                        const struct GNUNET_CONFIGURATION_Handle *cfg,
                        const char *hostname,
                        int domain,
                        struct GNUNET_TIME_Relative timeout,
                        GNUNET_RESOLVER_AddressCallback callback, 
			void *callback_cls)
{
  struct GNUNET_CLIENT_Connection *client;
  struct GNUNET_RESOLVER_GetMessage *msg;
  struct GetAddressContext *actx;
  size_t slen;
  unsigned int i;
  struct sockaddr_in v4;
  struct sockaddr_in6 v6;

  memset (&v4, 0, sizeof(v4));
  v4.sin_addr.s_addr = htonl (INADDR_LOOPBACK);  
  v4.sin_family = AF_INET;
#if HAVE_SOCKADDR_IN_SIN_LEN
  v4.sin_len = sizeof(v4);
#endif
  memset (&v6, 0, sizeof(v6)); 
  v6.sin6_family = AF_INET6;
#if HAVE_SOCKADDR_IN_SIN_LEN
  v6.sin6_len = sizeof(v6);
#endif
  /* first, check if this is a numeric address */
  if ( ( (domain == AF_UNSPEC) || (domain == AF_INET) ) && 
       (0 == inet_pton (AF_INET,
			hostname,
			&v4.sin_addr)) )
    {
      callback (callback_cls,
		(const struct sockaddr*) &v4,
		sizeof(v4));
      if ( (domain == AF_UNSPEC) && 
	   (0 == inet_pton (AF_INET6,
			    hostname,
			    &v6.sin6_addr)) )
	{
	  /* this can happen on some systems IF "hostname" is "localhost" */
	  callback (callback_cls,
		    (const struct sockaddr*) &v6,
		    sizeof(v6));
	}
      callback (callback_cls, NULL, 0);      
      return;
    }
  if ( ( (domain == AF_UNSPEC) ||(domain == AF_INET) ) && 
       (0 == inet_pton (AF_INET6,
			hostname,
			&v6.sin6_addr)) )
    {
      callback (callback_cls,
		(const struct sockaddr*) &v6,
		sizeof(v6));
      callback (callback_cls, NULL, 0);
      return;
    }
  check_config (cfg);
  /* then, check if this is a loopback address */
  i = 0;
  while (loopback[i] != NULL)
    if (0 == strcmp (loopback[i++], hostname))
      {
	v4.sin_addr.s_addr = htonl (INADDR_LOOPBACK);  
	v6.sin6_addr = in6addr_loopback;
	switch (domain)
	  {
	  case AF_INET:
	    callback (callback_cls, 
		      (const struct sockaddr*) &v4,
		      sizeof(v4));
	    break;
	  case AF_INET6:
	    callback (callback_cls, 
		      (const struct sockaddr*) &v6,
		      sizeof(v6));
	    break;
	  case AF_UNSPEC:
	    callback (callback_cls, 
		      (const struct sockaddr*) &v6,
		      sizeof(v6));
	    callback (callback_cls, 
		      (const struct sockaddr*) &v4,
		      sizeof(v4));
	    break;
	  }
	callback (callback_cls, NULL, 0);
	return;
      }
  slen = strlen (hostname) + 1;
  if (slen + sizeof (struct GNUNET_RESOLVER_GetMessage) >
      GNUNET_SERVER_MAX_MESSAGE_SIZE)
    {
      GNUNET_break (0);
      callback (callback_cls, NULL, 0);
      return;
    }
  client = GNUNET_CLIENT_connect (sched, "resolver", cfg);
  if (client == NULL)
    {
      callback (callback_cls, NULL, 0);
      return;
    }
  msg = GNUNET_malloc (sizeof (struct GNUNET_RESOLVER_GetMessage) + slen);
  msg->header.size =
    htons (sizeof (struct GNUNET_RESOLVER_GetMessage) + slen);
  msg->header.type = htons (GNUNET_MESSAGE_TYPE_RESOLVER_REQUEST);
  msg->direction = htonl (GNUNET_NO);
  msg->domain = htonl (domain);
  memcpy (&msg[1], hostname, slen);
  actx = GNUNET_malloc (sizeof (struct GetAddressContext));
  actx->callback = callback;
  actx->cls = callback_cls;
  actx->client = client;
  actx->timeout = GNUNET_TIME_relative_to_absolute (timeout);

#if DEBUG_RESOLVER
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              _("Resolver requests DNS resolution of hostname `%s'.\n"),
              hostname);
#endif
  if (GNUNET_OK !=
      GNUNET_CLIENT_transmit_and_get_response (client,
					       &msg->header,
					       timeout,
					       GNUNET_YES,
					       &handle_address_response,
					       actx))
    {
      GNUNET_free (msg);
      GNUNET_free (actx);
      callback (callback_cls, NULL, 0);
      GNUNET_CLIENT_disconnect (client);
      return;
    }
  GNUNET_free (msg);      
}


/**
 * FIXME.
 */
struct GetHostnameContext
{

  /**
   * FIXME.
   */
  GNUNET_RESOLVER_HostnameCallback callback;
  
  /**
   * FIXME.
   */
  void *cls;
  
  /**
   * FIXME.
   */  
  struct GNUNET_CLIENT_Connection *client;
  
  /**
   * FIXME.
   */ 
  struct GNUNET_TIME_Absolute timeout;
};


/**
 * FIXME.
 *
 * @param cls FIXME
 * @param msg FIXME
 */
static void
handle_hostname_response (void *cls, const struct GNUNET_MessageHeader *msg)
{
  struct GetHostnameContext *ghc = cls;
  uint16_t size;
  const char *hostname;

  if (msg == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                  _("Timeout trying to resolve IP address.\n"));
      ghc->callback (ghc->cls, NULL);
      GNUNET_CLIENT_disconnect (ghc->client);
      GNUNET_free (ghc);
      return;
    }
  size = ntohs (msg->size);
  if (size == sizeof (struct GNUNET_MessageHeader))
    {
#if DEBUG_RESOLVER
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  _("Received end message resolving IP address.\n"));
#endif
      ghc->callback (ghc->cls, NULL);
      GNUNET_CLIENT_disconnect (ghc->client);
      GNUNET_free (ghc);
      return;
    }
  hostname = (const char *) &msg[1];
  if (hostname[size - sizeof (struct GNUNET_MessageHeader) - 1] != '\0')
    {
      GNUNET_break (0);
      ghc->callback (ghc->cls, NULL);
      GNUNET_CLIENT_disconnect (ghc->client);
      GNUNET_free (ghc);
      return;
    }
#if DEBUG_RESOLVER
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              _("Resolver returns `%s'.\n"), hostname);
#endif
  ghc->callback (ghc->cls, hostname);
  GNUNET_CLIENT_receive (ghc->client,
                         &handle_hostname_response,
                         ghc,
                         GNUNET_TIME_absolute_get_remaining (ghc->timeout));
}


/**
 * Get an IP address as a string.
 *
 * @param sched scheduler to use
 * @param cfg configuration to use
 * @param sa host address
 * @param salen length of host address
 * @param do_resolve use GNUNET_NO to return numeric hostname
 * @param timeout how long to try resolving
 * @param callback function to call with hostnames
 * @param cls closure for callback
 */
void
GNUNET_RESOLVER_hostname_get (struct GNUNET_SCHEDULER_Handle *sched,
                              const struct GNUNET_CONFIGURATION_Handle *cfg,
                              const struct sockaddr *sa,
                              socklen_t salen,
                              int do_resolve,
                              struct GNUNET_TIME_Relative timeout,
                              GNUNET_RESOLVER_HostnameCallback callback,
                              void *cls)
{
  char *result;
  struct GNUNET_CLIENT_Connection *client;
  struct GNUNET_RESOLVER_GetMessage *msg;
  struct GetHostnameContext *hctx;

  check_config (cfg);
  if (GNUNET_NO == do_resolve)
    {
      result = no_resolve (sa, salen);
#if DEBUG_RESOLVER
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  _("Resolver returns `%s'.\n"), result);
#endif
      callback (cls, result);
      if (result != NULL)
        {
          GNUNET_free (result);
          callback (cls, NULL);
        }
      return;
    }
  if (salen + sizeof (struct GNUNET_RESOLVER_GetMessage) >
      GNUNET_SERVER_MAX_MESSAGE_SIZE)
    {
      GNUNET_break (0);
      callback (cls, NULL);
      return;
    }
  client = GNUNET_CLIENT_connect (sched, "resolver", cfg);
  if (client == NULL)
    {
      callback (cls, NULL);
      return;
    }
  msg = GNUNET_malloc (sizeof (struct GNUNET_RESOLVER_GetMessage) + salen);
  msg->header.size =
    htons (sizeof (struct GNUNET_RESOLVER_GetMessage) + salen);
  msg->header.type = htons (GNUNET_MESSAGE_TYPE_RESOLVER_REQUEST);
  msg->direction = htonl (GNUNET_YES);
  msg->domain = htonl (sa->sa_family);
  memcpy (&msg[1], sa, salen);
#if DEBUG_RESOLVER
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              _("Resolver requests DNS resolution of IP address.\n"));
#endif
  hctx = GNUNET_malloc (sizeof (struct GetHostnameContext));
  hctx->callback = callback;
  hctx->cls = cls;
  hctx->client = client;
  hctx->timeout = GNUNET_TIME_relative_to_absolute (timeout);
  if (GNUNET_OK !=
      GNUNET_CLIENT_transmit_and_get_response (client,
					       &msg->header,
					       timeout,
					       GNUNET_YES,
					       &handle_hostname_response,
					       hctx))
    {
      GNUNET_free (msg);
      callback (cls, NULL);
      GNUNET_CLIENT_disconnect (client);
      GNUNET_free (hctx);
    }
  GNUNET_free (msg);
}

/**
 * Maximum supported length of hostname
 */
#define MAX_HOSTNAME 1024


/**
 * Resolve our hostname to an IP address.
 *
 * @param sched scheduler to use
 * @param cfg configuration to use
 * @param domain AF_INET or AF_INET6; use AF_UNSPEC for "any"
 * @param callback function to call with addresses
 * @param cls closure for callback
 * @param timeout how long to try resolving
 */
void
GNUNET_RESOLVER_hostname_resolve (struct GNUNET_SCHEDULER_Handle *sched,
                                  const struct GNUNET_CONFIGURATION_Handle *cfg,
                                  int domain,
                                  struct GNUNET_TIME_Relative timeout,
                                  GNUNET_RESOLVER_AddressCallback callback,
                                  void *cls)
{
  char hostname[MAX_HOSTNAME];

  check_config (cfg);
  if (0 != gethostname (hostname, sizeof (hostname) - 1))
    {
      GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR |
                           GNUNET_ERROR_TYPE_BULK, "gethostname");
      callback (cls, NULL, 0);
      return;
    }
#if DEBUG_RESOLVER
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              _("Resolving our hostname `%s'\n"), hostname);
#endif
  GNUNET_RESOLVER_ip_get (sched,
                          cfg, hostname, domain, timeout, callback, cls);
}




/* end of resolver_api.c */
