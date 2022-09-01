#include <stdio.h>
#include <iostream>
#include <enet/enet.h>
#include <SDL.h>
#include <string>
#include "Main.h"
#include "NetworkDefine.h"

ENetHost *host;
ENetAddress hostAddress;
CLIENT clients[MAX_CLIENTS];

//Functions needed for getting ENetAddress from IP and port
static int ConvertIpToAddress(ENetAddress *address, const char *name)
{
	enet_uint8 vals [4] = {0, 0, 0, 0};
	for (int i = 0; i < 4; i++)
	{
		const char * next = name + 1;
		if (*name != '0')
		{
			long val = strtol(name, (char**)&next, 10);
			if (val < 0 || val > 0xFF || next == name || next - name > 3)
				return -1;
			vals[i] = (enet_uint8)val;
		}

		if (*next != (i < 3 ? '.' : '\0'))
			return -1;
		name = next + 1;
	}
	
	memcpy(&address->host, vals, sizeof(enet_uint32));
	address->host = SDL_SwapLE32(address->host);
	return 0;
}

static bool VerifyPort(const char *port)
{
	for (int i = 0; port[i]; i++)
	{
		if (port[i] < '0' || port[i] > '9')
			return false;
	}
	
	return true;
}

//Start hosting a server
SDL_Thread *ServerThread;
bool toEndThread = false;

