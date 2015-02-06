/*
 This file is part of GNUnet.
 (C) 2010-2015 Christian Grothoff (and other contributing authors)

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
 * @file include/gnunet_ats_service.h
 * @brief automatic transport selection and outbound bandwidth determination
 * @author Christian Grothoff
 * @author Matthias Wachs
 */
#ifndef GNUNET_ATS_SERVICE_H
#define GNUNET_ATS_SERVICE_H

#include "gnunet_constants.h"
#include "gnunet_util_lib.h"
#include "gnunet_hello_lib.h"

/**
 * Number of network types supported by ATS
 */
#define GNUNET_ATS_NetworkTypeCount 6


/**
 * Types of networks (with separate quotas) we support.
 */
enum GNUNET_ATS_Network_Type
{
  /**
   * Category of last resort.
   */
  GNUNET_ATS_NET_UNSPECIFIED = 0,

  /**
   * Loopback (same host).
   */
  GNUNET_ATS_NET_LOOPBACK = 1,

  /**
   * Local area network.
   */
  GNUNET_ATS_NET_LAN = 2,

  /**
   * Wide area network (i.e. Internet)
   */
  GNUNET_ATS_NET_WAN = 3,

  /**
   * Wireless LAN (i.e. 802.11abgn)
   */
  GNUNET_ATS_NET_WLAN = 4,

  /**
   * Bluetooth LAN
   */
  GNUNET_ATS_NET_BT = 5
};


/**
 * ATS network types as array initializer
 */
#define GNUNET_ATS_NetworkType { GNUNET_ATS_NET_UNSPECIFIED, GNUNET_ATS_NET_LOOPBACK, GNUNET_ATS_NET_LAN, GNUNET_ATS_NET_WAN, GNUNET_ATS_NET_WLAN, GNUNET_ATS_NET_BT }



/**
 * Default bandwidth assigned to a network : 64 KB/s
 */
#define GNUNET_ATS_DefaultBandwidth 65536

/**
 * Undefined value for an `enum GNUNET_ATS_Property`
 */
#define GNUNET_ATS_VALUE_UNDEFINED UINT32_MAX

/**
 * String representation for GNUNET_ATS_VALUE_UNDEFINED
 */
#define GNUNET_ATS_VALUE_UNDEFINED_STR "undefined"

/**
 * Maximum bandwidth assigned to a network : 4095 MB/s
 */
#define GNUNET_ATS_MaxBandwidth UINT32_MAX

/**
 * Textual equivalent for GNUNET_ATS_MaxBandwidth
 */
#define GNUNET_ATS_MaxBandwidthString "unlimited"

/**
 * Number of property types supported by ATS
 */
#define GNUNET_ATS_PropertyCount 11


/**
 * Enum defining all known property types for ATS Enum values are used
 * in the GNUNET_ATS_Information struct as
 * (key,value)-pairs.
 *
 * Cost are always stored in uint32_t, so all units used to define costs
 * have to be normalized to fit in uint32_t [0 .. UINT32_MAX-1]
 *
 * UINT32_MAX is reserved for uninitialized values #GNUNET_ATS_VALUE_UNDEFINED
 */
enum GNUNET_ATS_Property
{

  /**
   * End of the array.
   * @deprecated
   */
  GNUNET_ATS_ARRAY_TERMINATOR = 0,

  /**
   * Actual traffic on this connection from this peer to the other peer.
   * Includes transport overhead
   *
   * Unit: [bytes/second]
   */
  GNUNET_ATS_UTILIZATION_OUT,

  /**
   * Actual traffic on this connection from the other peer to this peer.
   * Includes transport overhead
   *
   * Unit: [bytes/second]
   */
  GNUNET_ATS_UTILIZATION_IN,

  /**
   * Actual traffic on this connection from this peer to the other peer.
   * Only payload from layers > transport
   *
   * Unit: [bytes/second]
   */
  GNUNET_ATS_UTILIZATION_PAYLOAD_OUT,

  /**
   * Actual traffic on this connection from the other peer to this peer.
   * Only payload from layers > transport
   *
   * Unit: [bytes/second]
   */
  GNUNET_ATS_UTILIZATION_PAYLOAD_IN,

