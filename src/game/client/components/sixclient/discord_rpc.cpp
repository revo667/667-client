#include "discord_rpc.h"
#include "discord_ipc.h"

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
#if defined(_WIN32)
#define IStorage WindowsIStorage
#include <discord_game_sdk.h>
#undef IStorage
#else
#include <discord_game_sdk.h>
#endif

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

static constexpr DiscordClientId SIXCLIENT_DISCORD_APP_ID = 1561145168697106564;
static constexpr const char *SIXCLIENT_DISCORD_APP_ID_STR = "1561145168697106564";

static bool ResolveDiscordClientId(DiscordClientId &OutId, char *pOutIdStr, int OutIdStrSize)
{
	char aTrimmed[32];
	str_copy(aTrimmed, str_utf8_skip_whitespaces(g_Config.m_ScDiscordClientId), sizeof(aTrimmed));
	str_utf8_trim_right(aTrimmed);

	if(aTrimmed[0] == '\0')
	{
		OutId = SIXCLIENT_DISCORD_APP_ID;
		str_copy(pOutIdStr, SIXCLIENT_DISCORD_APP_ID_STR, OutIdStrSize);
		return true;
	}

	bool DigitsOnly = true;
	for(const char *p = aTrimmed; *p; p++)
	{
		if(*p < '0' || *p > '9')
		{
			DigitsOnly = false;
			break;
		}
	}
	if(!DigitsOnly || aTrimmed[0] == '0')
	{
		OutId = SIXCLIENT_DISCORD_APP_ID;
		str_copy(pOutIdStr, SIXCLIENT_DISCORD_APP_ID_STR, OutIdStrSize);
		return false;
	}

	const DiscordClientId Parsed = (DiscordClientId)str_toint64_base(aTrimmed, 10);
	if(Parsed <= 0)
	{
		OutId = SIXCLIENT_DISCORD_APP_ID;
		str_copy(pOutIdStr, SIXCLIENT_DISCORD_APP_ID_STR, OutIdStrSize);
		return false;
	}

	OutId = Parsed;
	str_copy(pOutIdStr, aTrimmed, OutIdStrSize);
	return true;
}

static void TrimDiscordImage(char *pDst, int DstSize, const char *pSrc)
{
	str_copy(pDst, str_utf8_skip_whitespaces(pSrc), DstSize);
	str_utf8_trim_right(pDst);
}

static bool IsHttpDiscordImage(const char *pValue)
{
	return str_startswith_nocase(pValue, "http://") || str_startswith_nocase(pValue, "https://");
}

static void NormalizeDiscordImageUrl(char *pDst, int DstSize, const char *pSrc)
{
	TrimDiscordImage(pDst, DstSize, pSrc);
	if(!IsHttpDiscordImage(pDst))
		return;

	if(char *pHash = (char *)str_find(pDst, "#"))
		*pHash = '\0';

	const char *pHost = str_startswith_nocase(pDst, "https://") ? pDst + 8 : (str_startswith_nocase(pDst, "http://") ? pDst + 7 : nullptr);
	if(!pHost)
		return;

	const char *pPath = str_find(pHost, "/");
	if(!pPath || pPath[1] == '\0')
		return;

	const bool Imgur =
		str_startswith_nocase(pHost, "imgur.com/") ||
		str_startswith_nocase(pHost, "www.imgur.com/") ||
		str_startswith_nocase(pHost, "m.imgur.com/") ||
		str_startswith_nocase(pHost, "i.imgur.com/");
	if(!Imgur)
		return;

	if(str_startswith_nocase(pPath, "/a/") || str_startswith_nocase(pPath, "/gallery/"))
		return;

	char aId[64];
	str_copy(aId, pPath + 1, sizeof(aId));
	if(char *pQuery = (char *)str_find(aId, "?"))
		*pQuery = '\0';
	if(aId[0] == '\0')
		return;

	char *pExt = (char *)str_find(aId, ".");
	const bool HasImageExt = pExt && (str_endswith_nocase(aId, ".png") || str_endswith_nocase(aId, ".jpg") ||
		str_endswith_nocase(aId, ".jpeg") || str_endswith_nocase(aId, ".gif") || str_endswith_nocase(aId, ".webp"));
	if(!HasImageExt)
	{
		if(pExt)
			*pExt = '\0';
		if(aId[0] == '\0')
			return;
		str_format(pDst, DstSize, "https://i.imgur.com/%s.png", aId);
		return;
	}

	str_format(pDst, DstSize, "https://i.imgur.com/%s", aId);
}

