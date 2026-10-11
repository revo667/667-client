#include "discord_rpc.h"

#include "version.h"

#include <base/dbg.h>
#include <base/hash.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/discord.h>
#include <engine/shared/config.h>

static void AppendDiscordVar(char *pBuf, int BufSize, const char *pName, int NameLength, const SDiscordPresenceVars &Vars)
{
	char aNumber[16];
	const char *pValue = nullptr;
	if(str_comp_num(pName, "map", NameLength) == 0 && NameLength == 3)
		pValue = Vars.m_pMap;
	else if(str_comp_num(pName, "player", NameLength) == 0 && NameLength == 6)
		pValue = Vars.m_pPlayer;
	else if(str_comp_num(pName, "clan", NameLength) == 0 && NameLength == 4)
		pValue = Vars.m_pClan;
	else if(str_comp_num(pName, "skin", NameLength) == 0 && NameLength == 4)
		pValue = Vars.m_pSkin;
	else if(str_comp_num(pName, "server", NameLength) == 0 && NameLength == 6)
		pValue = Vars.m_pServer;
	else if(str_comp_num(pName, "gametype", NameLength) == 0 && NameLength == 8)
		pValue = Vars.m_pGameType;
	else if(str_comp_num(pName, "version", NameLength) == 0 && NameLength == 7)
		pValue = SIXCLIENT_VERSION;
	else if(str_comp_num(pName, "players", NameLength) == 0 && NameLength == 7)
	{
		str_format(aNumber, sizeof(aNumber), "%d", Vars.m_Players);
		pValue = aNumber;
	}
	else if(str_comp_num(pName, "maxplayers", NameLength) == 0 && NameLength == 10)
	{
		str_format(aNumber, sizeof(aNumber), "%d", Vars.m_MaxPlayers);
		pValue = aNumber;
	}

	if(pValue != nullptr)
	{
		str_append(pBuf, pValue, BufSize);
		return;
	}

	char aRaw[64];
	str_format(aRaw, sizeof(aRaw), "{%.*s}", NameLength, pName);
	str_append(pBuf, aRaw, BufSize);
}

void SixClientFormatDiscordText(char *pBuf, int BufSize, const char *pTemplate, const SDiscordPresenceVars &Vars)
{
	pBuf[0] = '\0';
	const char *pCur = pTemplate;
	while(*pCur)
	{
		const char *pOpen = str_find(pCur, "{");
		if(!pOpen)
		{
			str_append(pBuf, pCur, BufSize);
			break;
		}
		const char *pClose = str_find(pOpen + 1, "}");
		if(!pClose)
		{
			str_append(pBuf, pCur, BufSize);
			break;
		}

		char aChunk[256];
		str_truncate(aChunk, sizeof(aChunk), pCur, pOpen - pCur);
		str_append(pBuf, aChunk, BufSize);
		AppendDiscordVar(pBuf, BufSize, pOpen + 1, pClose - pOpen - 1, Vars);
		pCur = pClose + 1;
	}

	char aTrimmed[256];
	str_copy(aTrimmed, str_utf8_skip_whitespaces(pBuf));
	str_utf8_trim_right(aTrimmed);
	str_copy(pBuf, aTrimmed, BufSize);
	if(str_length(pBuf) < 2)
		pBuf[0] = '\0';
}

#if defined(CONF_DISCORD)
#include <discord_game_sdk.h>

typedef enum EDiscordResult(DISCORD_API *FDiscordCreate)(DiscordVersion, struct DiscordCreateParams *, struct IDiscordCore **);

#if defined(CONF_DISCORD_DYNAMIC)
#include <dlfcn.h>
static FDiscordCreate GetDiscordCreate()
{
	void *pSdk = dlopen("discord_game_sdk.so", RTLD_NOW);
	if(!pSdk)
	{
		return nullptr;
	}
	return (FDiscordCreate)dlsym(pSdk, "DiscordCreate");
}
#else
static FDiscordCreate GetDiscordCreate()
{
	return DiscordCreate;
}
#endif

