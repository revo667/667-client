#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_TEAM_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_TEAM_SCOREBOARD_H

#include <engine/shared/protocol.h>

#include <game/client/component.h>

class CTeamScoreboard : public CComponent
{
	struct SPlayerState
	{
		int m_Saves = 0;
		int m_Freezes = 0;
		int m_Deaths = 0;
		bool m_WasFrozen = false;
		int m_LastFrozenTick = -1;
	};
	SPlayerState m_aState[MAX_CLIENTS];
	int m_LastLocalTeam = -1;

	bool IsFrozen(int ClientId, int CurTick) const;
	bool JustHammered(int ClientId, int CurTick) const;
	int LocalTeam() const;
	int CreditNearestHammerer(int VictimId, int CurTick) const;
	void ResetStats();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnNewSnapshot() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnRender() override;
};

#endif
