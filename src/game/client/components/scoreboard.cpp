/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "scoreboard.h"

#include <base/dbg.h>
#include <base/time.h>

#include <cmath>

#include <engine/console.h>
#include <engine/demo.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/sixclient/sixclient.h> // sixclient
#include <game/client/components/sixclient/social_card.h> // 667client
#include <generated/client_data7.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/motd.h>
#include <game/client/components/statboard.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

static constexpr float POPUP_WIDTH = 230.0f; // 667client: room for the profile card

// Horizontal spacing of the scoreboard contents, both to its edges and between columns
static constexpr float MARGIN = 10.0f;

CScoreboard::CScoreboard()
{
	CScoreboard::OnReset();
}

void CScoreboard::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();

	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CScoreboard::LockMouse()
{
	Ui()->ClosePopupMenus();
	m_MouseUnlocked = false;
	SetUiMousePos(m_LastMousePos.value());
	m_LastMousePos = Ui()->MousePos();
}

void CScoreboard::ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	pSelf->GameClient()->m_Spectator.OnRelease();
	pSelf->GameClient()->m_Emoticon.OnRelease();

	pSelf->m_Active = pResult->GetInteger(0) != 0;

	if(!pSelf->IsActive() && pSelf->m_MouseUnlocked)
	{
		pSelf->LockMouse();
	}
}

void CScoreboard::ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	if(!pSelf->IsActive() ||
		pSelf->GameClient()->m_Menus.IsActive() ||
		pSelf->GameClient()->m_Chat.IsActive() ||
		pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		return;
	}

	pSelf->m_MouseUnlocked = !pSelf->m_MouseUnlocked;

	if(!pSelf->m_MouseUnlocked)
	{
		pSelf->Ui()->ClosePopupMenus();
	}

	vec2 OldMousePos = pSelf->Ui()->MousePos();

	if(pSelf->m_LastMousePos == std::nullopt)
	{
		pSelf->SetUiMousePos(pSelf->Ui()->Screen()->Center());
	}
	else
	{
		pSelf->SetUiMousePos(pSelf->m_LastMousePos.value());
	}

	// save pos, so moving the mouse in esc menu doesn't change the position
	pSelf->m_LastMousePos = OldMousePos;
}

void CScoreboard::OnConsoleInit()
{
	Console()->Register("+scoreboard", "", CFGFLAG_CLIENT, ConKeyScoreboard, this, "Show scoreboard");
	Console()->Register("toggle_scoreboard_cursor", "", CFGFLAG_CLIENT, ConToggleScoreboardCursor, this, "Toggle scoreboard cursor");
}

void CScoreboard::OnInit()
{
	m_DeadTeeTexture = Graphics()->LoadTexture("deadtee.png", IStorage::TYPE_ALL);
}

void CScoreboard::OnReset()
{
	m_Active = false;
	m_MouseUnlocked = false;
	m_LastMousePos = std::nullopt;
}

void CScoreboard::ResetTexts()
{
	for(CPlayerElement &Player : m_aPlayers)
	{
		Player.m_Score.Reset(TextRender());
		Player.m_ScoreMillis.Reset(TextRender());
		Player.m_Name.Reset(TextRender());
		Player.m_MuteMark.Reset(TextRender());
		Player.m_ReadyMark.Reset(TextRender());
		Player.m_Clan.Reset(TextRender());
		Player.m_Ping.Reset(TextRender());
	}
	m_TitleScore.Reset(TextRender());
	m_TitleScoreMillis.Reset(TextRender());
	m_HeadlineScore.Reset(TextRender());
	m_HeadlineName.Reset(TextRender());
	m_HeadlineClan.Reset(TextRender());
	m_HeadlinePing.Reset(TextRender());
}

void CScoreboard::OnShutdown()
{
	ResetTexts();
}

void CScoreboard::OnWindowResize()
{
	ResetTexts();
}

void CScoreboard::OnRelease()
{
	m_Active = false;

	if(m_MouseUnlocked)
	{
		LockMouse();
	}
}

bool CScoreboard::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive() || !m_MouseUnlocked)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CScoreboard::OnInput(const IInput::CEvent &Event)
{
	if(m_MouseUnlocked && Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		LockMouse();
		return true;
	}

	return IsActive() && m_MouseUnlocked;
}

void CScoreboard::RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize)
{
	const bool IsMapTitle = !GameClient()->IsTeamPlay();
	if(IsMapTitle && m_MouseUnlocked && GameClient()->m_aMapDescription[0] != '\0')
	{
		const int ButtonResult = Ui()->DoButtonLogic(&m_MapTitleButtonId, 0, &TitleLabel, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(ButtonResult != 0)
		{
			m_MapTitlePopupContext.m_pScoreboard = this;

			m_MapTitlePopupContext.m_FontSize = 12.0f;
			const float MaxWidth = 300.0f;
			const float Margin = 5.0f;
			const char *pDescription = GameClient()->m_aMapDescription;
			const float TextWidth = std::min(std::ceil(TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription) + 0.5f), MaxWidth);
			float TextHeight = 0.0f;
			STextSizeProperties TextSizeProps{};
			TextSizeProps.m_pHeight = &TextHeight;
			TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription, -1, TextWidth, 0, TextSizeProps);

			Ui()->DoPopupMenu(&m_MapTitlePopupContext, Ui()->MouseX(), Ui()->MouseY(), TextWidth + Margin * 2, TextHeight + Margin * 2, &m_MapTitlePopupContext, CMapTitlePopupContext::Render);
		}
		if(Ui()->HotItem() == &m_MapTitleButtonId)
		{
			TitleLabel.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.3f), IGraphics::CORNER_ALL, 5.0f);
		}
	}

	SLabelProperties Props;
	Props.m_MaxWidth = TitleLabel.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&TitleLabel, pTitle, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_ML : TEXTALIGN_MR, Props);
}

void CScoreboard::RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize)
{
	// map best
	char aScore[128] = "";
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes || TimeScore || Race7)
	{
		if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET)
		{
			Ui()->RenderTime(ScoreLabel,
				TitleFontSize,
				GameClient()->m_MapBestTimeSeconds,
				GameClient()->m_MapBestTimeSeconds == FinishTime::NOT_FINISHED_MILLIS,
				GameClient()->m_MapBestTimeMillis,
				GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis,
				m_TitleScore, m_TitleScoreMillis, TextRender()->DefaultTextColor());
			return;
		}
	}
	else if(GameClient()->IsTeamPlay()) // normal score
	{
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if(pGameDataObj)
		{
			str_format(aScore, sizeof(aScore), "%d", Team == TEAM_RED ? pGameDataObj->m_TeamscoreRed : pGameDataObj->m_TeamscoreBlue);
		}
	}
	else
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active &&
			GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW &&
			GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId])
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId]->m_Score);
		}
		else if(GameClient()->m_Snap.m_pLocalInfo)
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_pLocalInfo->m_Score);
		}
	}

	const float ScoreTextWidth = aScore[0] != '\0' ? TextRender()->TextWidth(TitleFontSize, aScore, -1, -1.0f, 0) : 0.0f;
	if(ScoreTextWidth != 0.0f)
	{
		Ui()->DoLabel(&ScoreLabel, aScore, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_MR : TEXTALIGN_ML);
	}
}

void CScoreboard::RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const float TitleFontSize = 20.0f;
	const float ScoreTextWidth = TextRender()->TextWidth(TitleFontSize, "00:00:00");
	const float TitleTextWidth = TextRender()->TextWidth(TitleFontSize, pTitle);

	TitleBar.VMargin(MARGIN, &TitleBar);
	CUIRect TitleLabel, ScoreLabel;
	if(Team == TEAM_RED)
	{
		TitleBar.VSplitRight(ScoreTextWidth, &TitleLabel, &ScoreLabel);
		TitleLabel.VSplitRight(5.0f, &TitleLabel, nullptr);
		TitleLabel.VSplitLeft(std::min(TitleTextWidth + 2.0f, TitleLabel.w), &TitleLabel, nullptr);
	}
	else
	{
		TitleBar.VSplitLeft(ScoreTextWidth, &ScoreLabel, &TitleLabel);
		TitleLabel.VSplitLeft(5.0f, nullptr, &TitleLabel);
		TitleLabel.VSplitRight(std::min(TitleTextWidth + 2.0f, TitleLabel.w), nullptr, &TitleLabel);
	}

	RenderTitle(TitleLabel, Team, pTitle, TitleFontSize);
	RenderTitleScore(ScoreLabel, Team, TitleFontSize);
}

void CScoreboard::RenderGoals(CUIRect Goals)
{
	Goals.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);
	Goals.VMargin(5.0f, &Goals);

	const float FontSize = 10.0f;
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	char aBuf[64];

	if(pGameInfoObj->m_ScoreLimit)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Score limit"), pGameInfoObj->m_ScoreLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_ML);
	}

	if(pGameInfoObj->m_TimeLimit)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Time limit: %d min"), pGameInfoObj->m_TimeLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MC);
	}

	if(pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Round %d/%d"), pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MR);
	}
}

