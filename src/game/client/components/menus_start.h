/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_START_H
#define GAME_CLIENT_COMPONENTS_MENUS_START_H

#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <cstdint>

class CMenusStart : public CComponentInterfaces
{
public:
	void RenderStartMenu(CUIRect MainView);

private:
	bool CheckHotKey(int Key) const;
	void RenderTextLogo(float CenterX, float Top, float Height, bool Decorated = false); // 667client
	float RenderStartFox(CUIRect MainView); // 667client
	void StartFoxLayout(CUIRect MainView, vec2 *pCenter, float *pSize) const; // 667client
	CUIRect StartFoxMenuRect(CUIRect MainView) const; // 667client

	float m_LogoTime = 0.0f;
	int64_t m_LastLogoRender = 0;
	IGraphics::CTextureHandle m_FoxTexture; // 667client
	bool m_FoxTextureLoaded = false; // 667client
	float m_IntroTime = 0.0f; // 667client
	bool m_IntroDone = false; // 667client
};

#endif
