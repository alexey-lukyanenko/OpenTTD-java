/* $Id: network_udp.h 2962 2005-09-18 20:56:44Z Darkvater $ */

#ifndef NETWORK_UDP_H
#define NETWORK_UDP_H

#ifdef ENABLE_NETWORK

void NetworkUDPInitialize(void);
bool NetworkUDPListen(SOCKET *udp, uint32 host, uint16 port, bool broadcast);
void NetworkUDPReceive(SOCKET udp);
void NetworkUDPSearchGame(void);
void NetworkUDPQueryMasterServer(void);
NetworkGameList *NetworkUDPQueryServer(const char* host, unsigned short port);
void NetworkUDPAdvertise(void);
void NetworkUDPRemoveAdvertise(void);

#endif

#endif /* NETWORK_UDP_H */
