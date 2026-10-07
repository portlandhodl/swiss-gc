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

enum {
	CC_NULL    = 0x00,
	CC_VERSION = 0x10,
	CC_ERR     = 0x40,
	CC_GET_DIR,
	CC_GET_FILE,
};

typedef struct {
	uint8_t command;
	uint8_t checksum;
	uint16_t key;
	uint16_t sequence;
	uint16_t data_length;
	uint32_t position;
	uint8_t data[];
} __attribute((packed)) fsp_header_t;

static struct {
	uint8_t command;
	uint16_t key;
	uint16_t sequence;
	uint16_t data_length;
	uint32_t position;
	struct {
		void *buffer;
		uint32_t length;
		uint32_t offset;
		uint32_t position;
		const char *path;
		uint16_t pathlen;
		frag_callback callback;
	} queue[QUEUE_SIZE], *queued;
} _fsp;

static uint8_t fsp_checksum(fsp_header_t *header, size_t size)
{
	uint8_t *data = (uint8_t *)header;
	uint32_t sum = size;

	for (int i = 0; i < size; i++)
		sum += *data++;

	sum += sum >> 8;
	return sum;
}

static void fsp_get_file(uint32_t offset, uint32_t length, const char *path, uint16_t pathlen)
{
	uint8_t *data = (*_bba.page)[1];
	eth_header_t *eth = (eth_header_t *)data;
	ipv4_header_t *ipv4 = (ipv4_header_t *)eth->data;
	udp_header_t *udp = (udp_header_t *)ipv4->data;
	fsp_header_t *fsp = (fsp_header_t *)udp->data;

	_fsp.command = CC_GET_FILE;
	_fsp.sequence++;
	_fsp.position = offset;
	_fsp.data_length = MIN(length, env->pmtu - (fsp->data - eth->data));

	fsp->command = _fsp.command;
	fsp->checksum = 0x00;
	fsp->key = _fsp.key;
	fsp->sequence = _fsp.sequence;
	fsp->position = _fsp.position;
	fsp->data_length = pathlen;
	*(uint16_t *)(memcpy(fsp->data, path, pathlen) + fsp->data_length) = _fsp.data_length;
	fsp->checksum = fsp_checksum(fsp, sizeof(*fsp) + fsp->data_length + sizeof(uint16_t));

	udp->src_port = env->port;
	udp->dst_port = env->port;
	udp->length = sizeof(*udp) + sizeof(*fsp) + fsp->data_length + sizeof(uint16_t);
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

static void fsp_read_queued(void)
{
	if (!_bba.locked) {
		if (!_bba.callback) {
			_bba.callback = fsp_read_queued;
			exi_lock();
		}
		return;
	}

	void *buffer = _fsp.queued->buffer + _fsp.queued->offset;
	uint32_t length = _fsp.queued->length - _fsp.queued->offset;
	uint32_t offset = _fsp.queued->position + _fsp.queued->offset;
	const char *path = _fsp.queued->path;
	uint16_t pathlen = _fsp.queued->pathlen;

	fsp_get_file(offset, length, path, pathlen);

	OSSetAlarm(&read_alarm, OSSecondsToTicks(1), (OSAlarmHandler)fsp_read_queued);
}

static void fsp_pop_queue(void)
{
	#if QUEUE_SIZE > 2
	for (int i = 0; i < QUEUE_SIZE; i++) {
		if (_fsp.queue[i].callback != NULL && _fsp.queue[i].length + _fsp.queue[i].position % 512 <= 512) {
			_fsp.queued = &_fsp.queue[i];
			fsp_read_queued();
			return;
		}
	}
	#endif
	for (int i = 0; i < QUEUE_SIZE; i++) {
		if (_fsp.queue[i].callback != NULL) {
			_fsp.queued = &_fsp.queue[i];
			fsp_read_queued();
			return;
		}
	}
}

static void fsp_done_queued(void)
{
	void *buffer = _fsp.queued->buffer;
	uint32_t length = _fsp.queued->length;
	uint32_t offset = _fsp.queued->offset;
	const char *path = _fsp.queued->path;
	uint16_t pathlen = _fsp.queued->pathlen;

	_fsp.queued->callback(buffer, offset);

	_fsp.queued->callback = NULL;
	_fsp.queued = NULL;

	fsp_pop_queue();
}

bool do_read_disc(void *buffer, uint32_t length, uint32_t offset, const frag_t *frag, frag_callback callback)
{
	for (int i = 0; i < QUEUE_SIZE; i++) {
		if (_fsp.queue[i].callback == NULL) {
			_fsp.queue[i].buffer = buffer;
			_fsp.queue[i].length = length;
			_fsp.queue[i].offset = 0;
			_fsp.queue[i].position = offset;
			_fsp.queue[i].path = frag->path;
			_fsp.queue[i].pathlen = frag->pathlen;
			_fsp.queue[i].callback = callback;

			if (_fsp.queued == NULL) {
				_fsp.queued = &_fsp.queue[i];
				fsp_read_queued();
			}
			return true;
		}
	}

	return false;
}

static bool fsp_input(bba_page_t *page, eth_header_t *eth, ipv4_header_t *ipv4, udp_header_t *udp, fsp_header_t *fsp, size_t size)
{
	if (size < sizeof(*fsp) + fsp->data_length)
		return false;
	if (udp->length < sizeof(*udp) + sizeof(*fsp) + fsp->data_length)
		return false;

	size -= sizeof(*fsp);

	_fsp.key = fsp->key;

	switch (fsp->command) {
		case CC_ERR:
			break;
		case CC_GET_FILE:
			if (fsp->command  == _fsp.command  &&
				fsp->sequence == _fsp.sequence &&
				fsp->position == _fsp.position) {

				OSCancelAlarm(&read_alarm);

				uint8_t *data = _fsp.queued->buffer + _fsp.queued->offset;
				size_t data_size = MIN(_fsp.data_length, OSRoundDown32B(fsp->data_length));

				_fsp.command = CC_NULL;
				_fsp.queued->offset += data_size;

				if (_fsp.queued->offset != _fsp.queued->length)
					fsp_pop_queue();

				bba_input(data, data_size, fsp->data - page[0]);

				if (_fsp.queued->offset == _fsp.queued->length)
					fsp_done_queued();
			}
			break;
	}

	return true;
}