  /**
   * Is this address located in WAN, LAN or a loopback address
   * Value is element of GNUNET_ATS_Network_Type
   */
  GNUNET_ATS_NETWORK_TYPE,

  /**
   * Delay
   * Time between when the time packet is sent and the packet arrives
   *
   * Unit: [microseconds]
   *
   * Examples:
   *
   * LAN   :    1
   * WLAN  :    2
   * Dialup:  500
   */
  GNUNET_ATS_QUALITY_NET_DELAY,

  /**
   * Distance on network layer (required for distance-vector routing).
   *
   * Unit: [DV-hops]
   */
  GNUNET_ATS_QUALITY_NET_DISTANCE,

  /**
   * Network overhead on WAN (Wide-Area Network)
   *
   * How many bytes are sent on the WAN when 1 kilobyte (1024 bytes)
   * of application data is transmitted?
   * A factor used with connect cost, bandwidth cost and energy cost
   * to describe the overhead produced by the transport protocol
   *
   * Unit: [bytes/kb]
   *
   * Interpretation: less is better
   *
   * Examples:
   *
   * TCP/IPv4 over Ethernet: 1024 + 38 + 20 + 20 = 1102 [bytes/kb]
   * TCP/IPv6 over Ethernet: 1024 + 38 + 20 + 40 = 1122 [bytes/kb]
   * UDP/IPv4 over Ethernet: 1024 + 38 + 20 + 8  = 1090 [bytes/kb]
   * UDP/IPv6 over Ethernet: 1024 + 38 + 40 + 8  = 1110 [bytes/kb]
   */
  GNUNET_ATS_COST_WAN,

  /**
   * Network overhead on LAN (Local-Area Network)
   *
   * How many bytes are sent on the LAN when 1 kilobyte (1024 bytes)
   * of application data is transmitted?
   * A factor used with connect cost, bandwidth cost and energy cost
   * to describe the overhead produced by the transport protocol
   *
   * Unit: [bytes/kb]
   *
   * Interpretation: less is better
   *
   * Examples:
   *
   * TCP/IPv4 over Ethernet: 1024 + 38 + 20 + 20 = 1102 [bytes/kb]
   * TCP/IPv6 over Ethernet: 1024 + 38 + 20 + 40 = 1122 [bytes/kb]
   * UDP/IPv4 over Ethernet: 1024 + 38 + 20 + 8  = 1090 [bytes/kb]
   * UDP/IPv6 over Ethernet: 1024 + 38 + 40 + 8  = 1110 [bytes/kb]
   */
  GNUNET_ATS_COST_LAN,

  /**
   * Network overhead on WLAN (Wireless Local Area Network)
   *
   * How many bytes are sent on the LAN when 1 kilobyte (1024 bytes)
   * of application data is transmitted?
   * A factor used with connect cost, bandwidth cost and energy cost
   * to describe the overhead produced by the transport protocol
   *
   * Unit: [bytes/kb]
   *
   * Interpretation: less is better
   *
   * Examples:
   *
   * TCP/IPv4 over Ethernet: 1024 + 38 + 20 + 20 = 1102 [bytes/kb]
   * TCP/IPv6 over Ethernet: 1024 + 38 + 20 + 40 = 1122 [bytes/kb]
   * UDP/IPv4 over Ethernet: 1024 + 38 + 20 + 8  = 1090 [bytes/kb]
   * UDP/IPv6 over Ethernet: 1024 + 38 + 40 + 8  = 1110 [bytes/kb]
   */
  GNUNET_ATS_COST_WLAN

};


/**
 * Number of ATS quality properties
 */
#define GNUNET_ATS_QualityPropertiesCount 2

/**
 * ATS quality properties as array initializer
 */
#define GNUNET_ATS_QualityProperties { GNUNET_ATS_QUALITY_NET_DELAY, GNUNET_ATS_QUALITY_NET_DISTANCE }

/**
 * ATS quality properties as string array initializer
 */
#define GNUNET_ATS_QualityPropertiesString {"Delay", "Distance"}

GNUNET_NETWORK_STRUCT_BEGIN

