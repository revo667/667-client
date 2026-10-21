/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-26a05e33 */

#include "aim_trainer.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <generated/client_data.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr int FLICK_TARGETS = 30;
	constexpr int PSA_TARGETS = 10;
	constexpr int PSA_ROUNDS = 7;
	constexpr float TIMED_SECONDS = 30.0f;
	constexpr int TIMED_VISIBLE = 3;
	constexpr float HUD_HEIGHT = 70.0f;

	const ColorRGBA ACCENT(0.98f, 0.36f, 0.42f, 1.0f);
	const ColorRGBA GOOD(0.45f, 0.9f, 0.55f, 1.0f);
	const ColorRGBA TEXT_DIM(1.0f, 1.0f, 1.0f, 0.6f);

	float Seconds(int64_t Ticks)
	{
		return Ticks / (float)time_freq();
	}

	float EaseOutBack(float t)
	{
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.0f;
		return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
	}
}

void CAimTrainer::OnConsoleInit()
{
	Console()->Register("sc_aim_trainer", "?i[mode]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		CAimTrainer *pSelf = static_cast<CAimTrainer *>(pUserData);
		if(pSelf->m_Active)
			pSelf->Deactivate();
		else
			pSelf->Activate(pResult->NumArguments() > 0 ? pResult->GetInteger(0) : -1);
	}, this, "Open the aim trainer (0 = flick, 1 = timed, 2 = find my sensitivity)");
}

void CAimTrainer::OnShutdown()
{
	RestorePsaSens();
}

int *CAimTrainer::SensConfig() const
{
	if(m_PsaSensSaved)
		return m_PsaDyncam ? &g_Config.m_ClDyncamMousesens : &g_Config.m_InpMousesens;
	if(g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens)
		return &g_Config.m_ClDyncamMousesens;
	return &g_Config.m_InpMousesens;
}

void CAimTrainer::ApplyPsaTestSens()
{
	if(!m_PsaSensSaved)
		return;
	*SensConfig() = std::max(1, round_to_int(m_PsaTestingHigh ? m_PsaHigh : m_PsaLow));
}

void CAimTrainer::RestorePsaSens()
{
	if(!m_PsaSensSaved)
		return;
	*SensConfig() = m_PsaOriginalSens;
	m_PsaSensSaved = false;
}

float CAimTrainer::BaseSensitivity() const
{
	if(g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens)
		return (float)g_Config.m_ClDyncamMousesens;
	return (float)g_Config.m_InpMousesens;
}

float CAimTrainer::ActiveSensitivity() const
{
	return BaseSensitivity();
}

void CAimTrainer::UpdateWorldSize()
{
	float Zoom = GameClient()->m_Camera.m_Zoom;
	if(!(Zoom > 0.0f))
		Zoom = 1.0f;
	float Width, Height;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), Zoom, &Width, &Height);
	m_World = vec2(Width, Height);
}

void CAimTrainer::EnterReady()
{
	m_State = STATE_READY;
	m_ReadyTime = time_get();
	m_vTargets.clear();
	if(m_Mode == MODE_PSA)
		ApplyPsaTestSens();
}

void CAimTrainer::Activate(int Mode)
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->Echo("Join a server first, the aim trainer runs on top of the game.");
		return;
	}
	GameClient()->m_Menus.SetActive(false);
	RestorePsaSens();
	m_Active = true;
	m_Random.seed((unsigned)time_get());
	UpdateWorldSize();
	m_Cursor = m_World / 2.0f;
	m_vTargets.clear();
	m_vEffects.clear();
	m_PendingClick = false;
	if(Mode == MODE_PSA)
	{
		m_Mode = MODE_PSA;
		m_PsaRound = 0;
		m_PsaTestingHigh = false;
		m_PsaDyncam = g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens;
		m_PsaOriginalSens = m_PsaDyncam ? g_Config.m_ClDyncamMousesens : g_Config.m_InpMousesens;
		m_PsaSensSaved = true;
		m_PsaLow = std::max(1.0f, m_PsaOriginalSens * 0.5f);
		m_PsaHigh = std::max(2.0f, m_PsaOriginalSens * 1.5f);
		m_PsaLastScoreA = -1.0f;
		m_PsaLastScoreB = -1.0f;
	}
	else
		m_Mode = Mode == MODE_TIMED ? MODE_TIMED : MODE_FLICK;
	EnterReady();
}