void CScoreboard::RenderSpectators(CUIRect Spectators)
{
	Spectators.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);
	constexpr float SpectatorCut = 5.0f;
	Spectators.Margin(SpectatorCut, &Spectators);

	CTextCursor Cursor;
	Cursor.SetPosition(Spectators.TopLeft());
	Cursor.m_FontSize = 11.0f;
	Cursor.m_LineWidth = Spectators.w;
	Cursor.m_MaxLines = round_truncate(Spectators.h / Cursor.m_FontSize);

	int RemainingSpectators = 0;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;
		++RemainingSpectators;
	}

	TextRender()->TextEx(&Cursor, Localize("Spectators"));

	if(RemainingSpectators > 0)
	{
		TextRender()->TextEx(&Cursor, ": ");
	}

	bool CommaNeeded = false;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;

		if(CommaNeeded)
		{
			TextRender()->TextEx(&Cursor, ", ");
		}

		if(Cursor.m_LineCount == Cursor.m_MaxLines && RemainingSpectators >= 2)
		{
			// This is less expensive than checking with a separate invisible
			// text cursor though we waste some space at the end of the line.
			char aRemaining[64];
			str_format(aRemaining, sizeof(aRemaining), Localize("%d others…", "Spectators"), RemainingSpectators);
			TextRender()->TextEx(&Cursor, aRemaining);
			break;
		}

		CUIRect SpectatorRect, SpectatorRectLineBreak;
		float Margin = 1.0f;
		SpectatorRect.x = Cursor.m_X - Margin;
		SpectatorRect.y = Cursor.m_Y;

		if(g_Config.m_ClShowIds)
		{
			char aClientId[16];
			GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::NO_INDENT);
			TextRender()->TextEx(&Cursor, aClientId);
		}

		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[pInfo->m_ClientId];
		{
			const char *pClanName = ClientData.m_aClan;
			if(pClanName[0] != '\0')
			{
				if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(pClanName, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)));
				}
				else
				{
					TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.7f));
				}

				TextRender()->TextEx(&Cursor, pClanName);
				TextRender()->TextEx(&Cursor, " ");

				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}

		if(GameClient()->m_aClients[pInfo->m_ClientId].m_AuthLevel)
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)));
		}

		TextRender()->TextEx(&Cursor, GameClient()->m_aClients[pInfo->m_ClientId].m_aName);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CommaNeeded = true;
		--RemainingSpectators;

		bool LineBreakDetected = false;
		SpectatorRect.h = Cursor.m_FontSize;

		// detect line breaks
		if(Cursor.m_Y != SpectatorRect.y)
		{
			LineBreakDetected = true;
			SpectatorRectLineBreak.x = Spectators.x - SpectatorCut;
			SpectatorRectLineBreak.y = Cursor.m_Y;
			SpectatorRectLineBreak.h = Cursor.m_FontSize;
			SpectatorRectLineBreak.w = Cursor.m_X - Spectators.x + SpectatorCut + 2 * Margin;

			SpectatorRect.w = Spectators.x + Spectators.w + SpectatorCut - SpectatorRect.x;
		}
		else
		{
			SpectatorRect.w = Cursor.m_X - SpectatorRect.x + 2 * Margin;
		}

		if(m_MouseUnlocked)
		{
			int ButtonResult = Ui()->DoButtonLogic(&m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId, 0, &SpectatorRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);

			if(LineBreakDetected && ButtonResult == 0)
			{
				ButtonResult = Ui()->DoButtonLogic(&m_aPlayers[pInfo->m_ClientId].m_SpectatorSecondLineButtonId, 0, &SpectatorRectLineBreak, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
			}
			if(ButtonResult != 0)
			{
				m_ScoreboardPopupContext.m_pScoreboard = this;
				m_ScoreboardPopupContext.m_ClientId = pInfo->m_ClientId;
				m_ScoreboardPopupContext.m_IsLocal = GameClient()->m_aLocalIds[0] == pInfo->m_ClientId ||
								     (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == pInfo->m_ClientId);
				m_ScoreboardPopupContext.m_IsSpectating = true;

				m_ScoreboardPopupContext.m_LastHeight = CScoreboardPopupContext::Height(GameClient(), m_ScoreboardPopupContext.m_IsLocal, true, m_ScoreboardPopupContext.m_ClientId);
				Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), POPUP_WIDTH,
					m_ScoreboardPopupContext.m_LastHeight, &m_ScoreboardPopupContext, CScoreboardPopupContext::Render);
			}

			if(Ui()->HotItem() == &m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId ||
				Ui()->HotItem() == &m_aPlayers[pInfo->m_ClientId].m_SpectatorSecondLineButtonId ||
				(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == pInfo->m_ClientId))
			{
				if(!LineBreakDetected)
				{
					SpectatorRect.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_ALL, 2.5f);
				}
				else
				{
					SpectatorRect.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_L, 2.5f);
					SpectatorRectLineBreak.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_R, 2.5f);
				}
			}
		}
	}
}

