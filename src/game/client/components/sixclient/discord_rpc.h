#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_DISCORD_RPC_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_DISCORD_RPC_H

class IDiscord;

struct SDiscordPresenceVars
{
	const char *m_pMap = "";
	const char *m_pPlayer = "";
	const char *m_pClan = "";
	const char *m_pSkin = "";
	const char *m_pServer = "";
	const char *m_pGameType = "";
	int m_Players = 0;
	int m_MaxPlayers = 0;
};

inline constexpr const char *DISCORD_PRESENCE_VARIABLES = "{map} {player} {clan} {skin} {server} {gametype} {players} {maxplayers} {version}";

inline constexpr const char *SIXCLIENT_DISCORD_DEFAULT_LARGE_IMAGE = "https://cdn.jsdelivr.net/gh/revo667/cecen-client-releases@main/discord/cecenclient-v2.png";
inline constexpr const char *SIXCLIENT_DISCORD_OLD_DEFAULT_LARGE_IMAGE = "https://cdn.jsdelivr.net/gh/revo667/cecen-client-releases@main/discord/cecenclient.png";
inline constexpr const char *SIXCLIENT_DISCORD_LEGACY_LARGE_IMAGE_KEY = "cecenclient";

void SixClientFormatDiscordText(char *pBuf, int BufSize, const char *pTemplate, const SDiscordPresenceVars &Vars);
void SixClientResolveDiscordImage(char *pDst, int DstSize, const char *pConfigured);

IDiscord *CreateSixClientDiscord();

#endif
