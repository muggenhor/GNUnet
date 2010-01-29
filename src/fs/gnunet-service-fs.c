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
 * @file fs/gnunet-service-fs.c
 * @brief gnunet anonymity protocol implementation
 * @author Christian Grothoff
 *
 * TODO:
 * - forward_request_task (P2P forwarding!) 
 * - track stats for hot-path routing
 * - implement hot-path routing decision procedure
 * - detect duplicate requests (P2P and CS)
 * - implement: bound_priority, test_load_too_high, validate_skblock
 * - add content migration support (store locally)
 * - add random delay
 * - statistics
 * 
 */
#include "platform.h"
#include <float.h>
#include "gnunet_constants.h"
#include "gnunet_core_service.h"
#include "gnunet_datastore_service.h"
#include "gnunet_peer_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_signatures.h"
#include "gnunet_util_lib.h"
#include "gnunet-service-fs_drq.h"
#include "gnunet-service-fs_indexing.h"
#include "fs.h"

/**
 * Maximum number of outgoing messages we queue per peer.
 * FIXME: set to a tiny value for testing; make configurable.
 */
#define MAX_QUEUE_PER_PEER 2



/**
 * Maximum number of requests (from other peers) that we're
 * willing to have pending at any given point in time.
 * FIXME: set from configuration (and 32 is a tiny value for testing only).
 */
static uint64_t max_pending_requests = 32;


/**
 * Information we keep for each pending reply.  The
 * actual message follows at the end of this struct.
 */
struct PendingMessage;


/**
 * Function called upon completion of a transmission.
 *
 * @param cls closure
 * @param pid ID of receiving peer, 0 on transmission error
 */
typedef void (*TransmissionContinuation)(void * cls, 
					 GNUNET_PEER_Id tpid);


/**
 * Information we keep for each pending reply.  The
 * actual message follows at the end of this struct.
 */
struct PendingMessage
{
  /**
   * This is a doubly-linked list of messages to the same peer.
   */
  struct PendingMessage *next;

  /**
   * This is a doubly-linked list of messages to the same peer.
   */
  struct PendingMessage *prev;

  /**
   * Entry in pending message list for this pending message.
   */ 
  struct PendingMessageList *pml;  

  /**
   * Function to call immediately once we have transmitted this
   * message.
   */
  TransmissionContinuation cont;

  /**
   * Closure for cont.
   */
  void *cont_cls;

  /**
   * Size of the reply; actual reply message follows
   * at the end of this struct.
   */
  size_t msize;
  
  /**
   * How important is this message for us?
   */
  uint32_t priority;
 
};


/**
 * Information about a peer that we are connected to.
 * We track data that is useful for determining which
 * peers should receive our requests.  We also keep
 * a list of messages to transmit to this peer.
 */
struct ConnectedPeer
{

  /**
   * List of the last clients for which this peer successfully
   * answered a query.
   */
  struct GNUNET_SERVER_Client *last_client_replies[CS2P_SUCCESS_LIST_SIZE];

  /**
   * List of the last PIDs for which
   * this peer successfully answered a query;
   * We use 0 to indicate no successful reply.
   */
  GNUNET_PEER_Id last_p2p_replies[P2P_SUCCESS_LIST_SIZE];

  /**
   * Average delay between sending the peer a request and
   * getting a reply (only calculated over the requests for
   * which we actually got a reply).   Calculated
   * as a moving average: new_delay = ((n-1)*last_delay+curr_delay) / n
   */ 
  struct GNUNET_TIME_Relative avg_delay;

  /**
   * Handle for an active request for transmission to this
   * peer, or NULL.
   */
  struct GNUNET_CORE_TransmitHandle *cth;

  /**
   * Messages (replies, queries, content migration) we would like to
   * send to this peer in the near future.  Sorted by priority, head.
   */
  struct PendingMessage *pending_messages_head;

  /**
   * Messages (replies, queries, content migration) we would like to
   * send to this peer in the near future.  Sorted by priority, tail.
   */
  struct PendingMessage *pending_messages_tail;

  /**
   * Average priority of successful replies.  Calculated
   * as a moving average: new_avg = ((n-1)*last_avg+curr_prio) / n
   */
  double avg_priority;

  /**
   * Increase in traffic preference still to be submitted
   * to the core service for this peer. FIXME: double or 'uint64_t'?
   */
  double inc_preference;

  /**
   * The peer's identity.
   */
  GNUNET_PEER_Id pid;  

  /**
   * Size of the linked list of 'pending_messages'.
   */
  unsigned int pending_requests;

  /**
   * Which offset in "last_p2p_replies" will be updated next?
   * (we go round-robin).
   */
  unsigned int last_p2p_replies_woff;

  /**
   * Which offset in "last_client_replies" will be updated next?
   * (we go round-robin).
   */
  unsigned int last_client_replies_woff;

};


/**
 * Information we keep for each pending request.  We should try to
 * keep this struct as small as possible since its memory consumption
 * is key to how many requests we can have pending at once.
 */
struct PendingRequest;


/**
 * Doubly-linked list of requests we are performing
 * on behalf of the same client.
 */
struct ClientRequestList
{

  /**
   * This is a doubly-linked list.
   */
  struct ClientRequestList *next;

  /**
   * This is a doubly-linked list.
   */
  struct ClientRequestList *prev;

  /**
   * Request this entry represents.
   */
  struct PendingRequest *req;

  /**
   * Client list this request belongs to.
   */
  struct ClientList *client_list;

};


/**
 * Replies to be transmitted to the client.  The actual
 * response message is allocated after this struct.
 */
struct ClientResponseMessage
{
  /**
   * This is a doubly-linked list.
   */
  struct ClientResponseMessage *next;

  /**
   * This is a doubly-linked list.
   */
  struct ClientResponseMessage *prev;

  /**
   * Client list entry this response belongs to.
   */
  struct ClientList *client_list;

  /**
   * Number of bytes in the response.
   */
  size_t msize;
};


/**
 * Linked list of clients we are performing requests
 * for right now.
 */
struct ClientList
{
  /**
   * This is a linked list.
   */
  struct ClientList *next;

  /**
   * ID of a client making a request, NULL if this entry is for a
   * peer.
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * Head of list of requests performed on behalf
   * of this client right now.
   */
  struct ClientRequestList *rl_head;

  /**
   * Tail of list of requests performed on behalf
   * of this client right now.
   */
  struct ClientRequestList *rl_tail;

  /**
   * Head of linked list of responses.
   */
  struct ClientResponseMessage *res_head;

  /**
   * Tail of linked list of responses.
   */
  struct ClientResponseMessage *res_tail;

  /**
   * Context for sending replies.
   */
  struct GNUNET_CONNECTION_TransmitHandle *th;

};


/**
 * Hash map entry of requests we are performing
 * on behalf of the same peer.
 */
struct PeerRequestEntry
{

  /**
   * Request this entry represents.
   */
  struct PendingRequest *req;

  /**
   * Entry of peer responsible for this entry.
   */
  struct ConnectedPeer *cp;

};


/**
 * Doubly-linked list of messages we are performing
 * due to a pending request.
 */
struct PendingMessageList
{

  /**
   * This is a doubly-linked list of messages on behalf of the same request.
   */
  struct PendingMessageList *next;

  /**
   * This is a doubly-linked list of messages on behalf of the same request.
   */
  struct PendingMessageList *prev;

  /**
   * Message this entry represents.
   */
  struct PendingMessage *pm;

  /**
   * Request this entry belongs to.
   */
  struct PendingRequest *req;

  /**
   * Peer this message is targeted for.
   */
  struct ConnectedPeer *target;

};


/**
 * Information we keep for each pending request.  We should try to
 * keep this struct as small as possible since its memory consumption
 * is key to how many requests we can have pending at once.
 */
struct PendingRequest
{

  /**
   * If this request was made by a client, this is our entry in the
   * client request list; otherwise NULL.
   */
  struct ClientRequestList *client_request_list;
  
  /**
   * If this request was made by a peer, this is our entry in the
   * per-peer multi-hash map; otherwise NULL.
   */
  struct PeerRequestEntry *pht_entry;

  /**
   * If this is a namespace query, pointer to the hash of the public
   * key of the namespace; otherwise NULL.  Pointer will be to the 
   * end of this struct (so no need to free it).
   */
  const GNUNET_HashCode *namespace;

  /**
   * Bloomfilter we use to filter out replies that we don't care about
   * (anymore).  NULL as long as we are interested in all replies.
   */
  struct GNUNET_CONTAINER_BloomFilter *bf;

  /**
   * Context of our GNUNET_CORE_peer_change_preference call.
   */
  struct GNUNET_CORE_InformationRequestContext *irc;

  /**
   * Hash code of all replies that we have seen so far (only valid
   * if client is not NULL since we only track replies like this for
   * our own clients).
   */
  GNUNET_HashCode *replies_seen;

