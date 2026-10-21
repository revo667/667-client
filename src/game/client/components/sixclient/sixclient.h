#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_SIXCLIENT_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_SIXCLIENT_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/client/enums.h>

#include <game/client/component.h>
#include <game/teamscore.h>

#include "menu_media_background.h"

#include <memory>

class IHttpRequest;
class CTeeRenderInfo;

constexpr float SIXCLIENT_LOCAL_HAMMER_HIT_RADIUS = 120.0f;

bool SixClientShouldMuteOthersHookSound(int SoundId, bool MuteEnabled, bool LocalJustGrabbed);
bool SixClientShouldMuteOthersHammerHitSound(bool MuteEnabled, bool IsOtherPlayer);
bool SixClientShouldMuteOthersHammerWorldSound(int SoundId, bool MuteEnabled, bool IsOtherPlayer);
bool SixClientShouldMuteOthersAirJumpSound(bool MuteEnabled, bool IsOtherPlayer);
bool SixClientIsLocalHammerHitEvent(vec2 HitPos, bool LocalJustFired, vec2 LocalPos, float Radius = SIXCLIENT_LOCAL_HAMMER_HIT_RADIUS);

class CGameClient;
void SixClientPlayAirJump(CGameClient *pGameClient, int ClientId, vec2 Pos, float Alpha, float Volume);
void SixClientPlayHammerHit(CGameClient *pGameClient, vec2 Pos, float Alpha, float Volume, int ClientId);

class CSixClient : public CComponent
{
	CMenuMediaBackground m_MenuMediaBackground;
	bool m_GoresHeavyWeapon = false;

	int m_aAutoTeamLockLastTeam[NUM_DUMMIES] = {TEAM_FLOCK, TEAM_FLOCK};
	int64_t m_aAutoTeamLockDeadlineTick[NUM_DUMMIES] = {};
	bool m_aAutoTeamLockPending[NUM_DUMMIES] = {};
	int m_SpecMovedActiveTick = -1;
	int m_SpecMovedLastTick = -1;
	float m_SpecMovedNotifyTime = -999.0f;
	bool m_SavesNotifySchedule = false;
	int64_t m_SavesNotifyDeadline = 0;

	std::shared_ptr<IHttpRequest> m_pFindHoursTask;
	char m_aFindHoursPlayer[MAX_NAME_LENGTH]{};

	void UpdateFindHours();
	void ResetFindHours();

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnMapLoad() override;
	void OnUpdate() override;
	void OnConsoleInit() override;

	CMenuMediaBackground &MenuMediaBackground() { return m_MenuMediaBackground; }
	const CMenuMediaBackground &MenuMediaBackground() const { return m_MenuMediaBackground; }

	bool ApplyFrozenSkin(CTeeRenderInfo &RenderInfo, bool Frozen);

	void EnsureAudioDefaultPack();

	void UpdateGoresMode();
	void OnGoresFireInput(int Pressed);

	void UpdateAutoTeamLock();
	void UpdateSpecMovedNotify();
	void RenderSpecMovedNotify();

	void OnClientNameChanged(int ClientId, const char *pNewName, const char *pNewClan);

	void CopyPlayerSkin(int ClientId);
	void FetchFindHours(const char *pName);
};

float SixClientScoreboardPlayerPopupHeight(const class CGameClient *pGameClient, bool IsLocal, bool IsSpectatorRow, int ClientId);
float SixClientTeeRenderSize(const class CGameClient *pGameClient, int ClientId);
void SixClientRenderVotePercentages(class CUi *pUi, class ITextRender *pTextRender, const class CUIRect &Bars, int Yes, int No, int Total);

void SixClientApplyMenuFont(class ITextRender *pTextRender);

#endif