static constexpr DiscordClientId SIXCLIENT_DISCORD_APP_ID = 1513036786325258445;

class CDiscord : public IDiscord
{
	DiscordActivity m_Activity;
	bool m_UpdateActivity = false;
	int64_t m_LastActivityUpdate = 0;
	int64_t m_LastConfigCheck = 0;
	unsigned m_ConfigHash = 0;

	IDiscordCore *m_pCore = nullptr;
	IDiscordActivityEvents m_ActivityEvents;
	IDiscordActivityManager *m_pActivityManager = nullptr;

	FDiscordCreate m_pfnDiscordCreate = nullptr;
	bool m_Enabled = false;

	bool m_InGame = false;
	bool m_Registered = false;
	char m_aMap[128] = "";
	char m_aPlayer[64] = "";
	char m_aSkin[64] = "";
	char m_aServer[256] = "";
	char m_aGameType[16] = "";
	char m_aJoinSecret[128] = "";
	char m_aPartyId[128] = "";
	int m_NumPlayers = 0;
	int m_MaxPlayers = 0;
	int64_t m_GameStartTime = 0;
	int64_t m_MenuStartTime = 0;

	static unsigned HashConfig()
	{
		unsigned Hash = 0;
		const char *apStrings[] = {
			g_Config.m_ScDiscordDetails,
			g_Config.m_ScDiscordState,
			g_Config.m_ScDiscordMenuDetails,
			g_Config.m_ScDiscordMenuState,
			g_Config.m_ScDiscordLargeImage,
			g_Config.m_ScDiscordLargeText,
			g_Config.m_ScDiscordSmallImage,
			g_Config.m_ScDiscordSmallText,
			g_Config.m_PlayerClan,
		};
		for(const char *pString : apStrings)
			Hash = Hash * 31u ^ str_quickhash(pString);
		Hash = Hash * 31u ^ (unsigned)(g_Config.m_ScDiscordShowTime | (g_Config.m_ScDiscordShowParty << 1) | (g_Config.m_ScDiscordAllowJoin << 2));
		return Hash;
	}

	void RebuildActivity()
	{
		mem_zero(&m_Activity, sizeof(DiscordActivity));

		SDiscordPresenceVars Vars;
		Vars.m_pMap = m_aMap;
		Vars.m_pPlayer = m_aPlayer;
		Vars.m_pClan = g_Config.m_PlayerClan;
		Vars.m_pSkin = m_aSkin;
		Vars.m_pServer = m_aServer;
		Vars.m_pGameType = m_aGameType;
		Vars.m_Players = m_NumPlayers;
		Vars.m_MaxPlayers = m_MaxPlayers;

		SixClientFormatDiscordText(m_Activity.details, sizeof(m_Activity.details), m_InGame ? g_Config.m_ScDiscordDetails : g_Config.m_ScDiscordMenuDetails, Vars);
		SixClientFormatDiscordText(m_Activity.state, sizeof(m_Activity.state), m_InGame ? g_Config.m_ScDiscordState : g_Config.m_ScDiscordMenuState, Vars);

		str_copy(m_Activity.assets.large_image, str_utf8_skip_whitespaces(g_Config.m_ScDiscordLargeImage));
		SixClientFormatDiscordText(m_Activity.assets.large_text, sizeof(m_Activity.assets.large_text), g_Config.m_ScDiscordLargeText, Vars);
		str_copy(m_Activity.assets.small_image, str_utf8_skip_whitespaces(g_Config.m_ScDiscordSmallImage));
		if(m_Activity.assets.small_image[0] != '\0')
			SixClientFormatDiscordText(m_Activity.assets.small_text, sizeof(m_Activity.assets.small_text), g_Config.m_ScDiscordSmallText, Vars);

		if(g_Config.m_ScDiscordShowTime)
			m_Activity.timestamps.start = m_InGame ? m_GameStartTime : m_MenuStartTime;

		m_Activity.instance = m_InGame;
		if(m_InGame && (g_Config.m_ScDiscordShowParty || g_Config.m_ScDiscordAllowJoin))
		{
			str_copy(m_Activity.party.id, m_aPartyId);
			m_Activity.party.privacy = m_Registered ? DiscordActivityPartyPrivacy_Public : DiscordActivityPartyPrivacy_Private;
			if(g_Config.m_ScDiscordShowParty && m_MaxPlayers > 0)
			{
				m_Activity.party.size.current_size = m_NumPlayers;
				m_Activity.party.size.max_size = m_MaxPlayers;
			}
			if(g_Config.m_ScDiscordAllowJoin)
				str_copy(m_Activity.secrets.join, m_aJoinSecret);
		}

		m_UpdateActivity = true;
	}

