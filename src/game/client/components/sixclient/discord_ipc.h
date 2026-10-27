/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-3f20ef6c */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_DISCORD_IPC_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_DISCORD_IPC_H

#include <base/detect.h>

#include <cstdint>
#include <string>
#include <vector>

bool SixClientDiscordUser(std::string &Id, std::string &Name);

class CDiscordIpc
{
public:
	~CDiscordIpc();

	void Configure(const char *pClientId);
	void Update();
	void Close();
	bool Ready() const { return m_Ready; }
	bool SetActivity(const std::string &ActivityJson);
	bool ClearActivity();
	bool TakeJoinSecret(std::string &Secret);

	static std::string Escape(const char *pText);

private:
	enum
	{
		OP_HANDSHAKE = 0,
		OP_FRAME = 1,
		OP_CLOSE = 2,
		OP_PING = 3,
		OP_PONG = 4,
	};

	std::string m_ClientId;
	bool m_Connected = false;
	bool m_Ready = false;
	int64_t m_NextConnectAttempt = 0;
	int m_Nonce = 0;
	std::vector<unsigned char> m_vReadBuffer;
	std::vector<std::string> m_vJoinSecrets;

#if defined(CONF_FAMILY_WINDOWS)
	void *m_Pipe = nullptr;
#else
	int m_Socket = -1;
#endif

	bool Connect();
	bool Write(int Opcode, const std::string &Payload);
	void Read();
	void HandleFrame(int Opcode, const std::string &Payload);
	std::string NextNonce();
};

#endif