void CScoreboard::RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State, int NumPlayersForSize)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool MillisecondScore = GameClient()->m_ReceivedDDNetPlayerFinishTimes;
	const bool TrueMilliseconds = GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis;
	const int NumPlayers = NumPlayersForSize >= 0 ? NumPlayersForSize : (CountEnd - CountStart);
	const bool LowScoreboardWidth = Scoreboard.w < 350.0f;

	bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;

	const bool UseTime = Race7 || TimeScore || MillisecondScore;

	// calculate measurements
	float LineHeight;
	float TeeSizeMod;
	float Spacing;
	float RoundRadius;
	float FontSize;
	if(NumPlayers <= 8)
	{
		LineHeight = 30.0f;
		TeeSizeMod = 0.5f;
		Spacing = 8.0f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 12)
	{
		LineHeight = 25.0f;
		TeeSizeMod = 0.45f;
		Spacing = 2.5f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 16)
	{
		LineHeight = 20.0f;
		TeeSizeMod = 0.4f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 24)
	{
		LineHeight = 13.5f;
		TeeSizeMod = 0.3f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 10.0f;
	}
	else if(NumPlayers <= 32)
	{
		LineHeight = 10.0f;
		TeeSizeMod = 0.2f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 8.0f;
	}
	else if(LowScoreboardWidth && NumPlayers <= 48)
	{
		LineHeight = 7.5f;
		TeeSizeMod = 0.125f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 7.0f;
	}
	else
	{
		LineHeight = 5.0f;
		TeeSizeMod = 0.1f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 5.0f;
	}

	const float ScoreOffset = Scoreboard.x + MARGIN;
	const float ScoreLength = TextRender()->TextWidth(FontSize, UseTime ? "00:00:00" : "99999");
	// 667client: Cecen Client badges get their own column between the score and the tee,
	// reserved for every row so tees and names stay aligned.
	// Sized from the font so sparse boards with tall rows don't blow it up, capped so dense boards stay tidy.
	const float BadgeHeight = g_Config.m_ScClientIndicator ? std::min(FontSize * 1.8f, LineHeight - Spacing * 0.5f) : 0.0f;
	const float BadgeSlot = BadgeHeight > 0.0f ? GameClient()->m_ClientIndicator.IconWidth(BadgeHeight) + MARGIN * 0.5f : 0.0f;
	const float TeeOffset = ScoreOffset + ScoreLength + MARGIN + BadgeSlot;
	const float TeeLength = 60.0f * TeeSizeMod;
	const float NameOffset = TeeOffset + TeeLength;
	const float CountryLength = (LineHeight - Spacing - TeeSizeMod * 5.0f) * 2.0f;
	const float PingLength = 27.5f;
	const float PingOffset = Scoreboard.x + Scoreboard.w - PingLength - MARGIN;
	const float CountryOffset = PingOffset - CountryLength;

	float NameLength = (LowScoreboardWidth ? 90.0f : 150.0f) - TeeLength;
	const float MinMiddleGap = 5.0f; // 2.5 before and after clan
	const float AvailableMiddle = CountryOffset - NameOffset;
	if(NameLength + MinMiddleGap > AvailableMiddle)
	{
		const float Shrinkable = AvailableMiddle - MinMiddleGap;
		NameLength = std::max(0.0f, Shrinkable * 0.7f);
	}

	const float ClanOffset = NameOffset + NameLength + 2.5f;
	const float ClanLength = std::max(0.0f, CountryOffset - ClanOffset - 2.5f);

	// render headlines
	const float HeadlineFontsize = 11.0f;
	CUIRect Headline;
	Scoreboard.HSplitTop(HeadlineFontsize * 2.0f, &Headline, &Scoreboard);
	const float HeadlineY = Headline.y + Headline.h / 2.0f - HeadlineFontsize / 2.0f;
	const ColorRGBA HeadlineColor = TextRender()->DefaultTextColor();
	m_HeadlineScore.Update(TextRender(), UseTime ? Localize("Time") : Localize("Score"), HeadlineFontsize);
	m_HeadlineName.Update(TextRender(), Localize("Name"), HeadlineFontsize);
	m_HeadlineClan.Update(TextRender(), Localize("Clan"), HeadlineFontsize);
	m_HeadlinePing.Update(TextRender(), Localize("Ping"), HeadlineFontsize);
	m_HeadlineScore.Render(TextRender(), vec2(ScoreOffset + ScoreLength - m_HeadlineScore.Width(), HeadlineY), HeadlineColor);
	m_HeadlineName.Render(TextRender(), vec2(NameOffset, HeadlineY), HeadlineColor);
	m_HeadlineClan.Render(TextRender(), vec2(ClanOffset + (ClanLength - m_HeadlineClan.Width()) / 2.0f, HeadlineY), HeadlineColor);
	m_HeadlinePing.Render(TextRender(), vec2(PingOffset + PingLength - m_HeadlinePing.Width(), HeadlineY), HeadlineColor);

	// render player entries
	int CountRendered = 0;
	int PrevDDTeam = -1;
	int &CurrentDDTeamSize = State.m_CurrentDDTeamSize;

	char aBuf[64];
	int MaxTeamSize = GameClient()->MaxTeamSize();

	for(int RenderDead = 0; RenderDead < 2; RenderDead++)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			// make sure that we render the correct team
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
			if(!pInfo || pInfo->m_Team != Team)
				continue;
			bool IsDead = Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD;
			if(!RenderDead && IsDead)
				continue;
			if(RenderDead && !IsDead)
				continue;
			if(CountRendered++ < CountStart)
				continue;

			int DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
			int NextDDTeam = 0;

			ColorRGBA TextColor = TextRender()->DefaultTextColor();
			TextColor.a = RenderDead ? 0.5f : 1.0f;
			TextRender()->TextColor(TextColor);

			for(int j = i + 1; j < MAX_CLIENTS; j++)
			{
				const CNetObj_PlayerInfo *pInfoNext = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
				if(!pInfoNext || pInfoNext->m_Team != Team)
					continue;

				NextDDTeam = GameClient()->m_Teams.Team(pInfoNext->m_ClientId);
				break;
			}

			if(PrevDDTeam == -1)
			{
				for(int j = i - 1; j >= 0; j--)
				{
					const CNetObj_PlayerInfo *pInfoPrev = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
					if(!pInfoPrev || pInfoPrev->m_Team != Team)
						continue;

					PrevDDTeam = GameClient()->m_Teams.Team(pInfoPrev->m_ClientId);
					break;
				}
			}

			CUIRect RowAndSpacing, Row;
			Scoreboard.HSplitTop(LineHeight + Spacing, &RowAndSpacing, &Scoreboard);
			RowAndSpacing.HSplitTop(LineHeight, &Row, nullptr);

			// team background
			if(DDTeam != TEAM_FLOCK)
			{
				const ColorRGBA Color = GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f);
				int TeamRectCorners = 0;
				if(PrevDDTeam != DDTeam)
				{
					TeamRectCorners |= IGraphics::CORNER_T;
					State.m_TeamStartX = Row.x;
					State.m_TeamStartY = Row.y;
				}
				if(NextDDTeam != DDTeam)
					TeamRectCorners |= IGraphics::CORNER_B;
				RowAndSpacing.Draw(Color, TeamRectCorners, RoundRadius);

				CurrentDDTeamSize++;

				if(NextDDTeam != DDTeam)
				{
					const float TeamFontSize = FontSize / 1.5f;

					if(NumPlayers > 8)
					{
						if(DDTeam == GameClient()->m_Teams.TeamSuper())
							str_copy(aBuf, Localize("Super"));
						else if(CurrentDDTeamSize <= 1)
							str_format(aBuf, sizeof(aBuf), "%d", DDTeam);
						else
							str_format(aBuf, sizeof(aBuf), Localize("%d\n(%d/%d)", "Team and size"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
						TextRender()->Text(State.m_TeamStartX, std::max(State.m_TeamStartY + Row.h / 2.0f - TeamFontSize, State.m_TeamStartY + 1.5f /* padding top */), TeamFontSize, aBuf);
					}
					else
					{
						if(DDTeam == GameClient()->m_Teams.TeamSuper())
							str_copy(aBuf, Localize("Super"));
						else if(CurrentDDTeamSize > 1)
							str_format(aBuf, sizeof(aBuf), Localize("Team %d (%d/%d)"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
						else
							str_format(aBuf, sizeof(aBuf), Localize("Team %d"), DDTeam);
						TextRender()->Text(Row.x + Row.w / 2.0f - TextRender()->TextWidth(TeamFontSize, aBuf) / 2.0f + 5.0f, Row.y + Row.h, TeamFontSize, aBuf);
					}

					CurrentDDTeamSize = 0;
				}
			}
			PrevDDTeam = DDTeam;

			// background so it's easy to find the local player or the followed one in spectator mode
			if((!GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_ClientId == GameClient()->m_Snap.m_SpecInfo.m_SpectatorId))
			{
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, RoundRadius);
			}

			const CGameClient::CClientData &ClientData = GameClient()->m_aClients[pInfo->m_ClientId];
			CPlayerElement &Player = m_aPlayers[pInfo->m_ClientId];

			if(m_MouseUnlocked)
			{
				const int ButtonResult = Ui()->DoButtonLogic(&Player.m_PlayerButtonId, 0, &Row, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
				if(ButtonResult != 0)
				{
					m_ScoreboardPopupContext.m_pScoreboard = this;
					m_ScoreboardPopupContext.m_ClientId = pInfo->m_ClientId;
					m_ScoreboardPopupContext.m_IsLocal = GameClient()->m_aLocalIds[0] == pInfo->m_ClientId ||
									     (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == pInfo->m_ClientId);
					m_ScoreboardPopupContext.m_IsSpectating = false;

					m_ScoreboardPopupContext.m_LastHeight = CScoreboardPopupContext::Height(GameClient(), m_ScoreboardPopupContext.m_IsLocal, false, m_ScoreboardPopupContext.m_ClientId);
					Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), POPUP_WIDTH,
						m_ScoreboardPopupContext.m_LastHeight, &m_ScoreboardPopupContext, CScoreboardPopupContext::Render);
				}

				if(Ui()->HotItem() == &Player.m_PlayerButtonId ||
					(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == pInfo->m_ClientId))
				{
					Row.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f), IGraphics::CORNER_ALL, RoundRadius);
				}
			}

			// score
			CUIRect ScorePosition;
			ScorePosition.x = ScoreOffset;
			ScorePosition.w = ScoreLength;
			ScorePosition.y = Row.y;
			ScorePosition.h = Row.h;

			if(Race7)
			{
				Ui()->RenderTime(ScorePosition, FontSize, pInfo->m_Score / 1000, pInfo->m_Score == protocol7::FinishTime::NOT_FINISHED, pInfo->m_Score % 1000, true,
					Player.m_Score, Player.m_ScoreMillis, TextColor);
			}
			else if(MillisecondScore)
			{
				Ui()->RenderTime(ScorePosition, FontSize, ClientData.m_FinishTimeSeconds, ClientData.m_FinishTimeSeconds == FinishTime::NOT_FINISHED_MILLIS, ClientData.m_FinishTimeMillis, TrueMilliseconds,
					Player.m_Score, Player.m_ScoreMillis, TextColor);
			}
			else if(TimeScore)
			{
				Ui()->RenderTime(ScorePosition, FontSize, pInfo->m_Score, pInfo->m_Score == FinishTime::NOT_FINISHED_TIMESCORE, -1, false,
					Player.m_Score, Player.m_ScoreMillis, TextColor);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Score, -999, 99999));
				Player.m_Score.Update(TextRender(), aBuf, FontSize);
				Player.m_Score.Render(TextRender(), vec2(ScoreOffset + ScoreLength - Player.m_Score.Width(), ScorePosition.y + (Row.h - FontSize) / 2.0f), TextColor);
			}

			// 667client
			if(BadgeHeight > 0.0f && GameClient()->m_ClientIndicator.IsUserInGame(pInfo->m_ClientId))
			{
				const float BadgeWidth = GameClient()->m_ClientIndicator.IconWidth(BadgeHeight);
				const float GapStart = ScoreOffset + ScoreLength;
				GameClient()->m_ClientIndicator.RenderIcon(GapStart + (TeeOffset - GapStart - BadgeWidth) / 2.0f, Row.y + (Row.h - BadgeHeight) / 2.0f, BadgeHeight, TextColor.a);
			}
			// 667client

			// CTF flag
			if(pGameInfoObj && (pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
				pGameDataObj && (pGameDataObj->m_FlagCarrierRed == pInfo->m_ClientId || pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId))
			{
				Graphics()->TextureSet(pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId ? GameClient()->m_GameSkin.m_SpriteFlagBlue : GameClient()->m_GameSkin.m_SpriteFlagRed);
				Graphics()->QuadsBegin();
				Graphics()->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y - 2.5f - Spacing / 2.0f, Row.h / 2.0f, Row.h);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}

			// skin
			if(RenderDead)
			{
				Graphics()->TextureSet(m_DeadTeeTexture);
				Graphics()->QuadsBegin();
				if(GameClient()->IsTeamPlay())
				{
					Graphics()->SetColor(GameClient()->m_Skins7.GetTeamColor(true, 0, GameClient()->m_aClients[pInfo->m_ClientId].m_Team, protocol7::SKINPART_BODY));
				}
				CTeeRenderInfo TeeInfo = GameClient()->m_aClients[pInfo->m_ClientId].m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y, TeeInfo.m_Size, TeeInfo.m_Size);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}
			else
			{
				CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
				const vec2 TeeRenderPos = vec2(TeeOffset + TeeLength / 2, Row.y + Row.h / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);

				if(m_MouseUnlocked)
				{
					const CUIRect SkinRect = {TeeOffset, Row.y, TeeLength, Row.h};
					GameClient()->m_Tooltips.DoToolTip(&m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId, &SkinRect, ClientData.m_aSkinName);
				}
			}

			const float TextY = Row.y + (Row.h - FontSize) / 2.0f;

			// name
			{
				if(g_Config.m_ClShowIds)
				{
					char aClientId[16];
					GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
					str_copy(aBuf, aClientId);
					str_append(aBuf, ClientData.m_aName);
				}
				else
				{
					str_copy(aBuf, ClientData.m_aName);
				}
				ColorRGBA NameColor = TextColor;
				if(ClientData.m_AuthLevel)
				{
					NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor));
				}
				// TClient
				if(pInfo->m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(pInfo->m_ClientId))
				{
					NameColor = GameClient()->m_WarList.GetNameplateColor(pInfo->m_ClientId);
				}

				float NameTextOffset = NameOffset;
				const bool Muted = pInfo->m_ClientId >= 0 && (GameClient()->m_aClients[pInfo->m_ClientId].m_Foe || GameClient()->m_aClients[pInfo->m_ClientId].m_ChatIgnore);
				if(Muted)
				{
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					Player.m_MuteMark.Update(TextRender(), FontIcon::COMMENT_SLASH, FontSize);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					Player.m_MuteMark.Render(TextRender(), vec2(NameTextOffset, TextY), NameColor);
					NameTextOffset += Player.m_MuteMark.Width();
				}
				// 667client: room for the Spotify icon after the name
				const CClientIndicator::SMusic *pMusic = GameClient()->m_ClientIndicator.MusicInGame(pInfo->m_ClientId);
				const float SpotifySize = FontSize * 1.05f;
				const float SpotifyGap = FontSize * 0.35f;
				const float NameMaxWidth = std::max(NameLength - (NameTextOffset - NameOffset) - (pMusic ? SpotifySize + SpotifyGap : 0.0f), 0.0f);
				Player.m_Name.Update(TextRender(), aBuf, FontSize, NameMaxWidth, TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END);
				Player.m_Name.Render(TextRender(), vec2(NameTextOffset, TextY), NameColor);
				if(pMusic)
				{
					const bool Listening = GameClient()->m_ClientIndicator.IsListeningTo(ClientData.m_aName);
					const CUIRect SpotifyRect = {NameTextOffset + Player.m_Name.Width() + SpotifyGap, Row.y + (Row.h - SpotifySize) / 2.0f, SpotifySize, SpotifySize};
					GameClient()->m_ClientIndicator.RenderSpotifyIcon(SpotifyRect.x, SpotifyRect.y, SpotifyRect.w, (pMusic->m_Playing || Listening ? 1.0f : 0.5f) * TextColor.a);
					if(m_MouseUnlocked)
					{
						const bool Local = GameClient()->m_aLocalIds[0] == pInfo->m_ClientId || (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == pInfo->m_ClientId);
						char aSong[256];
						CClientIndicator::FormatSong(*pMusic, aSong, sizeof(aSong));
						char aTooltip[384];
						if(Local || !pMusic->Joinable())
							str_format(aTooltip, sizeof(aTooltip), "♪ %s", aSong);
						else
							str_format(aTooltip, sizeof(aTooltip), "♪ %s\n%s", aSong, Listening ? Localize("Click to stop listening along") : Localize("Click to listen along"));
						GameClient()->m_Tooltips.DoToolTip(&Player.m_SpotifyButtonId, &SpotifyRect, aTooltip);
						if(!Local && pMusic->Joinable() && Ui()->DoButtonLogic(&Player.m_SpotifyButtonId, 0, &SpotifyRect, BUTTONFLAG_LEFT))
							GameClient()->m_ClientIndicator.ToggleListening(ClientData.m_aName);
					}
				}
				// 667client

				// ready / watching
				if(Client()->IsSixup() && Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_READY)
				{
					Player.m_ReadyMark.Update(TextRender(), "✓", FontSize);
					Player.m_ReadyMark.Render(TextRender(), vec2(NameTextOffset + Player.m_Name.Width(), TextY), ColorRGBA(0.1f, 1.0f, 0.1f, TextColor.a));
				}
			}

			// clan
			{
				ColorRGBA ClanColor = TextColor;
				if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(ClientData.m_aClan, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					ClanColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor));
				}
				// TClient
				if(pInfo->m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(pInfo->m_ClientId))
				{
					ClanColor = GameClient()->m_WarList.GetClanColor(pInfo->m_ClientId);
				}

				Player.m_Clan.Update(TextRender(), ClientData.m_aClan, FontSize, ClanLength, TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END);
				Player.m_Clan.Render(TextRender(), vec2(ClanOffset + (ClanLength - std::min(Player.m_Clan.Width(), ClanLength)) / 2.0f, TextY), ClanColor);
			}

			// country flag
			GameClient()->m_CountryFlags.Render(ClientData.m_Country, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f),
				CountryOffset, Row.y + (Spacing + TeeSizeMod * 5.0f) / 2.0f, CountryLength, Row.h - Spacing - TeeSizeMod * 5.0f);

			// ping
			ColorRGBA PingColor = TextRender()->DefaultTextColor();
			if(g_Config.m_ClEnablePingColor)
			{
				PingColor = color_cast<ColorRGBA>(ColorHSLA((300.0f - std::clamp(pInfo->m_Latency, 0, 300)) / 1000.0f, 1.0f, 0.5f));
			}
			str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Latency, 0, 999));
			Player.m_Ping.Update(TextRender(), aBuf, FontSize);
			Player.m_Ping.Render(TextRender(), vec2(PingOffset + PingLength - Player.m_Ping.Width(), TextY), PingColor);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(CountRendered == CountEnd)
				break;
		}
		if(CountRendered == CountEnd)
			break;
	}
}