void CAimTrainer::Deactivate()
{
	RestorePsaSens();
	m_Active = false;
	m_vTargets.clear();
	m_vEffects.clear();
}

bool CAimTrainer::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;
	const float Factor = ActiveSensitivity() / 100.0f;
	m_Cursor += vec2(x, y) * Factor;
	m_Cursor.x = std::clamp(m_Cursor.x, 0.0f, m_World.x);
	m_Cursor.y = std::clamp(m_Cursor.y, 0.0f, m_World.y);
	return true;
}

bool CAimTrainer::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;
	if(Event.m_Flags & IInput::FLAG_RELEASE && !(Event.m_Flags & IInput::FLAG_PRESS))
		return false;
	if(!(Event.m_Flags & IInput::FLAG_PRESS))
		return true;
	if(Event.m_Key == KEY_ESCAPE)
	{
		Deactivate();
		return true;
	}
	if(Event.m_Key == KEY_R && m_State == STATE_RESULTS)
	{
		Activate(m_Mode);
		return true;
	}
	if((Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER) && (m_State == STATE_RESULTS || m_State == STATE_PSA_RESULT))
	{
		if(m_State == STATE_PSA_RESULT)
			*SensConfig() = std::max(1, round_to_int(m_PsaRecommended));
		Deactivate();
		return true;
	}
	if(Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2)
		m_PendingClick = true;
	return true;
}

vec2 CAimTrainer::RandomTargetPos() const
{
	const float Margin = TargetRadius() + 30.0f;
	std::uniform_real_distribution<float> DistX(Margin, std::max(Margin + 1.0f, m_World.x - Margin));
	std::uniform_real_distribution<float> DistY(HUD_HEIGHT + Margin, std::max(HUD_HEIGHT + Margin + 1.0f, m_World.y - Margin));
	std::mt19937 &Random = const_cast<std::mt19937 &>(m_Random);
	for(int Attempt = 0; Attempt < 30; Attempt++)
	{
		const vec2 Pos(DistX(Random), DistY(Random));
		bool Overlaps = false;
		for(const STarget &Target : m_vTargets)
			if(distance(Target.m_Pos, Pos) < TargetRadius() * 3.0f)
				Overlaps = true;
		if(m_Mode == MODE_PSA && Pos.x < 320.0f + TargetRadius() && Pos.y < 190.0f + TargetRadius())
			continue;
		if(!Overlaps && distance(Pos, m_Cursor) > TargetRadius() * 3.0f)
			return Pos;
	}
	return vec2(DistX(Random), DistY(Random));
}

void CAimTrainer::StartTrial()
{
	m_vTargets.clear();
	m_vEffects.clear();
	m_Current = STrialResult();
	m_ReactionSum = 0.0f;
	m_TrialStart = time_get();
	m_Cursor = m_World / 2.0f;
	const int Visible = m_Mode == MODE_TIMED ? TIMED_VISIBLE : 1;
	m_TargetsLeft = m_Mode == MODE_FLICK ? FLICK_TARGETS : (m_Mode == MODE_PSA ? PSA_TARGETS : 0);
	for(int i = 0; i < Visible; i++)
		m_vTargets.push_back({RandomTargetPos(), m_TrialStart});
	m_State = STATE_PLAYING;
}