/**
 * struct used to communicate the transport's properties like cost and
 * quality of service as well as high-level constraints on resource
 * consumption.
 *
 *                             +---+
 *  +-----------+ Constraints  |   |  Plugin properties +---------+
 *  | Highlevel |------------> |ATS| <------------------|Transport|
 *  | Component | ATS struct   |   |    ATS struct      | Plugin  |
 *  +-----------+              |   |                    +---------+
 *                             +---+
 *
 * This structure will be used by transport plugins to communicate
 * costs to ATS or by higher level components to tell ATS their
 * constraints.  Always a pair of (GNUNET_ATS_Property,
 * uint32_t value).  Value is always uint32_t, so all units used to
 * define costs have to be normalized to fit uint32_t.
 */
struct GNUNET_ATS_Information
{
  /**
   * ATS property type, in network byte order.
   */
  uint32_t type GNUNET_PACKED;

  /**
   * ATS property value, in network byte order.
   */
  uint32_t value GNUNET_PACKED;
};
GNUNET_NETWORK_STRUCT_END


/* ********************* LAN Characterization library ************************ */
/* Note: these functions do not really communicate with the ATS service */

/**
 * Convert a ATS property to a string
 *
 * @param type the property type
 * @return a string or NULL if invalid
 */
const char *
GNUNET_ATS_print_property_type (enum GNUNET_ATS_Property type);


/**
 * Convert a `enum GNUNET_ATS_Network_Type` to a string
 *
 * @param net the network type
 * @return a string or NULL if invalid
 */
const char *
GNUNET_ATS_print_network_type (enum GNUNET_ATS_Network_Type net);


/**
 * Handle for the LAN Characterization library.
 */
struct GNUNET_ATS_InterfaceScanner;


/**
 * Returns where the address is located: loopback, LAN or WAN.
 *
 * @param is handle from #GNUNET_ATS_interface_scanner_init()
 * @param addr address
 * @param addrlen address length
 * @return type of the network the address belongs to
 */
enum GNUNET_ATS_Network_Type
GNUNET_ATS_scanner_address_get_type (struct GNUNET_ATS_InterfaceScanner *is,
                                     const struct sockaddr *addr,
                                     socklen_t addrlen);


/**
 * Initialize the ATS address characterization client handle.
 *
 * @return scanner handle, NULL on error
 */
struct GNUNET_ATS_InterfaceScanner *
GNUNET_ATS_scanner_init (void);


/**
 * Terminate interface scanner.
 *
 * @param is scanner we are done with
 */
void
GNUNET_ATS_scanner_done (struct GNUNET_ATS_InterfaceScanner *is);



/* ********************Connection Suggestion API ***************************** */

/**
 * Handle to the ATS subsystem for making suggestions about
 * connections the peer would like to have.
 */
struct GNUNET_ATS_ConnectivityHandle;

/**
 * Handle for address suggestion requests.
 */
struct GNUNET_ATS_ConnectivitySuggestHandle;


/**
 * Initialize the ATS connectivity suggestion client handle.
 *
 * @param cfg configuration to use
 * @return ats connectivity handle, NULL on error
 */
struct GNUNET_ATS_ConnectivityHandle *
GNUNET_ATS_connectivity_init (const struct GNUNET_CONFIGURATION_Handle *cfg);


/**
 * Shutdown ATS connectivity suggestion client.
 *
 * @param ch handle to destroy
 */
void
GNUNET_ATS_connectivity_done (struct GNUNET_ATS_ConnectivityHandle *ch);


/**
 * We would like to establish a new connection with a peer.  ATS
 * should suggest a good address to begin with.
 *
 * @param ch handle
 * @param peer identity of the peer we need an address for
 * @return suggestion handle, NULL if request is already pending
 */
struct GNUNET_ATS_ConnectivitySuggestHandle *
GNUNET_ATS_connectivity_suggest (struct GNUNET_ATS_ConnectivityHandle *ch,
                                 const struct GNUNET_PeerIdentity *peer);


/**
 * We no longer care about being connected to a peer.
 *
 * @param sh handle
 */
void
GNUNET_ATS_connectivity_suggest_cancel (struct GNUNET_ATS_ConnectivitySuggestHandle *sh);


/* ******************************** Scheduling API ***************************** */

/**
 * Handle to the ATS subsystem for bandwidth/transport scheduling information.
 */
struct GNUNET_ATS_SchedulingHandle;