void CScoreboard::RenderRecordingNotification(float x)
{
	char aBuf[512] = "";

	const auto &&AppendRecorderInfo = [&](int Recorder, const char *pName) {
		if(GameClient()->DemoRecorder(Recorder)->IsRecording())
		{
			char aTime[32];
			str_time((int64_t)GameClient()->DemoRecorder(Recorder)->Length() * 100, ETimeFormat::HOURS, aTime, sizeof(aTime));
			str_append(aBuf, pName);
			str_append(aBuf, " ");
			str_append(aBuf, aTime);
			str_append(aBuf, "  ");
		}
	};

	AppendRecorderInfo(RECORDER_MANUAL, Localize("Manual"));
	AppendRecorderInfo(RECORDER_RACE, Localize("Race"));
	AppendRecorderInfo(RECORDER_AUTO, Localize("Auto"));
	AppendRecorderInfo(RECORDER_REPLAYS, Localize("Replay"));

	if(aBuf[0] == '\0')
		return;

	const float FontSize = 10.0f;

	CUIRect Rect = {x, 0.0f, TextRender()->TextWidth(FontSize, aBuf) + 30.0f, 25.0f};
	Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 7.5f);
	Rect.VSplitLeft(10.0f, nullptr, &Rect);
	Rect.VSplitRight(5.0f, &Rect, nullptr);

	CUIRect Circle;
	Rect.VSplitLeft(10.0f, &Circle, &Rect);
	Circle.HMargin((Circle.h - Circle.w) / 2.0f, &Circle);
	Circle.Draw(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), IGraphics::CORNER_ALL, Circle.h / 2.0f);

	Rect.VSplitLeft(5.0f, nullptr, &Rect);
	Ui()->DoLabel(&Rect, aBuf, FontSize, TEXTALIGN_ML);
}