void SixClientResolveDiscordImage(char *pDst, int DstSize, const char *pConfigured)
{
	char aTrimmed[512];
	TrimDiscordImage(aTrimmed, sizeof(aTrimmed), pConfigured);
	if(aTrimmed[0] == '\0' || str_comp(aTrimmed, SIXCLIENT_DISCORD_LEGACY_LARGE_IMAGE_KEY) == 0 || str_comp(aTrimmed, SIXCLIENT_DISCORD_OLD_DEFAULT_LARGE_IMAGE) == 0)
		str_copy(pDst, SIXCLIENT_DISCORD_DEFAULT_LARGE_IMAGE, DstSize);
	else
		NormalizeDiscordImageUrl(pDst, DstSize, aTrimmed);
}

static void PrepareDiscordImageUrl(char *pDst, int DstSize, const char *pSrc, unsigned CacheBust)
{
	NormalizeDiscordImageUrl(pDst, DstSize, pSrc);
	if(pDst[0] == '\0' || CacheBust == 0 || !IsHttpDiscordImage(pDst))
		return;

	if(char *pSc = (char *)str_find(pDst, "?sc="))
		*pSc = '\0';
	else if(char *pScAnd = (char *)str_find(pDst, "&sc="))
		*pScAnd = '\0';

	char aBusted[512];
	const char Sep = str_find(pDst, "?") ? '&' : '?';
	str_format(aBusted, sizeof(aBusted), "%s%csc=%u", pDst, Sep, CacheBust);
	str_copy(pDst, aBusted, DstSize);
}

static void CopyDiscordImage(char *pDst, int DstSize, const char *pSrc)
{
	pDst[0] = '\0';
	if(!pSrc || pSrc[0] == '\0')
		return;

	if(str_length(pSrc) >= DstSize)
	{
		dbg_msg("discord", "image value longer than %d chars; skipping Game SDK field (IPC keeps full URL)", DstSize - 1);
		return;
	}

	str_copy(pDst, pSrc, DstSize);
}

class CDiscord : public IDiscord
{
	DiscordActivity m_Activity;
	bool m_UpdateActivity = false;
	int64_t m_LastActivityUpdate = 0;
	int64_t m_LastConfigCheck = 0;
	int64_t m_NextUpdate = 0;
	int64_t m_PresenceSetAt = 0;
	unsigned m_ConfigHash = 0;
	unsigned m_ImageCacheBust = 1;
	char m_aLastLargeImageConfig[512] = "";
	char m_aLastSmallImageConfig[512] = "";

	enum class EPresencePush
	{
		None,
		NeedClear,
		NeedSet,
	};
	EPresencePush m_PresencePush = EPresencePush::None;

	IDiscordCore *m_pCore = nullptr;
	CDiscordIpc m_Ipc;
	char m_aLargeImage[512] = "";
	char m_aSmallImage[512] = "";
	bool m_IpcWasReady = false;
	bool m_SdkActivitySet = false;
	IDiscordActivityEvents m_ActivityEvents;
	IDiscordActivityManager *m_pActivityManager = nullptr;

	FDiscordCreate m_pfnDiscordCreate = nullptr;
	bool m_Enabled = false;
	char m_aActiveClientId[32] = "";

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
			g_Config.m_ScDiscordClientId,
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

		const bool LargeImageChanged = str_comp(m_aLastLargeImageConfig, g_Config.m_ScDiscordLargeImage) != 0;
		const bool SmallImageChanged = str_comp(m_aLastSmallImageConfig, g_Config.m_ScDiscordSmallImage) != 0;
		if(LargeImageChanged || SmallImageChanged)
		{
			const unsigned Bust = (unsigned)time_timestamp();
			m_ImageCacheBust = Bust != 0 ? Bust : (m_ImageCacheBust + 1);
			str_copy(m_aLastLargeImageConfig, g_Config.m_ScDiscordLargeImage);
			str_copy(m_aLastSmallImageConfig, g_Config.m_ScDiscordSmallImage);
			m_PresencePush = EPresencePush::NeedClear;
		}