void HandleServerEvent(ENetEvent event)
{
	switch (event.type)
	{
		case ENET_EVENT_TYPE_CONNECT:
			//Add peer to client-list
			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				if (clients[i].peer == NULL)
				{
					//Remove old data
					free(clients[i].skinData);
					memset(&clients[i], 0, sizeof(CLIENT));
					
					//Set peer
					clients[i].peer = (void*)event.peer;
					printf("User successfully connected,\nHost: %d\n", event.peer->address.host);
					
					//Send all player skins
					for (int v = 0; v < MAX_CLIENTS; v++)
					{
						if (v != i && clients[v].peer && clients[v].skinData)
						{
							const int packetSize = 12 + clients[v].skinSize;
							uint8_t *skinPacket = (uint8_t*)malloc(packetSize);
							SDL_RWops *skinPacketData = SDL_RWFromMem(skinPacket, packetSize);
							SDL_WriteLE32(skinPacketData, NET_VERSION);
							SDL_WriteLE32(skinPacketData, PACKETCODE_SKIN);
							SDL_WriteLE32(skinPacketData, v);
							SDL_RWwrite(skinPacketData, clients[v].skinData, 1, clients[v].skinSize);
							SDL_RWclose(skinPacketData);
							
							//Send packet
							ENetPacket *definePacket = enet_packet_create(skinPacket, packetSize, ENET_PACKET_FLAG_RELIABLE);
							enet_peer_send((ENetPeer*)event.peer, 0, definePacket);
							free(skinPacket);
						}
					}
					break;
				}
				else if (i == MAX_CLIENTS - 1)
				{
					//Failed to connect
					printf("User attempted to connect,\nbut the server is full...\n(Host: %d)\n", event.peer->address.host);
					enet_peer_disconnect_now(event.peer, DISCONNECT_FORCE);
					break;
				}
			}
			
			break;
			
		case ENET_EVENT_TYPE_DISCONNECT:
			//Remove peer from client-list
			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				if (clients[i].peer == event.peer)
				{
					//Quit message if has a name
					if (strlen(clients[i].name) >= NAME_MIN)
					{
						char quitMsg[PACKET_DATA];
						sprintf(quitMsg, "%s has left", clients[i].name);
						BroadcastChatMessage(quitMsg);
					}
					
					//Remove data
					free(clients[i].skinData);
					memset(&clients[i], 0, sizeof(CLIENT));
					break;
				}
			}
			
			//Disconnected
			printf("User disconnected,\nHost: %d\n", event.peer->address.host);
			break;
			
		case ENET_EVENT_TYPE_RECEIVE:
			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				if (clients[i].peer == event.peer)
				{
					//Handle packet data
					SDL_RWops *packetData = SDL_RWFromConstMem(event.packet->data, event.packet->dataLength);
					SDL_RWops *repPacketData;
					//Brayconn changes (only apply to wounceMsg)
					char* wounceMsg = new char[event.packet->dataLength - 8];
					int netver;
					
					if ((netver = SDL_ReadLE32(packetData)) == NET_VERSION)
					{
						switch (SDL_ReadLE32(packetData))
						{
							case PACKETCODE_DEFINE_PLAYER:
								//Load name
								SDL_RWread(packetData, clients[i].name, 1, MAX_NAME);
								
								//Broadcast join message
								char joinMsg[PACKET_DATA];
								sprintf(joinMsg, "%s has joined", clients[i].name);
								BroadcastChatMessage(joinMsg);
								break;
							case PACKETCODE_CHAT_MESSAGE:
								SDL_RWread(packetData, wounceMsg, 1, event.packet->dataLength - 8);
								BroadcastChatMessage(wounceMsg);
								break;
							case PACKETCODE_REPLICATE_PLAYER:
								//Bounce to other clients
								char packet[0x100];
								memset(packet, 0, 0x100);
								
								repPacketData = SDL_RWFromMem(packet, 0x100);
								
								SDL_WriteLE32(repPacketData, NET_VERSION);
								SDL_WriteLE32(repPacketData, PACKETCODE_REPLICATE_PLAYER);
								
								//Set attributes
								SDL_WriteLE32(repPacketData, i);
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//cond
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//unit
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//flag
								SDL_RWwrite(repPacketData, clients[i].name, 1, MAX_NAME);	//name
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//x
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//y
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//up
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//down
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//arms
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//equip
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//ani_no
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//direct
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//shock
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//stage
								SDL_WriteLE32(repPacketData, SDL_ReadLE32(packetData));		//mim
								SDL_RWclose(repPacketData);
								
								for (int v = 0; v < MAX_CLIENTS; v++)
								{
									if (v != i && clients[v].peer)
									{
										//Send packet
										ENetPacket *definePacket = enet_packet_create(packet, 0x100, 0);
										enet_peer_send((ENetPeer*)clients[v].peer, 0, definePacket);
									}
								}
								break;
								
							case PACKETCODE_SKIN:
								//Set player's skin
								const int skinDataSize = event.packet->dataLength - 8;
								free(clients[i].skinData);
								clients[i].skinData = (uint8_t*)malloc(skinDataSize);
								clients[i].skinSize = skinDataSize;
								SDL_RWread(packetData, clients[i].skinData, 1, skinDataSize);
								
								printf("Received skin for %s\n", clients[i].name);
								
								//Send all players skin
								const unsigned int packetSize = 12 + skinDataSize;
								uint8_t *skinPacket = (uint8_t*)malloc(packetSize);
								SDL_RWops *skinPacketData = SDL_RWFromMem(skinPacket, packetSize);
								SDL_WriteLE32(skinPacketData, NET_VERSION);
								SDL_WriteLE32(skinPacketData, PACKETCODE_SKIN);
								SDL_WriteLE32(skinPacketData, i);
								SDL_RWwrite(skinPacketData, clients[i].skinData, 1, skinDataSize);
								SDL_RWclose(skinPacketData);
								
								for (int v = 0; v < MAX_CLIENTS; v++)
								{
									if (v != i && clients[v].peer)
									{
										//Send packet
										ENetPacket *definePacket = enet_packet_create(skinPacket, packetSize, ENET_PACKET_FLAG_RELIABLE);
										enet_peer_send((ENetPeer*)clients[v].peer, 0, definePacket);
										printf("Sent their skin to %s\n", clients[v].name);
									}
								}
								
								free(skinPacket);
								break;
						}
					}
					else
					{
						printf("User disconnected,\nReason: Invalid NET_VERSION (%d)\nHost: %d\n", netver, event.peer->address.host);
						enet_peer_disconnect_now(event.peer, DISCONNECT_FORCE);
					}
					
					// We have to delete it, since we used new -Brayconn
					delete[] wounceMsg;
					SDL_RWclose(packetData);
					break;
				}
			}
			
			//Finished with the packet
			enet_packet_destroy(event.packet);
			break;
	}
}

