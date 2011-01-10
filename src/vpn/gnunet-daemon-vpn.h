/*
     This file is part of GNUnet.
     (C) 2010 Christian Grothoff

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
 * @file vpn/gnunet-daemon-vpn.h
 * @brief
 * @author Philipp Toelke
 */
#ifndef GNUNET_DAEMON_VPN_H
#define GNUNET_DAEMON_VPN_H

#include "gnunet-service-dns-p.h"

/**
 * This gets scheduled with cls pointing to an answer_packet and does everything
 * needed in order to send it to the helper.
 *
 * At the moment this means "inventing" and IPv6-Address for .gnunet-services and
 * doing nothing for "real" services.
 */
void
process_answer(void* cls, const struct GNUNET_SCHEDULER_TaskContext* tc);

/**
 * Calculate the checksum of an IPv4-Header
 */
uint16_t
calculate_ip_checksum(uint16_t* hdr, short len);

void
send_icmp_response(void* cls, const struct GNUNET_SCHEDULER_TaskContext *tc);

size_t
send_udp_service (void *cls, size_t size, void *buf);

GNUNET_HashCode* address_mapping_exists(unsigned char addr[]);

unsigned int port_in_ports (uint64_t ports, uint16_t port);

void
send_udp_to_peer (void *cls, int success);

/**
 * The configuration to use
 */
const struct GNUNET_CONFIGURATION_Handle *cfg;

/**
 * The handle to core
 */
struct GNUNET_CORE_Handle *core_handle;

/**
 * The hashmap containing the mappings from ipv6-addresses to gnunet-descriptors
 */
struct GNUNET_CONTAINER_MultiHashMap* hashmap;

struct map_entry {
    struct GNUNET_vpn_service_descriptor desc;
    uint16_t namelen;
    uint64_t additional_ports;
    /**
     * In DNS-Format!
     */
    char name[1];
};

#endif /* end of include guard: GNUNET-DAEMON-VPN_H */
