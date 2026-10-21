#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_PROFILE_ASSETS_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_PROFILE_ASSETS_H

#include <game/client/ui.h>

class CMenus;
class CProfile;

void SixClientApplyProfileAssets(class CGameClient *pGameClient, const CProfile &Profile);
void SixClientFillProfileAssets(CProfile &Profile);
void SixClientDoProfileAssetsDropDown(CUi *pUi, CMenus *pMenus, CUIRect *pRect);

#endif