void CScoreboard::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!IsActive())
	{
		// lock mouse if scoreboard was opened by being dead or game pause
		if(m_MouseUnlocked)
		{
			LockMouse();
		}
		return;
	}

	// 667client
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideScoreboard)
	{
		if(m_MouseUnlocked)
			LockMouse();
		return;
	}
	// 667client

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	// 667client
	if(GameClient()->m_MusicDock.IsOpen())
	{
		if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
			Ui()->FinishCheck();
		return;
	}
	// 667client

	// if the score board is active, then we should clear the motd message as well
	if(GameClient()->m_Motd.IsActive())
		GameClient()->m_Motd.Clear();

	const CUIRect GlobalScreen = *Ui()->Screen(); // 667client
	Ui()->MapScreen();

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool Teams = GameClient()->IsTeamPlay();
	const auto &aTeamSize = GameClient()->m_Snap.m_aTeamSize;
	const int NumPlayers = Teams ? std::max(aTeamSize[TEAM_RED], aTeamSize[TEAM_BLUE]) : aTeamSize[TEAM_RED];

	const float ScoreboardSmallWidth = 375.0f + 10.0f;
	int NumScoreboardColumns = 1;
	if(Teams || (!Teams && NumPlayers > 16 && NumPlayers <= 64))
		NumScoreboardColumns = 2;
	else if(!Teams && NumPlayers > 64)
		NumScoreboardColumns = 3;
	const float ScoreboardWidthBase = !Teams && NumPlayers <= 16 ? ScoreboardSmallWidth : 750.0f;
	const float ScoreboardWidth = ScoreboardWidthBase;
	const float TitleHeight = 30.0f;

	const float UserScale = std::clamp(g_Config.m_ScScoreboardScale / 100.0f, 0.5f, 1.0f);
	const float HorizontalMargin = 40.0f;
	const float FitScale = ScoreboardWidth > 0.0f ? GlobalScreen.w / (ScoreboardWidth + HorizontalMargin * 2.0f) : UserScale;
	const float ScoreboardScale = std::clamp(std::min(UserScale, FitScale), 0.25f, 1.0f);
	const CUIRect Screen = {0.0f, 0.0f, GlobalScreen.w / ScoreboardScale, GlobalScreen.h / ScoreboardScale};
	Graphics()->MapScreenToSize(Screen.w, Screen.h);
	const vec2 RealMousePos = Ui()->MousePos();
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	Ui()->SetMousePos(Ui()->UpdatedMousePos() * vec2(Screen.w, Screen.h) / WindowSize);
	// 667client

	CUIRect Scoreboard = {(Screen.w - ScoreboardWidth) / 2.0f, 75.0f, ScoreboardWidth, 355.0f + TitleHeight};
	CScoreboardRenderState RenderState{};

	if(Teams)
	{
		const char *pRedTeamName = GetTeamName(TEAM_RED);
		const char *pBlueTeamName = GetTeamName(TEAM_BLUE);

		// Game over title
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if((pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && pGameDataObj)
		{
			char aTitle[256];
			if(pGameDataObj->m_TeamscoreRed > pGameDataObj->m_TeamscoreBlue)
			{
				TextRender()->TextColor(ColorRGBA(0.975f, 0.17f, 0.17f, 1.0f));
				if(pRedTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Red team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pRedTeamName);
				}
			}
			else if(pGameDataObj->m_TeamscoreBlue > pGameDataObj->m_TeamscoreRed)
			{
				TextRender()->TextColor(ColorRGBA(0.17f, 0.46f, 0.975f, 1.0f));
				if(pBlueTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Blue team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pBlueTeamName);
				}
			}
			else
			{
				TextRender()->TextColor(ColorRGBA(0.91f, 0.78f, 0.33f, 1.0f));
				str_copy(aTitle, Localize("Draw!"));
			}

			const float TitleFontSize = 36.0f;
			CUIRect GameOverTitle = {Scoreboard.x, Scoreboard.y - TitleFontSize - 6.0f, Scoreboard.w, TitleFontSize};
			Ui()->DoLabel(&GameOverTitle, aTitle, TitleFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		CUIRect RedScoreboard, BlueScoreboard, RedTitle, BlueTitle;
		Scoreboard.VSplitMid(&RedScoreboard, &BlueScoreboard, 7.5f);
		RedScoreboard.HSplitTop(TitleHeight, &RedTitle, &RedScoreboard);
		BlueScoreboard.HSplitTop(TitleHeight, &BlueTitle, &BlueScoreboard);

		RedTitle.Draw(ColorRGBA(0.975f, 0.17f, 0.17f, 0.5f), IGraphics::CORNER_T, 7.5f);
		BlueTitle.Draw(ColorRGBA(0.17f, 0.46f, 0.975f, 0.5f), IGraphics::CORNER_T, 7.5f);
		RedScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_B, 7.5f);
		BlueScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_B, 7.5f);

		RenderTitleBar(RedTitle, TEAM_RED, pRedTeamName == nullptr ? Localize("Red team") : pRedTeamName);
		RenderTitleBar(BlueTitle, TEAM_BLUE, pBlueTeamName == nullptr ? Localize("Blue team") : pBlueTeamName);

		auto RenderTeamScoreboard = [&](CUIRect TeamScoreboard, int Team, int TeamSize) {
			if(TeamSize <= 64)
			{
				RenderScoreboard(TeamScoreboard, Team, 0, TeamSize, RenderState);
			}
			else
			{
				const int FirstColumnSize = 64;
				CUIRect LeftColumn, RightColumn;
				TeamScoreboard.VSplitMid(&LeftColumn, &RightColumn, 2.5f);

				RenderScoreboard(LeftColumn, Team, 0, FirstColumnSize, RenderState, FirstColumnSize);
				RenderScoreboard(RightColumn, Team, FirstColumnSize, TeamSize, RenderState, FirstColumnSize);
			}
		};

		RenderTeamScoreboard(RedScoreboard, TEAM_RED, aTeamSize[TEAM_RED]);
		RenderTeamScoreboard(BlueScoreboard, TEAM_BLUE, aTeamSize[TEAM_BLUE]);
	}
	else
	{
		Scoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);

		const char *pTitle;
		if(pGameInfoObj && (pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			pTitle = Localize("Game over");
		}
		else
		{
			pTitle = GameClient()->Map()->BaseName();
		}

		CUIRect Title;
		Scoreboard.HSplitTop(TitleHeight, &Title, &Scoreboard);
		RenderTitleBar(Title, TEAM_GAME, pTitle);

		if(NumPlayers <= 16)
		{
			RenderScoreboard(Scoreboard, TEAM_GAME, 0, NumPlayers, RenderState);
		}
		else if(NumPlayers <= 64)
		{
			int PlayersPerSide;
			if(NumPlayers <= 24)
				PlayersPerSide = 12;
			else if(NumPlayers <= 32)
				PlayersPerSide = 16;
			else if(NumPlayers <= 48)
				PlayersPerSide = 24;
			else
				PlayersPerSide = 32;

			CUIRect LeftScoreboard, RightScoreboard;
			Scoreboard.VSplitMid(&LeftScoreboard, &RightScoreboard);
			RenderScoreboard(LeftScoreboard, TEAM_GAME, 0, PlayersPerSide, RenderState);
			RenderScoreboard(RightScoreboard, TEAM_GAME, PlayersPerSide, 2 * PlayersPerSide, RenderState);
		}
		else
		{
			const int NumColumns = 3;
			const int PlayersPerColumn = std::ceil(128.0f / NumColumns);
			CUIRect RemainingScoreboard = Scoreboard;
			for(int i = 0; i < NumColumns; ++i)
			{
				CUIRect Column;
				RemainingScoreboard.VSplitLeft(Scoreboard.w / NumColumns, &Column, &RemainingScoreboard);
				RenderScoreboard(Column, TEAM_GAME, i * PlayersPerColumn, (i + 1) * PlayersPerColumn, RenderState);
			}
		}
	}

	CUIRect Spectators = {(Screen.w - ScoreboardSmallWidth) / 2.0f, Scoreboard.y + Scoreboard.h + 5.0f, ScoreboardSmallWidth, 100.0f};
	if(pGameInfoObj && (pGameInfoObj->m_ScoreLimit || pGameInfoObj->m_TimeLimit || (pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)))
	{
		CUIRect Goals;
		Spectators.HSplitTop(25.0f, &Goals, &Spectators);
		Spectators.HSplitTop(5.0f, nullptr, &Spectators);
		RenderGoals(Goals);
	}
	RenderSpectators(Spectators);

	RenderRecordingNotification((Screen.w / 7) * 4 + 10);

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->RenderPopupMenus();

		if(m_MouseUnlocked)
			RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

		Ui()->FinishCheck();
	}

	// 667client
	Ui()->SetMousePos(RealMousePos);
	// 667client
}

