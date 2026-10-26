/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-ad7d01f7 */

#include "music_dock.h"

#include "music_player.h"
#include "system_volume.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/hud_layout.h>
#include <game/client/components/sixclient/spotify_connect.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

static constexpr float PANEL_WIDTH = 340.0f;
static constexpr float PANEL_HEIGHT = 247.0f;
static constexpr float PANEL_PADDING = 12.0f;
static constexpr float ART_SIZE = 92.0f;
static constexpr float LYRICS_HEIGHT = 58.0f;
static constexpr float ACTIONS_HEIGHT = 46.0f;
static constexpr float ACTION_ICON_SIZE = 30.0f;
static constexpr float ACTION_GAP = 8.0f;
static constexpr float CLOSE_DELAY = 0.18f;

static float s_VolumeBeforeMute = 0.5f;

static void FormatTrackTime(int64_t Ms, char *pBuf, int BufSize)
{
	const int64_t Seconds = std::max<int64_t>(0, Ms / 1000);
	str_format(pBuf, BufSize, "%d:%02d", (int)(Seconds / 60), (int)(Seconds % 60));
}

static bool Inside(const CUIRect &Rect, vec2 Pos, float Margin = 0.0f)
{
	return Pos.x >= Rect.x - Margin && Pos.x <= Rect.x + Rect.w + Margin && Pos.y >= Rect.y - Margin && Pos.y <= Rect.y + Rect.h + Margin;
}

void CMusicDock::OnReset()
{
	m_Open = 0.0f;
	m_OpenVelocity = 0.0f;
	m_WantOpen = false;
	m_DraggingVolume = false;
	for(float &Scale : m_aIconScale)
		Scale = 1.0f;
	for(float &Hover : m_aControlHover)
		Hover = 0.0f;
}

vec2 CMusicDock::UiMousePos() const
{
	const CUIRect UiScreen = *Ui()->Screen();
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	return vec2(UiScreen.x, UiScreen.y) + Ui()->UpdatedMousePos() * vec2(UiScreen.w, UiScreen.h) / WindowSize;
}

bool CMusicDock::InteractionAllowed() const
{
	return GameClient()->m_Chat.IsActive() || GameClient()->m_Scoreboard.IsMouseUnlocked();
}

void CMusicDock::UpdateOpenState(bool Hovered, float Delta)
{
	const int64_t Now = time_get();
	if(Hovered)
		m_LastInsideTick = Now;
	m_WantOpen = Hovered || (m_WantOpen && (Now - m_LastInsideTick) < (int64_t)(time_freq() * CLOSE_DELAY));

	const float Target = m_WantOpen ? 1.0f : 0.0f;
	const float Stiffness = m_WantOpen ? 240.0f : 320.0f;
	const float Damping = m_WantOpen ? 20.0f : 34.0f;
	const int Steps = std::max(1, (int)std::ceil(Delta / (1.0f / 240.0f)));
	const float Step = Delta / Steps;
	for(int i = 0; i < Steps; i++)
	{
		const float Acceleration = Stiffness * (Target - m_Open) - Damping * m_OpenVelocity;
		m_OpenVelocity += Acceleration * Step;
		m_Open += m_OpenVelocity * Step;
	}
	m_Open = std::clamp(m_Open, 0.0f, 1.2f);
	if(!m_WantOpen && m_Open < 0.002f)
	{
		m_Open = 0.0f;
		m_OpenVelocity = 0.0f;
	}
}

void CMusicDock::UpdateVolume(bool Open)
{
	const int64_t Now = time_get();
	if(Open && !m_DraggingVolume && Now - m_LastVolumeRead > time_freq() / 4)
	{
		m_LastVolumeRead = Now;
		CSpotifyConnect::SState SpotifyState;
		m_VolumeFromSpotify = g_Config.m_ScSpotifyConnect && GameClient()->m_MusicPlayer.SpotifyConnect().Ready() && GameClient()->m_MusicPlayer.SpotifyConnect().GetState(SpotifyState) && SpotifyState.m_Active;
		if(m_VolumeFromSpotify)
		{
			m_VolumeAvailable = true;
			m_Volume = std::clamp(SpotifyState.m_Volume / 65535.0f, 0.0f, 1.0f);
		}
		else
		{
			float Volume;
			m_VolumeAvailable = SystemVolumeGet(Volume);
			if(m_VolumeAvailable)
				m_Volume = Volume;
		}
	}
}