void CAimTrainer::FinishTrial()
{
	m_Current.m_Seconds = Seconds(time_get() - m_TrialStart);
	m_Current.m_AvgReactionMs = m_Current.m_Hits > 0 ? m_ReactionSum / m_Current.m_Hits * 1000.0f : 0.0f;
	m_vTargets.clear();

	if(m_Mode == MODE_PSA)
	{
		if(!m_PsaTestingHigh)
		{
			m_PsaLowResult = m_Current;
			m_PsaTestingHigh = true;
			EnterReady();
			return;
		}
		m_PsaLastScoreA = m_PsaLowResult.Score();
		m_PsaLastScoreB = m_Current.Score();
		const float Middle = (m_PsaLow + m_PsaHigh) / 2.0f;
		if(m_PsaLowResult.Score() >= m_Current.Score())
			m_PsaHigh = Middle;
		else
			m_PsaLow = Middle;
		m_PsaTestingHigh = false;
		m_PsaRound++;
		if(m_PsaRound >= PSA_ROUNDS)
		{
			m_PsaRecommended = std::round((m_PsaLow + m_PsaHigh) / 2.0f);
			RestorePsaSens();
			m_State = STATE_PSA_RESULT;
		}
		else
			EnterReady();
		return;
	}

	m_LastResult = m_Current;
	m_NewBest = false;
	if(m_Mode == MODE_FLICK && m_Current.m_Hits > 0)
	{
		const int AvgMs = round_to_int(m_Current.m_Seconds * 1000.0f / m_Current.m_Hits);
		if(g_Config.m_ScAimBestFlickMs == 0 || AvgMs < g_Config.m_ScAimBestFlickMs)
		{
			g_Config.m_ScAimBestFlickMs = AvgMs;
			m_NewBest = true;
		}
	}
	else if(m_Mode == MODE_TIMED && m_Current.m_Hits > g_Config.m_ScAimBestTimed)
	{
		g_Config.m_ScAimBestTimed = m_Current.m_Hits;
		m_NewBest = true;
	}
	m_State = STATE_RESULTS;
}

void CAimTrainer::HandleClick()
{
	m_ClickTime = time_get();
	GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_HOOK_ATTACH_GROUND, 1.0f);
	if(m_State != STATE_PLAYING)
		return;
	m_Current.m_Clicks++;
	for(size_t i = 0; i < m_vTargets.size(); i++)
	{
		STarget &Target = m_vTargets[i];
		if(distance(Target.m_Pos, m_Cursor) > TargetRadius())
			continue;
		const float Reaction = Seconds(m_ClickTime - Target.m_SpawnTime);
		m_ReactionSum += Reaction;
		m_Current.m_Hits++;
		SEffect Effect{true, Target.m_Pos, m_ClickTime, ""};
		str_format(Effect.m_aText, sizeof(Effect.m_aText), "%d ms", round_to_int(Reaction * 1000.0f));
		m_vEffects.push_back(Effect);
		m_vTargets.erase(m_vTargets.begin() + i);
		if(m_Mode == MODE_TIMED)
			m_vTargets.push_back({RandomTargetPos(), m_ClickTime});
		else if(--m_TargetsLeft > 0)
			m_vTargets.push_back({RandomTargetPos(), m_ClickTime});
		else
			FinishTrial();
		return;
	}
	m_vEffects.push_back({false, m_Cursor, m_ClickTime, ""});
}

void CAimTrainer::DrawRing(vec2 Center, float Inner, float Outer, ColorRGBA Color, int Segments)
{
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);
	for(int i = 0; i < Segments; i++)
	{
		const float a1 = 2.0f * pi * i / Segments;
		const float a2 = 2.0f * pi * (i + 1) / Segments;
		const vec2 d1(std::cos(a1), std::sin(a1));
		const vec2 d2(std::cos(a2), std::sin(a2));
		const IGraphics::CFreeformItem Item(
			Center.x + d1.x * Inner, Center.y + d1.y * Inner,
			Center.x + d1.x * Outer, Center.y + d1.y * Outer,
			Center.x + d2.x * Inner, Center.y + d2.y * Inner,
			Center.x + d2.x * Outer, Center.y + d2.y * Outer);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}
	Graphics()->QuadsEnd();
}

