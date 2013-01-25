/*
  This file is part of GNUnet.
  (C) 2012 Christian Grothoff (and other contributing authors)

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
 * @file testbed/gnunet-service-testbed.h
 * @brief data structures shared amongst components of TESTBED service
 * @author Sree Harsha Totakura
 */

#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_testbed_service.h"
#include "gnunet_transport_service.h"
#include "gnunet_core_service.h"

#include "testbed.h"
#include "testbed_api.h"
#include "testbed_api_operations.h"
#include "testbed_api_hosts.h"
#include "gnunet_testing_lib.h"


/**
 * Generic logging
 */
#define LOG(kind,...)                           \
  GNUNET_log (kind, __VA_ARGS__)

/**
 * Debug logging
 */
#define LOG_DEBUG(...)                          \
  LOG (GNUNET_ERROR_TYPE_DEBUG, __VA_ARGS__)

/**
 * By how much should the arrays lists grow
 */
#define LIST_GROW_STEP 10

/**
 * Default timeout for operations which may take some time
 */
#define TIMEOUT GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, 15)


/**
 * A routing entry
 */
struct Route
{
  /**
   * destination host
   */
  uint32_t dest;

  /**
   * The destination host is reachable thru
   */
  uint32_t thru;
};


/**
 * Context information for operations forwarded to subcontrollers
 */
struct ForwardedOperationContext
{
  /**
   * The next pointer for DLL
   */
  struct ForwardedOperationContext *next;

  /**
   * The prev pointer for DLL
   */
  struct ForwardedOperationContext *prev;
  
  /**
   * The generated operation context
   */
  struct OperationContext *opc;

  /**
   * The client to which we have to reply
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * Closure pointer
   */
  void *cls;

  /**
   * Task ID for the timeout task
   */
  GNUNET_SCHEDULER_TaskIdentifier timeout_task;

  /**
   * The id of the operation that has been forwarded
   */
  uint64_t operation_id;

  /**
   * The type of the operation which is forwarded
   */
  enum OperationType type;

};


/**
 * A DLL of host registrations to be made
 */
struct HostRegistration
{
  /**
   * next registration in the DLL
   */
  struct HostRegistration *next;

  /**
   * previous registration in the DLL
   */
  struct HostRegistration *prev;

  /**
   * The callback to call after this registration's status is available
   */
  GNUNET_TESTBED_HostRegistrationCompletion cb;

  /**
   * The closure for the above callback
   */
  void *cb_cls;

  /**
   * The host that has to be registered
   */
  struct GNUNET_TESTBED_Host *host;
};


/**
 * Context information used while linking controllers
 */
struct LinkControllersContext
{
  /**
   * The client which initiated the link controller operation
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * The ID of the operation
   */
  uint64_t operation_id;

};


/**
 * Structure representing a connected(directly-linked) controller
 */
struct Slave
{
  /**
   * The controller process handle if we had started the controller
   */
  struct GNUNET_TESTBED_ControllerProc *controller_proc;

  /**
   * The controller handle
   */
  struct GNUNET_TESTBED_Controller *controller;

  /**
   * The configuration of the slave. Cannot be NULL
   */
  struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * handle to lcc which is associated with this slave startup. Should be set to
   * NULL when the slave has successfully started up
   */
  struct LinkControllersContext *lcc;

  /**
   * Head of the host registration DLL
   */
  struct HostRegistration *hr_dll_head;

  /**
   * Tail of the host registration DLL
   */
  struct HostRegistration *hr_dll_tail;

  /**
   * The current host registration handle
   */
  struct GNUNET_TESTBED_HostRegistrationHandle *rhandle;

  /**
   * Hashmap to hold Registered host contexts
   */
  struct GNUNET_CONTAINER_MultiHashMap *reghost_map;

  /**
   * The id of the host this controller is running on
   */
  uint32_t host_id;

};


/**
 * A peer
 */

