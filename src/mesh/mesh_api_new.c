/*
     This file is part of GNUnet.
     (C) 2011 Christian Grothoff (and other contributing authors)

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
 * @file mesh/mesh_api_new.c
 * @brief mesh api: client implementation of mesh service
 * @author Bartlomiej Polot
 */

#ifdef __cplusplus

extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif


#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_client_lib.h"
#include "gnunet_util_lib.h"
#include "gnunet_mesh_service_new.h"
#include "mesh.h"

/**
 * Opaque handle to the service.
 */
struct GNUNET_MESH_Handle {
    /**
     * Handle to the server connection, to send messages later
     */
    struct GNUNET_CLIENT_Connection             *client;

    /**
     * Set of handlers used for processing incoming messages in the tunnels
     */
    const struct GNUNET_MESH_MessageHandler     *message_handlers;
    int                                         n_handlers;

    /**
     * Set of applications that should be claimed to be offered at this node.
     * Note that this is just informative, the appropiate handlers must be
     * registered independently and the mapping is up to the developer of the
     * client application.
     */
    const GNUNET_MESH_ApplicationType           *applications;
    int                                         n_applications;

    /**
     * Double linked list of the tunnels this client is connected to.
     */
    struct GNUNET_MESH_Tunnel                   *head;
    struct GNUNET_MESH_Tunnel                   *tail;

    /**
     * Callback for tunnel disconnection
     */
    GNUNET_MESH_TunnelEndHandler                *cleaner;

    /**
     * Handle to cancel pending transmissions in case of disconnection
     */
    struct GNUNET_CLIENT_TransmitHandle         *th;

    /**
     * Closure for all the handlers given by the client
     */
    void                                        *cls;
};

/**
 * Opaque handle to a tunnel.
 */
struct GNUNET_MESH_Tunnel {
    /**
     * Owner of the tunnel, either local or remote
     */
    GNUNET_PEER_Id                              owner;

    /**
     * Callback to execute when peers connect to the tunnel
     */
    GNUNET_MESH_TunnelConnectHandler            connect_handler;

    /**
     * Callback to execute when peers disconnect to the tunnel
     */
    GNUNET_MESH_TunnelDisconnectHandler         disconnect_handler;

    /**
     * All peers added to the tunnel
     */
    GNUNET_PEER_Id                              *peers;

    /**
     * Closure for the connect/disconnect handlers
     */
    void                                        *cls;
};

struct GNUNET_MESH_TransmitHandle {
    // TODO
};


/**
 * Function called to notify a client about the socket begin ready to queue more
 * data.  "buf" will be NULL and "size" zero if the socket was closed for
 * writing in the meantime.
 *
 * @param cls closure
 * @param size number of bytes available in buf
 * @param buf where the callee should write the message
 * @return number of bytes written to buf
 */
static size_t 
send_connect_packet (void *cls, size_t size, void *buf) {
    struct GNUNET_MESH_Handle           *h = cls;
    struct GNUNET_MESH_ClientConnect    *msg;
    uint16_t                            *types;
    int                                 ntypes;
    GNUNET_MESH_ApplicationType         *apps;
    int                                 napps;

    h->th = NULL;
    if (0 == size || buf == NULL) {
        GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                    "Send: buffer size 0 or buffer invalid\n");
	// FIXME: disconnect, reconnect, retry!
        return 0;
    }
    if (sizeof(struct GNUNET_MessageHeader) > size) {
        GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                    "Send: buffer size too small\n");
	// FIXME: disconnect, reconnect, retry!
        return 0;
    }
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "Sending into %lu bytes buffer\n",
                size);
    msg = (struct GNUNET_MESH_ClientConnect *) buf;
    msg->header.type = GNUNET_MESSAGE_TYPE_MESH_LOCAL_CONNECT;

    for(ntypes = 0, types = NULL; h->message_handlers[ntypes].type; ntypes++) {
        types = GNUNET_realloc(types, sizeof(uint16_t) * (ntypes + 1));
        types[ntypes] = h->message_handlers[ntypes].type;
    }

    for(napps = 0, apps = NULL; h->applications[napps]; napps++) {
        apps = GNUNET_realloc(apps,
                              sizeof(GNUNET_MESH_ApplicationType) *
                                (napps + 1));
        apps[napps] = h->applications[napps];
    }

    msg->header.size = sizeof(struct GNUNET_MESH_ClientConnect) +
                        sizeof(uint16_t) * ntypes +
                        sizeof(GNUNET_MESH_ApplicationType) * napps;
    if(msg->header.size > size) {
        /* TODO treat error / retry */
        return 0;
    }

    memcpy(&msg[1], types, sizeof(uint16_t) * ntypes);
    memcpy(&msg[1] + sizeof(uint16_t) * ntypes,
           apps,
           sizeof(GNUNET_MESH_ApplicationType) * napps);

    return msg->header.size;
}