bool CScoreboard::IsActive() const
{
	// if statboard is active don't show scoreboard
	if(GameClient()->m_Statboard.IsActive())
		return false;

	if(m_Active)
		return true;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(GameClient()->m_Snap.m_pLocalInfo && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// we are not a spectator, check if we are dead and the game isn't paused
		if(!GameClient()->m_Snap.m_pLocalCharacter && g_Config.m_ClScoreboardOnDeath &&
			!(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
			return true;
	}

	// if the game is over
	if(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
		return true;

	return false;
}

// 667client
bool CScoreboard::IsShown() const
{
	if(!IsActive())
		return false;
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideScoreboard)
		return false;
	return true;
}
// 667client

const char *CScoreboard::GetTeamName(int Team) const
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	int ClanPlayers = 0;
	const char *pClanName = nullptr;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
	{
		if(!pInfo || pInfo->m_Team != Team)
			continue;

		if(!pClanName)
		{
			pClanName = GameClient()->m_aClients[pInfo->m_ClientId].m_aClan;
			ClanPlayers++;
		}
		else
		{
			if(str_comp(GameClient()->m_aClients[pInfo->m_ClientId].m_aClan, pClanName) == 0)
				ClanPlayers++;
			else
				return nullptr;
		}
	}

	if(ClanPlayers > 1 && pClanName[0] != '\0')
		return pClanName;
	else
		return nullptr;
}

static constexpr float POPUP_MARGIN = 5.0f;
static constexpr float POPUP_ITEM_SPACING = 2.0f;
static constexpr float POPUP_HEADER_SIZE = 12.0f;
static constexpr float POPUP_ACTION_SIZE = 22.0f;
static constexpr float POPUP_BUTTON_SIZE = 15.5f;
static constexpr float POPUP_BUTTON_FONT = 10.0f;

float CScoreboard::CScoreboardPopupContext::Height(CGameClient *pGameClient, bool IsLocal, bool IsSpectatorRow, int ClientId)
{
	// 667client: the card replaces the name header
	return SixClientScoreboardPlayerPopupHeight(pGameClient, IsLocal, IsSpectatorRow, ClientId) - POPUP_HEADER_SIZE + CardHeight(pGameClient, ClientId);
}

// 667client
static constexpr float CARD_BANNER = 42.0f;
static constexpr float CARD_AVATAR = 52.0f;
static constexpr float CARD_RING = CARD_AVATAR + 6.0f;
static constexpr float CARD_PADDING = 8.0f;
static constexpr float CARD_ROW = 20.0f;
static constexpr float CARD_BIO_FONT = 10.0f;

// Cecen Client users have a profile on the presence service; everyone else gets the default card.
static const SSocialCard *PopupProfile(CGameClient *pGameClient, int ClientId)
{
	if(!pGameClient->m_ClientIndicator.IsUserInGame(ClientId))
		return nullptr;
	return CecenSocial::PlayerCard(pGameClient->Http(), pGameClient->m_aClients[ClientId].m_aName);
}

static float CardBioHeight(CGameClient *pGameClient, const SSocialCard &Card)
{
	if(Card.m_Bio.empty())
		return 0.0f;
	const float Width = POPUP_WIDTH - 2.0f * CARD_PADDING;
	return std::min(3.0f * CARD_BIO_FONT * 1.25f, pGameClient->TextRender()->TextBoundingBox(CARD_BIO_FONT, Card.m_Bio.c_str(), -1, Width).m_H) + 2.0f;
}

float CScoreboard::CScoreboardPopupContext::CardHeight(CGameClient *pGameClient, int ClientId)
{
	const CGameClient::CClientData &Client = pGameClient->m_aClients[ClientId];
	const SSocialCard *pCard = PopupProfile(pGameClient, ClientId);
	float Height = CARD_BANNER + CARD_RING / 2.0f + 4.0f; // banner and the lower half of the picture
	Height += 17.0f; // name
	if(Client.m_aClan[0])
		Height += 13.0f;
	Height += 16.0f; // points and hours, or "Private profile"
	if(pCard && !pCard->m_Private)
	{
		const float Bio = CardBioHeight(pGameClient, *pCard);
		if(Bio > 0.0f)
			Height += 6.0f + Bio;
		if(!pCard->m_vConnections.empty())
			Height += 6.0f + 12.0f + std::min<size_t>(pCard->m_vConnections.size(), 10) * CARD_ROW;
	}
	return Height + 2.0f;
}

void CScoreboard::CScoreboardPopupContext::RenderCard(CUIRect Card)
{
	CScoreboard *pScoreboard = m_pScoreboard;
	CGameClient *pGameClient = pScoreboard->GameClient();
	CUi *pUi = pScoreboard->Ui();
	IGraphics *pGraphics = pScoreboard->Graphics();
	ITextRender *pTextRender = pScoreboard->TextRender();
	const CGameClient::CClientData &Client = pGameClient->m_aClients[m_ClientId];
	const SSocialCard *pCard = PopupProfile(pGameClient, m_ClientId);

	auto &&Label = [&](float X, float Y, float W, float H, const char *pText, float Size, ColorRGBA Color) {
		CUIRect Rect = {X, Y, W, H};
		SLabelProperties Props;
		Props.m_MaxWidth = W;
		Props.m_EllipsisAtEnd = true;
		pTextRender->TextColor(Color);
		pUi->DoLabel(&Rect, pText, Size, TEXTALIGN_ML, Props);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
	};
	auto &&DrawTexture = [&](IGraphics::CTextureHandle Texture, float X, float Y, float Size, float Alpha) {
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		const IGraphics::CQuadItem Quad(X, Y, Size, Size);
		pGraphics->QuadsDrawTL(&Quad, 1);
		pGraphics->QuadsEnd();
	};

	CUIRect Banner;
	Card.HSplitTop(CARD_BANNER, &Banner, nullptr);
	Banner.Draw(CecenSocial::BannerColor(Client.m_aName), IGraphics::CORNER_T, 3.0f);

	// Picture, or the player's tee on a soft disc.
	const float RingX = Card.x + CARD_PADDING;
	const float RingY = Banner.y + Banner.h - CARD_RING / 2.0f;
	CUIRect Ring = {RingX, RingY, CARD_RING, CARD_RING};
	Ring.Draw(ColorRGBA(0.08f, 0.08f, 0.1f, 1.0f), IGraphics::CORNER_ALL, CARD_RING / 2.0f);
	CUIRect Avatar = {RingX + 3.0f, RingY + 3.0f, CARD_AVATAR, CARD_AVATAR};
	const IGraphics::CTextureHandle AvatarTexture = pCard && pCard->m_AvatarVersion > 0 ? CecenSocial::AvatarTexture(pGameClient->Http(), pGraphics, pCard->m_Name, pCard->m_AvatarVersion) : IGraphics::CTextureHandle();
	if(AvatarTexture.IsValid())
		DrawTexture(AvatarTexture, Avatar.x, Avatar.y, Avatar.w, 1.0f);
	else
	{
		Avatar.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, CARD_AVATAR / 2.0f);
		CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
		TeeInfo.m_Size = CARD_AVATAR * 0.8f;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
		pScoreboard->RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(Avatar.x + Avatar.w / 2.0f, Avatar.y + Avatar.h / 2.0f + OffsetToMid.y));
	}

	// Badges on the banner's lower edge, right of the picture: Cecen Client, Spotify.
	{
		float BadgeX = Card.x + Card.w - CARD_PADDING;
		const float BadgeY = Banner.y + Banner.h + 3.0f;
		const float BadgeH = 12.0f;
		const CClientIndicator::SMusic *pMusic = pGameClient->m_ClientIndicator.MusicInGame(m_ClientId);
		if(pMusic)
		{
			BadgeX -= BadgeH;
			pGameClient->m_ClientIndicator.RenderSpotifyIcon(BadgeX, BadgeY, BadgeH, pMusic->m_Playing ? 1.0f : 0.5f);
			BadgeX -= 5.0f;
		}
		if(pGameClient->m_ClientIndicator.IsUserInGame(m_ClientId))
		{
			BadgeX -= pGameClient->m_ClientIndicator.IconWidth(BadgeH);
			pGameClient->m_ClientIndicator.RenderIcon(BadgeX, BadgeY, BadgeH);
		}
	}

	const float X = Card.x + CARD_PADDING;
	const float Width = Card.w - 2.0f * CARD_PADDING;
	float Y = Banner.y + Banner.h + CARD_RING / 2.0f + 4.0f;
	{
		const unsigned Badges = pCard ? pCard->m_Badges : 0;
		const float BadgesW = CecenSocial::RenderBadges(pGameClient->Ui(), Badges, 0.0f, 0.0f, 15.0f, true);
		const float NameRoom = Width - (BadgesW > 0.0f ? BadgesW + 6.0f : 0.0f);
		Label(X, Y, NameRoom, 16.0f, Client.m_aName, 14.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		if(BadgesW > 0.0f)
		{
			const float NameW = std::min(NameRoom, pGameClient->TextRender()->TextWidth(14.0f, Client.m_aName));
			CecenSocial::RenderBadges(pGameClient->Ui(), Badges, X + NameW + 6.0f, Y + 0.5f, 15.0f);
		}
	}
	Y += 17.0f;
	if(Client.m_aClan[0])
	{
		Label(X, Y, Width, 12.0f, Client.m_aClan, 10.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f));
		Y += 13.0f;
	}
	if(pCard && pCard->m_Private)
	{
		Label(X, Y + 2.0f, Width, 12.0f, Localize("Private profile"), 9.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f));
		return;
	}

	// Points and hours, fetched when the card opens; a spinner until they arrive.
	{
		const SSocialStats *pStats = CecenSocial::PlayerStats(pGameClient->Http(), Client.m_aName);
		CKogPoints &KogPoints = pGameClient->m_KogPoints;
		const bool OnKog = KogPoints.OnKogServer();
		if(OnKog)
			KogPoints.Request(Client.m_aName, false);
		const CKogPoints::SEntry *pKog = OnKog ? KogPoints.Find(Client.m_aName) : nullptr;
		const float RowY = Y + 2.0f;
		if(!pStats || (OnKog && !pKog && KogPoints.IsQueued(Client.m_aName)))
		{
			SProgressSpinnerProperties Spinner;
			Spinner.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f);
			pUi->RenderProgressSpinner(vec2(X + 5.0f, RowY + 6.0f), 5.0f, Spinner);
			Label(X + 14.0f, RowY, Width - 14.0f, 12.0f, Localize("Loading stats…"), 9.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
		}
		else if(pStats->m_Private)
			Label(X, RowY, Width, 12.0f, Localize("Private profile"), 9.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f));
		else if(!pStats->m_Ok && !pKog)
			Label(X, RowY, Width, 12.0f, Localize("Stats unavailable"), 9.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f));
		else
		{
			char aPoints[64], aHours[64], aLine[160];
			if(pKog && pKog->m_Rank > 0)
				str_format(aPoints, sizeof(aPoints), "KoG %d %s · #%d", pKog->m_Points, Localize("pts"), pKog->m_Rank);
			else if(pKog)
				str_format(aPoints, sizeof(aPoints), "KoG %d %s", pKog->m_Points, Localize("pts"));
			else if(pStats->m_Rank > 0)
				str_format(aPoints, sizeof(aPoints), "%d %s · #%d", pStats->m_Points, Localize("pts"), pStats->m_Rank);
			else
				str_format(aPoints, sizeof(aPoints), "%d %s", pStats->m_Points, Localize("pts"));
			if(pStats->m_Hours >= 0)
				str_format(aHours, sizeof(aHours), "%d h", pStats->m_Hours);
			else if(pStats->m_HoursYear > 0)
				str_format(aHours, sizeof(aHours), "%d h %s", pStats->m_HoursYear, Localize("this year"));
			else
				aHours[0] = '\0';
			if(aHours[0])
				str_format(aLine, sizeof(aLine), "%s   ·   %s", aPoints, aHours);
			else
				str_copy(aLine, aPoints);
			Label(X, RowY, Width, 12.0f, aLine, 10.0f, ColorRGBA(1.0f, 0.86f, 0.45f, 0.95f));
		}
		Y += 16.0f;
	}
	if(!pCard)
		return;

	const float Bio = CardBioHeight(pGameClient, *pCard);
	if(Bio > 0.0f)
	{
		Y += 6.0f;
		CUIRect BioRect = {X, Y, Width, Bio};
		SLabelProperties Props;
		Props.m_MaxWidth = Width;
		pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
		pUi->DoLabel(&BioRect, pCard->m_Bio.c_str(), CARD_BIO_FONT, TEXTALIGN_TL, Props);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		Y += Bio;
	}
	if(pCard->m_vConnections.empty())
		return;

	Y += 6.0f;
	CUIRect Line = {X, Y - 3.0f, Width, 1.0f};
	Line.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_NONE, 0.0f);
	Label(X, Y, Width, 12.0f, Localize("Connections"), 9.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f));
	Y += 12.0f;
	for(size_t i = 0; i < pCard->m_vConnections.size() && i < std::size(m_aConnectionButtons); i++)
	{
		const SSocialConnection &Connection = pCard->m_vConnections[i];
		const SSocialKind *pKind = CecenSocial::FindKind(Connection.m_Kind);
		CUIRect Row = {X - 3.0f, Y, Width + 6.0f, CARD_ROW - 2.0f};
		const bool Hovered = pUi->HotItem() == &m_aConnectionButtons[i];
		if(Hovered)
			Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f), IGraphics::CORNER_ALL, 4.0f);
		const float LogoSize = 14.0f;
		const IGraphics::CTextureHandle Logo = CecenSocial::KindLogo(pGraphics, *pKind);
		if(Logo.IsValid())
			DrawTexture(Logo, X, Row.y + (Row.h - LogoSize) / 2.0f, LogoSize, 1.0f);
		const char *pName = Connection.m_Display.empty() ? Connection.m_Value.c_str() : Connection.m_Display.c_str();
		const float TextX = X + LogoSize + 6.0f;
		const float MaxTextWidth = Width - LogoSize - 6.0f - 14.0f;
		const float TextWidth = std::min(MaxTextWidth, pTextRender->TextWidth(10.5f, pName));
		Label(TextX, Row.y, MaxTextWidth, Row.h, pName, 10.5f, ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 1.0f : 0.9f));
		CUIRect Arrow = {TextX + TextWidth + 4.0f, Row.y, 10.0f, Row.h};
		pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
		pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		pTextRender->TextColor(1.0f, 1.0f, 1.0f, Hovered ? 0.9f : 0.45f);
		pUi->DoLabel(&Arrow, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 7.0f, TEXTALIGN_ML);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		pTextRender->SetRenderFlags(0);
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(pUi->DoButtonLogic(&m_aConnectionButtons[i], 0, &Row, BUTTONFLAG_LEFT) && (str_startswith(Connection.m_Url.c_str(), "https://") || str_startswith(Connection.m_Url.c_str(), "http://")))
			pScoreboard->Client()->ViewLink(Connection.m_Url.c_str());
		Y += CARD_ROW;
	}
}
// 667client