int HandleServerSynchronous(void *ptr)
{
	ENetEvent event;
	
	while (IsHosting() && !toEndThread)
	{
		int ret = enet_host_service(host, &event, 2000);
		
		if (ret < 0)
		{
			KillServer();
			return -1;
		}
		
		if (ret == 0)
			continue;
		
		HandleServerEvent(event);
	}
	
	return 0;
}

bool StartServer(const char* ip, const char *port)
{
	//Set IP address
	if (enet_address_set_host(&hostAddress, ip) < 0)
		return false;
	
	//Set port
	if (!VerifyPort(port))
		return false;
	hostAddress.port = std::stoi(std::string(port), nullptr, 10);
	
	//Create host
	if ((host = enet_host_create(&hostAddress, MAX_CLIENTS, 1, 0, 0)) == NULL)
		return false;
	
	//Start thread
	toEndThread = false;
	ServerThread = SDL_CreateThread(HandleServerSynchronous, "ServerThread", (void*)NULL);
	return true;
}

//Kill server
void KillServer()
{
	//End thread
	toEndThread = true;
	SDL_WaitThread(ServerThread, NULL);
	
	//Disconnect all clients
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (clients[i].peer != NULL)
		{
			enet_peer_disconnect_now((ENetPeer*)clients[i].peer, DISCONNECT_FORCE);
			clients[i].peer = NULL;
		}
	}
	//Kill host
	if (host)
		enet_host_destroy(host);
	host = NULL;
}

//Check if host still exists
bool IsHosting()
{
	return (host != NULL);
}

//Chat
void BroadcastChatMessage(const char *text)
{
	//Write packet data
	auto packetSize = 8 + (strlen(text) + 1);
	uint8_t* packet = new uint8_t[packetSize];
	
	SDL_RWops *packetData = SDL_RWFromMem(packet, packetSize);
	SDL_WriteLE32(packetData, NET_VERSION);
	SDL_WriteLE32(packetData, PACKETCODE_CHAT_MESSAGE);
	SDL_RWwrite(packetData, text, 1, strlen(text) + 1);
	SDL_RWclose(packetData);

	//Send packet
	ENetPacket *definePacket = enet_packet_create(packet, packetSize, ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(host, 0, definePacket);
	delete[] packet;
}

#undef main

int main(int argc, char *argv[])
{
	if (argc <= 2)
	{
		std::cout << "Freeware Online Command-line server\nHow to use:\nserver 'ip' 'port'\n";
		return -1;
	}
	else
	{
		//Init enet
		if (enet_initialize() < 0)
			std::cout << "Failed to initialize ENet\n";

		//Init SDL
		if (SDL_Init(0) < 0)
		{
			std::cout << "Failed to initialize SDL2\n";
			return -1;
		}
		// port "
		//Start server
		std::cout << "Trying to run server at " << argv[1] << ":" << argv[2] << std::endl;
		
		if (!StartServer(argv[1],argv[2]))
		{
			std::cout << "Failed to start server\n";
			return -1;
		}
		
		std::cout << "Success, type 'quit' to shutdown...\n";
		
		//Wait for q to be pressed
		while (true)
		{
			std::string command;
			std::cin >> command;

			if (command == "quit")
				break;
		}
		
		KillServer();
		std::cout << "Shutdown server!\n";
	}
	
	enet_deinitialize();
	SDL_Quit();
	return 0;
}