  /**
   * Node in the heap representing this entry; NULL
   * if we have no heap node.
   */
  struct GNUNET_CONTAINER_HeapNode *hnode;

  /**
   * Head of list of messages being performed on behalf of this
   * request.
   */
  struct PendingMessageList *pending_head;

  /**
   * Tail of list of messages being performed on behalf of this
   * request.
   */
  struct PendingMessageList *pending_tail;

  /**
   * When did we first see this request (form this peer), or, if our
   * client is initiating, when did we last initiate a search?
   */
  struct GNUNET_TIME_Absolute start_time;

  /**
   * The query that this request is for.
   */
  GNUNET_HashCode query;

  /**
   * The task responsible for transmitting queries
   * for this request.
   */
  GNUNET_SCHEDULER_TaskIdentifier task;

  /**
   * (Interned) Peer identifier that identifies a preferred target
   * for requests.
   */
  GNUNET_PEER_Id target_pid;

  /**
   * (Interned) Peer identifiers of peers that have already
   * received our query for this content.
   */
  GNUNET_PEER_Id *used_pids;
  
  /**
   * Our entry in the DRQ (non-NULL while we wait for our
   * turn to interact with the local database).
   */
  struct DatastoreRequestQueue *drq;

  /**
   * Size of the 'bf' (in bytes).
   */
  size_t bf_size;

  /**
   * Desired anonymity level; only valid for requests from a local client.
   */
  uint32_t anonymity_level;

  /**
   * How many entries in "used_pids" are actually valid?
   */
  unsigned int used_pids_off;

  /**
   * How long is the "used_pids" array?
   */
  unsigned int used_pids_size;

  /**
   * Number of results found for this request.
   */
  unsigned int results_found;

  /**
   * How many entries in "replies_seen" are actually valid?
   */
  unsigned int replies_seen_off;

  /**
   * How long is the "replies_seen" array?
   */
  unsigned int replies_seen_size;
  
  /**
   * Priority with which this request was made.  If one of our clients
   * made the request, then this is the current priority that we are
   * using when initiating the request.  This value is used when
   * we decide to reward other peers with trust for providing a reply.
   */
  uint32_t priority;

  /**
   * Priority points left for us to spend when forwarding this request
   * to other peers.
   */
  uint32_t remaining_priority;

  /**
   * Number to mingle hashes for bloom-filter
   * tests with.
   */
  int32_t mingle;

  /**
   * TTL with which we saw this request (or, if we initiated, TTL that
   * we used for the request).
   */
  int32_t ttl;
  
  /**
   * Type of the content that this request is for.
   */
  uint32_t type;

};


/**
 * Our scheduler.
 */
static struct GNUNET_SCHEDULER_Handle *sched;

/**
 * Our configuration.
 */
const struct GNUNET_CONFIGURATION_Handle *cfg;

/**
 * Map of peer identifiers to "struct ConnectedPeer" (for that peer).
 */
static struct GNUNET_CONTAINER_MultiHashMap *connected_peers;

/**
 * Map of peer identifiers to "struct PendingRequest" (for that peer).
 */
static struct GNUNET_CONTAINER_MultiHashMap *peer_request_map;

/**
 * Map of query identifiers to "struct PendingRequest" (for that query).
 */
static struct GNUNET_CONTAINER_MultiHashMap *query_request_map;

/**
 * Heap with the request that will expire next at the top.  Contains
 * pointers of type "struct PendingRequest*"; these will *also* be
 * aliased from the "requests_by_peer" data structures and the
 * "requests_by_query" table.  Note that requests from our clients
 * don't expire and are thus NOT in the "requests_by_expiration"
 * (or the "requests_by_peer" tables).
 */
static struct GNUNET_CONTAINER_Heap *requests_by_expiration_heap;

/**
 * Linked list of clients we are currently processing requests for.
 */
struct ClientList *client_list;

/**
 * Pointer to handle to the core service (points to NULL until we've
 * connected to it).
 */
struct GNUNET_CORE_Handle *core;


/* ******************* clean up functions ************************ */


/**
 * We're done with a particular message list entry.
 * Free all associated resources.
 * 
 * @param pml entry to destroy
 */
static void
destroy_pending_message_list_entry (struct PendingMessageList *pml)
{
  GNUNET_CONTAINER_DLL_remove (pml->req->pending_head,
			       pml->req->pending_tail,
			       pml);
  GNUNET_CONTAINER_DLL_remove (pml->target->pending_messages_head,
			       pml->target->pending_messages_tail,
			       pml->pm);
  pml->target->pending_requests--;
  GNUNET_free (pml->pm);
  GNUNET_free (pml);
}


/**
 * Destroy the given pending message (and call the respective
 * continuation).
 *
 * @param pm message to destroy
 * @param tpid id of peer that the message was delivered to, or 0 for none
 */
static void
destroy_pending_message (struct PendingMessage *pm,
			 GNUNET_PEER_Id tpid)
{
  struct PendingMessageList *pml = pm->pml;

  GNUNET_assert (pml->pm == pm);
  GNUNET_assert ( (tpid == 0) || (tpid == pml->target->pid) );
  pm->cont (pm->cont_cls, 0);  
  destroy_pending_message_list_entry (pml);
}



/**
 * We're done processing a particular request.
 * Free all associated resources.
 *
 * @param pr request to destroy
 */
static void
destroy_pending_request (struct PendingRequest *pr)
{
  struct GNUNET_PeerIdentity pid;

  if (pr->hnode != NULL)
    {
      GNUNET_CONTAINER_heap_remove_node (requests_by_expiration_heap,
					 pr->hnode);
      pr->hnode = NULL;
    }
  /* might have already been removed from map
     in 'process_reply' if there was a unique 
     reply; hence ignore the return value here */
  (void) GNUNET_CONTAINER_multihashmap_remove (query_request_map,
					       &pr->query,
					       pr);
  if (pr->drq != NULL)
    {
      GNUNET_FS_drq_get_cancel (pr->drq);
      pr->drq = NULL;
    }
  if (pr->client_request_list != NULL)
    {
      GNUNET_CONTAINER_DLL_remove (pr->client_request_list->client_list->rl_head,
				   pr->client_request_list->client_list->rl_tail,
				   pr->client_request_list);
      GNUNET_free (pr->client_request_list);
      pr->client_request_list = NULL;
    }
  if (pr->pht_entry != NULL)
    {
      GNUNET_PEER_resolve (pr->pht_entry->cp->pid,
			   &pid);
      GNUNET_CONTAINER_multihashmap_remove (peer_request_map,
					    &pid.hashPubKey,
					    pr->pht_entry);
      GNUNET_free (pr->pht_entry);
      pr->pht_entry = NULL;
    }
  if (pr->bf != NULL)
    {
      GNUNET_CONTAINER_bloomfilter_free (pr->bf);					 
      pr->bf = NULL;
    }
  if (pr->irc != NULL)
    {
      GNUNET_CORE_peer_change_preference_cancel (pr->irc);
      pr->irc = NULL;
    }
  if (pr->replies_seen != NULL)
    {
      GNUNET_free (pr->replies_seen);
      pr->replies_seen = NULL;
    }
  if (pr->task != GNUNET_SCHEDULER_NO_TASK)
    {
      GNUNET_SCHEDULER_cancel (sched,
			       pr->task);
      pr->task = GNUNET_SCHEDULER_NO_TASK;
    }
  while (NULL != pr->pending_head)    
    destroy_pending_message_list_entry (pr->pending_head);
  GNUNET_PEER_change_rc (pr->target_pid, -1);
  if (pr->used_pids != NULL)
    {
      GNUNET_PEER_decrement_rcs (pr->used_pids, pr->used_pids_off);
      GNUNET_free (pr->used_pids);
      pr->used_pids_off = 0;
      pr->used_pids_size = 0;
      pr->used_pids = NULL;
    }
  GNUNET_free (pr);
}


/**
 * Method called whenever a given peer connects.
 *
 * @param cls closure, not used
 * @param peer peer identity this notification is about
 * @param latency reported latency of the connection with 'other'
 * @param distance reported distance (DV) to 'other' 
 */
static void 
peer_connect_handler (void *cls,
		      const struct
		      GNUNET_PeerIdentity * peer,
		      struct GNUNET_TIME_Relative latency,
		      uint32_t distance)
{
  struct ConnectedPeer *cp;

  cp = GNUNET_malloc (sizeof (struct ConnectedPeer));
  cp->pid = GNUNET_PEER_intern (peer);
  GNUNET_CONTAINER_multihashmap_put (connected_peers,
				     &peer->hashPubKey,
				     cp,
				     GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_ONLY);
}


/**
 * Free (each) request made by the peer.
 *
 * @param cls closure, points to peer that the request belongs to
 * @param key current key code
 * @param value value in the hash map
 * @return GNUNET_YES (we should continue to iterate)
 */