		char aResolvedLarge[512];
		char aResolvedSmall[512];
		SixClientResolveDiscordImage(aResolvedLarge, sizeof(aResolvedLarge), g_Config.m_ScDiscordLargeImage);
		NormalizeDiscordImageUrl(aResolvedSmall, sizeof(aResolvedSmall), g_Config.m_ScDiscordSmallImage);
		PrepareDiscordImageUrl(m_aLargeImage, sizeof(m_aLargeImage), aResolvedLarge, m_ImageCacheBust);
		PrepareDiscordImageUrl(m_aSmallImage, sizeof(m_aSmallImage), aResolvedSmall, m_ImageCacheBust);
		CopyDiscordImage(m_Activity.assets.large_image, sizeof(m_Activity.assets.large_image), m_aLargeImage);
		SixClientFormatDiscordText(m_Activity.assets.large_text, sizeof(m_Activity.assets.large_text), g_Config.m_ScDiscordLargeText, Vars);
		CopyDiscordImage(m_Activity.assets.small_image, sizeof(m_Activity.assets.small_image), m_aSmallImage);
		if(m_aSmallImage[0] != '\0')
		{
			SixClientFormatDiscordText(m_Activity.assets.small_text, sizeof(m_Activity.assets.small_text), g_Config.m_ScDiscordSmallText, Vars);
			if(m_Activity.assets.small_text[0] == '\0')
				str_copy(m_Activity.assets.small_text, ".");
		}
		else
			m_Activity.assets.small_text[0] = '\0';

		if(g_Config.m_ScDiscordShowTime)
			m_Activity.timestamps.start = m_InGame ? m_GameStartTime : m_MenuStartTime;

		m_Activity.type = DiscordActivityType_Playing;
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

		DiscordClientId ClientId = SIXCLIENT_DISCORD_APP_ID;
		ResolveDiscordClientId(ClientId, m_aActiveClientId, sizeof(m_aActiveClientId));
		Params.client_id = ClientId;
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

	std::string BuildActivityJson() const
	{
		std::string Json = "{";
		bool First = true;
		auto Field = [&](const char *pName, const std::string &Value) {
			Json += First ? "" : ",";
			First = false;
			Json += "\"";
			Json += pName;
			Json += "\":";
			Json += Value;
		};
		auto Quote = [](const char *pText) { return "\"" + CDiscordIpc::Escape(pText) + "\""; };

		if(m_Activity.details[0])
			Field("details", Quote(m_Activity.details));
		if(m_Activity.state[0])
			Field("state", Quote(m_Activity.state));
		if(m_Activity.timestamps.start > 0)
		{
			char aStart[32];
			str_format(aStart, sizeof(aStart), "{\"start\":%lld}", (long long)m_Activity.timestamps.start * 1000);
			Field("timestamps", aStart);
		}
		std::string Assets;
		auto Asset = [&](const char *pName, const char *pValue) {
			if(!pValue || !pValue[0])
				return;
			Assets += Assets.empty() ? "" : ",";
			Assets += "\"";
			Assets += pName;
			Assets += "\":" + Quote(pValue);
		};
		if(m_aLargeImage[0])
		{
			Asset("large_image", m_aLargeImage);
			Asset("large_text", m_Activity.assets.large_text);
		}
		if(m_aSmallImage[0])
		{
			Asset("small_image", m_aSmallImage);
			Asset("small_text", m_Activity.assets.small_text);
		}
		if(!Assets.empty())
			Field("assets", "{" + Assets + "}");
		if(m_Activity.party.id[0])
		{
			std::string Party = "{\"id\":" + Quote(m_Activity.party.id);
			if(m_Activity.party.size.max_size > 0)
			{
				char aSize[64];
				str_format(aSize, sizeof(aSize), ",\"size\":[%d,%d]", m_Activity.party.size.current_size, m_Activity.party.size.max_size);
				Party += aSize;
			}
			Field("party", Party + "}");
		}
		if(m_Activity.secrets.join[0])
			Field("secrets", "{\"join\":" + Quote(m_Activity.secrets.join) + "}");
		Field("instance", m_Activity.instance ? "true" : "false");
		Json += "}";
		return Json;
	}