	void UpdateServer(const CServerInfo &ServerInfo, const char *pMapName, const char *pPlayerName, const char *pSkinName)
	{
		const char *pDisplayMap = (pMapName && pMapName[0] != '\0') ? pMapName : ServerInfo.m_aMap;
		str_copy(m_aMap, pDisplayMap);
		str_copy(m_aServer, ServerInfo.m_aName);
		str_copy(m_aGameType, ServerInfo.m_aGameType);
		if(pPlayerName && pPlayerName[0] != '\0')
			str_copy(m_aPlayer, pPlayerName);
		if(pSkinName && pSkinName[0] != '\0')
			str_copy(m_aSkin, pSkinName);
		m_MaxPlayers = ServerInfo.m_MaxClients;

		if(str_length(ServerInfo.m_aAddress) < (int)sizeof(m_aJoinSecret))
		{
			str_copy(m_aJoinSecret, ServerInfo.m_aAddress);
		}
		else
		{
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&ServerInfo.m_aAddresses[0], aAddr, sizeof(aAddr), true);
			str_copy(m_aJoinSecret, aAddr);
		}

		if(m_Registered)
		{
			char aPartyId[SHA256_MAXSTRSIZE];
			SHA256_DIGEST PartyIdSha256 = sha256(m_aJoinSecret, str_length(m_aJoinSecret));
			sha256_str(PartyIdSha256, aPartyId, sizeof(aPartyId));
			str_copy(m_aPartyId, aPartyId);
		}
	}