/**
 * Opaque session handle, defined by plugins.  Contents not known to ATS.
 */
struct Session;

/**
 * Signature of a function called by ATS with the current bandwidth
 * and address preferences as determined by ATS.
 *
 * @param cls closure
 * @param peer for which we suggest an address, NULL if ATS connection died
 * @param address suggested address (including peer identity of the peer),
 *             may be NULL to signal disconnect from peer
 * @param session session to use, NULL to establish a new outgoing session
 * @param bandwidth_out assigned outbound bandwidth for the connection,
 *        0 to signal disconnect
 * @param bandwidth_in assigned inbound bandwidth for the connection,
 *        0 to signal disconnect
 */
typedef void
(*GNUNET_ATS_AddressSuggestionCallback) (void *cls,
    const struct GNUNET_PeerIdentity *peer,
    const struct GNUNET_HELLO_Address *address,
    struct Session *session,
    struct GNUNET_BANDWIDTH_Value32NBO bandwidth_out,
    struct GNUNET_BANDWIDTH_Value32NBO bandwidth_in);


/**
 * Initialize the ATS scheduling subsystem.
 *
 * @param cfg configuration to use
 * @param suggest_cb notification to call whenever the suggestation changed
 * @param suggest_cb_cls closure for @a suggest_cb
 * @return ats context
 */
struct GNUNET_ATS_SchedulingHandle *
GNUNET_ATS_scheduling_init (const struct GNUNET_CONFIGURATION_Handle *cfg,
                            GNUNET_ATS_AddressSuggestionCallback suggest_cb,
                            void *suggest_cb_cls);


/**
 * Client is done with ATS scheduling, release resources.
 *
 * @param sh handle to release
 */
void
GNUNET_ATS_scheduling_done (struct GNUNET_ATS_SchedulingHandle *sh);


/**
 * Test if a address and a session is known to ATS.
 *
 * @param sh the scheduling handle
 * @param address the address
 * @param session the session
 * @return #GNUNET_YES or #GNUNET_NO
 */
int
GNUNET_ATS_session_known (struct GNUNET_ATS_SchedulingHandle *sh,
                          const struct GNUNET_HELLO_Address *address,
                          struct Session *session);


/**
 * Handle used within ATS to track an address.
 */
struct GNUNET_ATS_AddressRecord;


/**
 * We have a new address ATS should know. Addresses have to be added with this
 * function before they can be: updated, set in use and destroyed
 *
 * @param sh handle
 * @param address the address
 * @param session session handle (if available, i.e. for incoming connections)
 * @param ats performance data for the address
 * @param ats_count number of performance records in @a ats
 * @return handle to the address representation inside ATS, NULL
 *         on error (i.e. ATS knows this exact address already, or
 *         address is invalid)
 */
struct GNUNET_ATS_AddressRecord *
GNUNET_ATS_address_add (struct GNUNET_ATS_SchedulingHandle *sh,
                        const struct GNUNET_HELLO_Address *address,
                        struct Session *session,
                        const struct GNUNET_ATS_Information *ats,
                        uint32_t ats_count);


/**
 * An address was used to initiate a session.
 *
 * @param ar address record to update information for
 * @param session session handle
 */
void
GNUNET_ATS_address_add_session (struct GNUNET_ATS_AddressRecord *ar,
                                struct Session *session);


/**
 * A session was destroyed, disassociate it from the
 * given address record.  If this was an incoming
 * addess, destroy the address as well.
 *
 * @param ar address record to update information for
 * @param session session handle
 * @return #GNUNET_YES if the @a ar was destroyed because
 *                     it was an incoming address,
 *         #GNUNET_NO if the @ar was kept because we can
 *                    use it still to establish a new session
 */
int
GNUNET_ATS_address_del_session (struct GNUNET_ATS_AddressRecord *ar,
                                struct Session *session);


/**
 * We have updated performance statistics for a given address.  Note
 * that this function can be called for addresses that are currently
 * in use as well as addresses that are valid but not actively in use.
 * Furthermore, the peer may not even be connected to us right now (@a
 * session value of NULL used to signal disconnect, or somehow we
 * otherwise got updated on @a ats information).  Based on the
 * information provided, ATS may update bandwidth assignments and
 * suggest to switch addresses.
 *
 * @param ar address record to update information for
 * @param ats performance data for the address
 * @param ats_count number of performance records in @a ats
 */
