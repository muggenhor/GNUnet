/*
     This file is part of GNUnet.
     (C) 2009, 2010 Christian Grothoff (and other contributing authors)

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
 * @file dht/dht_api.c
 * @brief library to access the DHT service
 * @author Christian Grothoff
 * @author Nathan Evans
 *
 * TODO: Only allow a single message until confirmed as received by
 *       the service.  For put messages call continuation as soon as
 *       receipt acknowledged (then remove), for GET or other messages
 *       only call continuation when data received.
 *       Add unique identifier to message types requesting data to be
 *       returned.
 */
#include "platform.h"
#include "gnunet_bandwidth_lib.h"
#include "gnunet_client_lib.h"
#include "gnunet_constants.h"
#include "gnunet_container_lib.h"
#include "gnunet_arm_service.h"
#include "gnunet_hello_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_server_lib.h"
#include "gnunet_time_lib.h"
#include "gnunet_dht_service.h"
#include "dht.h"

#define DEBUG_DHT_API GNUNET_YES

#define DEFAULT_DHT_TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 5)

struct PendingMessage
{
  /**
   * Message that is pending
   */
  struct GNUNET_MessageHeader *msg;

  /**
   * Timeout for this message
   */
  struct GNUNET_TIME_Relative timeout;

  /**
   * Continuation to call on message send
   * or message receipt confirmation
   */
  GNUNET_DHT_MessageCallback cont;

  /**
   * Continuation closure
   */
  void *cont_cls;

  /**
   * Whether or not to await verification the message
   * was received by the service
   */
  size_t is_unique;

  /**
   * Unique ID for this request
   */
  uint64_t unique_id;

};

struct GNUNET_DHT_GetContext
{


  /**
   * Iterator to call on data receipt
   */
  GNUNET_DHT_GetIterator iter;

  /**
   * Closure for the iterator callback
   */
  void *iter_cls;

};

/**
 * Handle to control a unique operation (one that is
 * expected to return results)
 */
struct GNUNET_DHT_RouteHandle
{

  /**
   * Unique identifier for this request (for key collisions)
   */
  uint64_t uid;

  /**
   * Key that this get request is for
   */
  GNUNET_HashCode key;

  /**
   * Iterator to call on data receipt
   */
  GNUNET_DHT_ReplyProcessor iter;

  /**
   * Closure for the iterator callback
   */
  void *iter_cls;

  /**
   * Main handle to this DHT api
   */
  struct GNUNET_DHT_Handle *dht_handle;
};

/**
 * Handle for a non unique request, holds callback
 * which needs to be called before we allow other
 * messages to be processed and sent to the DHT service
 */
struct GNUNET_DHT_NonUniqueHandle
{
  /**
   * Key that this get request is for
   */
  GNUNET_HashCode key;

  /**
   * Type of data get request was for
   */
  uint32_t type;

  /**
   * Continuation to call on service
   * confirmation of message receipt.
   */
  GNUNET_SCHEDULER_Task cont;

  /**
   * Send continuation cls
   */
  void *cont_cls;
};


/**
 * Connection to the DHT service.
 */
struct GNUNET_DHT_Handle
{
  /**
   * Our scheduler.
   */
  struct GNUNET_SCHEDULER_Handle *sched;

  /**
   * Configuration to use.
   */
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Socket (if available).
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * Currently pending transmission request.
   */
  struct GNUNET_CLIENT_TransmitHandle *th;

  /**
   * Message we are currently sending, only allow
   * a single message to be queued.  If not unique
   * (typically a put request), await a confirmation
   * from the service that the message was received.
   * If unique, just fire and forget.
   */
  struct PendingMessage *current;

  /**
   * Hash map containing the current outstanding unique requests
   */
  struct GNUNET_CONTAINER_MultiHashMap *outstanding_requests;

  /**
   * Non unique handle.  If set don't schedule another non
   * unique request.
   */
  struct GNUNET_DHT_NonUniqueHandle *non_unique_request;

  /**
   * Kill off the connection and any pending messages.
   */
  int do_destroy;

};