static int
destroy_request (void *cls,
		 const GNUNET_HashCode * key,
		 void *value)
{
  const struct GNUNET_PeerIdentity * peer = cls;
  struct PendingRequest *pr = value;
  
  GNUNET_CONTAINER_multihashmap_remove (peer_request_map,
					&peer->hashPubKey,
					pr);
  destroy_pending_request (pr);
  return GNUNET_YES;
}


/**
 * Method called whenever a peer disconnects.
 *
 * @param cls closure, not used
 * @param peer peer identity this notification is about
 */
static void
peer_disconnect_handler (void *cls,
			 const struct
			 GNUNET_PeerIdentity * peer)
{
  struct ConnectedPeer *cp;
  struct PendingMessage *pm;
  unsigned int i;

  GNUNET_CONTAINER_multihashmap_get_multiple (peer_request_map,
					      &peer->hashPubKey,
					      &destroy_request,
					      (void*) peer);
  cp = GNUNET_CONTAINER_multihashmap_get (connected_peers,
					  &peer->hashPubKey);
  if (cp == NULL)
    return;
  for (i=0;i<CS2P_SUCCESS_LIST_SIZE;i++)
    {
      if (NULL != cp->last_client_replies[i])
	{
	  GNUNET_SERVER_client_drop (cp->last_client_replies[i]);
	  cp->last_client_replies[i] = NULL;
	}
    }
  GNUNET_CONTAINER_multihashmap_remove (connected_peers,
					&peer->hashPubKey,
					cp);
  GNUNET_PEER_change_rc (cp->pid, -1);
  GNUNET_PEER_decrement_rcs (cp->last_p2p_replies, P2P_SUCCESS_LIST_SIZE);
  if (NULL != cp->cth)
    GNUNET_CORE_notify_transmit_ready_cancel (cp->cth);
  while (NULL != (pm = cp->pending_messages_head))
    destroy_pending_message (pm, 0 /* delivery failed */);
  GNUNET_break (0 == cp->pending_requests);
  GNUNET_free (cp);
}


/**
 * Iterator over hash map entries that removes all occurences
 * of the given 'client' from the 'last_client_replies' of the
 * given connected peer.
 *
 * @param cls closure, the 'struct GNUNET_SERVER_Client*' to remove
 * @param key current key code (unused)
 * @param value value in the hash map (the 'struct ConnectedPeer*' to change)
 * @return GNUNET_YES (we should continue to iterate)
 */
static int
remove_client_from_last_client_replies (void *cls,
					const GNUNET_HashCode * key,
					void *value)
{
  struct GNUNET_SERVER_Client *client = cls;
  struct ConnectedPeer *cp = value;
  unsigned int i;

  for (i=0;i<CS2P_SUCCESS_LIST_SIZE;i++)
    {
      if (cp->last_client_replies[i] == client)
	{
	  GNUNET_SERVER_client_drop (cp->last_client_replies[i]);
	  cp->last_client_replies[i] = NULL;
	}
    }  
  return GNUNET_YES;
}


/**
 * A client disconnected.  Remove all of its pending queries.
 *
 * @param cls closure, NULL
 * @param client identification of the client
 */
static void
handle_client_disconnect (void *cls,
			  struct GNUNET_SERVER_Client
			  * client)
{
  struct ClientList *pos;
  struct ClientList *prev;
  struct ClientRequestList *rcl;
  struct ClientResponseMessage *creply;

  if (client == NULL)
    return; /* huh? is this allowed? */
  prev = NULL;
  pos = client_list;
  while ( (NULL != pos) &&
	  (pos->client != client) )
    {
      prev = pos;
      pos = pos->next;
    }
  if (pos == NULL)
    return; /* no requests pending for this client */
  while (NULL != (rcl = pos->rl_head))
    destroy_pending_request (rcl->req);
  if (prev == NULL)
    client_list = pos->next;
  else
    prev->next = pos->next;
  if (pos->th != NULL)
    {
      GNUNET_CONNECTION_notify_transmit_ready_cancel (pos->th);
      pos->th = NULL;
    }
  while (NULL != (creply = pos->res_head))
    {
      GNUNET_CONTAINER_DLL_remove (pos->res_head,
				   pos->res_tail,
				   creply);
      GNUNET_free (creply);
    }    
  GNUNET_SERVER_client_drop (pos->client);
  GNUNET_free (pos);
  GNUNET_CONTAINER_multihashmap_iterate (connected_peers,
					 &remove_client_from_last_client_replies,
					 client);
}


/**
 * Iterator to free peer entries.
 *
 * @param cls closure, unused
 * @param key current key code
 * @param value value in the hash map (peer entry)
 * @return GNUNET_YES (we should continue to iterate)
 */
static int 
clean_peer (void *cls,
	    const GNUNET_HashCode * key,
	    void *value)
{
  peer_disconnect_handler (NULL, (const struct GNUNET_PeerIdentity*) key);
  return GNUNET_YES;
}


/**
 * Task run during shutdown.
 *
 * @param cls unused
 * @param tc unused
 */
static void
shutdown_task (void *cls,
	       const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_CONTAINER_multihashmap_iterate (connected_peers,
					 &clean_peer,
					 NULL);
  GNUNET_CONTAINER_multihashmap_destroy (connected_peers);
  connected_peers = NULL;
  while (client_list != NULL)
    handle_client_disconnect (NULL,
			      client_list->client);
  GNUNET_break (0 == GNUNET_CONTAINER_heap_get_size (requests_by_expiration_heap));
  GNUNET_CONTAINER_heap_destroy (requests_by_expiration_heap);
  requests_by_expiration_heap = 0;
  GNUNET_break (0 == GNUNET_CONTAINER_multihashmap_size (query_request_map));
  GNUNET_CONTAINER_multihashmap_destroy (query_request_map);
  query_request_map = NULL;
  GNUNET_break (0 == GNUNET_CONTAINER_multihashmap_size (peer_request_map));
  GNUNET_CONTAINER_multihashmap_destroy (peer_request_map);
  peer_request_map = NULL;
  GNUNET_assert (NULL != core);
  GNUNET_CORE_disconnect (core);
  core = NULL;
  sched = NULL;
  cfg = NULL;  
}


/* ******************* Utility functions  ******************** */


/**
 * Transmit the given message by copying it to the target buffer
 * "buf".  "buf" will be NULL and "size" zero if the socket was closed
 * for writing in the meantime.  In that case, do nothing
 * (the disconnect or shutdown handler will take care of the rest).
 * If we were able to transmit messages and there are still more
 * pending, ask core again for further calls to this function.
 *
 * @param cls closure, pointer to the 'struct ConnectedPeer*'
 * @param size number of bytes available in buf
 * @param buf where the callee should write the message
 * @return number of bytes written to buf
 */
static size_t
transmit_to_peer (void *cls,
		  size_t size, void *buf)
{
  struct ConnectedPeer *cp = cls;
  char *cbuf = buf;
  struct GNUNET_PeerIdentity pid;
  struct PendingMessage *pm;
  size_t msize;
  
  cp->cth = NULL;
  if (NULL == buf)
    {
#if DEBUG_FS
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Dropping reply, core too busy.\n");
#endif
      return 0;
    }
  msize = 0;
  while ( (NULL != (pm = cp->pending_messages_head) ) &&
	  (pm->msize <= size) )
    {
      memcpy (&cbuf[msize], &pm[1], pm->msize);
      msize += pm->msize;
      size -= pm->msize;
      destroy_pending_message (pm, cp->pid);
    }
  if (NULL != pm)
    {
      GNUNET_PEER_resolve (cp->pid,
			   &pid);
      cp->cth = GNUNET_CORE_notify_transmit_ready (core,
						   pm->priority,
						   GNUNET_CONSTANTS_SERVICE_TIMEOUT,
						   &pid,
						   pm->msize,
						   &transmit_to_peer,
						   pm);
    }
  return msize;
}


/**
 * Add a message to the set of pending messages for the given peer.
 *
 * @param cp peer to send message to
 * @param pm message to queue
 * @param pr request on which behalf this message is being queued
 */
static void
add_to_pending_messages_for_peer (struct ConnectedPeer *cp,
				  struct PendingMessage *pm,
				  struct PendingRequest *pr)
{
  struct PendingMessage *pos;
  struct PendingMessageList *pml;
  struct GNUNET_PeerIdentity pid;