	void Update(bool Enabled) override
	{
		if(!Enabled)
		{
			if(m_Enabled)
			{
				m_Enabled = false;
				dbg_msg("discord", "rpc disabled");
				InitDiscord();
				m_Ipc.ClearActivity();
				m_Ipc.Close();
				m_IpcWasReady = false;
				m_aActiveClientId[0] = '\0';
			}
			return;
		}

		const int64_t Now = time_get();
		if(m_Enabled && Now < m_NextUpdate)
			return;
		m_NextUpdate = Now + time_freq() / 20;

		DiscordClientId ResolvedClientId = SIXCLIENT_DISCORD_APP_ID;
		char aResolvedClientId[32];
		ResolveDiscordClientId(ResolvedClientId, aResolvedClientId, sizeof(aResolvedClientId));

		if(!m_Enabled || str_comp(m_aActiveClientId, aResolvedClientId) != 0)
		{
			m_Enabled = true;
			dbg_msg("discord", "rpc enabled");
			InitDiscord();
		}

		m_Ipc.Configure(aResolvedClientId);
		m_Ipc.Update();
		if(m_Ipc.Ready() != m_IpcWasReady)
		{
			m_IpcWasReady = m_Ipc.Ready();
			m_UpdateActivity = true;
			m_LastActivityUpdate = 0;
		}
		std::string JoinSecret;
		while(m_Ipc.TakeJoinSecret(JoinSecret))
		{
			if(!g_Config.m_ScDiscordAllowJoin)
				continue;
			if(IClient *pClient = Kernel()->RequestInterface<IClient>())
				pClient->Connect(JoinSecret.c_str());
		}

		if(m_pCore || m_Ipc.Ready())
		{
			if(Now > m_LastConfigCheck + time_freq() / 4)
			{
				m_LastConfigCheck = Now;
				const unsigned ConfigHash = HashConfig();
				if(ConfigHash != m_ConfigHash)
				{
					m_ConfigHash = ConfigHash;
					RebuildActivity();
					m_LastActivityUpdate = 0;
				}
			}

			if(m_PresencePush == EPresencePush::NeedClear)
			{
				dbg_msg("discord", "clearing activity before image refresh");
				if(m_Ipc.Ready())
					m_Ipc.ClearActivity();
				if(m_SdkActivitySet && m_pActivityManager)
				{
					m_pActivityManager->clear_activity(m_pActivityManager, nullptr, nullptr);
					m_SdkActivitySet = false;
				}
				m_PresencePush = EPresencePush::NeedSet;
				m_PresenceSetAt = Now + time_freq();
				m_UpdateActivity = true;
			}

			const int64_t ActivityCooldown = m_LastActivityUpdate == 0 ? 0 : time_freq() * 5;
			const bool ImageRefreshPush = m_PresencePush == EPresencePush::NeedSet;
			const bool ReadyToPush = ImageRefreshPush ? Now >= m_PresenceSetAt : (m_UpdateActivity && Now > m_LastActivityUpdate + ActivityCooldown);
			if(ReadyToPush && (m_UpdateActivity || ImageRefreshPush))
			{
				m_UpdateActivity = false;
				m_LastActivityUpdate = Now;
				m_PresencePush = EPresencePush::None;

				dbg_msg("discord", "updating activity large_image='%s' small_image='%s'",
					m_aLargeImage, m_aSmallImage);
				if(m_Ipc.Ready())
				{
					if(m_SdkActivitySet && m_pActivityManager)
					{
						m_pActivityManager->clear_activity(m_pActivityManager, nullptr, nullptr);
						m_SdkActivitySet = false;
					}
					m_Ipc.SetActivity(BuildActivityJson());
				}
				else if(m_pActivityManager)
				{
					DiscordActivity PushActivity = m_Activity;
					CopyDiscordImage(PushActivity.assets.large_image, sizeof(PushActivity.assets.large_image), m_aLargeImage);
					CopyDiscordImage(PushActivity.assets.small_image, sizeof(PushActivity.assets.small_image), m_aSmallImage);
					if(m_aLargeImage[0] == '\0')
						PushActivity.assets.large_text[0] = '\0';
					if(m_aSmallImage[0] == '\0')
						PushActivity.assets.small_text[0] = '\0';
					m_pActivityManager->update_activity(m_pActivityManager, &PushActivity, nullptr, nullptr);
					m_SdkActivitySet = true;
				}
			}

			if(m_pCore)
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

		if(!g_Config.m_ScDiscordAllowJoin)
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
