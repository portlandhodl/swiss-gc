/* 
 * Copyright (c) 2017-2024, Extrems <extrems@extremscorner.org>
 * 
 * This file is part of Swiss.
 * 
 * Swiss is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Swiss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * with Swiss.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "bba.h"
#include "common.h"
#include "dolphin/exi.h"
#include "dolphin/os.h"
#include "frag.h"

#ifndef QUEUE_SIZE
#define QUEUE_SIZE 2
#endif

#define MIN_FRAME_SIZE 60

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_VLAN 0x8100

#define HW_ETHERNET 1

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17

enum {
	ARP_REQUEST = 1,
	ARP_REPLY,
};

struct eth_addr {
	uint64_t addr : 48;
} __attribute((packed));

struct ipv4_addr {
	uint32_t addr;
} __attribute((packed));

typedef struct {
	struct eth_addr dst_addr;
	struct eth_addr src_addr;
	uint16_t type;
	uint8_t data[];
} __attribute((packed)) eth_header_t;

typedef struct {
	uint16_t pcp : 3;
	uint16_t dei : 1;
	uint16_t vid : 12;
	uint16_t type;
	uint8_t data[];
} __attribute((packed)) vlan_header_t;

typedef struct {
	uint16_t hardware_type;
	uint16_t protocol_type;
	uint8_t hardware_length;
	uint8_t protocol_length;
	uint16_t operation;

	struct eth_addr src_mac;
	struct ipv4_addr src_ip;
	struct eth_addr dst_mac;
	struct ipv4_addr dst_ip;
} __attribute((packed)) arp_packet_t;

typedef struct {
	uint8_t version : 4;
	uint8_t words   : 4;
	uint8_t dscp    : 6;
	uint8_t ecn     : 2;
	uint16_t length;
	uint16_t id;
	uint16_t flags  : 3;
	uint16_t offset : 13;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t checksum;
	struct ipv4_addr src_addr;
	struct ipv4_addr dst_addr;
	uint8_t data[];
} __attribute((packed)) ipv4_header_t;

typedef struct {
	uint16_t src_port;
	uint16_t dst_port;
	uint16_t length;
	uint16_t checksum;
	uint8_t data[];
} __attribute((packed)) udp_header_t;

static struct {
	struct eth_addr client_mac;
	struct eth_addr router_mac;
	struct ipv4_addr client_ip;
	struct ipv4_addr router_ip;
	struct ipv4_addr server_ip;
	uint16_t port;
	uint16_t pmtu;
} *const env = (void *)VAR_NETWORK_ENV;

static uint16_t ipv4_checksum(ipv4_header_t *header)
{
	uint16_t *data = (uint16_t *)header;
	uint32_t sum[2] = {0};

	for (int i = 0; i < header->words; i++) {
		sum[0] += *data++;
		sum[1] += *data++;
	}

	sum[0] += sum[1];
	sum[0] += sum[0] >> 16;
	return ~sum[0];
}

#include "fsp.c"

static bool udp_input(bba_page_t *page, eth_header_t *eth, ipv4_header_t *ipv4, udp_header_t *udp, size_t size)
{
	if (size < sizeof(*udp))
		return false;
	if (udp->length < sizeof(*udp))
		return false;

	size -= sizeof(*udp);

	if (ipv4->src_addr.addr == env->server_ip.addr &&
		ipv4->dst_addr.addr == env->client_ip.addr) {

		env->router_mac = eth->src_addr;

		if (udp->src_port == env->port &&
			udp->dst_port == env->port)
			return fsp_input(page, eth, ipv4, udp, (void *)udp->data, size);
	}

	return false;
}

static bool ipv4_input(bba_page_t *page, eth_header_t *eth, ipv4_header_t *ipv4, size_t size)
{
	if (ipv4->version != 4)
		return false;
	if (ipv4->words < 5 || ipv4->words * 4 > ipv4->length)
		return false;
	if (size < ipv4->length)
		return false;
	if (ipv4->offset != 0 || (ipv4->flags & 0b001))
		return false;
	if (ipv4_checksum(ipv4))
		return false;

	size = ipv4->length - ipv4->words * 4;

	switch (ipv4->protocol) {
		case IP_PROTO_UDP:
			return udp_input(page, eth, ipv4, (void *)ipv4 + ipv4->words * 4, size);
	}

	return false;
}

static void arp_reply(arp_packet_t *request)
{
	uint8_t *data = (*_bba.page)[1];
	eth_header_t *eth = (eth_header_t *)data;
	arp_packet_t *arp = (arp_packet_t *)eth->data;

	arp->hardware_type = HW_ETHERNET;
	arp->hardware_length = sizeof(struct eth_addr);
	arp->protocol_type = ETH_TYPE_IPV4;
	arp->protocol_length = sizeof(struct ipv4_addr);
	arp->operation = ARP_REPLY;
	arp->src_mac = env->client_mac;
	arp->src_ip = env->client_ip;
	arp->dst_mac = request->src_mac;
	arp->dst_ip = request->src_ip;

	eth->dst_addr = arp->dst_mac;
	eth->src_addr = arp->src_mac;
	eth->type = ETH_TYPE_ARP;
	bba_output(eth, MIN_FRAME_SIZE);
}

static bool arp_input(bba_page_t *page, eth_header_t *eth, arp_packet_t *arp, size_t size)
{
	if (arp->hardware_type != HW_ETHERNET || arp->hardware_length != sizeof(struct eth_addr))
		return false;
	if (arp->protocol_type != ETH_TYPE_IPV4 || arp->protocol_length != sizeof(struct ipv4_addr))
		return false;

	switch (arp->operation) {
		case ARP_REQUEST:
			if ((!arp->dst_mac.addr ||
				arp->dst_mac.addr == env->client_mac.addr) &&
				arp->dst_ip.addr  == env->client_ip.addr) {
				arp_reply(arp);

				if (arp->src_ip.addr == env->router_ip.addr)
					env->router_mac = arp->src_mac;

				return true;
			}
			break;
		case ARP_REPLY:
			if (arp->dst_mac.addr == env->client_mac.addr &&
				arp->dst_ip.addr  == env->client_ip.addr &&
				arp->src_ip.addr  == env->router_ip.addr)
				env->router_mac = arp->src_mac;
			break;
	}

	return false;
}

static bool eth_input(bba_page_t *page, eth_header_t *eth, size_t size)
{
	if (size < MIN_FRAME_SIZE)
		return true;

	size -= sizeof(*eth);

	switch (eth->type) {
		case ETH_TYPE_ARP:
			return arp_input(page, eth, (void *)eth->data, size);
		case ETH_TYPE_IPV4:
			return ipv4_input(page, eth, (void *)eth->data, size);
	}

	return false;
}
