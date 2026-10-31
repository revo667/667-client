#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_SKINPROFILES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_SKINPROFILES_H

#include <engine/console.h>
#include <engine/keys.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <vector>

class CProfile
{
public:
	static constexpr int NO_CUSTOM_COLORS = -2; // 667client
	int m_BodyColor;
	int m_FeetColor;
	int m_CountryFlag;
	int m_Emote;
	char m_SkinName[24];
	char m_Name[MAX_NAME_LENGTH];
	char m_Clan[MAX_CLAN_LENGTH];
	// 667client
	char m_AssetEntities[50];
	char m_AssetGame[50];
	char m_AssetParticles[50];
	char m_AssetHud[50];
	char m_AssetExtras[50];
	char m_AssetCursor[50];
	char m_AssetArrow[50];
	char m_AssetEmoticons[50];
	char m_AssetAudio[64];
	CProfile(int BodyColor, int FeetColor, int CountryFlag, int Emote, const char *pSkinName, const char *pName, const char *pClan,
		const char *pAssetEntities = "", const char *pAssetGame = "", const char *pAssetParticles = "",
		const char *pAssetHud = "", const char *pAssetExtras = "", const char *pAssetCursor = "", const char *pAssetArrow = "",
		const char *pAssetEmoticons = "", const char *pAssetAudio = "");
	// 667client
};

class CSkinProfiles : public CComponent
{
	static void ConAddProfile(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

public:
	std::vector<CProfile> m_Profiles;
	// 667client
	void AddProfile(int BodyColor, int FeetColor, int CountryFlag, int Emote, const char *pSkinName, const char *pName, const char *pClan,
		const char *pAssetEntities = "", const char *pAssetGame = "", const char *pAssetParticles = "",
		const char *pAssetHud = "", const char *pAssetExtras = "", const char *pAssetCursor = "", const char *pAssetArrow = "",
		const char *pAssetEmoticons = "", const char *pAssetAudio = "");
	// 667client
	void ApplyProfile(int Dummy, const CProfile &Profile);

	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
};
#endif
