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

void SixClientFormatDiscordText(char *pBuf, int BufSize, const char *pTemplate, const SDiscordPresenceVars &Vars);

IDiscord *CreateSixClientDiscord();

#endif
