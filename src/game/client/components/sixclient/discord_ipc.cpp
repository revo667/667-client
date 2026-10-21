/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-3f0344a1 */

#include "discord_ipc.h"

#include <base/dbg.h>
#include <base/mem.h>
#include <base/process.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/external/json-parser/json.h>
#include <engine/shared/json.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static std::mutex gs_DiscordUserMutex;
static std::string gs_DiscordUserId;
static std::string gs_DiscordUserName;

bool SixClientDiscordUser(std::string &Id, std::string &Name)
{
	const std::lock_guard<std::mutex> Lock(gs_DiscordUserMutex);
	Id = gs_DiscordUserId;
	Name = gs_DiscordUserName;
	return !Id.empty();
}

CDiscordIpc::~CDiscordIpc()
{
	Close();
}

std::string CDiscordIpc::Escape(const char *pText)
{
	std::string Out;
	for(const unsigned char *p = (const unsigned char *)pText; *p; p++)
	{
		switch(*p)
		{
		case '"': Out += "\\\""; break;
		case '\\': Out += "\\\\"; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		default:
			if(*p < 0x20)
			{
				char aBuf[8];
				str_format(aBuf, sizeof(aBuf), "\\u%04x", *p);
				Out += aBuf;
			}
			else
				Out += (char)*p;
		}
	}
	return Out;
}

void CDiscordIpc::Configure(const char *pClientId)
{
	if(m_ClientId == pClientId)
		return;
	Close();
	m_ClientId = pClientId;
	m_NextConnectAttempt = 0;
}

std::string CDiscordIpc::NextNonce()
{
	char aNonce[32];
	str_format(aNonce, sizeof(aNonce), "%d", ++m_Nonce);
	return aNonce;
}

void CDiscordIpc::Close()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(m_Pipe)
	{
		CloseHandle((HANDLE)m_Pipe);
		m_Pipe = nullptr;
	}
#else
	if(m_Socket >= 0)
	{
		close(m_Socket);
		m_Socket = -1;
	}
#endif
	m_Connected = false;
	m_Ready = false;
	m_vReadBuffer.clear();
}