struct Peer
{
  
  union
  {
    struct
    {
      /**
       * The peer handle from testing API
       */
      struct GNUNET_TESTING_Peer *peer;

      /**
       * The modified (by GNUNET_TESTING_peer_configure) configuration this
       * peer is configured with
       */
      struct GNUNET_CONFIGURATION_Handle *cfg;
      
      /**
       * Is the peer running
       */
      int is_running;

    } local;

    struct
    {
      /**
       * The slave this peer is started through
       */
      struct Slave *slave;

      /**
       * The id of the remote host this peer is running on
       */
      uint32_t remote_host_id;

    } remote;

  } details;

  /**
   * Is this peer locally created?
   */
  int is_remote;

  /**
   * Our local reference id for this peer
   */
  uint32_t id;

  /**
   * References to peers are using in forwarded overlay contexts and remote
   * overlay connect contexts. A peer can only be destroyed after all such
   * contexts are destroyed. For this, we maintain a reference counter. When we
   * use a peer in any such context, we increment this counter. We decrement it
   * when we are destroying these contexts
   */
  uint32_t reference_cnt;

  /**
   * While destroying a peer, due to the fact that there could be references to
   * this peer, we delay the peer destroy to a further time. We do this by using
   * this flag to destroy the peer while destroying a context in which this peer
   * has been used. When the flag is set to 1 and reference_cnt = 0 we destroy
   * the peer
   */
  uint32_t destroy_flag;

};


/**
 * The main context information associated with the client which started us
 */
struct Context
{
  /**
   * The client handle associated with this context
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * The network address of the master controller
   */
  char *master_ip;

  /**
   * The TESTING system handle for starting peers locally
   */
  struct GNUNET_TESTING_System *system;
  
  /**
   * Our host id according to this context
   */
  uint32_t host_id;
};


/**
 * The structure for identifying a shared service
 */
struct SharedService
{
  /**
   * The name of the shared service
   */
  char *name;

  /**
   * Number of shared peers per instance of the shared service
   */
  uint32_t num_shared;

  /**
   * Number of peers currently sharing the service
   */
  uint32_t num_sharing;
};


/**
 * Context information to used during operations which forward the overlay
 * connect message
 */
struct ForwardedOverlayConnectContext
{
  /**
   * next ForwardedOverlayConnectContext in the DLL
   */
  struct ForwardedOverlayConnectContext *next;

  /**
   * previous ForwardedOverlayConnectContext in the DLL
   */
  struct ForwardedOverlayConnectContext *prev;

  /**
   * A copy of the original overlay connect message
   */
  struct GNUNET_MessageHeader *orig_msg;

  /**
   * The id of the operation which created this context information
   */
  uint64_t operation_id;

  /**
   * the id of peer 1
   */
  uint32_t peer1;
  
  /**
   * The id of peer 2
   */
  uint32_t peer2;
  
  /**
   * Id of the host where peer2 is running
   */
  uint32_t peer2_host_id;
};


/**
 * This context information will be created for each host that is registered at
 * slave controllers during overlay connects.
 */
struct RegisteredHostContext
{
  /**
   * The host which is being registered
   */
  struct GNUNET_TESTBED_Host *reg_host;

  /**
   * The host of the controller which has to connect to the above rhost
   */
  struct GNUNET_TESTBED_Host *host;

  /**
   * The gateway to which this operation is forwarded to
   */
  struct Slave *gateway;

  /**
   * The gateway through which peer2's controller can be reached
   */
  struct Slave *gateway2;

  /**
   * Handle for sub-operations
   */
  struct GNUNET_TESTBED_Operation *sub_op;

  /**
   * The client which initiated the link controller operation
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * Head of the ForwardedOverlayConnectContext DLL
   */
  struct ForwardedOverlayConnectContext *focc_dll_head;

  /**
   * Tail of the ForwardedOverlayConnectContext DLL
   */
  struct ForwardedOverlayConnectContext *focc_dll_tail;
  