static struct GNUNET_TIME_Relative default_request_timeout;

/* Forward declaration */
static void process_pending_message(struct GNUNET_DHT_Handle *handle);

static GNUNET_HashCode * hash_from_uid(uint64_t uid)
{
  int count;
  int remaining;
  GNUNET_HashCode *hash;
  hash = GNUNET_malloc(sizeof(GNUNET_HashCode));
  count = 0;

  while (count < sizeof(GNUNET_HashCode))
    {
      remaining = sizeof(GNUNET_HashCode) - count;
      if (remaining > sizeof(uid))
        remaining = sizeof(uid);

      memcpy(hash, &uid, remaining);
      count += remaining;
    }

  return hash;
}

/**
 * Handler for messages received from the DHT service
 * a demultiplexer which handles numerous message types
 *
 */
void service_message_handler (void *cls,
                              const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_DHT_Handle *handle = cls;
  struct GNUNET_DHT_Message *dht_msg;
  struct GNUNET_DHT_StopMessage *stop_msg;
  struct GNUNET_MessageHeader *enc_msg;
  struct GNUNET_DHT_RouteHandle *route_handle;
  uint64_t uid;
  GNUNET_HashCode *uid_hash;
  size_t enc_size;
  /* TODO: find out message type, handle callbacks for different types of messages.
   * Should be a non unique acknowledgment, or unique result. */

  if (msg == NULL)
  {
#if DEBUG_DHT_API
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "`%s': Received NULL from server, connection down?\n", "DHT API");
#endif
    return;
  }


  if (ntohs(msg->type) == GNUNET_MESSAGE_TYPE_DHT)
  {
    dht_msg = (struct GNUNET_DHT_Message *)msg;
    uid = GNUNET_ntohll(dht_msg->unique_id);
#if DEBUG_DHT_API
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "`%s': Received response to message (uid %llu)\n", "DHT API", uid);
#endif
    if (ntohs(dht_msg->unique))
      {
        uid_hash = hash_from_uid(ntohl(dht_msg->unique_id));

        route_handle = GNUNET_CONTAINER_multihashmap_get(handle->outstanding_requests, uid_hash);
        if (route_handle == NULL) /* We have no recollection of this request */
          {
#if DEBUG_DHT_API
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "`%s': Received response to message (uid %llu), but have no recollection of it!\n", "DHT API", ntohl(dht_msg->unique_id));
#endif
          }
        else
          {
            enc_size = ntohs(dht_msg->header.size) - sizeof(struct GNUNET_DHT_Message);
            GNUNET_assert(enc_size > 0);
            enc_msg = (struct GNUNET_MessageHeader *)&dht_msg[1];
            route_handle->iter(route_handle->iter_cls, enc_msg);
          }
      }
  }
  else if (ntohs(msg->type) == GNUNET_MESSAGE_TYPE_DHT_STOP)
  {
    stop_msg = (struct GNUNET_DHT_StopMessage *)msg;
    uid = GNUNET_ntohll(stop_msg->unique_id);
#if DEBUG_DHT_API
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "`%s': Received response to message (uid %llu)\n", "DHT API", uid);
#endif
    if (handle->current->unique_id == uid)
      {
#if DEBUG_DHT_API
        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                    "`%s': Have pending confirmation for this message!\n", "DHT API", uid);
#endif
        if (handle->current->cont != NULL)
          handle->current->cont(handle->current->cont_cls, GNUNET_OK);
        GNUNET_free(handle->current->msg);
        handle->current = NULL;
        GNUNET_free(handle->current);
      }
  }

}


/**
 * Initialize the connection with the DHT service.
 *
 * @param cfg configuration to use
 * @param sched scheduler to use
 * @param ht_len size of the internal hash table to use for
 *               processing multiple GET/FIND requests in parallel
 * @return NULL on error
 */