void CMusicDock::RunAction(int Action)
{
	switch(Action)
	{
	case ACTION_SETTINGS:
		GameClient()->m_Menus.SetActive(true);
		break;
	case ACTION_KILL:
		Console()->ExecuteLine("kill", IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	case ACTION_DUMMY:
		if(Client()->DummyConnected())
			Console()->ExecuteLine("toggle cl_dummy 0 1", IConsole::CLIENT_ID_UNSPECIFIED);
		else if(!Client()->DummyConnecting())
			Console()->ExecuteLine("dummy_connect", IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	case ACTION_SCREENSHOT:
		Console()->ExecuteLine("screenshot", IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	case ACTION_REPLAY:
		Console()->ExecuteLine("save_replay", IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	case ACTION_RECONNECT:
	{
		char aAddress[512];
		str_copy(aAddress, Client()->ConnectAddressString());
		if(aAddress[0] != '\0')
			Client()->Connect(aAddress);
		break;
	}
	case ACTION_DISCONNECT:
		Client()->Disconnect();
		break;
	case ACTION_QUIT:
		if(g_Config.m_ScConfirmQuit || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
		{
			GameClient()->m_Menus.SetActive(true);
			GameClient()->m_Menus.ShowQuitPopup();
		}
		else
		{
			Client()->Quit();
		}
		break;
	}
}

void CMusicDock::RenderIcon(const CUIRect &Rect, const char *pIcon, float Size, ColorRGBA Color)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(Color);
	Ui()->DoLabel(&Rect, pIcon, Size, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CMusicDock::RenderPanel(const CUIRect &Panel, float Scale, float Alpha, vec2 Mouse, bool Clicked)
{
	CMusicPlayer::SDockInfo Info;
	GameClient()->m_MusicPlayer.GetDockInfo(Info);
	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);

	const ColorRGBA Base = Info.m_Valid ? Info.m_PaletteDark : ColorRGBA(0.07f, 0.08f, 0.09f, 1.0f);
	const ColorRGBA Accent = Info.m_Valid ? Info.m_Accent : ColorRGBA(0.55f, 0.6f, 0.95f, 1.0f);
	CUIRect Shadow = Panel;
	Shadow.y += 4.0f * Scale;
	Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f * Alpha), IGraphics::CORNER_ALL, 16.0f * Scale);
	Panel.Draw(ColorRGBA(Base.r * 0.85f, Base.g * 0.85f, Base.b * 0.85f, 0.94f * Alpha), IGraphics::CORNER_ALL, 16.0f * Scale);

	CUIRect Content;
	Panel.Margin(PANEL_PADDING * Scale, &Content);

	CUIRect Header, Art, Details;
	Content.HSplitTop(ART_SIZE * Scale, &Header, &Content);
	Header.VSplitLeft(ART_SIZE * Scale, &Art, &Details);
	Details.VSplitLeft(12.0f * Scale, nullptr, &Details);

	if(Info.m_HasArt)
	{
		Graphics()->TextureSet(Info.m_ArtTexture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
		const IGraphics::CQuadItem Quad(Art.x, Art.y, Art.w, Art.h);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
		Graphics()->TextureClear();
	}
	else
	{
		Art.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha), IGraphics::CORNER_ALL, 10.0f * Scale);
		RenderIcon(Art, FontIcon::MUSIC, 30.0f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f * Alpha));
	}

	SLabelProperties Props;
	Props.m_MaxWidth = Details.w;
	Props.m_EllipsisAtEnd = true;

	CUIRect Line;
	Details.HSplitTop(2.0f * Scale, nullptr, &Details);
	Details.HSplitTop(17.0f * Scale, &Line, &Details);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	Ui()->DoLabel(&Line, Info.m_Valid ? Info.m_Title.c_str() : Localize("Nothing playing"), 14.0f * Scale, TEXTALIGN_ML, Props);
	Details.HSplitTop(14.0f * Scale, &Line, &Details);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.65f * Alpha);
	Ui()->DoLabel(&Line, Info.m_Valid ? Info.m_Artist.c_str() : Localize("Start music in Spotify, Apple Music or your browser"), 10.5f * Scale, TEXTALIGN_ML, Props);

	CUIRect Progress, Times;
	Details.HSplitTop(9.0f * Scale, nullptr, &Details);
	Details.HSplitTop(4.0f * Scale, &Progress, &Details);
	Progress.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f * Alpha), IGraphics::CORNER_ALL, 2.0f * Scale);
	if(Info.m_DurationMs > 0)
	{
		CUIRect Fill = Progress;
		Fill.w *= std::clamp((float)Info.m_PositionMs / (float)Info.m_DurationMs, 0.0f, 1.0f);
		Fill.Draw(Accent.WithAlpha(Alpha), IGraphics::CORNER_ALL, 2.0f * Scale);
	}
	Details.HSplitTop(3.0f * Scale, nullptr, &Details);
	Details.HSplitTop(11.0f * Scale, &Times, &Details);
	if(Info.m_Valid)
	{
		char aElapsed[16], aRemaining[24], aDuration[16];
		FormatTrackTime(Info.m_PositionMs, aElapsed, sizeof(aElapsed));
		FormatTrackTime(std::max<int64_t>(0, Info.m_DurationMs - Info.m_PositionMs), aDuration, sizeof(aDuration));
		str_format(aRemaining, sizeof(aRemaining), "-%s", aDuration);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f * Alpha);
		Ui()->DoLabel(&Times, aElapsed, 8.5f * Scale, TEXTALIGN_ML);
		Ui()->DoLabel(&Times, aRemaining, 8.5f * Scale, TEXTALIGN_MR);
	}

	CUIRect Controls, VolumeArea;
	Details.HSplitTop(4.0f * Scale, nullptr, &Details);
	Details.HSplitTop(24.0f * Scale, &Controls, &Details);
	Controls.VSplitLeft(96.0f * Scale, &Controls, &VolumeArea);
	const char *apControlIcons[] = {FontIcon::BACKWARD_STEP, Info.m_Playing ? FontIcon::PAUSE : FontIcon::PLAY, FontIcon::FORWARD_STEP};
	const bool aControlEnabled[] = {Info.m_CanPrev, Info.m_CanPlayPause, Info.m_CanNext};
	const float aControlSize[] = {13.0f, 17.0f, 13.0f};
	for(int i = 0; i < 3; i++)
	{
		CUIRect Button;
		Controls.VSplitLeft(32.0f * Scale, &Button, &Controls);
		const bool Hovered = aControlEnabled[i] && Inside(Button, Mouse);
		m_aControlHover[i] = std::clamp(m_aControlHover[i] + (Hovered ? 10.0f : -8.0f) * Delta, 0.0f, 1.0f);
		if(m_aControlHover[i] > 0.0f)
			Button.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * m_aControlHover[i] * Alpha), IGraphics::CORNER_ALL, 12.0f * Scale);
		RenderIcon(Button, apControlIcons[i], aControlSize[i] * Scale * (1.0f + 0.12f * m_aControlHover[i]), ColorRGBA(1.0f, 1.0f, 1.0f, (aControlEnabled[i] ? 0.95f : 0.3f) * Alpha));
		if(Hovered && Clicked)
		{
			if(i == 0)
				GameClient()->m_MusicPlayer.DockPrevious();
			else if(i == 1)
				GameClient()->m_MusicPlayer.DockPlayPause();
			else
				GameClient()->m_MusicPlayer.DockNext();
		}
	}

	CUIRect VolumeIcon, VolumeBar;
	VolumeArea.VSplitLeft(8.0f * Scale, nullptr, &VolumeArea);
	VolumeArea.VSplitLeft(18.0f * Scale, &VolumeIcon, &VolumeBar);
	VolumeBar.VSplitLeft(6.0f * Scale, nullptr, &VolumeBar);
	VolumeBar.HMargin((VolumeBar.h - 4.0f * Scale) * 0.5f, &VolumeBar);
	const char *pVolumeIcon = m_Volume <= 0.001f ? FontIcon::VOLUME_XMARK : (m_Volume < 0.5f ? FontIcon::VOLUME_LOW : FontIcon::VOLUME_HIGH);
	RenderIcon(VolumeIcon, pVolumeIcon, 11.0f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, (m_VolumeAvailable ? 0.85f : 0.3f) * Alpha));
	auto &&ApplyVolume = [&](float Volume) {
		m_Volume = std::clamp(Volume, 0.0f, 1.0f);
		if(m_VolumeFromSpotify)
			GameClient()->m_MusicPlayer.SpotifyConnect().SetVolume(m_Volume);
		else
			SystemVolumeSet(m_Volume);
	};
	if(m_VolumeAvailable && Clicked && Inside(VolumeIcon, Mouse))
	{
		if(m_Volume > 0.001f)
		{
			s_VolumeBeforeMute = m_Volume;
			ApplyVolume(0.0f);
		}
		else
			ApplyVolume(std::max(0.05f, s_VolumeBeforeMute));
	}

	VolumeBar.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f * Alpha), IGraphics::CORNER_ALL, 2.0f * Scale);
	CUIRect VolumeFill = VolumeBar;
	VolumeFill.w *= std::clamp(m_Volume, 0.0f, 1.0f);
	VolumeFill.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, (m_VolumeAvailable ? 0.8f : 0.25f) * Alpha), IGraphics::CORNER_ALL, 2.0f * Scale);
	const float KnobSize = (m_DraggingVolume || Inside(VolumeBar, Mouse, 6.0f * Scale) ? 9.0f : 7.0f) * Scale;
	CUIRect Knob = {VolumeBar.x + VolumeFill.w - KnobSize * 0.5f, VolumeBar.y + VolumeBar.h * 0.5f - KnobSize * 0.5f, KnobSize, KnobSize};
	if(m_VolumeAvailable)
		Knob.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), IGraphics::CORNER_ALL, KnobSize * 0.5f);

	if(m_VolumeAvailable)
	{
		if(Clicked && Inside(VolumeBar, Mouse, 6.0f * Scale))
			m_DraggingVolume = true;
		const bool MouseHeld = Ui()->MouseButton(0) || Input()->KeyIsPressed(KEY_MOUSE_1);
		if(m_DraggingVolume)
		{
			const float NextVolume = std::clamp((Mouse.x - VolumeBar.x) / std::max(1.0f, VolumeBar.w), 0.0f, 1.0f);
			const int64_t Now = time_get();
			if(!MouseHeld || Now - m_LastVolumeWrite > time_freq() / 25)
			{
				m_LastVolumeWrite = Now;
				ApplyVolume(NextVolume);
			}
			else
				m_Volume = NextVolume;
			if(!MouseHeld)
				m_DraggingVolume = false;
		}
	}

	CUIRect Lyrics;
	Content.HSplitTop(10.0f * Scale, nullptr, &Content);
	Content.HSplitTop(LYRICS_HEIGHT * Scale, &Lyrics, &Content);
	SLabelProperties LyricsProps;
	LyricsProps.m_MaxWidth = Lyrics.w;
	LyricsProps.m_EllipsisAtEnd = true;
	CUIRect PrevLine, CurrentLine, NextLine;
	Lyrics.HSplitTop(16.0f * Scale, &PrevLine, &Lyrics);
	Lyrics.HSplitTop(24.0f * Scale, &CurrentLine, &NextLine);
	if(Info.m_Valid && Info.m_Lyrics.m_HasLines)
	{
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.4f * Alpha);
		Ui()->DoLabel(&PrevLine, Info.m_Lyrics.m_Previous.c_str(), 10.0f * Scale, TEXTALIGN_MC, LyricsProps);
		const char *pCurrent = Info.m_Lyrics.m_Current.empty() ? "♪" : Info.m_Lyrics.m_Current.c_str();
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
		Ui()->DoLabel(&CurrentLine, pCurrent, 14.0f * Scale, TEXTALIGN_MC, LyricsProps);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.4f * Alpha);
		Ui()->DoLabel(&NextLine, Info.m_Lyrics.m_Next.c_str(), 10.0f * Scale, TEXTALIGN_MC, LyricsProps);
	}
	else
	{
		const char *pState = Localize("Lyrics appear here while a song is playing");
		if(!g_Config.m_ScMusicPlayerShowLyrics)
			pState = Localize("Lyrics are disabled in the music player settings");
		else if(Info.m_Valid && Info.m_LyricsState == CMusicPlayerLyrics::EDisplayState::Loading)
			pState = Localize("Loading lyrics…");
		else if(Info.m_Valid && Info.m_LyricsState == CMusicPlayerLyrics::EDisplayState::NotFound)
			pState = Localize("No synced lyrics for this song");
		else if(Info.m_Valid && Info.m_LyricsState == CMusicPlayerLyrics::EDisplayState::Offline)
			pState = Localize("Lyrics are offline");
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.45f * Alpha);
		Ui()->DoLabel(&CurrentLine, pState, 10.5f * Scale, TEXTALIGN_MC, LyricsProps);
	}

	CUIRect Separator;
	Content.HSplitTop(8.0f * Scale, nullptr, &Content);
	Content.HSplitTop(1.0f * Scale, &Separator, &Content);
	Separator.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f * Alpha), IGraphics::CORNER_NONE, 0.0f);
	Content.HSplitTop(8.0f * Scale, nullptr, &Content);

	CUIRect Actions;
	Content.HSplitTop(ACTIONS_HEIGHT * Scale, &Actions, &Content);

	const bool DummyConnected = Client()->DummyConnected();
	const char *apIcons[NUM_ACTIONS] = {FontIcon::GEAR, FontIcon::SKULL, FontIcon::USER_GROUP, FontIcon::CAMERA, FontIcon::FLOPPY_DISK, FontIcon::ARROWS_ROTATE, FontIcon::RIGHT_FROM_BRACKET, FontIcon::POWER_OFF};
	const char *apLabels[NUM_ACTIONS] = {
		Localize("Settings"),
		Localize("Kill"),
		DummyConnected ? Localize("Switch dummy") : Localize("Connect dummy"),
		Localize("Screenshot"),
		Localize("Save replay"),
		Localize("Reconnect"),
		Localize("Disconnect"),
		Localize("Quit"),
	};
	const bool aEnabled[NUM_ACTIONS] = {true, true, true, true, g_Config.m_ClReplays != 0, true, true, true};

	const float BaseSize = ACTION_ICON_SIZE * Scale;
	const float Gap = ACTION_GAP * Scale;
	const float BaseTotal = (float)NUM_ACTIONS * BaseSize + (float)(NUM_ACTIONS - 1) * Gap;
	const float BaseStart = Actions.x + (Actions.w - BaseTotal) * 0.5f;
	const bool MouseNearRow = Mouse.y >= Actions.y - 18.0f * Scale && Mouse.y <= Actions.y + Actions.h + 6.0f * Scale;
	float Total = (float)(NUM_ACTIONS - 1) * Gap;
	for(int i = 0; i < NUM_ACTIONS; i++)
	{
		const float BaseCenter = BaseStart + i * (BaseSize + Gap) + BaseSize * 0.5f;
		const float Distance = (Mouse.x - BaseCenter) / (48.0f * Scale);
		const float Target = MouseNearRow ? 1.0f + 0.5f * std::exp(-Distance * Distance) : 1.0f;
		m_aIconScale[i] += (Target - m_aIconScale[i]) * std::clamp(Delta * 16.0f, 0.0f, 1.0f);
		Total += BaseSize * m_aIconScale[i];
	}

	float X = Actions.x + (Actions.w - Total) * 0.5f;
	const float Bottom = Actions.y + Actions.h;
	for(int i = 0; i < NUM_ACTIONS; i++)
	{
		const float Size = BaseSize * m_aIconScale[i];
		CUIRect Icon = {X, Bottom - Size, Size, Size};
		X += Size + Gap;

		const bool Hovered = Inside(Icon, Mouse);
		ColorRGBA Background = i == ACTION_QUIT ? ColorRGBA(0.85f, 0.25f, 0.25f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		Background.a = (Hovered ? 0.28f : 0.14f) * (aEnabled[i] ? 1.0f : 0.5f) * Alpha;
		Icon.Draw(Background, IGraphics::CORNER_ALL, Size * 0.3f);
		RenderIcon(Icon, apIcons[i], Size * 0.46f, ColorRGBA(1.0f, 1.0f, 1.0f, (aEnabled[i] ? 0.95f : 0.35f) * Alpha));

		if(Hovered)
		{
			const float LabelSize = 9.0f * Scale;
			const float LabelWidth = TextRender()->TextWidth(LabelSize, apLabels[i]) + 12.0f * Scale;
			CUIRect Label = {Icon.x + Icon.w * 0.5f - LabelWidth * 0.5f, Icon.y - 18.0f * Scale, LabelWidth, 14.0f * Scale};
			Label.x = std::clamp(Label.x, Panel.x + 4.0f * Scale, Panel.x + Panel.w - Label.w - 4.0f * Scale);
			Label.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.7f * Alpha), IGraphics::CORNER_ALL, 7.0f * Scale);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
			Ui()->DoLabel(&Label, apLabels[i], LabelSize, TEXTALIGN_MC);
			if(Clicked && aEnabled[i])
				RunAction(i);
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CMusicDock::OnRender()
{
	const bool Enabled = g_Config.m_ScMusicDock && g_Config.m_ScMusicPlayer && Client()->State() == IClient::STATE_ONLINE && !GameClient()->m_Menus.IsActive();
	if(!Enabled)
	{
		OnReset();
		return;
	}

	Ui()->MapScreen();
	const CUIRect UiScreen = *Ui()->Screen();
	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	const vec2 Mouse = UiMousePos();
	const bool Allowed = InteractionAllowed();

	CMusicPlayer::SDockInfo Info;
	GameClient()->m_MusicPlayer.GetDockInfo(Info);
	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	CUIRect Anchor;
	if(Info.m_HudVisible && Info.m_HudRect.w > 0.0f)
	{
		Anchor = Info.m_HudRect;
	}
	else
	{
		const auto Layout = HudLayout::Get(HudLayout::MODULE_MUSIC_PLAYER, HudWidth, HudHeight);
		Anchor = {Layout.m_X, Layout.m_Y, 60.0f, 12.0f};
	}
	Anchor.x = UiScreen.x + Anchor.x * UiScreen.w / HudWidth;
	Anchor.y = UiScreen.y + Anchor.y * UiScreen.h / HudHeight;
	Anchor.w *= UiScreen.w / HudWidth;
	Anchor.h *= UiScreen.h / HudHeight;

	const float OpenT = std::clamp(m_Open, 0.0f, 1.2f);
	const float Scale = 0.78f + 0.22f * OpenT;
	const float CenterX = std::clamp(Anchor.x + Anchor.w * 0.5f, UiScreen.x + PANEL_WIDTH * 0.5f + 6.0f, UiScreen.x + UiScreen.w - PANEL_WIDTH * 0.5f - 6.0f);
	CUIRect Panel;
	Panel.w = PANEL_WIDTH * Scale;
	Panel.h = PANEL_HEIGHT * Scale;
	Panel.x = CenterX - Panel.w * 0.5f;
	Panel.y = UiScreen.y + 6.0f - (1.0f - std::min(OpenT, 1.0f)) * 24.0f;

	bool Hovered = false;
	if(Allowed)
	{
		const bool OnAnchor = Inside(Anchor, Mouse, 6.0f);
		const bool OnTopEdge = Mouse.y <= UiScreen.y + 10.0f && std::abs(Mouse.x - (Anchor.x + Anchor.w * 0.5f)) <= std::max(90.0f, Anchor.w * 0.5f + 20.0f);
		const bool OnPanel = m_Open > 0.05f && Inside(Panel, Mouse, 14.0f);
		Hovered = OnAnchor || OnTopEdge || OnPanel || m_DraggingVolume;
	}
	UpdateOpenState(Hovered, Delta);
	UpdateVolume(m_Open > 0.05f);

	if(m_Open <= 0.01f)
		return;

	const float Alpha = std::clamp(m_Open * 1.6f, 0.0f, 1.0f);
	const bool Clicked = Allowed && m_Open > 0.7f && (Ui()->MouseButtonClicked(0) || Input()->KeyPress(KEY_MOUSE_1));
	RenderPanel(Panel, Scale, Alpha, Mouse, Clicked);

	if(GameClient()->m_Scoreboard.IsMouseUnlocked() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->MapScreen();
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
	}
}