static void EscapeConsoleString(char *pBuf, int BufSize, const char *pPrefix, const char *pText, const char *pSuffix)
{
	str_copy(pBuf, pPrefix, BufSize);
	char *pDst = pBuf + str_length(pBuf);
	str_escape(&pDst, pText, pBuf + BufSize);
	str_append(pBuf, pSuffix, BufSize);
}

CUi::EPopupMenuFunctionResult CScoreboard::CScoreboardPopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CScoreboardPopupContext *pPopupContext = static_cast<CScoreboardPopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;
	CUi *pUi = pPopupContext->m_pScoreboard->Ui();
	CGameClient *pGameClient = pScoreboard->GameClient();
	IConsole *pConsole = pScoreboard->Console();

	CGameClient::CClientData &Client = pGameClient->m_aClients[pPopupContext->m_ClientId];

	if(!Client.m_Active)
		return CUi::POPUP_CLOSE_CURRENT;

	// 667client: the profile card replaces the name header. Its size changes once the
	// profile arrives, so the popup follows it.
	const float WantedHeight = Height(pGameClient, pPopupContext->m_IsLocal, pPopupContext->m_IsSpectating, pPopupContext->m_ClientId);
	if(std::abs(WantedHeight - pPopupContext->m_LastHeight) > 0.5f)
	{
		pPopupContext->m_LastHeight = WantedHeight;
		pUi->SetPopupMenuHeight(pPopupContext, WantedHeight);
	}
	CUIRect CardRect;
	View.HSplitTop(CardHeight(pGameClient, pPopupContext->m_ClientId), &CardRect, &View);
	pPopupContext->RenderCard(CardRect);

	View.Margin(POPUP_MARGIN, &View);

	CUIRect Container, Action;

	auto SayNamed = [&](const char *pCommand) {
		char aEscapedCommand[2 * MAX_NAME_LENGTH + 64];
		str_format(aEscapedCommand, sizeof(aEscapedCommand), "say %s \"", pCommand);
		char *pDst = aEscapedCommand + str_length(aEscapedCommand);
		str_escape(&pDst, Client.m_aName, aEscapedCommand + sizeof(aEscapedCommand));
		str_append(aEscapedCommand, "\"");
		pConsole->ExecuteLine(aEscapedCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	};

	// 667client: every action is a small icon in one centred row: friend, mute, mute emoticons,
	// spectate, teammate, whisper, copy skin. The war list and /swap buttons are gone.
	if(!pPopupContext->m_IsLocal)
	{
		const int ActionCorners = IGraphics::CORNER_ALL;
		const float Gap = 4.0f;
		const int NumIcons = 7;
		const float RowWidth = NumIcons * POPUP_ACTION_SIZE + (NumIcons - 1) * Gap;
		View.HSplitTop(POPUP_ITEM_SPACING * 2, nullptr, &View);
		View.HSplitTop(POPUP_ACTION_SIZE, &Container, &View);
		Container.VSplitLeft(std::max(0.0f, (Container.w - RowWidth) / 2.0f), nullptr, &Container);
		auto &&NextAction = [&]() {
			Container.VSplitLeft(POPUP_ACTION_SIZE, &Action, &Container);
			Container.VSplitLeft(Gap, nullptr, &Container);
		};

		NextAction();
		ColorRGBA FriendActionColor = Client.m_Friend ? ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction)) :
								ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction));
		const char *pFriendActionIcon = pUi->HotItem() == &pPopupContext->m_FriendAction && Client.m_Friend ? FontIcon::HEART_CRACK : FontIcon::HEART;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_FriendAction, pFriendActionIcon, Client.m_Friend, &Action, BUTTONFLAG_LEFT, ActionCorners, true, FriendActionColor))
		{
			if(Client.m_Friend)
				pGameClient->Friends()->RemoveFriend(Client.m_aName, Client.m_aClan);
			else
				pGameClient->Friends()->AddFriend(Client.m_aName, Client.m_aClan);
		}
		pGameClient->m_Tooltips.DoToolTip(&pPopupContext->m_FriendAction, &Action, Client.m_Friend ? Localize("Remove friend") : Localize("Add friend"));

		NextAction();
		if(pUi->DoButton_FontIcon(&pPopupContext->m_MuteAction, FontIcon::BAN, Client.m_ChatIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
			Client.m_ChatIgnore ^= 1;
		pGameClient->m_Tooltips.DoToolTip(&pPopupContext->m_MuteAction, &Action, Client.m_ChatIgnore ? Localize("Unmute") : Localize("Mute"));

		NextAction();
		const char *pEmoticonActionIcon = Client.m_EmoticonIgnore ? FontIcon::COMMENT_SLASH : FontIcon::COMMENT;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_EmoticonAction, pEmoticonActionIcon, Client.m_EmoticonIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
			Client.m_EmoticonIgnore ^= 1;
		pGameClient->m_Tooltips.DoToolTip(&pPopupContext->m_EmoticonAction, &Action, Client.m_EmoticonIgnore ? Localize("Unmute emoticons") : Localize("Mute emoticons"));

		NextAction();
		const bool IsSpectating = pGameClient->m_Snap.m_SpecInfo.m_Active && pGameClient->m_Snap.m_SpecInfo.m_SpectatorId == pPopupContext->m_ClientId;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_SpectateButton, IsSpectating ? FontIcon::EYE_SLASH : FontIcon::EYE, IsSpectating, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			if(IsSpectating)
			{
				pGameClient->m_Spectator.Spectate(SPEC_FREEVIEW);
				pConsole->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
			}
			else if(pGameClient->m_Snap.m_SpecInfo.m_Active)
			{
				pGameClient->m_Spectator.Spectate(pPopupContext->m_ClientId);
			}
			else
			{
				char aCommand[2 * MAX_NAME_LENGTH + 32];
				EscapeConsoleString(aCommand, sizeof(aCommand), "say /spec \"", Client.m_aName, "\"");
				pConsole->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
			}
		}
		pGameClient->m_Tooltips.DoToolTip(&pPopupContext->m_SpectateButton, &Action, IsSpectating ? Localize("Stop spectating") : Localize("Spectate"));

		NextAction();
		const CWarDataCache &WarData = pGameClient->m_WarList.GetWarData(pPopupContext->m_ClientId);
		const bool IsInTeamList = WarData.m_WarGroupMatches.size() > 2 && WarData.m_WarGroupMatches[2];
		const bool TeamHot = pUi->HotItem() == &pPopupContext->m_TeamAction;
		ColorRGBA TeamActionColor = TeamHot ? (IsInTeamList ? ColorRGBA(1.0f, 0.42f, 0.42f, 0.8f * pUi->ButtonColorMul(&pPopupContext->m_TeamAction)) : ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f * pUi->ButtonColorMul(&pPopupContext->m_TeamAction))) : (IsInTeamList ? ColorRGBA(0.53f, 0.78f, 0.53f, 0.8f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f));
		if(pUi->DoButton_FontIcon(&pPopupContext->m_TeamAction, FontIcon::ICON_USERS, IsInTeamList, &Action, BUTTONFLAG_LEFT, ActionCorners, true, TeamActionColor))
		{
			if(IsInTeamList)
				pGameClient->m_WarList.RemoveWarEntryInGame(2, Client.m_aName, false);
			else
				pGameClient->m_WarList.AddWarEntryInGame(2, Client.m_aName, "", false);
		}
		pGameClient->m_Tooltips.DoToolTip(&pPopupContext->m_TeamAction, &Action, IsInTeamList ? Localize("Remove from teammates") : Localize("Add to teammates"));

		NextAction();
		if(pUi->DoButton_FontIcon(&pPopupContext->m_aQuickActions[0], FontIcon::PAPER_PLANE, 0, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			char aCommand[256];
			EscapeConsoleString(aCommand, sizeof(aCommand), "chat all \"/w ", Client.m_aName, " \"");
			pConsole->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
			return CUi::POPUP_CLOSE_CURRENT;
		}
		pGameClient->m_Tooltips.DoToolTip(&pPopupContext->m_aQuickActions[0], &Action, Localize("Whisper"));

		NextAction();
		if(pUi->DoButton_FontIcon(&pPopupContext->m_aQuickActions[1], FontIcon::SHIRT, 0, &Action, BUTTONFLAG_LEFT, ActionCorners))
			pGameClient->m_SixClient.CopyPlayerSkin(pPopupContext->m_ClientId);
		pGameClient->m_Tooltips.DoToolTip(&pPopupContext->m_aQuickActions[1], &Action, Localize("Copy Skin"));
	}
	View.HSplitTop(POPUP_ITEM_SPACING * 2, nullptr, &View);

	// 667client: Spotify song this player shares
	const CClientIndicator::SMusic *pMusic = pGameClient->m_ClientIndicator.MusicInGame(pPopupContext->m_ClientId);
	if(!pPopupContext->m_IsLocal && pMusic)
	{
		char aSong[256];
		CClientIndicator::FormatSong(*pMusic, aSong, sizeof(aSong));
		View.HSplitTop(POPUP_ITEM_SPACING * 2, nullptr, &View);
		View.HSplitTop(POPUP_BUTTON_SIZE, &Container, &View);
		CUIRect Icon, Song;
		Container.VSplitLeft(Container.h, &Icon, &Song);
		Song.VSplitLeft(3.0f, nullptr, &Song);
		pGameClient->m_ClientIndicator.RenderSpotifyIcon(Icon.x + 1.0f, Icon.y + 1.0f, Icon.h - 2.0f);
		SLabelProperties SongProps;
		SongProps.m_MaxWidth = Song.w;
		SongProps.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Song, aSong, POPUP_BUTTON_FONT, TEXTALIGN_ML, SongProps);

		View.HSplitTop(POPUP_ITEM_SPACING * 2, nullptr, &View);
		View.HSplitTop(POPUP_BUTTON_SIZE, &Container, &View);
		const bool Listening = pGameClient->m_ClientIndicator.IsListeningTo(Client.m_aName);
		const char *pLabel = !pMusic->Joinable() ? Localize("Can't listen along (no track link)") : (Listening ? Localize("Stop listening along") : Localize("Listen along"));
		const ColorRGBA ListenColor = pUi->HotItem() == &pPopupContext->m_ListenButton ? ColorRGBA(0.16f, 0.72f, 0.36f, 0.85f) : ColorRGBA(0.12f, 0.6f, 0.3f, pMusic->Joinable() ? 0.8f : 0.35f);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ListenButton, pLabel, &Container, POPUP_BUTTON_FONT, TEXTALIGN_MC, 0.0f, false, true, ListenColor) && pMusic->Joinable())
			pGameClient->m_ClientIndicator.ToggleListening(Client.m_aName);
	}
	// 667client

	const int LocalId = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
	const int LocalTeam = LocalId >= 0 ? pGameClient->m_Teams.Team(LocalId) : TEAM_FLOCK;
	const int TargetTeam = pGameClient->m_Teams.Team(pPopupContext->m_ClientId);
	const bool LocalInTeam = LocalTeam != TEAM_FLOCK && LocalTeam != TEAM_SUPER;
	const bool TargetInTeam = TargetTeam != TEAM_FLOCK && TargetTeam != TEAM_SUPER;
	const bool LocalIsTarget = LocalId == pPopupContext->m_ClientId;
	if(LocalInTeam || TargetInTeam)
	{
		View.HSplitTop(POPUP_ITEM_SPACING * 4, nullptr, &View);
		auto AddTeamButton = [&](CButtonContainer *pButton, const char *pLabel, auto &&OnClick) {
			View.HSplitTop(POPUP_ITEM_SPACING * 2, nullptr, &View);
			View.HSplitTop(POPUP_BUTTON_SIZE, &Container, &View);
			const ColorRGBA ButtonColor = pUi->HotItem() == pButton ? ColorRGBA(0.31f, 0.52f, 0.78f, 0.8f) : ColorRGBA(0.32f, 0.32f, 0.72f, 0.8f);
			if(pUi->DoButton_PopupMenu(pButton, pLabel, &Container, POPUP_BUTTON_FONT, TEXTALIGN_MC, 0.0f, false, true, ButtonColor))
				OnClick();
		};

		if(LocalInTeam && LocalTeam == TargetTeam)
			AddTeamButton(&pPopupContext->m_TeamExitButton, Localize("Exit"), [&]() { pConsole->ExecuteLine("say /team 0", IConsole::CLIENT_ID_UNSPECIFIED); });
		if(TargetInTeam && LocalTeam != TargetTeam)
		{
			AddTeamButton(&pPopupContext->m_TeamJoinButton, Localize("Join"), [&]() {
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "say /team %d", TargetTeam);
				pConsole->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			});
		}
		if(LocalInTeam && TargetTeam != LocalTeam)
			AddTeamButton(&pPopupContext->m_TeamInviteButton, Localize("Invite"), [&]() { SayNamed("/invite"); });
		if(!LocalIsTarget && LocalInTeam && TargetTeam == LocalTeam)
			AddTeamButton(&pPopupContext->m_TeamKickButton, Localize("Kick"), [&]() { pGameClient->m_Voting.CallvoteKick(pPopupContext->m_ClientId, ""); });
		if(LocalInTeam && LocalTeam == TargetTeam)
			AddTeamButton(&pPopupContext->m_TeamLockButton, Localize("Lock"), [&]() { pConsole->ExecuteLine("say /lock", IConsole::CLIENT_ID_UNSPECIFIED); });
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CScoreboard::CMapTitlePopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CMapTitlePopupContext *pPopupContext = static_cast<CMapTitlePopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;

	pScoreboard->TextRender()->Text(View.x, View.y, pPopupContext->m_FontSize, pScoreboard->GameClient()->m_aMapDescription, View.w);

	return CUi::POPUP_KEEP_OPEN;
}