  GNUNET_assert (pm->next == NULL);
  GNUNET_assert (pm->pml == NULL);    
  pml = GNUNET_malloc (sizeof (struct PendingMessageList));
  pml->req = pr;
  pml->target = cp;
  pml->pm = pm;
  pm->pml = pml;  
  GNUNET_CONTAINER_DLL_insert (pr->pending_head,
			       pr->pending_tail,
			       pml);
  pos = cp->pending_messages_head;
  while ( (pos != NULL) &&
	  (pm->priority < pos->priority) )
    pos = pos->next;    
  GNUNET_CONTAINER_DLL_insert_after (cp->pending_messages_head,
				     cp->pending_messages_tail,
				     pos,
				     pm);
  cp->pending_requests++;
  if (cp->pending_requests > MAX_QUEUE_PER_PEER)
    destroy_pending_message (cp->pending_messages_tail, 0);  
  if (cp->cth == NULL)
    {
      /* need to schedule transmission */
      GNUNET_PEER_resolve (cp->pid, &pid);
      cp->cth = GNUNET_CORE_notify_transmit_ready (core,
						   cp->pending_messages_head->priority,
						   GNUNET_TIME_UNIT_FOREVER_REL,
						   &pid,
						   cp->pending_messages_head->msize,
						   &transmit_to_peer,
						   cp);
    }
  if (cp->cth == NULL)
    {
      /* FIXME: call stats (rare, bad case) */
    }
}


/**
 * Mingle hash with the mingle_number to produce different bits.
 */
static void
mingle_hash (const GNUNET_HashCode * in,
	     int32_t mingle_number, 
	     GNUNET_HashCode * hc)
{
  GNUNET_HashCode m;

  GNUNET_CRYPTO_hash (&mingle_number, 
		      sizeof (int32_t), 
		      &m);
  GNUNET_CRYPTO_hash_xor (&m, in, hc);
}


/**
 * Test if the load on this peer is too high
 * to even consider processing the query at
 * all.
 * 
 * @return GNUNET_YES if the load is too high, GNUNET_NO otherwise
 */
static int
test_load_too_high ()
{
  return GNUNET_NO; // FIXME
}


/* ******************* Pending Request Refresh Task ******************** */


/**
 * Function called after we either failed or succeeded
 * at transmitting a query to a peer.  
 *
 * @param cls the requests "struct PendingRequest*"
 * @param pid ID of receiving peer, 0 on transmission error
 */
static void
transmit_query_continuation (void *cls,
			     GNUNET_PEER_Id tpid)
{
  struct PendingRequest *pr = cls;

  if (tpid == 0)    
    return;    
  GNUNET_PEER_change_rc (tpid, 1);
  if (pr->used_pids_off == pr->used_pids_size)
    GNUNET_array_grow (pr->used_pids,
		       pr->used_pids_size,
		       pr->used_pids_size * 2 + 2);
  pr->used_pids[pr->used_pids_off++] = tpid;
}


#if 0
/**
 * How many bytes should a bloomfilter be if we have already seen
 * entry_count responses?  Note that BLOOMFILTER_K gives us the number
 * of bits set per entry.  Furthermore, we should not re-size the
 * filter too often (to keep it cheap).
 *
 * Since other peers will also add entries but not resize the filter,
 * we should generally pick a slightly larger size than what the
 * strict math would suggest.
 *
 * @return must be a power of two and smaller or equal to 2^15.
 */
static size_t
compute_bloomfilter_size (unsigned int entry_count)
{
  size_t size;
  unsigned int ideal = (entry_count * BLOOMFILTER_K) / 4;
  uint16_t max = 1 << 15;

  if (entry_count > max)
    return max;
  size = 8;
  while ((size < max) && (size < ideal))
    size *= 2;
  if (size > max)
    return max;
  return size;
}


/**
 * Recalculate our bloom filter for filtering replies.
 *
 * @param count number of entries we are filtering right now
 * @param mingle set to our new mingling value
 * @param bf_size set to the size of the bloomfilter
 * @param entries the entries to filter
 * @return updated bloomfilter, NULL for none
 */
static struct GNUNET_CONTAINER_BloomFilter *
refresh_bloomfilter (unsigned int count,
		     int32_t * mingle,
		     size_t *bf_size,
		     const GNUNET_HashCode *entries)
{
  struct GNUNET_CONTAINER_BloomFilter *bf;
  size_t nsize;
  unsigned int i;
  GNUNET_HashCode mhash;

  if (0 == count)
    return NULL;
  nsize = compute_bloomfilter_size (count);
  *mingle = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, -1);
  *bf_size = nsize;
  bf = GNUNET_CONTAINER_bloomfilter_init (NULL, 
					  nsize,
					  BLOOMFILTER_K);
  for (i=0;i<count;i++)
    {
      mingle_hash (&entries[i], *mingle, &mhash);
      GNUNET_CONTAINER_bloomfilter_add (bf, &mhash);
    }
  return bf;
}
#endif


/**
 * We use a random delay to make the timing of requests less
 * predictable.  This function returns such a random delay.
 *
 * FIXME: make schedule dependent on the specifics of the request?
 * Or bandwidth and number of connected peers and load?
 *
 * @return random delay to use for some request, between 0 and TTL_DECREMENT ms
 */
static struct GNUNET_TIME_Relative
get_processing_delay ()
{
  return GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_MILLISECONDS,
					GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK,
								  TTL_DECREMENT));
}


/**
 * Function called after we've tried to reserve a certain amount of
 * bandwidth for a reply.  Check if we succeeded and if so send our
 * query.
 *
 * @param cls the requests "struct PendingRequest*"
 * @param peer identifies the peer
 * @param bpm_in set to the current bandwidth limit (receiving) for this peer
 * @param bpm_out set to the current bandwidth limit (sending) for this peer
 * @param amount set to the amount that was actually reserved or unreserved
 * @param preference current traffic preference for the given peer
 */
static void
target_reservation_cb (void *cls,
		       const struct
		       GNUNET_PeerIdentity * peer,
		       unsigned int bpm_in,
		       unsigned int bpm_out,
		       int amount,
		       uint64_t preference)
{
  struct PendingRequest *pr = cls;
  struct ConnectedPeer *cp;
  struct PendingMessage *pm;
  struct GetMessage *gm;
  GNUNET_HashCode *ext;
  char *bfdata;
  size_t msize;
  unsigned int k;

  pr->irc = NULL;
  GNUNET_assert (peer != NULL);
  if (amount != DBLOCK_SIZE) 
    {
      /* FIXME: call stats... */
      return; /* this target round failed */    
    }
  // (3) transmit, update ttl/priority
  cp = GNUNET_CONTAINER_multihashmap_get (connected_peers,
					  &peer->hashPubKey);
  if (cp == NULL)
    {
      /* Peer must have just left */
      return;
    }
  /* build message and insert message into priority queue */
  k = 0; // FIXME: count hash codes!
  msize = sizeof (struct GetMessage) + pr->bf_size + k * sizeof(GNUNET_HashCode);
  GNUNET_assert (msize < GNUNET_SERVER_MAX_MESSAGE_SIZE);
  pm = GNUNET_malloc (sizeof (struct PendingMessage) + msize);
  pm->msize = msize;
  gm = (struct GetMessage*) &pm[1];
  gm->header.type = htons (GNUNET_MESSAGE_TYPE_FS_GET);
  gm->header.size = htons (msize);
  gm->type = htonl (pr->type);
  pr->remaining_priority /= 2;
  gm->priority = htonl (pr->remaining_priority);
  gm->ttl = htonl (pr->ttl);
  gm->filter_mutator = htonl(pr->mingle); // FIXME: bad endianess conversion?
  gm->hash_bitmap = htonl (42); // FIXME!
  gm->query = pr->query;
  ext = (GNUNET_HashCode*) &gm[1];
  // FIXME: setup "ext[0]..[k-1]"
  bfdata = (char *) &ext[k];
  if (pr->bf != NULL)
    GNUNET_CONTAINER_bloomfilter_get_raw_data (pr->bf,
					       bfdata,
					       pr->bf_size);
  pm->cont = &transmit_query_continuation;
  pm->cont_cls = pr;
  add_to_pending_messages_for_peer (cp, pm, pr);
}



/**
 * Closure used for "target_peer_select_cb".
 */
struct PeerSelectionContext 
{
  /**
   * The request for which we are selecting
   * peers.
   */
  struct PendingRequest *pr;

  /**
   * Current "prime" target.
   */
  struct GNUNET_PeerIdentity target;

  /**
   * How much do we like this target?
   */
  double target_score;

};


/**
 * Function called for each connected peer to determine
 * which one(s) would make good targets for forwarding.
 *
 * @param cls closure (struct PeerSelectionContext)
 * @param key current key code (peer identity)
 * @param value value in the hash map (struct ConnectedPeer)
 * @return GNUNET_YES if we should continue to
 *         iterate,
 *         GNUNET_NO if not.
 */