/**
 * Type of a function to call when we receive a message
 * from the service.
 *
 * @param cls closure
 * @param msg message received, NULL on timeout or fatal error
 */
void
msg_received (void *cls, const struct GNUNET_MessageHeader * msg)
{
    uint16_t t;
    if(msg != NULL){
        t = ntohs(msg->type);
    } else {
        t = 0;
    }
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
               "received a message from mesh (of size %d)\n",
               t);
    return;
}


/**
 * Connect to the mesh service.
 *
 * @param cfg configuration to use
 * @param cls closure for the various callbacks that follow
 *            (including handlers in the handlers array)
 * @param cleaner function called when an *inbound* tunnel is destroyed
 * @param handlers callbacks for messages we care about, NULL-terminated
 *                 note that the mesh is allowed to drop notifications about
 *                 inbound messages if the client does not process them fast
 *                 enough (for this notification type, a bounded queue is used)
 * @param stypes Application Types the client claims to offer
 * @return handle to the mesh service 
 *         NULL on error (in this case, init is never called)
 */
struct GNUNET_MESH_Handle *
GNUNET_MESH_connect (const struct GNUNET_CONFIGURATION_Handle *cfg,
                     void *cls,
                     GNUNET_MESH_TunnelEndHandler cleaner,
                     const struct GNUNET_MESH_MessageHandler *handlers,
                     const GNUNET_MESH_ApplicationType *stypes) 
{
    struct GNUNET_MESH_Handle           *h;
    size_t                              size;

    h = GNUNET_malloc(sizeof(struct GNUNET_MESH_Handle));

    h->cleaner = cleaner;
    h->client = GNUNET_CLIENT_connect("mesh", cfg);
    GNUNET_CLIENT_receive (h->client,
                         &msg_received,
                         h, 
                         GNUNET_TIME_UNIT_FOREVER_REL);
    if(h->client == NULL) {
        GNUNET_free(h);
        return NULL;
    }

    h->cls = cls;
    h->message_handlers = handlers;
    h->applications = stypes;

    for(h->n_handlers = 0; handlers[h->n_handlers].type; h->n_handlers++);
    for(h->n_applications = 0; stypes[h->n_applications]; h->n_applications++);

    size = sizeof(struct GNUNET_MESH_ClientConnect);
    size += h->n_handlers * sizeof(uint16_t);
    size += h->n_applications * sizeof(GNUNET_MESH_ApplicationType);

    h->th = GNUNET_CLIENT_notify_transmit_ready(h->client,
						size,
						GNUNET_TIME_UNIT_FOREVER_REL,
						GNUNET_YES,
						&send_connect_packet,
						(void *)h);

    return h;
}


/**
 * Disconnect from the mesh service.
 *
 * @param handle connection to mesh to disconnect
 */
void 
GNUNET_MESH_disconnect (struct GNUNET_MESH_Handle *handle) 
{
    if (NULL != handle->th) {
        GNUNET_CLIENT_notify_transmit_ready_cancel (handle->th);
    }
    if (NULL != handle->client) {
        GNUNET_CLIENT_disconnect (handle->client, GNUNET_NO);
    }
    GNUNET_free(handle);
}


/**
 * Create a new tunnel (we're initiator and will be allowed to add/remove peers
 * and to broadcast).
 *
 * @param h mesh handle
 * @param connect_handler function to call when peers are actually connected
 * @param disconnect_handler function to call when peers are disconnected
 * @param handler_cls closure for connect/disconnect handlers
 */
