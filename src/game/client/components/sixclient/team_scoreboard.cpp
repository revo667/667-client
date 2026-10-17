#include "team_scoreboard.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/teamscore.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <algorithm>

namespace {
constexpr int SAVE_HAMMER_WINDOW = 12;
constexpr float SAVE_HAMMER_RANGE = 64.0f;
constexpr int FREEZE_DEBOUNCE_TICKS = 5;

constexpr const char *ICON_SNOWFLAKE = "";
constexpr const char *ICON_HAMMER = "";
constexpr const char *ICON_SKULL = "";
constexpr const char *ICON_HEART = "";

const ColorRGBA COLOR_FREEZE(0.36f, 0.84f, 1.0f, 1.0f);
const ColorRGBA COLOR_SAVE(0.42f, 1.0f, 0.42f, 1.0f);
const ColorRGBA COLOR_DEATH(1.0f, 0.42f, 0.5f, 1.0f);
const ColorRGBA COLOR_HEADER(0.68f, 0.68f, 0.74f, 1.0f);
const ColorRGBA COLOR_LOCAL_BAR(0.62f, 0.42f, 1.0f, 1.0f);
const ColorRGBA COLOR_FRIEND(1.0f, 0.32f, 0.38f, 1.0f);
} // namespace

bool CTeamScoreboard::IsFrozen(int ClientId, int CurTick) const
{
	const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
	return Client.m_DeepFrozen || Client.m_LiveFrozen || (Client.m_FreezeEnd != 0 && Client.m_FreezeEnd > CurTick);
}

bool CTeamScoreboard::JustHammered(int ClientId, int CurTick) const
{
	const CNetObj_Character &Cur = GameClient()->m_aClients[ClientId].m_RenderCur;
	if(Cur.m_Weapon != WEAPON_HAMMER || Cur.m_AttackTick <= 0)
		return false;
	const int Age = CurTick - Cur.m_AttackTick;
	return Age >= -2 && Age <= SAVE_HAMMER_WINDOW;
}

int CTeamScoreboard::LocalTeam() const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return -1;
	return GameClient()->m_Teams.Team(LocalId);
}

int CTeamScoreboard::CreditNearestHammerer(int VictimId, int CurTick) const
{
	const int Team = GameClient()->m_Teams.Team(VictimId);
	const vec2 VictimPos = GameClient()->m_aClients[VictimId].m_RenderPos;

	int Best = -1;
	float BestDist = SAVE_HAMMER_RANGE;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == VictimId || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;
		if(GameClient()->m_Teams.Team(i) != Team)
			continue;
		if(!JustHammered(i, CurTick))
			continue;
		const float Dist = distance(VictimPos, GameClient()->m_aClients[i].m_RenderPos);
		if(Dist <= BestDist)
		{
			BestDist = Dist;
			Best = i;
		}
	}
	return Best;
}

void CTeamScoreboard::ResetStats()
{
	for(SPlayerState &State : m_aState)
		State = SPlayerState();
}

void CTeamScoreboard::OnReset()
{
	ResetStats();
	m_LastLocalTeam = -1;
}

void CTeamScoreboard::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int CurTick = Client()->GameTick(g_Config.m_ClDummy);

	const int LocalTeamId = LocalTeam();
	if(LocalTeamId != m_LastLocalTeam)
	{
		m_LastLocalTeam = LocalTeamId;
		ResetStats();
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const bool Active = GameClient()->m_Snap.m_aCharacters[i].m_Active;
		const bool Frozen = Active && IsFrozen(i, CurTick);

		SPlayerState &State = m_aState[i];
		if(Frozen && !State.m_WasFrozen)
		{
			if(State.m_LastFrozenTick < 0 || CurTick - State.m_LastFrozenTick > FREEZE_DEBOUNCE_TICKS)
				State.m_Freezes++;
		}
		else if(State.m_WasFrozen && !Frozen && Active)
		{
			const int Saver = CreditNearestHammerer(i, CurTick);
			if(Saver >= 0)
				m_aState[Saver].m_Saves++;
		}
		if(Frozen)
			State.m_LastFrozenTick = CurTick;
		State.m_WasFrozen = Frozen;
	}
}

void CTeamScoreboard::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_KILLMSG)
		return;

	const CNetMsg_Sv_KillMsg *pMsg = (const CNetMsg_Sv_KillMsg *)pRawMsg;
	if(pMsg->m_Victim >= 0 && pMsg->m_Victim < MAX_CLIENTS)
		m_aState[pMsg->m_Victim].m_Deaths++;
}