static int
target_peer_select_cb (void *cls,
		       const GNUNET_HashCode * key,
		       void *value)
{
  struct PeerSelectionContext *psc = cls;
  struct ConnectedPeer *cp = value;
  struct PendingRequest *pr = psc->pr;
  double score;
  unsigned int i;
  
  /* 1) check if we have already (recently) forwarded to this peer */
  for (i=0;i<pr->used_pids_off;i++)
    if (pr->used_pids[i] == cp->pid)
      return GNUNET_YES; /* skip */
  // 2) calculate how much we'd like to forward to this peer
  score = 42; // FIXME!
  // FIXME: also need API to gather data on responsiveness
  // of this peer (we have fields for that in 'cp', but
  // they are never set!)
  
  /* store best-fit in closure */
  if (score > psc->target_score)
    {
      psc->target_score = score;
      psc->target.hashPubKey = *key; 
    }
  return GNUNET_YES;
}
  

/**
 * We're processing a GET request from another peer and have decided
 * to forward it to other peers.  This function is called periodically
 * and should forward the request to other peers until we have all
 * possible replies.  If we have transmitted the *only* reply to
 * the initiator we should destroy the pending request.  If we have
 * many replies in the queue to the initiator, we should delay sending
 * out more queries until the reply queue has shrunk some.
 *
 * @param cls our "struct ProcessGetContext *"
 * @param tc unused
 */
static void
forward_request_task (void *cls,
		     const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct PendingRequest *pr = cls;
  struct PeerSelectionContext psc;
  struct ConnectedPeer *cp; 

  pr->task = GNUNET_SCHEDULER_add_delayed (sched,
					   get_processing_delay (),
					   &forward_request_task,
					   pr);
  if (pr->irc != NULL)
    return; /* previous request still pending */
  /* (1) select target */
  psc.pr = pr;
  psc.target_score = DBL_MIN;
  GNUNET_CONTAINER_multihashmap_iterate (connected_peers,
					 &target_peer_select_cb,
					 &psc);  
  if (psc.target_score == DBL_MIN)
    return; /* nobody selected */

  /* (2) reserve reply bandwidth */
  cp = GNUNET_CONTAINER_multihashmap_get (connected_peers,
					  &psc.target.hashPubKey);
  pr->irc = GNUNET_CORE_peer_change_preference (sched, cfg,
						&psc.target,
						GNUNET_CONSTANTS_SERVICE_TIMEOUT, 
						(uint32_t) -1 /* no limit */, 
						DBLOCK_SIZE, 
						(uint64_t) cp->inc_preference,
						&target_reservation_cb,
						pr);
  cp->inc_preference = 0.0;
}


/* **************************** P2P PUT Handling ************************ */


/**
 * Function called after we either failed or succeeded
 * at transmitting a reply to a peer.  
 *
 * @param cls the requests "struct PendingRequest*"
 * @param pid ID of receiving peer, 0 on transmission error
 */
static void
transmit_reply_continuation (void *cls,
			     GNUNET_PEER_Id tpid)
{
  struct PendingRequest *pr = cls;

  switch (pr->type)
    {
    case GNUNET_DATASTORE_BLOCKTYPE_DBLOCK:
    case GNUNET_DATASTORE_BLOCKTYPE_IBLOCK:
      /* only one reply expected, done with the request! */
      destroy_pending_request (pr);
      break;
    case GNUNET_DATASTORE_BLOCKTYPE_KBLOCK:
    case GNUNET_DATASTORE_BLOCKTYPE_SBLOCK:
      break;
    default:
      GNUNET_break (0);
      break;
    }
}


/**
 * Check if the given KBlock is well-formed.
 *
 * @param kb the kblock data (or at least "dsize" bytes claiming to be one)
 * @param dsize size of "kb" in bytes; check for < sizeof(struct KBlock)!
 * @param query where to store the query that this block answers
 * @return GNUNET_OK if this is actually a well-formed KBlock
 */
static int
check_kblock (const struct KBlock *kb,
	      size_t dsize,
	      GNUNET_HashCode *query)
{
  if (dsize < sizeof (struct KBlock))
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
  if (dsize - sizeof (struct KBlock) !=
      ntohs (kb->purpose.size) 
      - sizeof (struct GNUNET_CRYPTO_RsaSignaturePurpose) 
      - sizeof(struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded) ) 
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
  if (GNUNET_OK !=
      GNUNET_CRYPTO_rsa_verify (GNUNET_SIGNATURE_PURPOSE_FS_KBLOCK,
				&kb->purpose,
				&kb->signature,
				&kb->keyspace)) 
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
  if (query != NULL)
    GNUNET_CRYPTO_hash (&kb->keyspace,
			sizeof (struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded),
			query);
  return GNUNET_OK;
}


/**
 * Check if the given SBlock is well-formed.
 *
 * @param sb the sblock data (or at least "dsize" bytes claiming to be one)
 * @param dsize size of "kb" in bytes; check for < sizeof(struct SBlock)!
 * @param query where to store the query that this block answers
 * @param namespace where to store the namespace that this block belongs to
 * @return GNUNET_OK if this is actually a well-formed SBlock
 */
static int
check_sblock (const struct SBlock *sb,
	      size_t dsize,
	      GNUNET_HashCode *query,	
	      GNUNET_HashCode *namespace)
{
  if (dsize < sizeof (struct SBlock))
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
  if (dsize !=
      ntohs (sb->purpose.size) + sizeof (struct GNUNET_CRYPTO_RsaSignature))
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
  if (GNUNET_OK !=
      GNUNET_CRYPTO_rsa_verify (GNUNET_SIGNATURE_PURPOSE_FS_SBLOCK,
				&sb->purpose,
				&sb->signature,
				&sb->subspace)) 
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
  if (query != NULL)
    *query = sb->identifier;
  if (namespace != NULL)
    GNUNET_CRYPTO_hash (&sb->subspace,
			sizeof (struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded),
			namespace);
  return GNUNET_OK;
}


/**
 * Transmit the given message by copying it to the target buffer
 * "buf".  "buf" will be NULL and "size" zero if the socket was closed
 * for writing in the meantime.  In that case, do nothing
 * (the disconnect or shutdown handler will take care of the rest).
 * If we were able to transmit messages and there are still more
 * pending, ask core again for further calls to this function.
 *
 * @param cls closure, pointer to the 'struct ClientList*'
 * @param size number of bytes available in buf
 * @param buf where the callee should write the message
 * @return number of bytes written to buf
 */
static size_t
transmit_to_client (void *cls,
		  size_t size, void *buf)
{
  struct ClientList *cl = cls;
  char *cbuf = buf;
  struct ClientResponseMessage *creply;
  size_t msize;
  
  cl->th = NULL;
  if (NULL == buf)
    {
#if DEBUG_FS
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Not sending reply, client communication problem.\n");
#endif
      return 0;
    }
  msize = 0;
  while ( (NULL != (creply = cl->res_head) ) &&
	  (creply->msize <= size) )
    {
      memcpy (&cbuf[msize], &creply[1], creply->msize);
      msize += creply->msize;
      size -= creply->msize;
      GNUNET_CONTAINER_DLL_remove (cl->res_head,
				   cl->res_tail,
				   creply);
      GNUNET_free (creply);
    }
  if (NULL != creply)
    cl->th = GNUNET_SERVER_notify_transmit_ready (cl->client,
						  creply->msize,
						  GNUNET_TIME_UNIT_FOREVER_REL,
						  &transmit_to_client,
						  cl);
  return msize;
}


/**
 * Closure for "process_reply" function.
 */
struct ProcessReplyClosure
{
  /**
   * The data for the reply.
   */
  const void *data;

  // FIXME: add 'struct ConnectedPeer' to track 'last_xxx_replies' here!

  /**
   * When the reply expires.
   */
  struct GNUNET_TIME_Absolute expiration;

  /**
   * Size of data.
   */
  size_t size;

  /**
   * Namespace that this reply belongs to
   * (if it is of type SBLOCK).
   */
  GNUNET_HashCode namespace;

  /**
   * Type of the block.
   */
  uint32_t type;

  /**
   * How much was this reply worth to us?
   */
  uint32_t priority;
};


/**
 * We have received a reply; handle it!
 *
 * @param cls response (struct ProcessReplyClosure)
 * @param key our query
 * @param value value in the hash map (info about the query)
 * @return GNUNET_YES (we should continue to iterate)
 */