void CAimTrainer::DrawDisc(vec2 Center, float Radius, ColorRGBA Color, int Segments)
{
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);
	Graphics()->DrawCircle(Center.x, Center.y, Radius, Segments);
	Graphics()->QuadsEnd();
}

void CAimTrainer::DrawTextCentered(float X, float Y, float Size, const char *pText, ColorRGBA Color)
{
	TextRender()->TextColor(Color);
	TextRender()->Text(X - TextRender()->TextWidth(Size, pText) / 2.0f, Y, Size, pText);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CAimTrainer::Button(float CenterX, float Y, float Width, const char *pText, bool Clicked, bool Primary)
{
	const float Height = 44.0f;
	const CUIRect Rect = {CenterX - Width / 2.0f, Y, Width, Height};
	const bool Hovered = m_Cursor.x >= Rect.x && m_Cursor.x <= Rect.x + Rect.w && m_Cursor.y >= Rect.y && m_Cursor.y <= Rect.y + Rect.h;
	ColorRGBA Color = Primary ? ACCENT : ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f);
	if(Hovered)
		Color = Primary ? ColorRGBA(1.0f, 0.48f, 0.53f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.2f);
	Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Color, IGraphics::CORNER_ALL, 10.0f);
	DrawTextCentered(CenterX, Y + Height / 2.0f - 11.0f, 20.0f, pText, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	return Hovered && Clicked;
}

void CAimTrainer::RenderBackground()
{
	const float Alpha = m_State == STATE_PLAYING ? 0.3f : 0.55f;
	Graphics()->DrawRect(0.0f, 0.0f, m_World.x, m_World.y, ColorRGBA(0.02f, 0.03f, 0.05f, Alpha), IGraphics::CORNER_NONE, 0.0f);
}

void CAimTrainer::RenderTargets()
{
	const int64_t Now = time_get();
	const float R = TargetRadius();

	CTeeRenderInfo TeeInfo;
	TeeInfo.Apply(GameClient()->m_Skins.Find("default"));
	TeeInfo.ApplyColors(false, 0, 0);

	for(const STarget &Target : m_vTargets)
	{
		const float Age = std::clamp(Seconds(Now - Target.m_SpawnTime) / 0.14f, 0.0f, 1.0f);
		const float Scale = EaseOutBack(Age);
		if(Scale <= 0.01f)
			continue;
		const bool Hovered = distance(Target.m_Pos, m_Cursor) <= R;

		TeeInfo.m_Size = R * 2.0f * Scale;
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
		const vec2 Delta = m_Cursor - Target.m_Pos;
		const vec2 Dir = length(Delta) > 0.001f ? normalize(Delta) : vec2(1.0f, 0.0f);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, Hovered ? EMOTE_SURPRISE : EMOTE_NORMAL, Dir, Target.m_Pos + OffsetToMid);
	}
}

void CAimTrainer::RenderEffects()
{
	const int64_t Now = time_get();
	const float R = TargetRadius();
	for(auto It = m_vEffects.begin(); It != m_vEffects.end();)
	{
		const float Age = Seconds(Now - It->m_Time);
		const float Duration = It->m_Hit ? 0.6f : 0.35f;
		if(Age > Duration)
		{
			It = m_vEffects.erase(It);
			continue;
		}
		const float t = Age / Duration;
		if(It->m_Hit)
		{
			const float Ring = R * (1.0f + t * 1.2f);
			DrawRing(It->m_Pos, Ring - 4.0f * (1.0f - t) - 1.0f, Ring, GOOD.WithAlpha(1.0f - t));
			for(int i = 0; i < 10; i++)
			{
				const float Angle = 2.0f * pi * i / 10.0f + 0.3f;
				const vec2 Dir(std::cos(Angle), std::sin(Angle));
				DrawDisc(It->m_Pos + Dir * (R * 0.6f + t * R * 1.6f), 4.0f * (1.0f - t), ColorRGBA(1.0f, 0.8f, 0.85f, 1.0f - t), 12);
			}
			DrawTextCentered(It->m_Pos.x, It->m_Pos.y - R - 26.0f - t * 30.0f, 18.0f, It->m_aText, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f - t));
		}
		else
		{
			const float Size = 10.0f + t * 4.0f;
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 0.35f, 0.35f, 1.0f - t);
			Graphics()->QuadsSetRotation(pi / 4.0f);
			IGraphics::CQuadItem aLines[] = {
				IGraphics::CQuadItem(It->m_Pos.x, It->m_Pos.y, Size * 2.0f, 3.0f),
				IGraphics::CQuadItem(It->m_Pos.x, It->m_Pos.y, 3.0f, Size * 2.0f)};
			Graphics()->QuadsDraw(aLines, 2);
			Graphics()->QuadsSetRotation(0.0f);
			Graphics()->QuadsEnd();
		}
		++It;
	}
}