void
GNUNET_ATS_address_update (struct GNUNET_ATS_AddressRecord *ar,
                           const struct GNUNET_ATS_Information *ats,
                           uint32_t ats_count);


/**
 * An address got destroyed, stop using it as a valid address.
 *
 * @param ar address record to destroy, it's validation has
 *           expired and ATS may no longer use it
 */
void
GNUNET_ATS_address_destroy (struct GNUNET_ATS_AddressRecord *ar);



/* ******************************** Performance API ***************************** */

/**
 * ATS Handle to obtain and/or modify performance information.
 */
struct GNUNET_ATS_PerformanceHandle;

/**
 * Signature of a function that is called with QoS information about an address.
 *
 * @param cls closure
 * @param address the address, NULL if ATS service was disconnected
 * @param address_active #GNUNET_YES if this address is actively used
 *        to maintain a connection to a peer;
 *        #GNUNET_NO if the address is not actively used;
 *        #GNUNET_SYSERR if this address is no longer available for ATS
 * @param bandwidth_out assigned outbound bandwidth for the connection
 * @param bandwidth_in assigned inbound bandwidth for the connection
 * @param ats performance data for the address (as far as known)
 * @param ats_count number of performance records in @a ats
 */
typedef void
(*GNUNET_ATS_AddressInformationCallback) (void *cls,
                                          const struct GNUNET_HELLO_Address *address,
                                          int address_active,
                                          struct GNUNET_BANDWIDTH_Value32NBO bandwidth_out,
                                          struct GNUNET_BANDWIDTH_Value32NBO bandwidth_in,
                                          const struct GNUNET_ATS_Information *ats,
                                          uint32_t ats_count);


/**
 * Handle for an address listing operation
 */
struct GNUNET_ATS_AddressListHandle;


/**
 * Get handle to access performance API of the ATS subsystem.
 *
 * @param cfg configuration to use
 * @param addr_info_cb callback called when performance characteristics for
 * 	an address change
 * @param addr_info_cb_cls closure for @a addr_info_cb
 * @return ats performance context
 */
struct GNUNET_ATS_PerformanceHandle *
GNUNET_ATS_performance_init (const struct GNUNET_CONFIGURATION_Handle *cfg,
                             GNUNET_ATS_AddressInformationCallback addr_info_cb,
                             void *addr_info_cb_cls);



/**
 * Get information about addresses known to the ATS subsystem.
 *
 * @param handle the performance handle to use
 * @param peer peer idm can be NULL for all peers
 * @param all #GNUNET_YES to get information about all addresses or #GNUNET_NO to
 *        get only address currently used
 * @param infocb callback to call with the addresses,
 *        will callback with address == NULL when done
 * @param infocb_cls closure for @a infocb
 * @return ats performance context
 */
struct GNUNET_ATS_AddressListHandle *
GNUNET_ATS_performance_list_addresses (struct GNUNET_ATS_PerformanceHandle *handle,
                                       const struct GNUNET_PeerIdentity *peer,
                                       int all,
                                       GNUNET_ATS_AddressInformationCallback infocb,
                                       void *infocb_cls);


/**
 * Cancel a pending address listing operation
 *
 * @param handle the `struct GNUNET_ATS_AddressListHandle` handle to cancel
 */
void
GNUNET_ATS_performance_list_addresses_cancel (struct GNUNET_ATS_AddressListHandle *handle);


/**
 * Client is done using the ATS performance subsystem, release resources.
 *
 * @param ph handle
 */
void
GNUNET_ATS_performance_done (struct GNUNET_ATS_PerformanceHandle *ph);


/**
 * Function called with reservation result.
 *
 * @param cls closure
 * @param peer identifies the peer
 * @param amount set to the amount that was actually reserved or unreserved;
 *               either the full requested amount or zero (no partial reservations)
 * @param res_delay if the reservation could not be satisfied (amount was 0), how
 *        long should the client wait until re-trying?
 */
typedef void
(*GNUNET_ATS_ReservationCallback) (void *cls,
                                   const struct GNUNET_PeerIdentity *peer,
                                   int32_t amount,
                                   struct GNUNET_TIME_Relative res_delay);