  /**
   * Enumeration of states for this context
   */
  enum RHCState {

    /**
     * The initial state
     */
    RHC_INIT = 0,

    /**
     * State where we attempt to get peer2's controller configuration
     */
    RHC_GET_CFG,

    /**
     * State where we attempt to link the controller of peer 1 to the controller
     * of peer2
     */
    RHC_LINK,

    /**
     * State where we attempt to do the overlay connection again
     */
    RHC_OL_CONNECT
    
  } state;

};


/**
 * States of LCFContext
 */
enum LCFContextState
{
  /**
   * The Context has been initialized; Nothing has been done on it
   */
  INIT,

  /**
   * Delegated host has been registered at the forwarding controller
   */
  DELEGATED_HOST_REGISTERED,
  
  /**
   * The slave host has been registred at the forwarding controller
   */
  SLAVE_HOST_REGISTERED,
  
  /**
   * The context has been finished (may have error)
   */
  FINISHED
};


/**
 * Link controllers request forwarding context
 */
struct LCFContext
{
  /**
   * The gateway which will pass the link message to delegated host
   */
  struct Slave *gateway;

  /**
   * The controller link message that has to be forwarded to
   */
  struct GNUNET_TESTBED_ControllerLinkMessage *msg;

  /**
   * The client which has asked to perform this operation
   */
  struct GNUNET_SERVER_Client *client;

  /**
   * Handle for operations which are forwarded while linking controllers
   */
  struct ForwardedOperationContext *fopc;

  /**
   * The id of the operation which created this context
   */
  uint64_t operation_id;

  /**
   * The state of this context
   */
  enum LCFContextState state;

  /**
   * The delegated host
   */
  uint32_t delegated_host_id;

  /**
   * The slave host
   */
  uint32_t slave_host_id;

};


/**
 * Structure of a queue entry in LCFContext request queue
 */
struct LCFContextQueue
{
  /**
   * The LCFContext
   */
  struct LCFContext *lcf;

  /**
   * Head prt for DLL
   */
  struct LCFContextQueue *next;

  /**
   * Tail ptr for DLL
   */
  struct LCFContextQueue *prev;
};

/**
 * Our configuration
 */
struct GNUNET_CONFIGURATION_Handle *our_config;

/**
 * The master context; generated with the first INIT message
 */
extern struct Context *TESTBED_context;

/**
 * DLL head for forwarded operation contexts
 */
extern struct ForwardedOperationContext *fopcq_head;

/**
 * DLL tail for forwarded operation contexts
 */
extern struct ForwardedOperationContext *fopcq_tail;

/**
 * A list of peers we know about
 */
extern struct Peer **TESTBED_peer_list;

/**
 * Array of hosts
 */
extern struct GNUNET_TESTBED_Host **TESTBED_host_list;

/**
 * A list of directly linked neighbours
 */
extern struct Slave **TESTBED_slave_list;

/**
 * The size of the peer list
 */
extern unsigned int TESTBED_peer_list_size;

/**
 * The size of the host list
 */
extern unsigned int TESTBED_host_list_size;

/**
 * The size of directly linked neighbours list
 */
extern unsigned int TESTBED_slave_list_size;


/**
 * Queues a message in send queue for sending to the service
 *
 * @param client the client to whom the queued message has to be sent
 * @param msg the message to queue
 */
void
TESTBED_queue_message (struct GNUNET_SERVER_Client *client,
                       struct GNUNET_MessageHeader *msg);


/**
 * Function to destroy a peer
 *
 * @param peer the peer structure to destroy
 */
void
TESTBED_destroy_peer (struct Peer *peer);


/**
 * Looks up in the hello cache and returns the HELLO of the given peer
 *
 * @param id the peer identity of the peer whose HELLO has to be looked up
 * @return the HELLO message; NULL if not found
 */