struct GNUNET_DHT_Handle *
GNUNET_DHT_connect (struct GNUNET_SCHEDULER_Handle *sched,
                    const struct GNUNET_CONFIGURATION_Handle *cfg,
                    unsigned int ht_len)
{
  struct GNUNET_DHT_Handle *handle;

  handle = GNUNET_malloc(sizeof(struct GNUNET_DHT_Handle));

  default_request_timeout = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, 5);
  handle->cfg = cfg;
  handle->sched = sched;

  handle->current = NULL;
  handle->do_destroy = GNUNET_NO;
  handle->th = NULL;

  handle->client = GNUNET_CLIENT_connect(sched, "dht", cfg);
  handle->outstanding_requests = GNUNET_CONTAINER_multihashmap_create(ht_len);

  if (handle->client == NULL)
    return NULL;
#if DEBUG_DHT_API
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "`%s': Connection to service in progress\n", "DHT API");
#endif
  GNUNET_CLIENT_receive (handle->client,
                         &service_message_handler,
                         handle, GNUNET_TIME_UNIT_FOREVER_REL);

  return handle;
}


/**
 * Shutdown connection with the DHT service.
 *
 * @param h connection to shut down
 */
void
GNUNET_DHT_disconnect (struct GNUNET_DHT_Handle *handle)
{
#if DEBUG_DHT_API
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "`%s': Called GNUNET_DHT_disconnect\n", "DHT API");
#endif
  GNUNET_assert(handle != NULL);

  if (handle->th != NULL) /* We have a live transmit request in the Aether */
    {
      GNUNET_CLIENT_notify_transmit_ready_cancel (handle->th);
      handle->th = NULL;
    }
  if (handle->current != NULL) /* We are trying to send something now, clean it up */
    GNUNET_free(handle->current);

  if (handle->client != NULL) /* Finally, disconnect from the service */
    {
      GNUNET_CLIENT_disconnect (handle->client, GNUNET_NO);
      handle->client = NULL;
    }

  GNUNET_free (handle);
}


/**
 * Send complete (or failed), schedule next (or don't)
 */
static void
finish (struct GNUNET_DHT_Handle *handle, int code)
{
  /* TODO: if code is not GNUNET_OK, do something! */
  struct PendingMessage *pos = handle->current;
#if DEBUG_DHT_API
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "`%s': Finish called!\n", "DHT API");
#endif
  GNUNET_assert(pos != NULL);

  if (pos->is_unique)
    {
      if (pos->cont != NULL)
        pos->cont(pos->cont_cls, code);

      GNUNET_free(pos->msg);
      handle->current = NULL;
      GNUNET_free(pos);
    }
  /* Otherwise we need to wait for a response to this message! */
}

/**
 * Transmit the next pending message, called by notify_transmit_ready
 */
static size_t
transmit_pending (void *cls, size_t size, void *buf)
{
  struct GNUNET_DHT_Handle *handle = cls;
  size_t tsize;

#if DEBUG_DHT_API
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "`%s': In transmit_pending\n", "DHT API");
#endif
  if (buf == NULL)
    {
#if DEBUG_DHT_API
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "`%s': In transmit_pending buf is NULL\n", "DHT API");
#endif
      /* FIXME: free associated resources or summat */
      finish(handle, GNUNET_SYSERR);
      return 0;
    }

  handle->th = NULL;

  if (handle->current != NULL)
  {
    tsize = ntohs(handle->current->msg->size);
    if (size >= tsize)
    {
#if DEBUG_DHT_API
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "`%s': Sending message size %d\n", "DHT API", tsize);
#endif
      memcpy(buf, handle->current->msg, tsize);
      finish(handle, GNUNET_OK);
      return tsize;
    }
    else
    {
      return 0;
    }
  }
  /* Have no pending request */
  return 0;
}


/**
 * Try to (re)connect to the dht service.
 *
 * @return GNUNET_YES on success, GNUNET_NO on failure.
 */
static int
try_connect (struct GNUNET_DHT_Handle *ret)
{
  if (ret->client != NULL)
    return GNUNET_OK;
  ret->client = GNUNET_CLIENT_connect (ret->sched, "dht", ret->cfg);
  if (ret->client != NULL)
    return GNUNET_YES;
#if DEBUG_STATISTICS
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              _("Failed to connect to the dht service!\n"));
#endif
  return GNUNET_NO;
}