static int
process_reply (void *cls,
	       const GNUNET_HashCode * key,
	       void *value)
{
  struct ProcessReplyClosure *prq = cls;
  struct PendingRequest *pr = value;
  struct PendingMessage *reply;
  struct ClientResponseMessage *creply;
  struct ClientList *cl;
  struct PutMessage *pm;
  struct ContentMessage *cm;
  struct ConnectedPeer *cp;
  GNUNET_HashCode chash;
  GNUNET_HashCode mhash;
  size_t msize;
  uint32_t prio;

  
  GNUNET_CRYPTO_hash (prq->data,
		      prq->size,
		      &chash);
  switch (prq->type)
    {
    case GNUNET_DATASTORE_BLOCKTYPE_DBLOCK:
    case GNUNET_DATASTORE_BLOCKTYPE_IBLOCK:
      /* only possible reply, stop requesting! */
      while (NULL != pr->pending_head)
	destroy_pending_message_list_entry (pr->pending_head);
      GNUNET_break (GNUNET_YES ==
		    GNUNET_CONTAINER_multihashmap_remove (query_request_map,
							  key,
							  prq));
      break;
    case GNUNET_DATASTORE_BLOCKTYPE_SBLOCK:
      if (0 != memcmp (pr->namespace,
		       &prq->namespace,
		       sizeof (GNUNET_HashCode)))
	return GNUNET_YES; /* wrong namespace */	
      /* then: fall-through! */
    case GNUNET_DATASTORE_BLOCKTYPE_KBLOCK:
      if (pr->bf != NULL) 
	{
	  mingle_hash (&chash, pr->mingle, &mhash);
	  if (GNUNET_YES == GNUNET_CONTAINER_bloomfilter_test (pr->bf,
							       &mhash))
	    return GNUNET_YES; /* duplicate */
	  GNUNET_CONTAINER_bloomfilter_add (pr->bf,
					    &mhash);
	}
      if (pr->client_request_list != NULL)
	{
	  cl = pr->client_request_list->client_list;
	  if (pr->replies_seen_size == pr->replies_seen_off)
	    {
	      GNUNET_array_grow (pr->replies_seen,
				 pr->replies_seen_size,
				 pr->replies_seen_size * 2 + 4);
	      // FIXME: recalculate BF!
	    }
	  pr->replies_seen[pr->replies_seen_off++] = chash;
	}
      break;
    case GNUNET_DATASTORE_BLOCKTYPE_SKBLOCK:
      // FIXME: any checks against duplicates for SKBlocks?
      break;
    default:
      GNUNET_break (0);
      return GNUNET_YES;
    }
  prio = pr->priority;
  prq->priority += pr->remaining_priority;
  pr->remaining_priority = 0;
  if (pr->client_request_list != NULL)
    {
      msize = sizeof (struct PutMessage) + prq->size;
      creply = GNUNET_malloc (msize + sizeof (struct ClientResponseMessage));
      creply->msize = msize;
      creply->client_list = cl;
      GNUNET_CONTAINER_DLL_insert_after (cl->res_head,
					 cl->res_tail,
					 cl->res_tail,
					 creply);      
      pm = (struct PutMessage*) &creply[1];
      pm->header.type = htons (GNUNET_MESSAGE_TYPE_FS_PUT);
      pm->header.size = htons (msize);
      pm->type = htonl (prq->type);
      pm->expiration = GNUNET_TIME_relative_hton (GNUNET_TIME_absolute_get_remaining (prq->expiration));
      memcpy (&creply[1], prq->data, prq->size);      
      if (NULL == cl->th)
	cl->th = GNUNET_SERVER_notify_transmit_ready (cl->client,
						      msize,
						      GNUNET_TIME_UNIT_FOREVER_REL,
						      &transmit_to_client,
						      cl);
      GNUNET_break (cl->th != NULL);
    }
  else
    {
      cp = pr->pht_entry->cp;
      msize = sizeof (struct ContentMessage) + prq->size;
      reply = GNUNET_malloc (msize + sizeof (struct PendingMessage));
      reply->cont = &transmit_reply_continuation;
      reply->cont_cls = pr;
      reply->msize = msize;
      reply->priority = (uint32_t) -1; /* send replies first! */
      cm = (struct ContentMessage*) &reply[1];
      cm->header.type = htons (GNUNET_MESSAGE_TYPE_FS_CONTENT);
      cm->header.size = htons (msize);
      cm->type = htonl (prq->type);
      cm->expiration = GNUNET_TIME_absolute_hton (prq->expiration);
      memcpy (&reply[1], prq->data, prq->size);
      add_to_pending_messages_for_peer (cp, reply, pr);
    }


  // FIXME: implement hot-path routing statistics keeping!
  return GNUNET_YES;
}


/**
 * Handle P2P "PUT" message.
 *
 * @param cls closure, always NULL
 * @param other the other peer involved (sender or receiver, NULL
 *        for loopback messages where we are both sender and receiver)
 * @param message the actual message
 * @param latency reported latency of the connection with 'other'
 * @param distance reported distance (DV) to 'other' 
 * @return GNUNET_OK to keep the connection open,
 *         GNUNET_SYSERR to close it (signal serious error)
 */
static int
handle_p2p_put (void *cls,
		const struct GNUNET_PeerIdentity *other,
		const struct GNUNET_MessageHeader *message,
		struct GNUNET_TIME_Relative latency,
		uint32_t distance)
{
  const struct PutMessage *put;
  uint16_t msize;
  size_t dsize;
  uint32_t type;
  struct GNUNET_TIME_Absolute expiration;
  GNUNET_HashCode query;
  struct ProcessReplyClosure prq;

  msize = ntohs (message->size);
  if (msize < sizeof (struct PutMessage))
    {
      GNUNET_break_op(0);
      return GNUNET_SYSERR;
    }
  put = (const struct PutMessage*) message;
  dsize = msize - sizeof (struct PutMessage);
  type = ntohl (put->type);
  expiration = GNUNET_TIME_relative_to_absolute (GNUNET_TIME_relative_ntoh (put->expiration));

  /* first, validate! */
  switch (type)
    {
    case GNUNET_DATASTORE_BLOCKTYPE_DBLOCK:
    case GNUNET_DATASTORE_BLOCKTYPE_IBLOCK:
      GNUNET_CRYPTO_hash (&put[1], dsize, &query);
      break;
    case GNUNET_DATASTORE_BLOCKTYPE_KBLOCK:
      if (GNUNET_OK !=
	  check_kblock ((const struct KBlock*) &put[1],
			dsize,
			&query))
	return GNUNET_SYSERR;
      break;
    case GNUNET_DATASTORE_BLOCKTYPE_SBLOCK:
      if (GNUNET_OK !=
	  check_sblock ((const struct SBlock*) &put[1],
			dsize,
			&query,
			&prq.namespace))
	return GNUNET_SYSERR;
      break;
    case GNUNET_DATASTORE_BLOCKTYPE_SKBLOCK:
      // FIXME -- validate SKBLOCK!
      GNUNET_break (0);
      return GNUNET_OK;
    default:
      /* unknown block type */
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }

  /* now, lookup 'query' */
  prq.data = (const void*) &put[1];
  prq.size = dsize;
  prq.type = type;
  prq.expiration = expiration;
  prq.priority = 0;
  GNUNET_CONTAINER_multihashmap_get_multiple (query_request_map,
					      &query,
					      &process_reply,
					      &prq);
  // FIXME: if migration is on and load is low,
  // queue to store data in datastore;
  // use "prq.priority" for that!
  return GNUNET_OK;
}


/* **************************** P2P GET Handling ************************ */


/**
 * We're processing (local) results for a search request
 * from another peer.  Pass applicable results to the
 * peer and if we are done either clean up (operation
 * complete) or forward to other peers (more results possible).
 *
 * @param cls our closure (struct LocalGetContext)
 * @param key key for the content
 * @param size number of bytes in data
 * @param data content stored
 * @param type type of the content
 * @param priority priority of the content
 * @param anonymity anonymity-level for the content
 * @param expiration expiration time for the content
 * @param uid unique identifier for the datum;
 *        maybe 0 if no unique identifier is available
 */