/**
 * Context that can be used to cancel a peer information request.
 */
struct GNUNET_ATS_ReservationContext;


/**
 * Reserve inbound bandwidth from the given peer.  ATS will look at
 * the current amount of traffic we receive from the peer and ensure
 * that the peer could add 'amount' of data to its stream.
 *
 * @param ph performance handle
 * @param peer identifies the peer
 * @param amount reserve N bytes for receiving, negative
 *                amounts can be used to undo a (recent) reservation;
 * @param rcb function to call with the resulting reservation information
 * @param rcb_cls closure for @a rcb
 * @return NULL on error
 * @deprecated will be replaced soon
 */
struct GNUNET_ATS_ReservationContext *
GNUNET_ATS_reserve_bandwidth (struct GNUNET_ATS_PerformanceHandle *ph,
			      const struct GNUNET_PeerIdentity *peer,
			      int32_t amount,
			      GNUNET_ATS_ReservationCallback rcb,
			      void *rcb_cls);


/**
 * Cancel request for reserving bandwidth.
 *
 * @param rc context returned by the original GNUNET_ATS_reserve_bandwidth call
 */
void
GNUNET_ATS_reserve_bandwidth_cancel (struct GNUNET_ATS_ReservationContext *rc);

/**
 * Number of preference types supported by ATS
 */
#define GNUNET_ATS_PreferenceCount 3

/**
 * ATS preference types as array initializer
 */
#define GNUNET_ATS_PreferenceType {GNUNET_ATS_PREFERENCE_END, GNUNET_ATS_PREFERENCE_BANDWIDTH, GNUNET_ATS_PREFERENCE_LATENCY}

/**
 * ATS preference types as string array initializer
 */
#define GNUNET_ATS_PreferenceTypeString {"END", "BANDWIDTH", "LATENCY"}

/**
 * Enum defining all known preference categories.
 */
enum GNUNET_ATS_PreferenceKind
{

  /**
   * End of preference list.
   */
  GNUNET_ATS_PREFERENCE_END = 0,

  /**
   * Change the peer's bandwidth value (value per byte of bandwidth in
   * the goal function) to the given amount.  The argument is followed
   * by a double value giving the desired value (can be negative).
   * Preference changes are forgotten if peers disconnect.
   */
  GNUNET_ATS_PREFERENCE_BANDWIDTH,

  /**
   * Change the peer's latency value to the given amount.  The
   * argument is followed by a double value giving the desired value
   * (can be negative).  The absolute score in the goal function is
   * the inverse of the latency in microseconds (minimum: 1
   * microsecond) multiplied by the latency preferences.
   */
  GNUNET_ATS_PREFERENCE_LATENCY
};

/**
 * Convert a GNUNET_ATS_PreferenceType to a string
 *
 * @param type the preference type
 * @return a string or NULL if invalid
 */
const char *
GNUNET_ATS_print_preference_type (uint32_t type);


/**
 * Change preferences for the given peer. Preference changes are forgotten if peers
 * disconnect.
 *
 * @param ph performance handle
 * @param peer identifies the peer
 * @param ... 0-terminated specification of the desired changes
 */
void
GNUNET_ATS_performance_change_preference (struct GNUNET_ATS_PerformanceHandle *ph,
                                          const struct GNUNET_PeerIdentity *peer,
					  ...);


/**
 * Application feedback on how good preference requirements are fulfilled
 * for the preferences included in the given time scope [now - scope .. now]
 *
 * An application notifies ATS if (and only if) it has feedback information
 * for specific properties. This values are valid until the feedback scores are
 * updated by the application.
 *
 * If the application has no feedback for this preference kind the application
 * will not explicitly call for this property and will not include it in this
 * function call.
 *
 * @param ph performance handle
 * @param scope the time interval this valid for: [now - scope .. now]
 * @param peer identifies the peer
 * @param ... 0-terminated specification of the desired changes
 */
void
GNUNET_ATS_performance_give_feedback (struct GNUNET_ATS_PerformanceHandle *ph,
				      const struct GNUNET_PeerIdentity *peer,
				      const struct GNUNET_TIME_Relative scope,
				      ...);

#endif
/* end of file gnunet-service-transport_ats.h */