/**
 * Try to send messages from list of messages to send
 */
static void process_pending_message(struct GNUNET_DHT_Handle *handle)
{

  if (handle->current == NULL)
    return;                     /* action already pending */
  if (GNUNET_YES != try_connect (handle))
    {
      finish (handle, GNUNET_SYSERR);
      return;
    }

  /* TODO: set do_destroy somewhere's, see what needs to happen in that case! */
  if (handle->do_destroy)
    {
      //GNUNET_DHT_disconnect (handle); /* FIXME: replace with proper disconnect stuffs */
    }


  if (NULL ==
      (handle->th = GNUNET_CLIENT_notify_transmit_ready (handle->client,
                                                    ntohs(handle->current->msg->size),
                                                    handle->current->timeout,
                                                    GNUNET_YES,
                                                    &transmit_pending, handle)))
    {
#if DEBUG_DHT_API
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Failed to transmit request to dht service.\n");
#endif
      finish (handle, GNUNET_SYSERR);
    }
#if DEBUG_DHT_API
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "`%s': Scheduled sending message of size %d to service\n", "DHT API", ntohs(handle->current->msg->size));
#endif
}

/**
 * Iterator called on each result obtained from a generic route
 * operation
 */
void get_reply_iterator (void *cls,
                         const struct GNUNET_MessageHeader *reply)
{

}

/**
 * Perform an asynchronous FIND_PEER operation on the DHT.
 *
 * @param handle handle to the DHT service
 * @param key the key to look up
 * @param desired_replication_level how many peers should ultimately receive
 *                this message (advisory only, target may be too high for the
 *                given DHT or not hit exactly).
 * @param options options for routing
 * @param enc send the encapsulated message to a peer close to the key
 * @param iter function to call on each result, NULL if no replies are expected
 * @param iter_cls closure for iter
 * @param timeout when to abort with an error if we fail to get
 *                a confirmation for the PUT from the local DHT service
 * @param cont continuation to call when done;
 *             reason will be TIMEOUT on error,
 *             reason will be PREREQ_DONE on success
 * @param cont_cls closure for cont
 * @return handle to stop the request
 */
struct GNUNET_DHT_RouteHandle *
GNUNET_DHT_route_start (struct GNUNET_DHT_Handle *handle,
                        const GNUNET_HashCode *key,
                        unsigned int desired_replication_level,
                        enum GNUNET_DHT_RouteOption options,
                        const struct GNUNET_MessageHeader *enc,
                        struct GNUNET_TIME_Relative timeout,
                        GNUNET_DHT_ReplyProcessor iter,
                        void *iter_cls,
                        GNUNET_DHT_MessageCallback cont,
                        void *cont_cls)
{
  struct GNUNET_DHT_RouteHandle *route_handle;
  struct PendingMessage *pending;
  struct GNUNET_DHT_Message *message;
  size_t is_unique;
  size_t msize;
  GNUNET_HashCode *uid_key;
  int count;
  uint64_t uid;

  uid = 0;
  is_unique = GNUNET_YES;
  if (iter == NULL)
    is_unique = GNUNET_NO;

  route_handle = NULL;

  if (is_unique)
    {
      route_handle = GNUNET_malloc(sizeof(struct GNUNET_DHT_RouteHandle));
      memcpy(&route_handle->key, key, sizeof(GNUNET_HashCode));
      route_handle->iter = iter;
      route_handle->iter_cls = iter_cls;
      route_handle->dht_handle = handle;
      route_handle->uid = GNUNET_CRYPTO_random_u64(GNUNET_CRYPTO_QUALITY_WEAK, -1);
      uid = route_handle->uid;
#if DEBUG_DHT_API
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "`%s': Unique ID is %llu\n", "DHT API", uid);
#endif
      count = 0;
      uid_key = hash_from_uid(route_handle->uid);
      /* While we have an outstanding request with the same identifier! */
      while (GNUNET_CONTAINER_multihashmap_contains(handle->outstanding_requests, uid_key) == GNUNET_YES)
        {
          GNUNET_free(uid_key);
          uid_key = hash_from_uid(route_handle->uid);
        }
      /**
       * Store based on random identifier!
       */
      GNUNET_CONTAINER_multihashmap_put(handle->outstanding_requests, uid_key, route_handle, GNUNET_CONTAINER_MULTIHASHMAPOPTION_MULTIPLE);
      msize = sizeof(struct GNUNET_DHT_Message) + ntohs(enc->size);
      GNUNET_free(uid_key);
    }
  else
    {
      msize = sizeof(struct GNUNET_DHT_Message) + ntohs(enc->size);
    }

  message = GNUNET_malloc(msize);
  message->header.size = htons(msize);
  message->header.type = htons(GNUNET_MESSAGE_TYPE_DHT);
  memcpy(&message->key, key, sizeof(GNUNET_HashCode));
  message->options = htons(options);
  message->desired_replication_level = htons(options);
  message->unique = htons(is_unique);
  message->unique_id = GNUNET_htonll(uid);
  memcpy(&message[1], enc, ntohs(enc->size));

  pending = GNUNET_malloc(sizeof(struct PendingMessage));
  pending->msg = &message->header;
  pending->timeout = timeout;
  pending->cont = cont;
  pending->cont_cls = cont_cls;
  pending->is_unique = is_unique;

  GNUNET_assert(handle->current == NULL);

  handle->current = pending;

  process_pending_message(handle);

  return route_handle;
}

void
GNUNET_DHT_route_stop (struct GNUNET_DHT_RouteHandle *fph);


void dht_get_processor (void *cls,
                        const struct GNUNET_MessageHeader *reply)
{

}

/**
 * Perform an asynchronous GET operation on the DHT identified.
 *
 * @param h handle to the DHT service
 * @param type expected type of the response object
 * @param key the key to look up
 * @param iter function to call on each result
 * @param iter_cls closure for iter
 * @return handle to stop the async get
 */
struct GNUNET_DHT_RouteHandle *
GNUNET_DHT_get_start (struct GNUNET_DHT_Handle *handle,
                      struct GNUNET_TIME_Relative timeout,
                      uint32_t type,
                      const GNUNET_HashCode * key,
                      GNUNET_DHT_GetIterator iter,
                      void *iter_cls)
{
  struct GNUNET_DHT_GetContext *get_context;
  struct GNUNET_DHT_GetMessage *get_msg;

  if (handle->current != NULL) /* Can't send right now, we have a pending message... */
    return NULL;

  get_context = GNUNET_malloc(sizeof(struct GNUNET_DHT_GetContext));
  get_context->iter = iter;
  get_context->iter_cls = iter_cls;

#if DEBUG_DHT_API
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "`%s': Inserting pending get request with key %s\n", "DHT API", GNUNET_h2s(key));
#endif

  get_msg = GNUNET_malloc(sizeof(struct GNUNET_DHT_GetMessage));
  get_msg->header.type = htons(GNUNET_MESSAGE_TYPE_DHT_GET);
  get_msg->header.size = htons(sizeof(struct GNUNET_DHT_GetMessage));
  get_msg->type = htonl(type);

  return GNUNET_DHT_route_start(handle, key, 0, 0, &get_msg->header, timeout, &get_reply_iterator, get_context, NULL, NULL);

}


