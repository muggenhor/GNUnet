#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "gnunet-vpn-packet.h"
#include "gnunet-dns-parser.h"

static char* pretty = /*{{{*/
/*     0       1         2         3         4        5          6
 0123456789012345678901234567890123456789012345678901234567890123456789 */
"IPv6-Paket from xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx    \n" //60
"             to xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx    \n" //120
/*     0       1         2         3         4        5          6
 0123456789012345678901234567890123456789012345678901234567890123456789 */
"        flow    0xXXX (        )                           \n" //180
"        length  0xXX  (   )                                \n" //240
"        nexthdr 0xXX  (                                    \n" //300
"        hoplmt  0xXX  (   )                                \n" //360
"first 128 bytes of payload:                                \n" //420
/*     0       1         2         3         4        5          6
 0123456789012345678901234567890123456789012345678901234567890123456789 */
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n" //490
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n" //560
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n" //630
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n" //700
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n" //770
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n" //840
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n" //910
"XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX | ................  \n";//980
/*}}}*/

static void pp_ip6adr(unsigned char* adr, char* dest) {{{
	char tmp[3];

	sprintf(tmp, "%02X", adr[0]);
	memcpy(dest+0, tmp, 2);
	sprintf(tmp, "%02X", adr[1]);
	memcpy(dest+2, tmp, 2);

	sprintf(tmp, "%02X", adr[2]);
	memcpy(dest+5, tmp, 2);
	sprintf(tmp, "%02X", adr[3]);
	memcpy(dest+7, tmp, 2);

	sprintf(tmp, "%02X", adr[4]);
	memcpy(dest+10, tmp, 2);
	sprintf(tmp, "%02X", adr[5]);
	memcpy(dest+12, tmp, 2);

	sprintf(tmp, "%02X", adr[6]);
	memcpy(dest+15, tmp, 2);
	sprintf(tmp, "%02X", adr[7]);
	memcpy(dest+17, tmp, 2);

	sprintf(tmp, "%02X", adr[8]);
	memcpy(dest+20, tmp, 2);
	sprintf(tmp, "%02X", adr[9]);
	memcpy(dest+22, tmp, 2);

	sprintf(tmp, "%02X", adr[10]);
	memcpy(dest+25, tmp, 2);
	sprintf(tmp, "%02X", adr[11]);
	memcpy(dest+27, tmp, 2);

	sprintf(tmp, "%02X", adr[12]);
	memcpy(dest+30, tmp, 2);
	sprintf(tmp, "%02X", adr[13]);
	memcpy(dest+32, tmp, 2);

	sprintf(tmp, "%02X", adr[14]);
	memcpy(dest+35, tmp, 2);
	sprintf(tmp, "%02X", adr[15]);
	memcpy(dest+37, tmp, 2);
}}}

void pp_hexdump(unsigned char* data, char* dest, int max) {{{
        int i;
	char tmp[3];
	char tmp2[2];
	int off = 0;
	int to = max > 16 ? 16 : max;
	for (i = 0; i < to; i++) {
		if (i == 8) off = 1;
		sprintf(tmp, "%02x", data[i]);
		memcpy(dest+(3*i)+off, tmp, 2);
		if (isprint(data[i])) {
			sprintf(tmp2, "%c", data[i]);
			memcpy(dest+51+i, tmp2, 1);
		}
	}
}}}

void pp_write_header(char* dest, struct ip6_pkt* pkt) {{{
	switch (pkt->ip6_hdr.nxthdr) {
		case 0x3a:
			memcpy(dest, "ICMPv6)", 7);
			break;
		case 0x06:
			memcpy(dest, "TCP)", 4);
			break;
		case 0x11:
			memcpy(dest, "UDP)", 4);
			break;
		default:
			memcpy(dest, "unknown)", 8);
			break;
	}
}}}