static void
process_local_reply (void *cls,
		     const GNUNET_HashCode * key,
		     uint32_t size,
		     const void *data,
		     uint32_t type,
		     uint32_t priority,
		     uint32_t anonymity,
		     struct GNUNET_TIME_Absolute
		     expiration, 
		     uint64_t uid)
{
  struct PendingRequest *pr = cls;
  struct ProcessReplyClosure prq;
  GNUNET_HashCode dhash;
  GNUNET_HashCode mhash;
  GNUNET_HashCode query;
  
  pr->drq = NULL;
  if (NULL == key)
    {
      /* no more results */
      if (pr->task == GNUNET_SCHEDULER_NO_TASK)
	pr->task = GNUNET_SCHEDULER_add_now (sched,
					     &forward_request_task,
					     pr);      
      return;
    }
  if (type == GNUNET_DATASTORE_BLOCKTYPE_ONDEMAND)
    {
      if (GNUNET_OK != 
	  GNUNET_FS_handle_on_demand_block (key, size, data, type, priority, 
					    anonymity, expiration, uid, 
					    &process_local_reply,
					    pr))
	GNUNET_FS_drq_get_next (GNUNET_YES);
      return;
    }
  /* check for duplicates */
  GNUNET_CRYPTO_hash (data, size, &dhash);
  mingle_hash (&dhash, 
	       pr->mingle,
	       &mhash);
  if ( (pr->bf != NULL) &&
       (GNUNET_YES ==
	GNUNET_CONTAINER_bloomfilter_test (pr->bf,
					   &mhash)) )
    {      
#if DEBUG_FS
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Result from datastore filtered by bloomfilter.\n");
#endif
      GNUNET_FS_drq_get_next (GNUNET_YES);
      return;
    }
  pr->results_found++;
  if ( (pr->type == GNUNET_DATASTORE_BLOCKTYPE_KBLOCK) ||
       (pr->type == GNUNET_DATASTORE_BLOCKTYPE_SBLOCK) ||
       (pr->type == GNUNET_DATASTORE_BLOCKTYPE_SKBLOCK) )
    {
      if (pr->bf == NULL)
	{
	  pr->bf_size = 32;
	  pr->bf = GNUNET_CONTAINER_bloomfilter_init (NULL,
						      pr->bf_size, 
						      BLOOMFILTER_K);
	}
      GNUNET_CONTAINER_bloomfilter_add (pr->bf, 
					&mhash);
    }
  memset (&prq, 0, sizeof (prq));
  prq.data = data;
  prq.expiration = expiration;
  prq.size = size;  
  if ( (type == GNUNET_DATASTORE_BLOCKTYPE_SBLOCK) &&
       (GNUNET_OK != check_sblock ((const struct SBlock*) data,
				   size,
				   &query,
				   &prq.namespace)) )
    {
      GNUNET_break (0);
      /* FIXME: consider removing the block? */
      GNUNET_FS_drq_get_next (GNUNET_YES);
      return;
    }
  prq.type = type;
  prq.priority = priority;  
  process_reply (&prq, key, pr);
  
  if ( (GNUNET_YES == test_load_too_high()) ||
       (pr->results_found > 5 + 2 * pr->priority) )
    {
      GNUNET_FS_drq_get_next (GNUNET_NO);
      return;
    }
  GNUNET_FS_drq_get_next (GNUNET_YES);
}


/**
 * The priority level imposes a bound on the maximum
 * value for the ttl that can be requested.
 *
 * @param ttl_in requested ttl
 * @param prio given priority
 * @return ttl_in if ttl_in is below the limit,
 *         otherwise the ttl-limit for the given priority
 */
static int32_t
bound_ttl (int32_t ttl_in, uint32_t prio)
{
  unsigned long long allowed;

  if (ttl_in <= 0)
    return ttl_in;
  allowed = ((unsigned long long) prio) * TTL_DECREMENT / 1000; 
  if (ttl_in > allowed)      
    {
      if (allowed >= (1 << 30))
        return 1 << 30;
      return allowed;
    }
  return ttl_in;
}


/**
 * We've received a request with the specified priority.  Bound it
 * according to how much we trust the given peer.
 * 
 * @param prio_in requested priority
 * @param cp the peer making the request
 * @return effective priority
 */
static uint32_t
bound_priority (uint32_t prio_in,
		struct ConnectedPeer *cp)
{
  return 0; // FIXME!
}


/**
 * Handle P2P "GET" request.
 *
 * @param cls closure, always NULL
 * @param other the other peer involved (sender or receiver, NULL
 *        for loopback messages where we are both sender and receiver)
 * @param message the actual message
 * @param latency reported latency of the connection with 'other'
 * @param distance reported distance (DV) to 'other' 
 * @return GNUNET_OK to keep the connection open,
 *         GNUNET_SYSERR to close it (signal serious error)
 */
static int
handle_p2p_get (void *cls,
		const struct GNUNET_PeerIdentity *other,
		const struct GNUNET_MessageHeader *message,
		struct GNUNET_TIME_Relative latency,
		uint32_t distance)
{
  struct PendingRequest *pr;
  struct PeerRequestEntry *pre;
  struct ConnectedPeer *cp;
  struct ConnectedPeer *cps;
  struct GNUNET_TIME_Relative timeout;
  uint16_t msize;
  const struct GetMessage *gm;
  unsigned int bits;
  const GNUNET_HashCode *opt;
  uint32_t bm;
  size_t bfsize;
  uint32_t ttl_decrement;
  uint32_t type;
  double preference;

  msize = ntohs(message->size);
  if (msize < sizeof (struct GetMessage))
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }
  gm = (const struct GetMessage*) message;
  bm = ntohl (gm->hash_bitmap);
  bits = 0;
  while (bm > 0)
    {
      if (1 == (bm & 1))
	bits++;
      bm >>= 1;
    }
  if (msize < sizeof (struct GetMessage) + bits * sizeof (GNUNET_HashCode))
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;
    }  
  opt = (const GNUNET_HashCode*) &gm[1];
  bfsize = msize - sizeof (struct GetMessage) + bits * sizeof (GNUNET_HashCode);

  bm = ntohl (gm->hash_bitmap);
  if ( (0 != (bm & GET_MESSAGE_BIT_SKS_NAMESPACE)) &&
       (ntohl (gm->type) == GNUNET_DATASTORE_BLOCKTYPE_SBLOCK) )
    {
      GNUNET_break_op (0);
      return GNUNET_SYSERR;      
    }
  bits = 0;
  cps = GNUNET_CONTAINER_multihashmap_get (connected_peers,
					   &other->hashPubKey);
  GNUNET_assert (NULL != cps);
  if (0 != (bm & GET_MESSAGE_BIT_RETURN_TO))
    cp = GNUNET_CONTAINER_multihashmap_get (connected_peers,
					    &opt[bits++]);
  else
    cp = cps;
  if (cp == NULL)
    {
      /* FIXME: try connect? */
      return GNUNET_OK;
    }
  /* note that we can really only check load here since otherwise
     peers could find out that we are overloaded by not being
     disconnected after sending us a malformed query... */
  if (GNUNET_YES == test_load_too_high ())
    {
#if DEBUG_FS
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Dropping query from `%s', this peer is too busy.\n",
		  GNUNET_i2s (other));
#endif
      return GNUNET_OK;
    }

  pr = GNUNET_malloc (sizeof (struct PendingRequest) + 
		      (bm & GET_MESSAGE_BIT_SKS_NAMESPACE)?sizeof(GNUNET_HashCode):0);
  if ((bm & GET_MESSAGE_BIT_SKS_NAMESPACE))
    pr->namespace = (GNUNET_HashCode*) &pr[1];
  pr->type = ntohl (gm->type);
  pr->mingle = gm->filter_mutator;
  if (0 != (bm & GET_MESSAGE_BIT_SKS_NAMESPACE))
    memcpy (&pr[1], &opt[bits++], sizeof (GNUNET_HashCode));
  else if (pr->type == GNUNET_DATASTORE_BLOCKTYPE_SBLOCK)
    {
      GNUNET_break_op (0);
      GNUNET_free (pr);
      return GNUNET_SYSERR;
    }
  if (0 != (bm & GET_MESSAGE_BIT_TRANSMIT_TO))
    pr->target_pid = GNUNET_PEER_intern ((const struct GNUNET_PeerIdentity*) &opt[bits++]);

  pr->anonymity_level = 1;
  pr->priority = bound_priority (ntohl (gm->priority), cps);
  pr->ttl = bound_ttl (ntohl (gm->ttl), pr->priority);
  pr->query = gm->query;
  /* decrement ttl (always) */
  ttl_decrement = 2 * TTL_DECREMENT +
    GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK,
			      TTL_DECREMENT);
  if ( (pr->ttl < 0) &&
       (pr->ttl - ttl_decrement > 0) )
    {
#if DEBUG_FS
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Dropping query from `%s' due to TTL underflow.\n",
		  GNUNET_i2s (other));
