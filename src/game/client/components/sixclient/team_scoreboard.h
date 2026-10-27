/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-c0d1776b */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_TEAM_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_TEAM_SCOREBOARD_H

#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/ui.h>

class CTeamScoreboard : public CComponent
{
	struct SPlayerState
	{
		int m_Saves = 0;
		int m_Freezes = 0;
		int m_Deaths = 0;
		bool m_WasFrozen = false;
		int m_LastFrozenTick = -1;
		int m_Helper = -1;
		int m_HelperTick = -1;
	};
	SPlayerState m_aState[MAX_CLIENTS];
	int m_LastLocalTeam = -1;

	CCachedText m_HeaderLabel;
	CCachedText m_aHeaderIcons[3];
	CCachedText m_Heart;
	CCachedText m_aNames[MAX_CLIENTS];
	CCachedText m_aaValues[MAX_CLIENTS][3];
	void ResetTexts();

	bool IsFrozen(int ClientId, int CurTick) const;
	bool JustHammered(int ClientId, int CurTick) const;
	int LocalTeam() const;
	int CreditNearestHammerer(int VictimId, int CurTick) const;
	int FindHooker(int VictimId) const;
	void ResetStats();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnWindowResize() override;
	void OnShutdown() override;
	void OnNewSnapshot() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnRender() override;
};

#endif
