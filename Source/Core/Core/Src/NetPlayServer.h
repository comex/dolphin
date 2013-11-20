// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _NETPLAY_SERVER_H
#define _NETPLAY_SERVER_H

#include "Common.h"
#include "CommonTypes.h"
#include "Thread.h"
#include "Timer.h"

#include "enet/enet.h"
#include "NetPlayProto.h"
#include "FifoQueue.h"
#include "TraversalClient.h"
#include "IOSync.h"

#include <functional>
#include <unordered_set>

class NetPlayUI;

class NetPlayServer : public NetHostClient, public TraversalClientClient
{
public:
	NetPlayServer();
	~NetPlayServer();

	bool ChangeGame(const std::string& game) /* ON(GUI) */;

	void SetNetSettings(const NetSettings &settings) /* ON(GUI) */;

	bool StartGame(const std::string &path) /* ON(GUI) */;

	void AdjustPadBufferSize(unsigned int size) /* multiple threads */;

	void SetDialog(NetPlayUI* dialog);

	virtual void OnENetEvent(ENetEvent*) override ON(NET);
	virtual void OnData(ENetEvent* event, Packet&& packet) ON(NET);
	virtual void OnTraversalStateChanged() override ON(NET);
	virtual void OnConnectReady(ENetAddress addr) override {}
	virtual void OnConnectFailed(u8 reason) override ON(NET) {}

	std::unordered_set<std::string> GetInterfaceSet();
	std::string GetInterfaceHost(std::string interface);

private:
	class Client
	{
	public:
		Client() { connected = false; }
		std::string		name;
		std::string		revision;

		u32 ping;
		u32 current_game;
		bool connected;
		bool in_game;
		//bool m_devices_present[IOSync::Class::NumClasses][IOSync::Class::MaxDeviceIndex];
		//bool is_localhost;
	};

	void SendToClients(Packet&& packet, const PlayerId skip_pid = -1) NOT_ON(NET);
	void SendToClientsOnThread(Packet&& packet, const PlayerId skip_pid = -1) ON(NET);
	MessageId OnConnect(PlayerId pid, Packet& hello) ON(NET);
	void OnDisconnect(PlayerId pid) ON(NET);
	void UpdatePings() ON(NET);
	bool IsSpectator(PlayerId pid);

	std::vector<std::pair<std::string, std::string>> GetInterfaceListInternal();

	NetSettings     m_settings;

	bool            m_is_running;
	Common::Timer	m_ping_timer;
	u32		m_ping_key;
	bool            m_update_pings;
	u32		m_current_game;
	u32				m_target_buffer_size;

	// Note about disconnects: Imagine a single player plus spectators.  The
	// client should not have to wait for the server for each frame.  However,
	// if the server decides to change the mapping, the client must not desync.
	// Therefore, in lieu of more complicated solutions, disconnects should be
	// requested rather than forced.

	std::pair<PlayerId, s8> m_device_map[IOSync::Class::NumClasses][IOSync::Class::MaxDeviceIndex];


	std::vector<Client>	m_players;
	unsigned m_num_players;

	// only protects m_selected_game
	std::recursive_mutex m_crit;

	std::string m_selected_game GUARDED_BY(m_crit);

	TraversalClient* m_traversal_client;
	NetHost*		m_net_host;
	ENetHost*		m_enet_host;
	NetPlayUI*		m_dialog;

#if defined(__APPLE__)
	const void* m_dynamic_store;
	const void* m_prefs;
#endif
};

#endif
