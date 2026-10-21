/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-bbbbb270 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_MUSIC_DOCK_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_MUSIC_DOCK_H

#include <base/vmath.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <cstdint>

class CMusicDock : public CComponent
{
	enum
	{
		ACTION_SETTINGS = 0,
		ACTION_KILL,
		ACTION_DUMMY,
		ACTION_SCREENSHOT,
		ACTION_REPLAY,
		ACTION_RECONNECT,
		ACTION_DISCONNECT,
		ACTION_QUIT,
		NUM_ACTIONS,
	};

	float m_Open = 0.0f;
	float m_OpenVelocity = 0.0f;
	bool m_WantOpen = false;
	int64_t m_LastInsideTick = 0;
	float m_aIconScale[NUM_ACTIONS] = {};
	float m_aControlHover[3] = {};

	float m_Volume = 0.5f;
	bool m_VolumeAvailable = false;
	bool m_DraggingVolume = false;
	int64_t m_LastVolumeRead = 0;
	int64_t m_LastVolumeWrite = 0;

	vec2 UiMousePos() const;
	bool InteractionAllowed() const;
	void UpdateOpenState(bool Hovered, float Delta);
	void UpdateVolume(bool Open);
	void RunAction(int Action);
	void RenderIcon(const CUIRect &Rect, const char *pIcon, float Size, ColorRGBA Color);
	void RenderPanel(const CUIRect &Panel, float Scale, float Alpha, vec2 Mouse, bool Clicked);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;

	bool IsOpen() const { return m_Open > 0.05f; }
};

#endif
