/*
     This file is part of GNUnet.
     (C) 2010 Christian Grothoff (and other contributing authors)

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
 * @file src/transport/gnunet-nat-client.c
 * @brief Tool to help bypass NATs using ICMP method; must run as root (SUID will do)
 *        This code will work under GNU/Linux only.  
 * @author Christian Grothoff
 *
 * This program will send ONE ICMP message using RAW sockets
 * to the IP address specified as the second argument.  Since
 * it uses RAW sockets, it must be installed SUID or run as 'root'.
 * In order to keep the security risk of the resulting SUID binary
 * minimal, the program ONLY opens the RAW socket with root
 * privileges, then drops them and only then starts to process
 * command line arguments.  The code also does not link against
 * any shared libraries (except libc) and is strictly minimal
 * (except for checking for errors).  The following list of people
 * have reviewed this code and considered it safe since the last
 * modification (if you reviewed it, please have your name added
 * to the list):
 *
 * - Christian Grothoff
 */
#define _GNU_SOURCE
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/in.h> 

/**
 * Must match IP given in the server.
 */
#define DUMMY_IP "1.2.3.4"
#define HAVE_PORT 1

struct ip_packet 
{
  uint8_t vers_ihl;
  uint8_t tos;
  uint16_t pkt_len;
  uint16_t id;
  uint16_t flags_frag_offset;
  uint8_t ttl;
  uint8_t proto;
  uint16_t checksum;
  uint32_t src_ip;
  uint32_t dst_ip;
};

struct icmp_packet 
{
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint32_t reserved;

};

struct icmp_echo_packet
{
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint32_t reserved;
  uint32_t data;
};

 
static int rawsock;

static struct in_addr dummy;
 
static struct in_addr target;

#if HAVE_PORT
  static uint32_t port;
#endif

static uint16_t 
calc_checksum(const uint16_t *data, 
	      unsigned int bytes)
{
  uint32_t sum;
  unsigned int i;

  sum = 0;
  for (i=0;i<bytes/2;i++) 
    sum += data[i];        
  sum = (sum & 0xffff) + (sum >> 16);
  sum = htons(0xffff - sum);
  return sum;
}



#if HAVE_PORT

static void
make_echo (const struct in_addr *src_ip,
	   struct icmp_echo_packet *echo, uint32_t num)
{
  memset(echo, 0, sizeof(struct icmp_echo_packet));
  echo->type = ICMP_ECHO;
  echo->code = 0;
  echo->reserved = 0;
  echo->checksum = 0;
  echo->data = htons(num);
  echo->checksum = htons(calc_checksum((uint16_t*)echo, 
				       sizeof (struct icmp_echo_packet)));
}
#else
static void
make_echo (const struct in_addr *src_ip,
           struct icmp_packet *echo)
{
  memset(echo, 0, sizeof(struct icmp_packet));
  echo->type = ICMP_ECHO;
  echo->code = 0;
  echo->reserved = 0;
  echo->checksum = 0;
  echo->checksum = htons(calc_checksum((uint16_t*)echo,
                                       sizeof (struct icmp_packet)));
}
#endif


/**
 * Send an ICMP message to the target.
 *
 * @param my_ip source address
 * @param other target address
 */