void
GNUNET_DHT_route_stop (struct GNUNET_DHT_RouteHandle *route_handle)
{
  struct PendingMessage *pending;
  struct GNUNET_DHT_StopMessage *message;
  size_t msize;
  GNUNET_HashCode *uid_key;

  msize = sizeof(struct GNUNET_DHT_StopMessage);

  message = GNUNET_malloc(msize);
  message->header.size = htons(msize);
  message->header.type = htons(GNUNET_MESSAGE_TYPE_DHT_STOP);
#if DEBUG_DHT_API
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "`%s': Remove outstanding request for uid %llu\n", "DHT API", route_handle->uid);
#endif
  message->unique_id = GNUNET_htonll(route_handle->uid);

  pending = GNUNET_malloc(sizeof(struct PendingMessage));
  pending->msg = (struct GNUNET_MessageHeader *)message;
  pending->timeout = DEFAULT_DHT_TIMEOUT;
  pending->cont = NULL;
  pending->cont_cls = NULL;
  pending->is_unique = GNUNET_NO;

  GNUNET_assert(route_handle->dht_handle->current == NULL);

  route_handle->dht_handle->current = pending;

  process_pending_message(route_handle->dht_handle);

  uid_key = hash_from_uid(route_handle->uid);

  if (GNUNET_CONTAINER_multihashmap_remove(route_handle->dht_handle->outstanding_requests, uid_key, route_handle) != GNUNET_YES)
    {
#if DEBUG_DHT_API
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "`%s': Remove outstanding request from hashmap failed for key %s, uid %llu\n", "DHT API", GNUNET_h2s(uid_key), route_handle->uid);
#endif
    }

  return;
}


/**
 * Stop async DHT-get.  Frees associated resources.
 *
 * @param record GET operation to stop.
 */
void
GNUNET_DHT_get_stop (struct GNUNET_DHT_RouteHandle *handle)
{
#if OLDREMOVE
  struct GNUNET_DHT_GetMessage *get_msg;
  struct GNUNET_DHT_Handle *handle;
  GNUNET_HashCode *uid_key;
#endif

  GNUNET_DHT_route_stop(handle);

#if OLDREMOVE
  uid_key = hash_from_uid(get_handle->uid);
  GNUNET_assert(GNUNET_CONTAINER_multihashmap_remove(handle->outstanding_requests, uid_key, get_handle) == GNUNET_YES);

  if (handle->do_destroy == GNUNET_NO)
    {
      get_msg = GNUNET_malloc(sizeof(struct GNUNET_DHT_GetMessage));
      get_msg->header.type = htons(GNUNET_MESSAGE_TYPE_DHT_GET_STOP);
      get_msg->header.size = htons(sizeof(struct GNUNET_DHT_GetMessage));


    }
#endif
#if DEBUG_DHT_API
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "`%s': Removing pending get request with key %s, uid %llu\n", "DHT API", GNUNET_h2s(&handle->key), handle->uid);
#endif
}


/**
 * Perform a PUT operation storing data in the DHT.
 *
 * @param h handle to DHT service
 * @param key the key to store under
 * @param type type of the value
 * @param size number of bytes in data; must be less than 64k
 * @param data the data to store
 * @param exp desired expiration time for the value
 * @param cont continuation to call when done;
 *             reason will be TIMEOUT on error,
 *             reason will be PREREQ_DONE on success
 * @param cont_cls closure for cont
 *
 * @return GNUNET_YES if put message is queued for transmission
 */
void
GNUNET_DHT_put (struct GNUNET_DHT_Handle *handle,
                const GNUNET_HashCode * key,
                uint32_t type,
                uint32_t size,
                const char *data,
                struct GNUNET_TIME_Absolute exp,
                struct GNUNET_TIME_Relative timeout,
                GNUNET_DHT_MessageCallback cont,
                void *cont_cls)
{
  struct GNUNET_DHT_PutMessage *put_msg;
  size_t msize;

  if (handle->current != NULL)
    {
      cont(cont_cls, GNUNET_SYSERR);
      return;
    }

#if DEBUG_DHT_API
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "`%s': Inserting pending put request with key %s\n", "DHT API", GNUNET_h2s(key));
#endif

  msize = sizeof(struct GNUNET_DHT_PutMessage) + size;
  put_msg = GNUNET_malloc(msize);
  put_msg->header.type = htons(GNUNET_MESSAGE_TYPE_DHT_PUT);
  put_msg->header.size = htons(msize);
  put_msg->type = htonl(type);
  memcpy(&put_msg[1], data, size);

  GNUNET_DHT_route_start(handle, key, 0, 0, &put_msg->header, timeout, NULL, NULL, cont, cont_cls);

}