struct GNUNET_MESH_Tunnel *
GNUNET_MESH_tunnel_create (struct GNUNET_MESH_Handle *h,
                           GNUNET_MESH_TunnelConnectHandler
                           connect_handler,
                           GNUNET_MESH_TunnelDisconnectHandler
                           disconnect_handler,
                           void *handler_cls)
{
    struct GNUNET_MESH_Tunnel           *tunnel;

    tunnel = GNUNET_malloc(sizeof(struct GNUNET_MESH_Tunnel));

    tunnel->connect_handler = connect_handler;
    tunnel->disconnect_handler = disconnect_handler;
    tunnel->cls = handler_cls;

    return tunnel;
}


/**
 * Request that a peer should be added to the tunnel.  The existing
 * connect handler will be called ONCE with either success or failure.
 *
 * @param tunnel handle to existing tunnel
 * @param timeout how long to try to establish a connection
 * @param peer peer to add
 */
void
GNUNET_MESH_peer_request_connect_add (struct GNUNET_MESH_Tunnel *tunnel,
                                      struct GNUNET_TIME_Relative timeout,
                                      const struct GNUNET_PeerIdentity *peer)
{
    static GNUNET_PEER_Id       peer_id;
    
    peer_id = GNUNET_PEER_intern(peer);
    
    /* FIXME ACTUALLY DO STUFF */
    tunnel->peers = &peer_id;
    tunnel->connect_handler(tunnel->cls, peer, NULL);
    return;
}


/**
 * Request that a peer should be removed from the tunnel.  The existing
 * disconnect handler will be called ONCE if we were connected.
 *
 * @param tunnel handle to existing tunnel
 * @param peer peer to remove
 */
void
GNUNET_MESH_peer_request_connect_del (struct GNUNET_MESH_Tunnel *tunnel,
                                      const struct GNUNET_PeerIdentity *peer)
{
    /* FIXME ACTUALLY DO STUFF */
    tunnel->peers = NULL;
    tunnel->disconnect_handler(tunnel->cls, peer);
    return;
}


/**
 * Request that the mesh should try to connect to a peer supporting the given
 * message type.
 *
 * @param tunnel handle to existing tunnel
 * @param timeout how long to try to establish a connection
 * @param app_type application type that must be supported by the peer (MESH
 *                 should discover peer in proximity handling this type)
 */
void
GNUNET_MESH_peer_request_connect_by_type (struct GNUNET_MESH_Tunnel *tunnel,
                                          struct GNUNET_TIME_Relative timeout,
                                          GNUNET_MESH_ApplicationType
                                          app_type)
{
    return;
}


/**
 * Ask the mesh to call "notify" once it is ready to transmit the
 * given number of bytes to the specified "target".  If we are not yet
 * connected to the specified peer, a call to this function will cause
 * us to try to establish a connection.
 *
 * @param tunnel tunnel to use for transmission
 * @param cork is corking allowed for this transmission?
 * @param priority how important is the message?
 * @param maxdelay how long can the message wait?
 * @param target destination for the message,
 *               NULL for multicast to all tunnel targets 
 * @param notify_size how many bytes of buffer space does notify want?
 * @param notify function to call when buffer space is available;
 *        will be called with NULL on timeout or if the overall queue
 *        for this peer is larger than queue_size and this is currently
 *        the message with the lowest priority
 * @param notify_cls closure for notify
 * @return non-NULL if the notify callback was queued,
 *         NULL if we can not even queue the request (insufficient
 *         memory); if NULL is returned, "notify" will NOT be called.
 */
struct GNUNET_MESH_TransmitHandle *
GNUNET_MESH_notify_transmit_ready (struct GNUNET_MESH_Tunnel *tunnel,
                                   int cork,
                                   uint32_t priority,
                                   struct GNUNET_TIME_Relative maxdelay,
                                   const struct GNUNET_PeerIdentity *target,
                                   size_t notify_size,
                                   GNUNET_CONNECTION_TransmitReadyNotify
                                   notify,
                                   void *notify_cls)
{
    struct GNUNET_MESH_TransmitHandle   *handle;

    handle = GNUNET_malloc(sizeof(struct GNUNET_MESH_TransmitHandle));

    return handle;
}


#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif
