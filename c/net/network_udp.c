/* $Id: network_udp.c 3337 2005-12-24 20:51:21Z tron $ */

#include "stdafx.h"
#include "debug.h"
#include "string.h"
#include "network_data.h"

#ifdef ENABLE_NETWORK

#include "map.h"
#include "network_gamelist.h"
#include "network_udp.h"
#include "variables.h"

extern void UpdateNetworkGameWindow(bool unselect);
extern void NetworkPopulateCompanyInfo(void);

//
// This file handles all the LAN-stuff
// Stuff like:
//   - UDP search over the network
//

typedef enum {
	PACKET_UDP_CLIENT_FIND_SERVER,
	PACKET_UDP_SERVER_RESPONSE,
	PACKET_UDP_CLIENT_DETAIL_INFO,
	PACKET_UDP_SERVER_DETAIL_INFO, // Is not used in OpenTTD itself, only for external querying
	PACKET_UDP_SERVER_REGISTER, // Packet to register itself to the master server
	PACKET_UDP_MASTER_ACK_REGISTER, // Packet indicating registration has succedeed
	PACKET_UDP_CLIENT_GET_LIST, // Request for serverlist from master server
	PACKET_UDP_MASTER_RESPONSE_LIST, // Response from master server with server ip's + port's
	PACKET_UDP_SERVER_UNREGISTER, // Request to be removed from the server-list
	PACKET_UDP_END
} PacketUDPType;

enum {
	ADVERTISE_NORMAL_INTERVAL = 450,	// interval between advertising in days
	ADVERTISE_RETRY_INTERVAL = 5,			// readvertise when no response after this amount of days
	ADVERTISE_RETRY_TIMES = 3					// give up readvertising after this much failed retries
};

#define DEF_UDP_RECEIVE_COMMAND(type) void NetworkPacketReceive_ ## type ## _command(Packet *p, struct sockaddr_in *client_addr)
void NetworkSendUDP_Packet(SOCKET udp, Packet *p, struct sockaddr_in *recv);

static NetworkClientState _udp_cs;

DEF_UDP_RECEIVE_COMMAND(PACKET_UDP_CLIENT_FIND_SERVER)
{
	Packet *packet;
	// Just a fail-safe.. should never happen
	if (!_network_udp_server)
		return;

	packet = NetworkSend_Init(PACKET_UDP_SERVER_RESPONSE);

	// Update some game_info
	_network_game_info.game_date = _date;
	_network_game_info.map_width = MapSizeX();
	_network_game_info.map_height = MapSizeY();
	_network_game_info.map_set = _opt.landscape;

	NetworkSend_uint8 (packet, NETWORK_GAME_INFO_VERSION);
	NetworkSend_string(packet, _network_game_info.server_name);
	NetworkSend_string(packet, _network_game_info.server_revision);
	NetworkSend_uint8 (packet, _network_game_info.server_lang);
	NetworkSend_uint8 (packet, _network_game_info.use_password);
	NetworkSend_uint8 (packet, _network_game_info.clients_max);
	NetworkSend_uint8 (packet, _network_game_info.clients_on);
	NetworkSend_uint8 (packet, _network_game_info.spectators_on);
	NetworkSend_uint16(packet, _network_game_info.game_date);
	NetworkSend_uint16(packet, _network_game_info.start_date);
	NetworkSend_string(packet, _network_game_info.map_name);
	NetworkSend_uint16(packet, _network_game_info.map_width);
	NetworkSend_uint16(packet, _network_game_info.map_height);
	NetworkSend_uint8 (packet, _network_game_info.map_set);
	NetworkSend_uint8 (packet, _network_game_info.dedicated);

	// Let the client know that we are here
	NetworkSendUDP_Packet(_udp_server_socket, packet, client_addr);

	free(packet);

	DEBUG(net, 2)("[NET][UDP] Queried from %s", inet_ntoa(client_addr->sin_addr));
}

DEF_UDP_RECEIVE_COMMAND(PACKET_UDP_SERVER_RESPONSE)
{
	NetworkGameList *item;
	byte game_info_version;

	// Just a fail-safe.. should never happen
	if (_network_udp_server)
		return;

	game_info_version = NetworkRecv_uint8(&_udp_cs, p);

	if (_udp_cs.quited)
		return;

	DEBUG(net, 6)("[NET][UDP] Server response from %s:%d", inet_ntoa(client_addr->sin_addr),ntohs(client_addr->sin_port));

	// Find next item
	item = NetworkGameListAddItem(inet_addr(inet_ntoa(client_addr->sin_addr)), ntohs(client_addr->sin_port));

	if (game_info_version == 1) {
		NetworkRecv_string(&_udp_cs, p, item->info.server_name, sizeof(item->info.server_name));
		NetworkRecv_string(&_udp_cs, p, item->info.server_revision, sizeof(item->info.server_revision));
		item->info.server_lang   = NetworkRecv_uint8(&_udp_cs, p);
		item->info.use_password  = NetworkRecv_uint8(&_udp_cs, p);
		item->info.clients_max   = NetworkRecv_uint8(&_udp_cs, p);
		item->info.clients_on    = NetworkRecv_uint8(&_udp_cs, p);
		item->info.spectators_on = NetworkRecv_uint8(&_udp_cs, p);
		item->info.game_date     = NetworkRecv_uint16(&_udp_cs, p);
		item->info.start_date    = NetworkRecv_uint16(&_udp_cs, p);
		NetworkRecv_string(&_udp_cs, p, item->info.map_name, sizeof(item->info.map_name));
		item->info.map_width     = NetworkRecv_uint16(&_udp_cs, p);
		item->info.map_height    = NetworkRecv_uint16(&_udp_cs, p);
		item->info.map_set       = NetworkRecv_uint8(&_udp_cs, p);
		item->info.dedicated     = NetworkRecv_uint8(&_udp_cs, p);

		str_validate(item->info.server_name);
		str_validate(item->info.server_revision);
		str_validate(item->info.map_name);
		if (item->info.server_lang >= NETWORK_NUM_LANGUAGES) item->info.server_lang = 0;
		if (item->info.map_set >= NUM_LANDSCAPE ) item->info.map_set = 0;

		if (item->info.hostname[0] == '\0')
			snprintf(item->info.hostname, sizeof(item->info.hostname), "%s", inet_ntoa(client_addr->sin_addr));
	}

	item->online = true;

	UpdateNetworkGameWindow(false);
}

DEF_UDP_RECEIVE_COMMAND(PACKET_UDP_CLIENT_DETAIL_INFO)
{
	NetworkClientState *cs;
	NetworkClientInfo *ci;
	Packet *packet;
	Player *player;
	byte active = 0;
	byte current = 0;
	int i;

	// Just a fail-safe.. should never happen
	if (!_network_udp_server)
		return;

	packet = NetworkSend_Init(PACKET_UDP_SERVER_DETAIL_INFO);

	FOR_ALL_PLAYERS(player) {
		if (player->is_active)
			active++;
	}

	/* Send the amount of active companies */
	NetworkSend_uint8 (packet, NETWORK_COMPANY_INFO_VERSION);
	NetworkSend_uint8 (packet, active);

	/* Fetch the latest version of everything */
	NetworkPopulateCompanyInfo();

	/* Go through all the players */
	FOR_ALL_PLAYERS(player) {
		/* Skip non-active players */
		if (!player->is_active)
			continue;

		current++;

		/* Send the information */
		NetworkSend_uint8 (packet, current);

		NetworkSend_string(packet, _network_player_info[player->index].company_name);
		NetworkSend_uint8 (packet, _network_player_info[player->index].inaugurated_year);
		NetworkSend_uint64(packet, _network_player_info[player->index].company_value);
		NetworkSend_uint64(packet, _network_player_info[player->index].money);
		NetworkSend_uint64(packet, _network_player_info[player->index].income);
		NetworkSend_uint16(packet, _network_player_info[player->index].performance);

                /* Send 1 if there is a passord for the company else send 0 */
		if (_network_player_info[player->index].password[0] != '\0') {
			NetworkSend_uint8 (packet, 1);
		} else {
			NetworkSend_uint8 (packet, 0);
		}

		for (i = 0; i < NETWORK_VEHICLE_TYPES; i++)
			NetworkSend_uint16(packet, _network_player_info[player->index].num_vehicle[i]);

		for (i = 0; i < NETWORK_STATION_TYPES; i++)
			NetworkSend_uint16(packet, _network_player_info[player->index].num_station[i]);

		/* Find the clients that are connected to this player */
		FOR_ALL_CLIENTS(cs) {
			ci = DEREF_CLIENT_INFO(cs);
			if ((ci->client_playas - 1) == player->index) {
				/* The uint8 == 1 indicates that a client is following */
				NetworkSend_uint8(packet, 1);
				NetworkSend_string(packet, ci->client_name);
				NetworkSend_string(packet, ci->unique_id);
				NetworkSend_uint16(packet, ci->join_date);
			}
		}
		/* Also check for the server itself */
		ci = NetworkFindClientInfoFromIndex(NETWORK_SERVER_INDEX);
		if ((ci->client_playas - 1) == player->index) {
			/* The uint8 == 1 indicates that a client is following */
			NetworkSend_uint8(packet, 1);
			NetworkSend_string(packet, ci->client_name);
			NetworkSend_string(packet, ci->unique_id);
			NetworkSend_uint16(packet, ci->join_date);
		}

		/* Indicates end of client list */
		NetworkSend_uint8(packet, 0);
	}

	/* And check if we have any spectators */
	FOR_ALL_CLIENTS(cs) {
		ci = DEREF_CLIENT_INFO(cs);
		if ((ci->client_playas - 1) > MAX_PLAYERS) {
			/* The uint8 == 1 indicates that a client is following */
			NetworkSend_uint8(packet, 1);
			NetworkSend_string(packet, ci->client_name);
			NetworkSend_string(packet, ci->unique_id);
			NetworkSend_uint16(packet, ci->join_date);
		}
	}
	/* Also check for the server itself */
	ci = NetworkFindClientInfoFromIndex(NETWORK_SERVER_INDEX);
	if ((ci->client_playas - 1) > MAX_PLAYERS) {
		/* The uint8 == 1 indicates that a client is following */
		NetworkSend_uint8(packet, 1);
		NetworkSend_string(packet, ci->client_name);
		NetworkSend_string(packet, ci->unique_id);
		NetworkSend_uint16(packet, ci->join_date);
	}

	/* Indicates end of client list */
	NetworkSend_uint8(packet, 0);

	NetworkSendUDP_Packet(_udp_server_socket, packet, client_addr);

	free(packet);
}

DEF_UDP_RECEIVE_COMMAND(PACKET_UDP_MASTER_RESPONSE_LIST) {
	int i;
	struct in_addr ip;
	uint16 port;
	uint8 ver;

	/* packet begins with the protocol version (uint8)
	 * then an uint16 which indicates how many
	 * ip:port pairs are in this packet, after that
	 * an uint32 (ip) and an uint16 (port) for each pair
	 */

	ver = NetworkRecv_uint8(&_udp_cs, p);

	if (_udp_cs.quited)
		return;

	if (ver == 1) {
		for (i = NetworkRecv_uint16(&_udp_cs, p); i != 0 ; i--) {
			ip.s_addr = TO_LE32(NetworkRecv_uint32(&_udp_cs, p));
			port = NetworkRecv_uint16(&_udp_cs, p);
			NetworkUDPQueryServer(inet_ntoa(ip), port);
		}
	}
}

DEF_UDP_RECEIVE_COMMAND(PACKET_UDP_MASTER_ACK_REGISTER) {
	_network_advertise_retries = 0;
	DEBUG(net, 2)("[NET][UDP] We are advertised on the master-server!");

	if (!_network_advertise)
		/* We are advertised, but we don't want to! */
		NetworkUDPRemoveAdvertise();
}


// The layout for the receive-functions by UDP
typedef void NetworkUDPPacket(Packet *p, struct sockaddr_in *client_addr);

static NetworkUDPPacket* const _network_udp_packet[] = {
	RECEIVE_COMMAND(PACKET_UDP_CLIENT_FIND_SERVER),
	RECEIVE_COMMAND(PACKET_UDP_SERVER_RESPONSE),
	RECEIVE_COMMAND(PACKET_UDP_CLIENT_DETAIL_INFO),
	NULL,
	NULL,
	RECEIVE_COMMAND(PACKET_UDP_MASTER_ACK_REGISTER),
	NULL,
	RECEIVE_COMMAND(PACKET_UDP_MASTER_RESPONSE_LIST),
	NULL
};


// If this fails, check the array above with network_data.h
assert_compile(lengthof(_network_udp_packet) == PACKET_UDP_END);


void NetworkHandleUDPPacket(Packet *p, struct sockaddr_in *client_addr)
{
	byte type;

	/* Fake a client, so we can see when there is an illegal packet */
	_udp_cs.socket = INVALID_SOCKET;
	_udp_cs.quited = false;

	type = NetworkRecv_uint8(&_udp_cs, p);

	if (type < PACKET_UDP_END && _network_udp_packet[type] != NULL && !_udp_cs.quited) {
		_network_udp_packet[type](p, client_addr);
	}	else {
		DEBUG(net, 0)("[NET][UDP] Received invalid packet type %d", type);
	}
}


// Send a packet over UDP
void NetworkSendUDP_Packet(SOCKET udp, Packet *p, struct sockaddr_in *recv)
{
	int res;

	// Put the length in the buffer
	p->buffer[0] = p->size & 0xFF;
	p->buffer[1] = p->size >> 8;

	// Send the buffer
	res = sendto(udp, p->buffer, p->size, 0, (struct sockaddr *)recv, sizeof(*recv));

	// Check for any errors, but ignore it for the rest
	if (res == -1) {
		DEBUG(net, 1)("[NET][UDP] Send error: %i", GET_LAST_ERROR());
	}
}

// Start UDP listener
bool NetworkUDPListen(SOCKET *udp, uint32 host, uint16 port, bool broadcast)
{
	struct sockaddr_in sin;

	// Make sure socket is closed
	closesocket(*udp);

	*udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (*udp == INVALID_SOCKET) {
		DEBUG(net, 1)("[NET][UDP] Failed to start UDP support");
		return false;
	}

	// set nonblocking mode for socket
	{
		unsigned long blocking = 1;
#ifndef BEOS_NET_SERVER
		ioctlsocket(*udp, FIONBIO, &blocking);
#else
		setsockopt(*udp, SOL_SOCKET, SO_NONBLOCK, &blocking, NULL);
#endif
	}

	sin.sin_family = AF_INET;
	// Listen on all IPs
	sin.sin_addr.s_addr = host;
	sin.sin_port = htons(port);

	if (bind(*udp, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
		DEBUG(net, 1) ("[NET][UDP] error: bind failed on %s:%i", inet_ntoa(*(struct in_addr *)&host), port);
		return false;
	}

	if (broadcast) {
		/* Enable broadcast */
		unsigned long val = 1;
#ifndef BEOS_NET_SERVER // will work around this, some day; maybe.
		setsockopt(*udp, SOL_SOCKET, SO_BROADCAST, (char *) &val , sizeof(val));
#endif
	}

	DEBUG(net, 1)("[NET][UDP] Listening on port %s:%d", inet_ntoa(*(struct in_addr *)&host), port);

	return true;
}

// Close UDP connection
void NetworkUDPClose(void)
{
	DEBUG(net, 1) ("[NET][UDP] Closed listeners");

	if (_network_udp_server) {
		if (_udp_server_socket != INVALID_SOCKET) {
			closesocket(_udp_server_socket);
			_udp_server_socket = INVALID_SOCKET;
		}

		if (_udp_master_socket != INVALID_SOCKET) {
			closesocket(_udp_master_socket);
			_udp_master_socket = INVALID_SOCKET;
		}

		_network_udp_server = false;
		_network_udp_broadcast = 0;
	} else {
		if (_udp_client_socket != INVALID_SOCKET) {
			closesocket(_udp_client_socket);
			_udp_client_socket = INVALID_SOCKET;
		}
		_network_udp_broadcast = 0;
	}
}

// Receive something on UDP level
void NetworkUDPReceive(SOCKET udp)
{
	struct sockaddr_in client_addr;
	socklen_t client_len;
	int nbytes;
	static Packet *p = NULL;
	int packet_len;

	// If p is NULL, malloc him.. this prevents unneeded mallocs
	if (p == NULL)
		p = malloc(sizeof(Packet));

	packet_len = sizeof(p->buffer);
	client_len = sizeof(client_addr);

	// Try to receive anything
	nbytes = recvfrom(udp, p->buffer, packet_len, 0, (struct sockaddr *)&client_addr, &client_len);

	// We got some bytes.. just asume we receive the whole packet
	if (nbytes > 0) {
		// Get the size of the buffer
		p->size = (uint16)p->buffer[0];
		p->size += (uint16)p->buffer[1] << 8;
		// Put the position on the right place
		p->pos = 2;
		p->next = NULL;

		// Handle the packet
		NetworkHandleUDPPacket(p, &client_addr);

		// Free the packet
		free(p);
		p = NULL;
	}
}

// Broadcast to all ips
void NetworkUDPBroadCast(SOCKET udp)
{
	int i;
	struct sockaddr_in out_addr;
	byte *bcptr;
	uint32 bcaddr;
	Packet *p;

	// Init the packet
	p = NetworkSend_Init(PACKET_UDP_CLIENT_FIND_SERVER);

	// Go through all the ips on this pc
	i = 0;
	while (_network_ip_list[i] != 0) {
		bcaddr = _network_ip_list[i];
		bcptr = (byte *)&bcaddr;
		// Make the address a broadcast address
		bcptr[3] = 255;

		DEBUG(net, 6)("[NET][UDP] Broadcasting to %s", inet_ntoa(*(struct in_addr *)&bcaddr));

		out_addr.sin_family = AF_INET;
		out_addr.sin_port = htons(_network_server_port);
		out_addr.sin_addr.s_addr = bcaddr;

		NetworkSendUDP_Packet(udp, p, &out_addr);

		i++;
	}
}


// Request the the server-list from the master server
void NetworkUDPQueryMasterServer(void)
{
	struct sockaddr_in out_addr;
	Packet *p;

	if (_udp_client_socket == INVALID_SOCKET)
		if (!NetworkUDPListen(&_udp_client_socket, 0, 0, true))
			return;

	p = NetworkSend_Init(PACKET_UDP_CLIENT_GET_LIST);

	out_addr.sin_family = AF_INET;
	out_addr.sin_port = htons(NETWORK_MASTER_SERVER_PORT);
	out_addr.sin_addr.s_addr = NetworkResolveHost(NETWORK_MASTER_SERVER_HOST);

	// packet only contains protocol version
	NetworkSend_uint8(p, NETWORK_MASTER_SERVER_VERSION);

	NetworkSendUDP_Packet(_udp_client_socket, p, &out_addr);

	DEBUG(net, 2)("[NET][UDP] Queried Master Server at %s:%d", inet_ntoa(out_addr.sin_addr),ntohs(out_addr.sin_port));

	free(p);
}

// Find all servers
void NetworkUDPSearchGame(void)
{
	// We are still searching..
	if (_network_udp_broadcast > 0)
		return;

	// No UDP-socket yet..
	if (_udp_client_socket == INVALID_SOCKET)
		if (!NetworkUDPListen(&_udp_client_socket, 0, 0, true))
			return;

	DEBUG(net, 0)("[NET][UDP] Searching server");

	NetworkUDPBroadCast(_udp_client_socket);
	_network_udp_broadcast = 300; // Stay searching for 300 ticks
}

NetworkGameList *NetworkUDPQueryServer(const char* host, unsigned short port)
{
	struct sockaddr_in out_addr;
	Packet *p;
	NetworkGameList *item;
	char hostname[NETWORK_HOSTNAME_LENGTH];

	// No UDP-socket yet..
	if (_udp_client_socket == INVALID_SOCKET)
		if (!NetworkUDPListen(&_udp_client_socket, 0, 0, true))
			return NULL;

	ttd_strlcpy(hostname, host, sizeof(hostname));

	out_addr.sin_family = AF_INET;
	out_addr.sin_port = htons(port);
	out_addr.sin_addr.s_addr = NetworkResolveHost(host);

	// Clear item in gamelist
	item = NetworkGameListAddItem(inet_addr(inet_ntoa(out_addr.sin_addr)), ntohs(out_addr.sin_port));
	memset(&item->info, 0, sizeof(item->info));
	snprintf(item->info.server_name, sizeof(item->info.server_name), "%s", hostname);
	snprintf(item->info.hostname, sizeof(item->info.hostname), "%s", hostname);
	item->online = false;

	// Init the packet
	p = NetworkSend_Init(PACKET_UDP_CLIENT_FIND_SERVER);

	NetworkSendUDP_Packet(_udp_client_socket, p, &out_addr);

	free(p);

	UpdateNetworkGameWindow(false);
	return item;
}

/* Remove our advertise from the master-server */
void NetworkUDPRemoveAdvertise(void)
{
	struct sockaddr_in out_addr;
	Packet *p;

	/* Check if we are advertising */
	if (!_networking || !_network_server || !_network_udp_server)
		return;

	/* check for socket */
	if (_udp_master_socket == INVALID_SOCKET)
		if (!NetworkUDPListen(&_udp_master_socket, _network_server_bind_ip, 0, false))
			return;

	DEBUG(net, 2)("[NET][UDP] Removing advertise..");

	/* Find somewhere to send */
	out_addr.sin_family = AF_INET;
	out_addr.sin_port = htons(NETWORK_MASTER_SERVER_PORT);
	out_addr.sin_addr.s_addr = NetworkResolveHost(NETWORK_MASTER_SERVER_HOST);

	/* Send the packet */
	p = NetworkSend_Init(PACKET_UDP_SERVER_UNREGISTER);
	/* Packet is: Version, server_port */
	NetworkSend_uint8(p, NETWORK_MASTER_SERVER_VERSION);
	NetworkSend_uint16(p, _network_server_port);
	NetworkSendUDP_Packet(_udp_master_socket, p, &out_addr);

	free(p);
}

/* Register us to the master server
     This function checks if it needs to send an advertise */
void NetworkUDPAdvertise(void)
{
	struct sockaddr_in out_addr;
	Packet *p;

	/* Check if we should send an advertise */
	if (!_networking || !_network_server || !_network_udp_server || !_network_advertise)
		return;

	/* check for socket */
	if (_udp_master_socket == INVALID_SOCKET)
		if (!NetworkUDPListen(&_udp_master_socket, _network_server_bind_ip, 0, false))
			return;

	/* Only send once in the 450 game-days (about 15 minutes) */
	if (_network_advertise_retries == 0) {
		if ( (_network_last_advertise_date + ADVERTISE_NORMAL_INTERVAL) > _date)
			return;
		_network_advertise_retries = ADVERTISE_RETRY_TIMES;
	}

	if ( (_network_last_advertise_date + ADVERTISE_RETRY_INTERVAL) > _date)
		return;

	_network_advertise_retries--;
	_network_last_advertise_date = _date;

	/* Find somewhere to send */
	out_addr.sin_family = AF_INET;
	out_addr.sin_port = htons(NETWORK_MASTER_SERVER_PORT);
	out_addr.sin_addr.s_addr = NetworkResolveHost(NETWORK_MASTER_SERVER_HOST);

	DEBUG(net, 1)("[NET][UDP] Advertising to master server");

	/* Send the packet */
	p = NetworkSend_Init(PACKET_UDP_SERVER_REGISTER);
	/* Packet is: WELCOME_MESSAGE, Version, server_port */
	NetworkSend_string(p, NETWORK_MASTER_SERVER_WELCOME_MESSAGE);
	NetworkSend_uint8(p, NETWORK_MASTER_SERVER_VERSION);
	NetworkSend_uint16(p, _network_server_port);
	NetworkSendUDP_Packet(_udp_master_socket, p, &out_addr);

	free(p);
}

void NetworkUDPInitialize(void)
{
	_udp_client_socket = INVALID_SOCKET;
	_udp_server_socket = INVALID_SOCKET;
	_udp_master_socket = INVALID_SOCKET;

	_network_udp_server = false;
	_network_udp_broadcast = 0;
}

#endif /* ENABLE_NETWORK */