bool CDiscordIpc::Connect()
{
	Close();
#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 0; i < 10; i++)
	{
		wchar_t aPath[64];
		_snwprintf(aPath, 64, L"\\\\?\\pipe\\discord-ipc-%d", i);
		HANDLE Pipe = CreateFileW(aPath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if(Pipe != INVALID_HANDLE_VALUE)
		{
			// 667client: writes run on the game thread. In blocking mode a Discord that stops reading
			// fills the pipe and WriteFile hangs the game; non-blocking it returns and we reconnect.
			DWORD Mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
			SetNamedPipeHandleState(Pipe, &Mode, nullptr, nullptr);
			m_Pipe = (void *)Pipe;
			m_Connected = true;
			break;
		}
	}
#else
	const char *apEnv[] = {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"};
	std::vector<std::string> vBases;
	for(const char *pEnv : apEnv)
		if(const char *pValue = getenv(pEnv); pValue && pValue[0])
			vBases.emplace_back(pValue);
	vBases.emplace_back("/tmp");
	const char *apSubdirs[] = {"", "app/com.discordapp.Discord/", "snap.discord/"};
	for(const std::string &Base : vBases)
	{
		for(const char *pSubdir : apSubdirs)
		{
			for(int i = 0; i < 10 && !m_Connected; i++)
			{
				char aPath[sizeof(sockaddr_un::sun_path)];
				str_format(aPath, sizeof(aPath), "%s%s%sdiscord-ipc-%d", Base.c_str(), str_endswith(Base.c_str(), "/") ? "" : "/", pSubdir, i);
				const int Socket = socket(AF_UNIX, SOCK_STREAM, 0);
				if(Socket < 0)
					continue;
#if defined(SO_NOSIGPIPE)
				const int One = 1;
				setsockopt(Socket, SOL_SOCKET, SO_NOSIGPIPE, &One, sizeof(One));
#endif
				sockaddr_un Addr = {};
				Addr.sun_family = AF_UNIX;
				str_copy(Addr.sun_path, aPath, sizeof(Addr.sun_path));
				if(connect(Socket, (sockaddr *)&Addr, sizeof(Addr)) == 0)
				{
					fcntl(Socket, F_SETFL, fcntl(Socket, F_GETFL, 0) | O_NONBLOCK);
					m_Socket = Socket;
					m_Connected = true;
				}
				else
					close(Socket);
			}
		}
	}
#endif
	if(!m_Connected)
		return false;
	std::string Handshake = "{\"v\":1,\"client_id\":\"" + Escape(m_ClientId.c_str()) + "\"}";
	if(!Write(OP_HANDSHAKE, Handshake))
	{
		Close();
		return false;
	}
	dbg_msg("discord", "connected to the discord client over ipc");
	return true;
}

bool CDiscordIpc::Write(int Opcode, const std::string &Payload)
{
	if(!m_Connected)
		return false;
	std::vector<unsigned char> vFrame(8 + Payload.size());
	const uint32_t Op = (uint32_t)Opcode;
	const uint32_t Length = (uint32_t)Payload.size();
	for(int i = 0; i < 4; i++)
	{
		vFrame[i] = (Op >> (8 * i)) & 0xff;
		vFrame[4 + i] = (Length >> (8 * i)) & 0xff;
	}
	mem_copy(vFrame.data() + 8, Payload.data(), Payload.size());
	size_t Sent = 0;
#if defined(CONF_FAMILY_WINDOWS)
	int FullRetries = 0;
#endif
	while(Sent < vFrame.size())
	{
#if defined(CONF_FAMILY_WINDOWS)
		DWORD Written = 0;
		if(!WriteFile((HANDLE)m_Pipe, vFrame.data() + Sent, (DWORD)(vFrame.size() - Sent), &Written, nullptr))
		{
			Close();
			return false;
		}
		if(Written == 0)
		{
			// The pipe is full. Give Discord a moment, then drop the connection rather than wait.
			if(++FullRetries > 5)
			{
				Close();
				return false;
			}
			Sleep(2);
			continue;
		}
#else
		int Flags = 0;
#if defined(MSG_NOSIGNAL)
		Flags |= MSG_NOSIGNAL;
#endif
		const ssize_t Written = send(m_Socket, vFrame.data() + Sent, vFrame.size() - Sent, Flags);
		if(Written <= 0)
		{
			if(Written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			{
				fd_set WriteSet;
				FD_ZERO(&WriteSet);
				FD_SET(m_Socket, &WriteSet);
				timeval Timeout = {0, 50000};
				if(select(m_Socket + 1, nullptr, &WriteSet, nullptr, &Timeout) > 0)
					continue;
			}
			Close();
			return false;
		}
#endif
		Sent += Written;
	}
	return true;
}

void CDiscordIpc::Read()
{
	while(m_Connected)
	{
		unsigned char aBuf[4096];
#if defined(CONF_FAMILY_WINDOWS)
		DWORD Available = 0;
		if(!PeekNamedPipe((HANDLE)m_Pipe, nullptr, 0, nullptr, &Available, nullptr))
		{
			Close();
			return;
		}
		if(Available == 0)
			break;
		DWORD Received = 0;
		if(!ReadFile((HANDLE)m_Pipe, aBuf, Available < sizeof(aBuf) ? Available : (DWORD)sizeof(aBuf), &Received, nullptr) || Received == 0)
		{
			Close();
			return;
		}
#else
		const ssize_t Received = recv(m_Socket, aBuf, sizeof(aBuf), 0);
		if(Received == 0)
		{
			Close();
			return;
		}
		if(Received < 0)
		{
			if(errno != EAGAIN && errno != EWOULDBLOCK)
				Close();
			break;
		}
#endif
		m_vReadBuffer.insert(m_vReadBuffer.end(), aBuf, aBuf + Received);
	}

	while(m_vReadBuffer.size() >= 8)
	{
		uint32_t Op = 0, Length = 0;
		for(int i = 0; i < 4; i++)
		{
			Op |= (uint32_t)m_vReadBuffer[i] << (8 * i);
			Length |= (uint32_t)m_vReadBuffer[4 + i] << (8 * i);
		}
		if(Length > 1024 * 1024)
		{
			Close();
			return;
		}
		if(m_vReadBuffer.size() < 8 + Length)
			break;
		const std::string Payload((const char *)m_vReadBuffer.data() + 8, Length);
		m_vReadBuffer.erase(m_vReadBuffer.begin(), m_vReadBuffer.begin() + 8 + Length);
		HandleFrame((int)Op, Payload);
		if(!m_Connected)
			return;
	}
}

void CDiscordIpc::HandleFrame(int Opcode, const std::string &Payload)
{
	if(Opcode == OP_PING)
	{
		Write(OP_PONG, Payload);
		return;
	}
	if(Opcode == OP_CLOSE)
	{
		dbg_msg("discord", "discord closed the ipc connection: %s", Payload.c_str());
		Close();
		m_NextConnectAttempt = time_get() + time_freq() * 30;
		return;
	}
	if(Opcode != OP_FRAME)
		return;

	json_value *pJson = json_parse(Payload.c_str(), Payload.size());
	if(!pJson)
		return;
	const char *pEvent = json_string_get(json_object_get(pJson, "evt"));
	if(pEvent && str_comp(pEvent, "READY") == 0)
	{
		m_Ready = true;
		const json_value *pUser = json_object_get(json_object_get(pJson, "data"), "user");
		const char *pId = json_string_get(json_object_get(pUser, "id"));
		const char *pName = json_string_get(json_object_get(pUser, "username"));
		if(pId && pId[0])
		{
			const std::lock_guard<std::mutex> Lock(gs_DiscordUserMutex);
			gs_DiscordUserId = pId;
			gs_DiscordUserName = pName ? pName : "";
		}
		Write(OP_FRAME, "{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN\",\"nonce\":\"" + NextNonce() + "\"}");
	}
	else if(pEvent && str_comp(pEvent, "ACTIVITY_JOIN") == 0)
	{
		const char *pSecret = json_string_get(json_object_get(json_object_get(pJson, "data"), "secret"));
		if(pSecret && pSecret[0])
			m_vJoinSecrets.emplace_back(pSecret);
	}
	else if(!pEvent && str_comp(json_string_get(json_object_get(pJson, "cmd")) ? json_string_get(json_object_get(pJson, "cmd")) : "", "SET_ACTIVITY") == 0)
	{
		const json_value *pAssets = json_object_get(json_object_get(pJson, "data"), "assets");
		const char *pLarge = json_string_get(json_object_get(pAssets, "large_image"));
		const char *pSmall = json_string_get(json_object_get(pAssets, "small_image"));
		dbg_msg("discord", "activity accepted large='%s' small='%s'", pLarge ? pLarge : "", pSmall ? pSmall : "");
	}
	else if(pEvent && str_comp(pEvent, "ERROR") == 0)
	{
		const char *pMessage = json_string_get(json_object_get(json_object_get(pJson, "data"), "message"));
		dbg_msg("discord", "ipc error: %s", pMessage ? pMessage : Payload.c_str());
	}
	json_value_free(pJson);
}

void CDiscordIpc::Update()
{
	if(m_ClientId.empty())
		return;
	if(!m_Connected)
	{
		const int64_t Now = time_get();
		if(Now < m_NextConnectAttempt)
			return;
		m_NextConnectAttempt = Now + time_freq() * 5;
		if(!Connect())
			return;
	}
	Read();
}

bool CDiscordIpc::SetActivity(const std::string &ActivityJson)
{
	if(!m_Ready)
		return false;
	char aPid[16];
	str_format(aPid, sizeof(aPid), "%d", (int)process_id());
	return Write(OP_FRAME, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::string(aPid) + ",\"activity\":" + ActivityJson + "},\"nonce\":\"" + NextNonce() + "\"}");
}

bool CDiscordIpc::ClearActivity()
{
	if(!m_Ready)
		return false;
	char aPid[16];
	str_format(aPid, sizeof(aPid), "%d", (int)process_id());
	// Explicit null activity is required; omitting the field is a no-op on some Discord builds.
	return Write(OP_FRAME, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::string(aPid) + ",\"activity\":null},\"nonce\":\"" + NextNonce() + "\"}");
}

bool CDiscordIpc::TakeJoinSecret(std::string &Secret)
{
	if(m_vJoinSecrets.empty())
		return false;
	Secret = m_vJoinSecrets.front();
	m_vJoinSecrets.erase(m_vJoinSecrets.begin());
	return true;
}