void CAimTrainer::RenderCrosshair()
{
	const float Pulse = std::clamp(1.0f - Seconds(time_get() - m_ClickTime) / 0.12f, 0.0f, 1.0f);
	float Zoom = GameClient()->m_Camera.m_Zoom;
	if(!(Zoom > 0.0f))
		Zoom = 1.0f;
	const float CursorScale = std::max(g_Config.m_TcCursorScale, 1) / 100.0f;

	int Weapon = WEAPON_HAMMER;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId >= 0 && LocalId < MAX_CLIENTS)
		Weapon = std::clamp(GameClient()->m_aClients[LocalId].m_Predicted.m_ActiveWeapon, 0, NUM_WEAPONS - 1);

	float ScaleX, ScaleY;
	Graphics()->GetSpriteScale(g_pData->m_Weapons.m_aId[Weapon].m_pSpriteCursor, ScaleX, ScaleY);
	const float Size = 64.0f * Zoom * CursorScale * (1.0f - Pulse * 0.15f);

	Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[Weapon]);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem Quad(m_Cursor.x, m_Cursor.y, Size * ScaleX, Size * ScaleY);
	Graphics()->QuadsDraw(&Quad, 1);
	Graphics()->QuadsEnd();
}

void CAimTrainer::RenderHud()
{
	Graphics()->DrawRect(0.0f, 0.0f, m_World.x, HUD_HEIGHT - 14.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_NONE, 0.0f);
	const float Elapsed = Seconds(time_get() - m_TrialStart);
	char aLeft[96], aMiddle[96], aRight[96];
	if(m_Mode == MODE_TIMED)
		str_format(aMiddle, sizeof(aMiddle), "%.1f s", std::max(0.0f, TIMED_SECONDS - Elapsed));
	else
		str_format(aMiddle, sizeof(aMiddle), "%d / %d", (m_Mode == MODE_FLICK ? FLICK_TARGETS : PSA_TARGETS) - m_TargetsLeft, m_Mode == MODE_FLICK ? FLICK_TARGETS : PSA_TARGETS);
	if(m_Mode == MODE_PSA)
		aLeft[0] = '\0';
	else
		str_copy(aLeft, m_Mode == MODE_FLICK ? "Flick" : "Timed", sizeof(aLeft));
	str_format(aRight, sizeof(aRight), "Hits %d  ·  Accuracy %.0f%%", m_Current.m_Hits, m_Current.Accuracy() * 100.0f);
	TextRender()->TextColor(TEXT_DIM);
	TextRender()->Text(24.0f, 16.0f, 20.0f, aLeft);
	TextRender()->Text(m_World.x - 24.0f - TextRender()->TextWidth(20.0f, aRight), 16.0f, 20.0f, aRight);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	DrawTextCentered(m_World.x / 2.0f, 12.0f, 26.0f, aMiddle, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	DrawTextCentered(m_World.x / 2.0f, m_World.y - 34.0f, 14.0f, "Esc to stop", TEXT_DIM);
}

void CAimTrainer::RenderReady()
{
	const float CX = m_World.x / 2.0f;
	const float CY = m_World.y / 2.0f;
	const float Elapsed = Seconds(time_get() - m_ReadyTime);
	if(Elapsed >= 3.0f)
	{
		StartTrial();
		return;
	}
	char aTitle[96], aSub[160], aCount[8];
	if(m_Mode == MODE_PSA)
	{
		str_format(aTitle, sizeof(aTitle), "Round %d of %d  ·  %s", m_PsaRound + 1, PSA_ROUNDS, m_PsaTestingHigh ? "B" : "A");
		str_format(aSub, sizeof(aSub), "Sensitivity %.0f  ·  hit %d targets fast and clean", ActiveSensitivity(), PSA_TARGETS);
	}
	else if(m_Mode == MODE_FLICK)
	{
		str_copy(aTitle, "Flick", sizeof(aTitle));
		str_format(aSub, sizeof(aSub), "Hit %d targets one after another", FLICK_TARGETS);
	}
	else
	{
		str_copy(aTitle, "Timed", sizeof(aTitle));
		str_format(aSub, sizeof(aSub), "Hit as many targets as you can in %.0f seconds", TIMED_SECONDS);
	}
	const int Count = 3 - (int)Elapsed;
	const float Pulse = 1.0f - (Elapsed - (int)Elapsed);
	str_format(aCount, sizeof(aCount), "%d", Count);
	DrawTextCentered(CX, CY - 130.0f, 34.0f, aTitle, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	DrawTextCentered(CX, CY - 82.0f, 18.0f, aSub, TEXT_DIM);
	DrawTextCentered(CX, CY - 40.0f - Pulse * 6.0f, 72.0f + Pulse * 12.0f, aCount, ACCENT.WithAlpha(0.4f + 0.6f * Pulse));
	DrawTextCentered(CX, CY + 80.0f, 18.0f, "Left or right click to shoot", ColorRGBA(1.0f, 1.0f, 1.0f, 0.85f));
	DrawTextCentered(CX, CY + 110.0f, 14.0f, "Esc to stop", TEXT_DIM);
}

void CAimTrainer::RenderResults(bool Clicked)
{
	const float CX = m_World.x / 2.0f;
	float Y = m_World.y * 0.25f;
	const STrialResult &R = m_LastResult;
	DrawTextCentered(CX, Y, 44.0f, m_NewBest ? "New best!" : "Results", m_NewBest ? GOOD : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	char aLine[128];
	Y += 80.0f;
	if(m_Mode == MODE_FLICK)
	{
		str_format(aLine, sizeof(aLine), "%.2f s total  ·  %d ms per target", R.m_Seconds, R.m_Hits ? round_to_int(R.m_Seconds * 1000.0f / R.m_Hits) : 0);
		DrawTextCentered(CX, Y, 26.0f, aLine, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}
	else
	{
		str_format(aLine, sizeof(aLine), "%d hits  ·  %.2f per second", R.m_Hits, R.m_Seconds > 0.0f ? R.m_Hits / R.m_Seconds : 0.0f);
		DrawTextCentered(CX, Y, 26.0f, aLine, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}
	str_format(aLine, sizeof(aLine), "Accuracy %.0f%% (%d / %d clicks)  ·  avg reaction %.0f ms", R.Accuracy() * 100.0f, R.m_Hits, R.m_Clicks, R.m_AvgReactionMs);
	DrawTextCentered(CX, Y + 44.0f, 18.0f, aLine, TEXT_DIM);
	if(Button(CX - 130.0f, Y + 110.0f, 240.0f, "Again (R)", Clicked, true))
	{
		Activate(m_Mode);
		return;
	}
	if(Button(CX + 130.0f, Y + 110.0f, 240.0f, "Close (Esc)", Clicked))
		Deactivate();
}

void CAimTrainer::RenderPsaPanel()
{
	const float X = 20.0f;
	const float Y = 20.0f;
	const float W = 300.0f;
	const float Line = 24.0f;
	const float H = 16.0f + Line * 6.0f + 10.0f;
	Graphics()->DrawRect(X, Y, W, H, ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 10.0f);

	char aLine[128];
	float LineY = Y + 10.0f;
	auto Row = [&](const char *pText, float Size, ColorRGBA Color) {
		TextRender()->TextColor(Color);
		TextRender()->Text(X + 14.0f, LineY, Size, pText);
		LineY += Line;
	};
	Row("Sensitivity test (PSA)", 18.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	str_format(aLine, sizeof(aLine), "Testing: %d  (%s)", std::max(1, round_to_int(m_PsaTestingHigh ? m_PsaHigh : m_PsaLow)), m_PsaTestingHigh ? "B" : "A");
	Row(aLine, 20.0f, ACCENT);
	str_format(aLine, sizeof(aLine), "Round %d / %d", std::min(m_PsaRound + 1, PSA_ROUNDS), PSA_ROUNDS);
	Row(aLine, 16.0f, TEXT_DIM);
	str_format(aLine, sizeof(aLine), "A: %d   B: %d", std::max(1, round_to_int(m_PsaLow)), std::max(1, round_to_int(m_PsaHigh)));
	Row(aLine, 16.0f, TEXT_DIM);
	if(m_PsaLastScoreA >= 0.0f)
		str_format(aLine, sizeof(aLine), "Last round: A %.2f  ·  B %.2f", m_PsaLastScoreA, m_PsaLastScoreB);
	else
		str_copy(aLine, "Last round: -", sizeof(aLine));
	Row(aLine, 16.0f, TEXT_DIM);
	str_format(aLine, sizeof(aLine), "Your sensitivity: %d  (Esc restores it)", m_PsaOriginalSens);
	Row(aLine, 14.0f, TEXT_DIM);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CAimTrainer::RenderPsaResult(bool Clicked)
{
	const float CX = m_World.x / 2.0f;
	float Y = m_World.y * 0.25f;
	DrawTextCentered(CX, Y, 40.0f, "Found your sensitivity", ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	char aLine[128];
	str_format(aLine, sizeof(aLine), "%.0f", m_PsaRecommended);
	DrawTextCentered(CX, Y + 70.0f, 64.0f, aLine, GOOD);
	str_format(aLine, sizeof(aLine), "Current %.0f  ·  narrowed down over %d rounds", BaseSensitivity(), PSA_ROUNDS);
	DrawTextCentered(CX, Y + 160.0f, 18.0f, aLine, TEXT_DIM);
	char aApply[64];
	str_format(aApply, sizeof(aApply), "Apply %.0f (Enter)", m_PsaRecommended);
	if(Button(CX - 130.0f, Y + 220.0f, 240.0f, aApply, Clicked, true))
	{
		*SensConfig() = std::max(1, round_to_int(m_PsaRecommended));
		Deactivate();
		return;
	}
	str_format(aLine, sizeof(aLine), "Keep %.0f (Esc)", BaseSensitivity());
	if(Button(CX + 130.0f, Y + 220.0f, 240.0f, aLine, Clicked))
		Deactivate();
}

void CAimTrainer::OnRender()
{
	if(!m_Active)
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Deactivate();
		return;
	}
	UpdateWorldSize();
	Graphics()->MapScreenToSize(m_World.x, m_World.y);

	const bool Clicked = m_PendingClick;
	m_PendingClick = false;
	if(Clicked)
		HandleClick();

	if(m_State == STATE_PLAYING && m_Mode == MODE_TIMED && Seconds(time_get() - m_TrialStart) >= TIMED_SECONDS)
		FinishTrial();

	RenderBackground();
	if(m_State == STATE_PLAYING)
	{
		RenderTargets();
		RenderEffects();
		RenderHud();
	}
	else
	{
		RenderEffects();
		if(m_State == STATE_READY)
			RenderReady();
		else if(m_State == STATE_RESULTS)
			RenderResults(Clicked);
		else if(m_State == STATE_PSA_RESULT)
			RenderPsaResult(Clicked);
	}
	if(m_Mode == MODE_PSA && (m_State == STATE_READY || m_State == STATE_PLAYING))
		RenderPsaPanel();
	RenderCrosshair();
}