void CTeamScoreboard::OnRender()
{
	if(!g_Config.m_ScTeamScoreboard)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int LocalTeamId = LocalTeam();
	if(LocalTeamId < 0 || LocalTeamId >= TEAM_SUPER)
		return;
	if(g_Config.m_ScTeamScoreboardOnlyInTeam && LocalTeamId <= TEAM_FLOCK)
		return;

	int aRows[MAX_CLIENTS];
	int Num = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;
		if(GameClient()->m_Teams.Team(i) != LocalTeamId)
			continue;
		aRows[Num++] = i;
	}
	if(Num == 0)
		return;

	std::sort(aRows, aRows + Num, [this](int a, int b) {
		const SPlayerState &A = m_aState[a];
		const SPlayerState &B = m_aState[b];
		if(A.m_Saves != B.m_Saves)
			return A.m_Saves > B.m_Saves;
		if(A.m_Freezes != B.m_Freezes)
			return A.m_Freezes < B.m_Freezes;
		return a < b;
	});

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const float Scale = std::clamp(g_Config.m_ScTeamScoreboardSize, 50, 300) / 100.0f;

	const float Width = 300.0f * Graphics()->ScreenAspect();
	const float Height = 300.0f;
	const CScreenRect PreviousScreen = Graphics()->GetScreen();
	Graphics()->MapScreenToSize(Width, Height);

	const float Pad = 3.0f * Scale;
	const float RowH = 10.0f * Scale;
	const float FontSize = 6.0f * Scale;
	const float HeadSize = 5.5f * Scale;
	const float IconSize = 6.0f * Scale;
	const float TeeSize = RowH * 1.05f;
	const float AccentW = 1.5f * Scale;
	const float NameW = 48.0f * Scale;
	const float ColW = 14.0f * Scale;
	const int NumCols = 3;

	const float StartX = 0.0f;
	const float StartY = std::clamp((float)g_Config.m_ScTeamScoreboardY, 0.0f, Height - RowH * 2.0f);
	const float TeeX = StartX + Pad + AccentW + TeeSize * 0.5f;
	const float NameX = StartX + Pad + AccentW + TeeSize + 1.5f * Scale;
	const float ColStart = NameX + NameW;
	const float PanelW = ColStart + NumCols * ColW + Pad - StartX;
	const float PanelH = Pad + RowH + Num * RowH + Pad;

	const int Corners = StartY <= 0.0f ? IGraphics::CORNER_BR : IGraphics::CORNER_R;
	Graphics()->DrawRect(StartX, StartY, PanelW, PanelH, ColorRGBA(0.04f, 0.04f, 0.06f, 0.72f), Corners, 4.0f * Scale);

	const auto ColCenter = [&](int Col) { return ColStart + ColW * (Col + 0.5f); };
	const auto TextCentered = [&](float CenterX, float Y, float Size, const char *pText) {
		TextRender()->Text(CenterX - TextRender()->TextWidth(Size, pText) * 0.5f, Y, Size, pText, -1.0f);
	};

	float y = StartY + Pad;
	TextRender()->TextColor(COLOR_HEADER);
	TextRender()->Text(StartX + Pad + AccentW, y + (RowH - HeadSize) * 0.5f, HeadSize, "Player", -1.0f);

	const char *apIcons[NumCols] = {ICON_SNOWFLAKE, ICON_HAMMER, ICON_SKULL};
	const ColorRGBA aColors[NumCols] = {COLOR_FREEZE, COLOR_SAVE, COLOR_DEATH};
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	for(int c = 0; c < NumCols; c++)
	{
		TextRender()->TextColor(aColors[c]);
		TextCentered(ColCenter(c), y + (RowH - IconSize) * 0.5f, IconSize, apIcons[c]);
	}
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	y += RowH;

	for(int r = 0; r < Num; r++)
	{
		const int ClientId = aRows[r];
		const SPlayerState &State = m_aState[ClientId];
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const float TextY = y + (RowH - FontSize) * 0.5f;

		Graphics()->DrawRect(StartX + Pad * 0.5f, y + RowH - 0.35f * Scale, PanelW - Pad, 0.35f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_NONE, 0.0f);
		if(ClientId == LocalId)
			Graphics()->DrawRect(StartX + Pad * 0.5f, y + 1.5f * Scale, AccentW, RowH - 3.0f * Scale, COLOR_LOCAL_BAR, IGraphics::CORNER_ALL, AccentW * 0.5f);

		CTeeRenderInfo TeeInfo = Data.m_RenderInfo;
		TeeInfo.m_Size = TeeSize;
		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(TeeX, y + RowH * 0.5f + OffsetToMid.y), 1.0f);

		float NameCursor = NameX;
		if(Data.m_Friend)
		{
			const float HeartSize = FontSize * 0.85f;
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->TextColor(COLOR_FRIEND);
			TextRender()->Text(NameCursor, y + (RowH - HeartSize) * 0.5f, HeartSize, ICON_HEART, -1.0f);
			NameCursor += TextRender()->TextWidth(HeartSize, ICON_HEART) + 1.5f * Scale;
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}

		const float NameMaxW = ColStart - NameCursor - 1.5f * Scale;
		char aName[MAX_NAME_LENGTH + 4];
		str_copy(aName, Data.m_aName);
		while(aName[0] != '\0' && TextRender()->TextWidth(FontSize, aName) > NameMaxW)
			aName[str_length(aName) - 1] = '\0';
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text(NameCursor, TextY, FontSize, aName, -1.0f);

		const int aValues[NumCols] = {State.m_Freezes, State.m_Saves, State.m_Deaths};
		for(int c = 0; c < NumCols; c++)
		{
			char aNum[16];
			str_format(aNum, sizeof(aNum), "%d", aValues[c]);
			TextRender()->TextColor(aColors[c]);
			TextCentered(ColCenter(c), TextY, FontSize, aNum);
		}

		y += RowH;
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->MapScreen(PreviousScreen);
}