const struct GNUNET_MessageHeader *
TESTBED_hello_cache_lookup (const struct GNUNET_PeerIdentity *id);

/**
 * Caches the HELLO of the given peer. Updates the HELLO if it was already
 * cached before
 *
 * @param id the peer identity of the peer whose HELLO has to be cached
 * @param hello the HELLO message
 */
void
TESTBED_hello_cache_add (const struct GNUNET_PeerIdentity *id,
                         const struct GNUNET_MessageHeader *hello);


/**
 * Initializes the cache
 *
 * @param size the size of the cache
 */
void
TESTBED_cache_init (unsigned int size);


/**
 * Clear cache
 */
void
TESTBED_cache_clear ();


/**
 * Finds the route with directly connected host as destination through which
 * the destination host can be reached
 *
 * @param host_id the id of the destination host
 * @return the route with directly connected destination host; NULL if no route
 *           is found
 */
struct Route *
TESTBED_find_dest_route (uint32_t host_id);


/**
 * Handler for GNUNET_MESSAGE_TYPE_TESTBED_OLCONNECT messages
 *
 * @param cls NULL
 * @param client identification of the client
 * @param message the actual message
 */
void
TESTBED_handle_overlay_connect (void *cls, struct GNUNET_SERVER_Client *client,
                                const struct GNUNET_MessageHeader *message);


/**
 * Adds a host registration's request to a slave's registration queue
 *
 * @param slave the slave controller at which the given host has to be
 *          registered 
 * @param cb the host registration completion callback
 * @param cb_cls the closure for the host registration completion callback
 * @param host the host which has to be registered
 */
void
TESTBED_queue_host_registration (struct Slave *slave,
                                 GNUNET_TESTBED_HostRegistrationCompletion cb,
                                 void *cb_cls,
                                 struct GNUNET_TESTBED_Host *host);


/**
 * Callback to relay the reply msg of a forwarded operation back to the client
 *
 * @param cls ForwardedOperationContext
 * @param msg the message to relay
 */
void
TESTBED_forwarded_operation_reply_relay (void *cls,
                                         const struct GNUNET_MessageHeader *msg);


/**
 * Task to free resources when forwarded operation has been timedout
 *
 * @param cls the ForwardedOperationContext
 * @param tc the task context from scheduler
 */
void
TESTBED_forwarded_operation_timeout (void *cls,
                                     const struct GNUNET_SCHEDULER_TaskContext *tc);


/**
 * Send operation failure message to client
 *
 * @param client the client to which the failure message has to be sent to
 * @param operation_id the id of the failed operation
 * @param emsg the error message; can be NULL
 */
void
TESTBED_send_operation_fail_msg (struct GNUNET_SERVER_Client *client,
                                 uint64_t operation_id, const char *emsg);


/**
 * Handler for GNUNET_MESSAGE_TYPE_TESTBED_REQUESTCONNECT messages
 *
 * @param cls NULL
 * @param client identification of the client
 * @param message the actual message
 */
void
TESTBED_handle_overlay_request_connect (void *cls,
                                        struct GNUNET_SERVER_Client *client, 
                                        const struct GNUNET_MessageHeader
                                        *message);


/**
 * Processes a forwarded overlay connect context in the queue of the given RegisteredHostContext
 *
 * @param rhc the RegisteredHostContext
 */
void
TESTBED_process_next_focc (struct RegisteredHostContext *rhc);


/**
 * Cleans up ForwardedOverlayConnectContext
 *
 * @param focc the ForwardedOverlayConnectContext to cleanup
 */
void
TESTBED_cleanup_focc (struct ForwardedOverlayConnectContext *focc);


/**
 * Clears all pending overlay connect contexts in queue
 */
void
TESTBED_free_occq ();


/**
 * Clears all pending remote overlay connect contexts in queue
 */
void
TESTBED_free_roccq ();



/* End of gnunet-service-testbed.h */