void pkt_printf(struct ip6_pkt* pkt) {{{
	char* buf = alloca(strlen(pretty)+1);
	char tmp[9];

	memcpy(buf, pretty, strlen(pretty)+1);

	pp_ip6adr(pkt->ip6_hdr.sadr, buf+16);
	pp_ip6adr(pkt->ip6_hdr.dadr, buf+76);

	int flow = (ntohl(pkt->ip6_hdr.flowlbl));
	sprintf(tmp, "%03x", flow);
	memcpy(buf+138, tmp, 3);
	sprintf(tmp, "%-8d", flow);
	memcpy(buf+143, tmp, 8);

	int length = ntohs(pkt->ip6_hdr.paylgth);
	sprintf(tmp, "%02x", length);
	memcpy(buf+198, tmp, 2);
	sprintf(tmp, "%-3d", length);
	memcpy(buf+203, tmp, 3);

	sprintf(tmp, "%02x", pkt->ip6_hdr.nxthdr);
	memcpy(buf+258, tmp, 2);
	pp_write_header(buf+263, pkt);

	sprintf(tmp, "%02x", pkt->ip6_hdr.hoplmt);
	memcpy(buf+318, tmp, 2);
	sprintf(tmp, "%-3d", pkt->ip6_hdr.hoplmt);
	memcpy(buf+323, tmp, 3);

	int size = ntohs(pkt->ip6_hdr.paylgth);
	int i;
	for(i = 0; i < 8; i++) {
		if (16*i > size) break;
		pp_hexdump(pkt->data + (16*i), buf + 420 + (i*70), size - 16*i);
	}

	printf("%s", buf);
	printf("version: %d\n", pkt->ip6_hdr.version);
}}}

void pkt_printf_ip6tcp(struct ip6_tcp* pkt) {{{
	printf("spt: %u\n", ntohs(pkt->tcp_hdr.spt));
	printf("dpt: %u\n", ntohs(pkt->tcp_hdr.dpt));
	printf("seq: %u\n", ntohs(pkt->tcp_hdr.seq));
	printf("ack: %u\n", ntohs(pkt->tcp_hdr.ack));
	printf("off: %u\n", ntohs(pkt->tcp_hdr.off));
	printf("wsz: %u\n", ntohs(pkt->tcp_hdr.wsz));
	printf("crc: 0x%x\n", ntohs(pkt->tcp_hdr.crc));
	printf("urg: %u\n", ntohs(pkt->tcp_hdr.urg));
	printf("flags: %c%c%c%c%c%c%c%c\n",
			pkt->tcp_hdr.flg & 0x80 ? 'C' : '.',
			pkt->tcp_hdr.flg & 0x40 ? 'E' : '.',
			pkt->tcp_hdr.flg & 0x20 ? 'U' : '.',
			pkt->tcp_hdr.flg & 0x10 ? 'A' : '.',
			pkt->tcp_hdr.flg & 0x08 ? 'P' : '.',
			pkt->tcp_hdr.flg & 0x04 ? 'R' : '.',
			pkt->tcp_hdr.flg & 0x02 ? 'S' : '.',
			pkt->tcp_hdr.flg & 0x01 ? 'F' : '.'
			);
}}}

void pkt_printf_ip6udp(struct ip6_udp* pkt) {{{
	printf("spt: %u\n", ntohs(pkt->udp_hdr.spt));
	printf("dpt: %u\n", ntohs(pkt->udp_hdr.dpt));
	printf("len: %u\n", ntohs(pkt->udp_hdr.len));
	printf("crc: 0x%x\n", ntohs(pkt->udp_hdr.crc));
}}}

static char* dns_types(unsigned short type) {{{
	static char* types[] = { /*{{{*/
		"",
		"A",              // 1 a host address
		"NS",             // 2 an authoritative name server
		"MD",             // 3 a mail destination (Obsolete - use MX)
		"MF",             // 4 a mail forwarder (Obsolete - use MX)
		"CNAME",          // 5 the canonical name for an alias
		"SOA",            // 6 marks the start of a zone of authority
		"MB",             // 7 a mailbox domain name (EXPERIMENTAL)
		"MG",             // 8 a mail group member (EXPERIMENTAL)
		"MR",             // 9 a mail rename domain name (EXPERIMENTAL)
		"NULL",           // 10 a null RR (EXPERIMENTAL)
		"WKS",            // 11 a well known service description
		"PTR",            // 12 a domain name pointer
		"HINFO",          // 13 host information
		"MINFO",          // 14 mailbox or mail list information
		"MX",             // 15 mail exchange
		"TXT",            // 16 text strings
		"RP",
		"AFSDB"
	}; /*}}}*/

	static char* qtypes[] = { /* + 252! {{{ */
		"AXFR",           // 252 A request for a transfer of an entire zone
		"MAILB",          // 253 A request for mailbox-related records (MB, MG or MR)
		"MAILA",          // 254 A request for mail agent RRs (Obsolete - see MX)
		"*",              // 255 A request for all records
	}; /*}}}*/

	if (type <= 18) return types[type];
	if (type >= 252 && type <= 255) return qtypes[type-252];
	
	switch(type) {
		case 24: return "SIG";
		case 25: return "KEY";
		case 28: return "AAAA";
		case 29: return "LOC";
		case 33: return "SRV";
		case 35: return "NAPTR";
		case 36: return "KX";
		case 37: return "CERT";
		case 39: return "DNAME";
		case 42: return "APL";
		case 43: return "DS";
		case 44: return "SSHFP";
		case 45: return "IPSECKEY";
		case 46: return "RRSIG";
		case 47: return "NSEC";
		case 48: return "DNSKEY";
		case 49: return "DHCID";
		case 50: return "NSEC3";
		case 51: return "NSEC3PARAM";
		case 55: return "HIP";
		case 99: return "SPF";
		case 249: return "TKEY";
		case 250: return "TSIG";
		case 32768: return "TA";
		case 32769: return "DLV";
	}

	return 0;

}}}

