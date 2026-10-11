#include <engine/discord.h>

class CDiscordStub : public IDiscord
{
	void Update(bool Enabled) override {}
	void ClearGameInfo() override {}
	// 667client
	void SetGameInfo(const CServerInfo &ServerInfo, const char *pMapName, const char *pPlayerName, const char *pSkinName, bool ShowMap, bool Registered) override {}
	void UpdateServerInfo(const CServerInfo &ServerInfo, const char *pMapName, const char *pPlayerName, const char *pSkinName) override {}
	// 667client
	void UpdatePlayerCount(int Count) override {}
};

IDiscord *CreateDiscord()
{
	// 667client
	IDiscord *pDiscord = CreateSixClientDiscord();
	// 667client
	if(pDiscord)
	{
		return pDiscord;
	}
	return new CDiscordStub();
}
