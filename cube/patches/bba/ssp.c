/*
 * Copyright (c) 2026, russeree
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

/*
 * Swiss Streaming Protocol (SSP)
 *
 * One READ asks the server for up to `window` DATA packets. Each DATA packet
 * carries its absolute file offset, so the client only accepts the next
 * expected offset and re-requests from there on anything else (go-back-N).
 * The server marks the final packet of a batch with SSP_FLAG_LAST, which
 * triggers the next READ. The server must pace a batch to the rate the BBA
 * receive ring drains over EXI, since the ring only holds about two full
 * frames.
 */

#ifndef SSP_WINDOW
#define SSP_WINDOW 16
#endif

#ifndef SSP_TIMEOUT
#define SSP_TIMEOUT OSMillisecondsToTicks(100)
#endif

enum {
	SSP_OP_READ = 0xA1,
	SSP_OP_DATA = 0xA2,
};

enum {
	SSP_FLAG_LAST  = 1 << 0,
	SSP_FLAG_ERROR = 1 << 1,
};

typedef struct {
	uint8_t op;
	uint8_t arg;      // READ: window, DATA: flags
	uint16_t req_id;
	uint32_t offset;
	uint32_t length;  // READ: bytes wanted, DATA: payload bytes
	uint8_t data[];   // READ: u16 chunk size + path, DATA: payload
} __attribute((packed)) ssp_header_t;

static struct {
	uint16_t req_id;
	struct {
		void *buffer;
		uint32_t length;
		uint32_t offset;
		uint32_t position;
		const char *path;
		uint16_t pathlen;
		frag_callback callback;
	} queue[QUEUE_SIZE], *queued;
} _ssp;

static void ssp_read(uint32_t offset, uint32_t length, const char *path, uint16_t pathlen)
{
	uint8_t *data = (*_bba.page)[1];
	eth_header_t *eth = (eth_header_t *)data;
	ipv4_header_t *ipv4 = (ipv4_header_t *)eth->data;
	udp_header_t *udp = (udp_header_t *)ipv4->data;
	ssp_header_t *ssp = (ssp_header_t *)udp->data;

	ssp->op = SSP_OP_READ;
	ssp->arg = SSP_WINDOW;
	ssp->req_id = ++_ssp.req_id;
	ssp->offset = offset;
	ssp->length = length;
	*(uint16_t *)ssp->data = OSRoundDown32B(env->pmtu - (ssp->data - eth->data));
	memcpy(ssp->data + sizeof(uint16_t), path, pathlen);

	udp->src_port = env->port;
	udp->dst_port = env->port;
	udp->length = sizeof(*udp) + sizeof(*ssp) + sizeof(uint16_t) + pathlen;
	udp->checksum = 0x0000;

	ipv4->version = 4;
	ipv4->words = sizeof(*ipv4) / 4;
	ipv4->dscp = 46;
	ipv4->ecn = 0b00;
	ipv4->length = sizeof(*ipv4) + udp->length;
	ipv4->id = 0;
	ipv4->flags = 0b000;
	ipv4->offset = 0;
	ipv4->ttl = 64;
	ipv4->protocol = IP_PROTO_UDP;
	ipv4->checksum = 0x0000;
	ipv4->src_addr = env->client_ip;
	ipv4->dst_addr = env->server_ip;
	ipv4->checksum = ipv4_checksum(ipv4);

	eth->dst_addr = env->router_mac;
	eth->src_addr = env->client_mac;
	eth->type = ETH_TYPE_IPV4;
	bba_output(eth, sizeof(*eth) + ipv4->length);
}

static void ssp_read_queued(void)
{
	if (!_bba.locked) {
		if (!_bba.callback) {
			_bba.callback = ssp_read_queued;
			exi_lock();
		}
		return;
	}

	uint32_t length = _ssp.queued->length - _ssp.queued->offset;
	uint32_t offset = _ssp.queued->position + _ssp.queued->offset;

	ssp_read(offset, length, _ssp.queued->path, _ssp.queued->pathlen);

	OSSetAlarm(&read_alarm, SSP_TIMEOUT, (OSAlarmHandler)ssp_read_queued);
}

static void ssp_pop_queue(void)
{
	#if QUEUE_SIZE > 2
	for (int i = 0; i < QUEUE_SIZE; i++) {
		if (_ssp.queue[i].callback != NULL && _ssp.queue[i].length + _ssp.queue[i].position % 512 <= 512) {
			_ssp.queued = &_ssp.queue[i];
			ssp_read_queued();
			return;
		}
	}
	#endif
	for (int i = 0; i < QUEUE_SIZE; i++) {
		if (_ssp.queue[i].callback != NULL) {
			_ssp.queued = &_ssp.queue[i];
			ssp_read_queued();
			return;
		}
	}
}

static void ssp_done_queued(void)
{
	_ssp.queued->callback(_ssp.queued->buffer, _ssp.queued->offset);

	_ssp.queued->callback = NULL;
	_ssp.queued = NULL;

	ssp_pop_queue();
}

bool do_read_disc(void *buffer, uint32_t length, uint32_t offset, const frag_t *frag, frag_callback callback)
{
	for (int i = 0; i < QUEUE_SIZE; i++) {
		if (_ssp.queue[i].callback == NULL) {
			_ssp.queue[i].buffer = buffer;
			_ssp.queue[i].length = length;
			_ssp.queue[i].offset = 0;
			_ssp.queue[i].position = offset;
			_ssp.queue[i].path = frag->path;
			_ssp.queue[i].pathlen = frag->pathlen;
			_ssp.queue[i].callback = callback;

			if (_ssp.queued == NULL) {
				_ssp.queued = &_ssp.queue[i];
				ssp_read_queued();
			}
			return true;
		}
	}

	return false;
}

static bool ssp_input(bba_page_t *page, eth_header_t *eth, ipv4_header_t *ipv4, udp_header_t *udp, ssp_header_t *ssp, size_t size)
{
	if (size < sizeof(*ssp) + ssp->length)
		return false;
	if (udp->length < sizeof(*udp) + sizeof(*ssp) + ssp->length)
		return false;

	if (ssp->op != SSP_OP_DATA || ssp->req_id != _ssp.req_id || _ssp.queued == NULL)
		return true;
	if (ssp->arg & SSP_FLAG_ERROR)
		return true;

	OSCancelAlarm(&read_alarm);

	if (ssp->offset != _ssp.queued->position + _ssp.queued->offset) {
		ssp_read_queued();
		return true;
	}

	uint8_t *data = _ssp.queued->buffer + _ssp.queued->offset;
	size_t data_size = MIN(_ssp.queued->length - _ssp.queued->offset, OSRoundDown32B(ssp->length));

	_ssp.queued->offset += data_size;

	if (_ssp.queued->offset != _ssp.queued->length) {
		if (ssp->arg & SSP_FLAG_LAST)
			ssp_pop_queue();
		else
			OSSetAlarm(&read_alarm, SSP_TIMEOUT, (OSAlarmHandler)ssp_read_queued);
	}

	bba_input(data, data_size, ssp->data - page[0]);

	if (_ssp.queued->offset == _ssp.queued->length)
		ssp_done_queued();

	return true;
}