static char* dns_classes(short class) { /* {{{ */
	static char* classes[] = { /*{{{*/
		"",
		"IN", // 1 the Internet
		"CS", // 2 the CSNET class (Obsolete - used only for examples in some obsolete RFCs)
		"CH", // 3 the CHAOS class
		"HS", // 4 Hesiod [Dyer 87]
	}; /*}}}*/

	if (class <= 4) return classes[class];
	return 0;
}
/*}}}*/

void pkt_printf_dns(struct dns_pkt* upkt) {{{
	struct dns_pkt_parsed* pkt = parse_dns_packet(upkt);

	printf("\nDNS-Packet: ");
	printf("\tid: %5d ", ntohs(pkt->s.id));
	printf("\t%d: %s ", pkt->s.qr, pkt->s.qr == 0 ? "query   " : "response");
	printf("\top: %s ", (char*[]){	"query     ",
									"inverse q.",
									"status    ",
									"inval     "}[pkt->s.op]);
	printf("\trecursion is%s desired ", pkt->s.rd == 0 ? " not" : "    ");
	unsigned short qdcount = ntohs(pkt->s.qdcount);
	unsigned short ancount = ntohs(pkt->s.ancount);
	unsigned short nscount = ntohs(pkt->s.nscount);
	unsigned short arcount = ntohs(pkt->s.arcount);
	printf("\t#qd: %5d ", qdcount);
	printf("\t#an: %5d ", ancount);
	printf("\t#ns: %5d ", nscount);
	printf("\t#ar: %5d\n", arcount);
	
	int i;
	for (i = 0; i < qdcount; i++) { /*{{{*/
		printf("query for %s type=%d (%s) class=%d (%s)\n", pkt->queries[i]->name, ntohs(pkt->queries[i]->qtype), dns_types(ntohs(pkt->queries[i]->qtype)), ntohs(pkt->queries[i]->qclass), dns_classes(ntohs(pkt->queries[i]->qclass)));
	}
	/*}}}*/
	for (i = 0; i < ancount; i++) { /*{{{*/
		printf("answer for %s type=%d (%s) class=%d (%s) ttl=%d data_len=%d\n", pkt->answers[i]->name, ntohs(pkt->answers[i]->type), dns_types(ntohs(pkt->answers[i]->type)), ntohs(pkt->answers[i]->class), dns_classes(ntohs(pkt->answers[i]->class)), ntohl(pkt->answers[i]->ttl), ntohs(pkt->answers[i]->data_len));
	}
	/*}}}*/
	for (i = 0; i < nscount; i++) { /*{{{*/
		printf("nameservers for %s type=%d (%s) class=%d (%s) ttl=%d data_len=%d\n", pkt->nameservers[i]->name, ntohs(pkt->nameservers[i]->type), dns_types(ntohs(pkt->nameservers[i]->type)), ntohs(pkt->nameservers[i]->class), dns_classes(ntohs(pkt->nameservers[i]->class)), ntohl(pkt->nameservers[i]->ttl), ntohs(pkt->nameservers[i]->data_len));
	}
	/*}}}*/
	for (i = 0; i < arcount; i++) { /*{{{*/
		printf("additional record for %s type=%d (%s) class=%d (%s) ttl=%d data_len=%d\n", pkt->additional[i]->name, ntohs(pkt->additional[i]->type), dns_types(ntohs(pkt->additional[i]->type)), ntohs(pkt->additional[i]->class), dns_classes(ntohs(pkt->additional[i]->class)), ntohl(pkt->additional[i]->ttl), ntohs(pkt->additional[i]->data_len));
	}
	/*}}}*/
	GNUNET_free(pkt);
}}}

void pkt_printf_udp_dns(struct udp_dns* pkt) {{{
	pkt_printf_dns(&pkt->data);
}}}

void pkt_printf_ip6dns(struct ip6_udp_dns* pkt) {{{
	pkt_printf_udp_dns(&pkt->udp_dns);
}}}

void pkt_printf_ipdns(struct ip_udp_dns* pkt) {{{
	pkt_printf_udp_dns(&pkt->udp_dns);
}}}
