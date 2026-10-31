#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_PROFILE_ASSETS_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_PROFILE_ASSETS_H

#include <game/client/ui.h>

enum
{
	SC_PROFILE_ASSET_ENTITIES = 1 << 0,
	SC_PROFILE_ASSET_GAME = 1 << 1,
	SC_PROFILE_ASSET_EMOTICONS = 1 << 2,
	SC_PROFILE_ASSET_PARTICLES = 1 << 3,
	SC_PROFILE_ASSET_HUD = 1 << 4,
	SC_PROFILE_ASSET_EXTRAS = 1 << 5,
	SC_PROFILE_ASSET_CURSOR = 1 << 6,
	SC_PROFILE_ASSET_ARROW = 1 << 7,
	SC_PROFILE_ASSET_AUDIO = 1 << 8,
	SC_PROFILE_ASSET_COUNT = 9,
};

class CMenus;
class CProfile;

void SixClientApplyProfileAssets(class CGameClient *pGameClient, const CProfile &Profile);
void SixClientFillProfileAssets(CProfile &Profile);
void SixClientDoProfileAssetsDropDown(CUi *pUi, CMenus *pMenus, CUIRect *pRect);

#endif