public:
	bool Init(FDiscordCreate pfnDiscordCreate)
	{
		m_pfnDiscordCreate = pfnDiscordCreate;
		m_Enabled = false;
		m_MenuStartTime = time_timestamp();
		return InitDiscord();
	}

	bool InitDiscord()
	{
		if(m_pCore)
		{
			m_pCore->destroy(m_pCore);
			m_pCore = nullptr;
			m_pActivityManager = nullptr;
		}
		if(!m_Enabled)
			return false;

		mem_zero(&m_ActivityEvents, sizeof(m_ActivityEvents));

		m_ActivityEvents.on_activity_join = &CDiscord::OnActivityJoin;
		m_pActivityManager = nullptr;

		DiscordCreateParams Params;
		DiscordCreateParamsSetDefault(&Params);

		Params.client_id = SIXCLIENT_DISCORD_APP_ID;
		Params.flags = EDiscordCreateFlags::DiscordCreateFlags_NoRequireDiscord;
		Params.event_data = this;
		Params.activity_events = &m_ActivityEvents;

		int Error = m_pfnDiscordCreate(DISCORD_VERSION, &Params, &m_pCore);

		if(Error != DiscordResult_Ok)
		{
			m_pCore = nullptr;
			dbg_msg("discord", "error initializing discord instance, error=%d", Error);
			return true;
		}

		if(!m_pCore)
		{
			dbg_msg("discord", "discord initialization returned no core");
			return true;
		}

		m_pActivityManager = m_pCore->get_activity_manager(m_pCore);
		if(!m_pActivityManager)
		{
			dbg_msg("discord", "discord initialization returned no activity manager");
			m_pCore->destroy(m_pCore);
			m_pCore = nullptr;
			return true;
		}

		m_pActivityManager->register_command(m_pActivityManager, CONNECTLINK_DOUBLE_SLASH);
		m_pActivityManager->register_steam(m_pActivityManager, 412220);

		m_ConfigHash = HashConfig();
		RebuildActivity();
		dbg_msg("discord", "initialized rich presence");

		return false;
	}

	void Update(bool Enabled) override
	{
		bool NeedsUpdate = m_Enabled != Enabled;
		m_Enabled = Enabled;

		if(NeedsUpdate)
		{
			dbg_msg("discord", "rpc %s", m_Enabled ? "enabled" : "disabled");
			InitDiscord();
		}

		if(m_pCore && m_Enabled)
		{
			const int64_t Now = time_get();
			if(Now > m_LastConfigCheck + time_freq())
			{
				m_LastConfigCheck = Now;
				const unsigned ConfigHash = HashConfig();
				if(ConfigHash != m_ConfigHash)
				{
					m_ConfigHash = ConfigHash;
					RebuildActivity();
				}
			}

			if(m_UpdateActivity && Now > m_LastActivityUpdate + time_freq() * 5)
			{
				m_UpdateActivity = false;
				m_LastActivityUpdate = Now;

				m_pActivityManager->update_activity(m_pActivityManager, &m_Activity, nullptr, nullptr);
			}

			m_pCore->run_callbacks(m_pCore);
		}
	}

	void ClearGameInfo() override
	{
		m_InGame = false;
		m_NumPlayers = 0;
		m_MaxPlayers = 0;
		m_aMap[0] = '\0';
		m_aServer[0] = '\0';
		m_aGameType[0] = '\0';
		m_aJoinSecret[0] = '\0';
		m_aPartyId[0] = '\0';
		RebuildActivity();
	}

	void SetGameInfo(const CServerInfo &ServerInfo, const char *pMapName, const char *pPlayerName, const char *pSkinName, bool ShowMap, bool Registered) override
	{
		m_InGame = true;
		m_Registered = Registered;
		m_GameStartTime = time_timestamp();
		m_NumPlayers = ServerInfo.m_NumClients;
		if(!Registered)
			secure_random_password(m_aPartyId, sizeof(m_aPartyId), 64);
		UpdateServer(ServerInfo, pMapName, pPlayerName, pSkinName);
		RebuildActivity();
	}

	void UpdateServerInfo(const CServerInfo &ServerInfo, const char *pMapName, const char *pPlayerName, const char *pSkinName) override
	{
		if(!m_InGame)
			return;

		UpdateServer(ServerInfo, pMapName, pPlayerName, pSkinName);
		RebuildActivity();
	}

	void UpdatePlayerCount(int Count) override
	{
		if(!m_InGame || m_NumPlayers == Count)
			return;

		m_NumPlayers = Count;
		RebuildActivity();
	}

	static void DISCORD_CALLBACK OnActivityJoin(void *pEventData, const char *pSecret)
	{
		if(!pEventData || !pSecret || pSecret[0] == '\0')
			return;

		CDiscord *pSelf = static_cast<CDiscord *>(pEventData);
		IClient *pClient = pSelf->Kernel()->RequestInterface<IClient>();
		if(pClient)
			pClient->Connect(pSecret);
	}

	~CDiscord() override
	{
		if(m_pCore)
			m_pCore->destroy(m_pCore);
	}
};

static IDiscord *CreateSixClientDiscordImpl()
{
	FDiscordCreate pfnDiscordCreate = GetDiscordCreate();
	if(!pfnDiscordCreate)
	{
		return nullptr;
	}
	CDiscord *pDiscord = new CDiscord();
	if(pDiscord->Init(pfnDiscordCreate))
	{
		delete pDiscord;
		return nullptr;
	}
	return pDiscord;
}
#else
static IDiscord *CreateSixClientDiscordImpl()
{
	return nullptr;
}
#endif

IDiscord *CreateSixClientDiscord()
{
	return CreateSixClientDiscordImpl();
}
