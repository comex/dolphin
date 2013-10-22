// This file is public domain, in case it's useful to anyone. -comex

#pragma once

#include "Common.h"
#include "Thread.h"
#include "FifoQueue.h"
#include "TraversalProto.h"
#include "enet/enet.h"
#include <functional>
#include <list>

DEFINE_THREAD_HAT(NET);

#define MAX_CLIENTS 200

#include "ChunkFile.h"
namespace ENetUtil
{
	ENetPacket* MakeENetPacket(Packet&& pac, enet_uint32 flags);
	void BroadcastPacket(ENetHost* host, Packet&& pac) ON(NET);
	void SendPacket(ENetPeer* peer, Packet&& pac) ON(NET);
	Packet MakePacket(ENetPacket* epacket);
	void Wakeup(ENetHost* host);
	int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event) /* ON(NET) */;
}

// Apparently nobody on the C++11 standards committee thought of
// combining two of its most prominent features: lambdas and moves.  Nor
// does bind work, despite a StackOverflow answer to the contrary.  Derp.
template <typename T>
class CopyAsMove
{
public:
	CopyAsMove(T&& t) : m_T(std::move(t)) {}
	CopyAsMove(const CopyAsMove& other) : m_T((T&&) other.m_T) {}
	T& operator*() { return m_T; }
private:
	T m_T;
};

class TraversalClientClient
{
public:
	virtual void OnENetEvent(ENetEvent*) ON(NET) = 0;
	virtual void OnTraversalStateChanged() ON(NET) = 0;
	virtual void OnConnectReady(ENetAddress addr) ON(NET) = 0;
	virtual void OnConnectFailed(u8 reason) ON(NET) = 0;
};

class ENetHostClient
{
public:
	ENetHostClient(size_t peerCount, u16 port, bool isTraversalClient = false);
	~ENetHostClient();
	void RunOnThread(std::function<void()> func);
	void CreateThread();
	void Reset();

	TraversalClientClient* m_Client;
	ENetHost* m_Host;
protected:
	virtual void HandleResends() ON(NET) {}
private:
	void ThreadFunc() /* ON(NET) */;

	Common::FifoQueue<std::function<void()>, false> m_RunQueue;
	std::mutex m_RunQueueWriteLock;
	std::thread m_Thread;
	Common::Event m_ResetEvent;
	bool m_ShouldEndThread ACCESS_ON(NET);
	bool m_isTraversalClient;
};

class TraversalClient : public ENetHostClient
{
public:
	enum State
	{
		InitFailure,
		Connecting,
		Connected,
		Failure
	};

	enum FailureReason
	{
		VersionTooOld = 0x300,
		ServerForgotAboutUs,
		SocketSendError,
		ResendTimeout,
		ConnectFailedError = 0x400,
	};

	TraversalClient(const std::string& server, u16 port);
	void Reset();
	void ConnectToClient(const std::string& host) ON(NET);
	void ReconnectToServer();
	u16 GetPort();

	TraversalHostId m_HostId;
	State m_State;
	int m_FailureReason;
protected:
	virtual void HandleResends() ON(NET);
private:
	struct OutgoingPacketInfo
	{
		TraversalPacket packet;
		int tries;
		enet_uint32 sendTime;
	};

	void HandleServerPacket(TraversalPacket* packet) ON(NET);
	void ResendPacket(OutgoingPacketInfo* info) ON(NET);
	TraversalRequestId SendPacket(const TraversalPacket& packet) ON(NET);
	void OnFailure(int reason) ON(NET);
	static int ENET_CALLBACK InterceptCallback(ENetHost* host, ENetEvent* event) /* ON(NET) */;
	void HandlePing() ON(NET);

	TraversalRequestId m_ConnectRequestId;
	bool m_PendingConnect;
	std::list<OutgoingPacketInfo> m_OutgoingPackets ACCESS_ON(NET);
	ENetAddress m_ServerAddress;
	enet_uint32 m_PingTime;
	std::string m_Server;
};

extern std::unique_ptr<TraversalClient> g_TraversalClient;
void EnsureTraversalClient(const std::string& server, u16 port);
void ReleaseTraversalClient();