#endif
      /* integer underflow => drop (should be very rare)! */
      GNUNET_free (pr);
      return GNUNET_OK;
    } 
  pr->ttl -= ttl_decrement;
  pr->start_time = GNUNET_TIME_absolute_get ();

  /* get bloom filter */
  if (bfsize > 0)
    {
      pr->bf = GNUNET_CONTAINER_bloomfilter_init ((const char*) &opt[bits],
						  bfsize,
						  BLOOMFILTER_K);
      pr->bf_size = bfsize;
    }

  /* FIXME: check somewhere if request already exists, and if so,
     recycle old state... */
  pre = GNUNET_malloc (sizeof (struct PeerRequestEntry));
  pre->cp = cp;
  pre->req = pr;
  GNUNET_CONTAINER_multihashmap_put (query_request_map,
				     &gm->query,
				     pre,
				     GNUNET_CONTAINER_MULTIHASHMAPOPTION_MULTIPLE);
  
  pr->hnode = GNUNET_CONTAINER_heap_insert (requests_by_expiration_heap,
					    pr,
					    GNUNET_TIME_absolute_get().value + pr->ttl);


  /* calculate change in traffic preference */
  preference = (double) pr->priority;
  if (preference < QUERY_BANDWIDTH_VALUE)
    preference = QUERY_BANDWIDTH_VALUE;
  cps->inc_preference += preference;

  /* process locally */
  type = pr->type;
  if (type == GNUNET_DATASTORE_BLOCKTYPE_DBLOCK)
    type = GNUNET_DATASTORE_BLOCKTYPE_ANY; /* to get on-demand as well */
  timeout = GNUNET_TIME_relative_multiply (BASIC_DATASTORE_REQUEST_DELAY,
					   (pr->priority + 1)); 
  pr->drq = GNUNET_FS_drq_get (&gm->query,
			       pr->type,			       
			       &process_local_reply,
			       pr,
			       timeout);

  /* Are multiple results possible?  If so, start processing remotely now! */
  switch (pr->type)
    {
    case GNUNET_DATASTORE_BLOCKTYPE_DBLOCK:
    case GNUNET_DATASTORE_BLOCKTYPE_IBLOCK:
      /* only one result, wait for datastore */
      break;
    default:
      pr->task = GNUNET_SCHEDULER_add_now (sched,
					   &forward_request_task,
					   pr);
    }

  /* make sure we don't track too many requests */
  if (GNUNET_CONTAINER_heap_get_size (requests_by_expiration_heap) > max_pending_requests)
    {
      pr = GNUNET_CONTAINER_heap_peek (requests_by_expiration_heap);
      destroy_pending_request (pr);
    }
  return GNUNET_OK;
}


/* **************************** CS GET Handling ************************ */


/**
 * Handle START_SEARCH-message (search request from client).
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 */
static void
handle_start_search (void *cls,
		     struct GNUNET_SERVER_Client *client,
		     const struct GNUNET_MessageHeader *message)
{
  static GNUNET_HashCode all_zeros;
  const struct SearchMessage *sm;
  struct ClientList *cl;
  struct ClientRequestList *crl;
  struct PendingRequest *pr;
  uint16_t msize;
  unsigned int sc;
  uint32_t type;
  
  msize = ntohs (message->size);
  if ( (msize < sizeof (struct SearchMessage)) ||
       (0 != (msize - sizeof (struct SearchMessage)) % sizeof (GNUNET_HashCode)) )
    {
      GNUNET_break (0);
      GNUNET_SERVER_receive_done (client,
				  GNUNET_SYSERR);
      return;
    }
  sc = (msize - sizeof (struct SearchMessage)) / sizeof (GNUNET_HashCode);
  sm = (const struct SearchMessage*) message;

  cl = client_list;
  while ( (cl != NULL) &&
	  (cl->client != client) )
    cl = cl->next;
  if (cl == NULL)
    {
      cl = GNUNET_malloc (sizeof (struct ClientList));
      cl->client = client;
      GNUNET_SERVER_client_keep (client);
      cl->next = client_list;
      client_list = cl;
    }
  type = ntohl (sm->type);

  /* FIXME: detect duplicate request; if duplicate, simply update (merge)
     'pr->replies_seen'! */
  pr = GNUNET_malloc (sizeof (struct PendingRequest) + 
		      (type == GNUNET_DATASTORE_BLOCKTYPE_SBLOCK)?sizeof(GNUNET_HashCode):0);
  crl = GNUNET_malloc (sizeof (struct ClientRequestList));
  crl->client_list = cl;
  GNUNET_CONTAINER_DLL_insert (cl->rl_head,
			       cl->rl_tail,
			       crl);  
  crl->req = pr;
  pr->type = type;
  pr->client_request_list = crl;
  GNUNET_array_grow (pr->replies_seen,
		     pr->replies_seen_size,
		     sc);
  memcpy (pr->replies_seen,
	  &sm[1],
	  sc * sizeof (GNUNET_HashCode));
  pr->replies_seen_off = sc;
  pr->anonymity_level = ntohl (sm->anonymity_level);
  pr->mingle = GNUNET_CRYPTO_random_u32(GNUNET_CRYPTO_QUALITY_WEAK,
					(uint32_t) -1);
  pr->query = sm->query;
  switch (type)
    {
    case GNUNET_DATASTORE_BLOCKTYPE_DBLOCK:
    case GNUNET_DATASTORE_BLOCKTYPE_IBLOCK:
      if (0 != memcmp (&sm->target,
		       &all_zeros,
		       sizeof (GNUNET_HashCode)))
	pr->target_pid = GNUNET_PEER_intern ((const struct GNUNET_PeerIdentity*) &sm->target);
      break;
    case GNUNET_DATASTORE_BLOCKTYPE_SBLOCK:
      pr->namespace = (GNUNET_HashCode*) &pr[1];
      memcpy (&pr[1], &sm->target, sizeof (GNUNET_HashCode));
      break;
    default:
      break;
    }
  pr->drq = GNUNET_FS_drq_get (&sm->query,
			       pr->type,			       
			       &process_local_reply,
			       pr,
			       GNUNET_TIME_UNIT_FOREVER_REL);
}


/* **************************** Startup ************************ */


/**
 * List of handlers for P2P messages
 * that we care about.
 */
static struct GNUNET_CORE_MessageHandler p2p_handlers[] =
  {
    { &handle_p2p_get, 
      GNUNET_MESSAGE_TYPE_FS_GET, 0 },
    { &handle_p2p_put, 
      GNUNET_MESSAGE_TYPE_FS_PUT, 0 },
    { NULL, 0, 0 }
  };


/**
 * List of handlers for the messages understood by this
 * service.
 */
static struct GNUNET_SERVER_MessageHandler handlers[] = {
  {&GNUNET_FS_handle_index_start, NULL, 
   GNUNET_MESSAGE_TYPE_FS_INDEX_START, 0},
  {&GNUNET_FS_handle_index_list_get, NULL, 
   GNUNET_MESSAGE_TYPE_FS_INDEX_LIST_GET, sizeof(struct GNUNET_MessageHeader) },
  {&GNUNET_FS_handle_unindex, NULL, GNUNET_MESSAGE_TYPE_FS_UNINDEX, 
   sizeof (struct UnindexMessage) },
  {&handle_start_search, NULL, GNUNET_MESSAGE_TYPE_FS_START_SEARCH, 
   0 },
  {NULL, NULL, 0, 0}
};


/**
 * Process fs requests.
 *
 * @param cls closure
 * @param s scheduler to use
 * @param server the initialized server
 * @param c configuration to use
 */
static int
main_init (struct GNUNET_SCHEDULER_Handle *s,
	   struct GNUNET_SERVER_Handle *server,
	   const struct GNUNET_CONFIGURATION_Handle *c)
{
  sched = s;
  cfg = c;
  core = GNUNET_CORE_connect (sched,
			      cfg,
			      GNUNET_TIME_UNIT_FOREVER_REL,
			      NULL,
			      NULL,
			      NULL,
			      &peer_connect_handler,
			      &peer_disconnect_handler,
			      NULL, GNUNET_NO,
			      NULL, GNUNET_NO,
			      p2p_handlers);
  if (NULL == core)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Failed to connect to `%s' service.\n"),
		  "core");
      return GNUNET_SYSERR;
    }  
  query_request_map = GNUNET_CONTAINER_multihashmap_create (128); // FIXME: get size from config
  peer_request_map = GNUNET_CONTAINER_multihashmap_create (128); // FIXME: get size from config
  requests_by_expiration_heap = GNUNET_CONTAINER_heap_create (GNUNET_CONTAINER_HEAP_ORDER_MIN); 
  GNUNET_SERVER_disconnect_notify (server, 
				   &handle_client_disconnect,
				   NULL);
  GNUNET_SERVER_add_handlers (server, handlers);
  GNUNET_SCHEDULER_add_delayed (sched,
				GNUNET_TIME_UNIT_FOREVER_REL,
				&shutdown_task,
				NULL);
  return GNUNET_OK;
}


/**
 * Process fs requests.
 *
 * @param cls closure
 * @param sched scheduler to use
 * @param server the initialized server
 * @param cfg configuration to use
 */
static void
run (void *cls,
     struct GNUNET_SCHEDULER_Handle *sched,
     struct GNUNET_SERVER_Handle *server,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  if ( (GNUNET_OK != GNUNET_FS_drq_init (sched, cfg)) ||
       (GNUNET_OK != GNUNET_FS_indexing_init (sched, cfg)) ||
       (GNUNET_OK != main_init (sched, server, cfg)) )
    {    
      GNUNET_SCHEDULER_shutdown (sched);
      return;   
    }
}


/**
 * The main function for the fs service.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  return (GNUNET_OK ==
          GNUNET_SERVICE_run (argc,
                              argv,
                              "fs",
			      GNUNET_SERVICE_OPTION_NONE,
			      &run, NULL)) ? 0 : 1;
}

/* end of gnunet-service-fs.c */