static void
send_icmp (const struct in_addr *my_ip,
	   const struct in_addr *other)
{
  struct ip_packet ip_pkt;
  struct icmp_packet *icmp_pkt;
#if HAVE_PORT
  struct icmp_echo_packet icmp_echo;
#else
  struct icmp_packet icmp_echo;
#endif
  struct sockaddr_in dst;
#if HAVE_PORT
  char packet[sizeof (struct ip_packet)*2 + sizeof (struct icmp_packet) + sizeof(struct icmp_echo_packet)];
#else
  char packet[sizeof (struct ip_packet)*2 + sizeof (struct icmp_packet)*2];
#endif
  size_t off;
  int err;

  /* ip header: send to (known) ip address */
  off = 0;
  memset(&ip_pkt, 0, sizeof(ip_pkt));
  ip_pkt.vers_ihl = 0x45;
  ip_pkt.tos = 0;
  ip_pkt.pkt_len = sizeof (packet); /* huh? */
  ip_pkt.id = 1; 
  ip_pkt.flags_frag_offset = 0;
  ip_pkt.ttl = IPDEFTTL;
  ip_pkt.proto = IPPROTO_ICMP;
  ip_pkt.checksum = 0; 
  ip_pkt.src_ip = my_ip->s_addr;
  ip_pkt.dst_ip = other->s_addr;
  ip_pkt.checksum = htons(calc_checksum((uint16_t*)&ip_pkt, sizeof (struct ip_packet)));
  memcpy (packet, &ip_pkt, sizeof (struct ip_packet));
  off += sizeof (ip_pkt);
  /* icmp reply: time exceeded */
  icmp_pkt = (struct icmp_packet*) &packet[off];
  memset(icmp_pkt, 0, sizeof(struct icmp_packet));
  icmp_pkt->type = ICMP_TIME_EXCEEDED;
  icmp_pkt->code = 0; 
  icmp_pkt->reserved = 0;
  icmp_pkt->checksum = 0;

  off += sizeof (struct icmp_packet);

  /* ip header of the presumably 'lost' udp packet */
  ip_pkt.vers_ihl = 0x45;
  ip_pkt.tos = 0;
#if HAVE_PORT
  ip_pkt.pkt_len = (sizeof (struct ip_packet) + sizeof (struct icmp_echo_packet));
#else
  ip_pkt.pkt_len = (sizeof (struct ip_packet) + sizeof (struct icmp_packet));
#endif

  ip_pkt.id = 1; 
  ip_pkt.flags_frag_offset = 0;
  ip_pkt.ttl = 1; /* real TTL would be 1 on a time exceeded packet */
  ip_pkt.proto = IPPROTO_ICMP;
  ip_pkt.src_ip = other->s_addr;
  ip_pkt.dst_ip = dummy.s_addr;
  ip_pkt.checksum = 0;
  ip_pkt.checksum = htons(calc_checksum((uint16_t*)&ip_pkt, sizeof (struct ip_packet)));  
  memcpy (&packet[off], &ip_pkt, sizeof (struct ip_packet));
  off += sizeof (struct ip_packet);

#if HAVE_PORT
  make_echo (other, &icmp_echo, port);
  memcpy (&packet[off], &icmp_echo, sizeof(struct icmp_echo_packet));
  off += sizeof (struct icmp_echo_packet);
#else
  make_echo (other, &icmp_echo);
  memcpy (&packet[off], &icmp_echo, sizeof(struct icmp_packet));
  off += sizeof (struct icmp_packet);
#endif

#if HAVE_PORT
  icmp_pkt->checksum = htons(calc_checksum((uint16_t*)icmp_pkt,
                                             sizeof (struct icmp_packet) + sizeof(struct ip_packet) + sizeof(struct icmp_echo_packet)));

#else
  icmp_pkt->checksum = htons(calc_checksum((uint16_t*)icmp_pkt, 
                                             sizeof (struct icmp_packet)*2 + sizeof(struct ip_packet)));

#endif



  memset (&dst, 0, sizeof (dst));
  dst.sin_family = AF_INET;
  dst.sin_addr = *other;
  err = sendto(rawsock, 
	       packet, 
	       off, 0, 
	       (struct sockaddr*)&dst, 
	       sizeof(dst)); /* or sizeof 'struct sockaddr'? */
  if (err < 0) 
    {
      fprintf(stderr,
	      "sendto failed: %s\n", strerror(errno));
    }
  else if (err != off) 
    {
      fprintf(stderr,
	      "Error: partial send of ICMP message\n");
    }
}


static int
make_raw_socket ()
{
  const int one = 1;
  int ret;

  ret = socket (AF_INET, SOCK_RAW, IPPROTO_RAW);
  if (-1 == ret)
    {
      fprintf (stderr,
	       "Error opening RAW socket: %s\n",
	       strerror (errno));
      return -1;
    }  
  if (setsockopt(ret, SOL_SOCKET, SO_BROADCAST,
		 (char *)&one, sizeof(one)) == -1)
    fprintf(stderr,
	    "setsockopt failed: %s\n",
	    strerror (errno));
  if (setsockopt(ret, IPPROTO_IP, IP_HDRINCL,
		 (char *)&one, sizeof(one)) == -1)
    fprintf(stderr,
	    "setsockopt failed: %s\n",
	    strerror (errno));
  return ret;
}


int
main (int argc, char *const *argv)
{
  struct in_addr external;
  uid_t uid;

  if (-1 == (rawsock = make_raw_socket()))
    return 1;     
  uid = getuid ();
  if (0 != setresuid (uid, uid, uid))
    fprintf (stderr,
	     "Failed to setresuid: %s\n",
	     strerror (errno));
#if HAVE_PORT
  if (argc != 4)
    {
      fprintf (stderr,
	       "This program must be started with our IP, the targets external IP, and our port as arguments.\n");
      return 1;
    }
  port = atoi(argv[3]);
#else
  if (argc != 3)
    {
      fprintf (stderr,
               "This program must be started with our IP and the targets external IP as arguments.\n");
      return 1;
    }
#endif
  if ( (1 != inet_pton (AF_INET, argv[1], &external)) ||
       (1 != inet_pton (AF_INET, argv[2], &target)) )
    {
      fprintf (stderr,
	       "Error parsing IPv4 address: %s\n",
	       strerror (errno));
      return 1;
    }
  if (1 != inet_pton (AF_INET, DUMMY_IP, &dummy)) abort ();
  send_icmp (&external,
	     &target);
  close (rawsock);
  return 0;
}

/* end of gnunet-nat-client.c */
