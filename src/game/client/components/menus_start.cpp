/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <base/math.h>
#include <base/time.h>
#include <base/vmath.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

// 667client
#include <game/client/components/sixclient/version.h>
// 667client
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

namespace {
float LogoSheen(float g)
{
	static const float s_aStops[][2] = {{0.0f, 0.50f}, {0.16f, 0.89f}, {0.32f, 0.59f}, {0.46f, 1.0f}, {0.62f, 0.55f}, {0.80f, 0.87f}, {1.0f, 0.50f}};
	g = std::clamp(g, 0.0f, 1.0f);
	for(int i = 0; i + 1 < (int)std::size(s_aStops); i++)
	{
		if(g <= s_aStops[i + 1][0])
		{
			const float t = (g - s_aStops[i][0]) / (s_aStops[i + 1][0] - s_aStops[i][0]);
			return mix(s_aStops[i][1], s_aStops[i + 1][1], t);
		}
	}
	return s_aStops[std::size(s_aStops) - 1][1];
}

float LogoEaseOut(float u)
{
	u = std::clamp(u, 0.0f, 1.0f);
	const float Inv = 1.0f - u;
	return 1.0f - Inv * Inv * Inv * Inv * Inv;
}
} // namespace

void CMenusStart::RenderTextLogo(float CenterX, float Top, float Height, bool Decorated)
{
	const int64_t Now = time_get();
	if(m_LastLogoRender == 0 || Now - m_LastLogoRender > time_freq() / 2)
		m_LogoTime = 0.0f;
	else
		m_LogoTime += std::min(Client()->RenderFrameTime(), 0.1f);
	m_LastLogoRender = Now;

	float Sheen = std::fmod(m_LogoTime / 9.0f, 2.0f);
	if(Sheen > 1.0f)
		Sheen = 2.0f - Sheen;
	Sheen = Sheen * Sheen * (3.0f - 2.0f * Sheen);
	const float SheenOffset = Sheen * 2.2f;
	const vec2 Mouse = Ui()->MousePos();

	const auto DrawSpaced = [&](const char *pText, float Size, float Tracking, float Y, float Delay, float Dark, ColorRGBA Tint) {
		const float Progress = LogoEaseOut((m_LogoTime - Delay) / 0.9f);
		if(Progress <= 0.0f)
			return 0.0f;
		const float Rise = (1.0f - Progress) * 18.0f;
		const int Len = str_length(pText);

		float aWidths[32];
		float Total = 0.0f;
		for(int i = 0; i < Len && i < (int)std::size(aWidths); i++)
		{
			const char aGlyph[2] = {pText[i], '\0'};
			aWidths[i] = TextRender()->TextWidth(Size, aGlyph);
			Total += aWidths[i] + (i + 1 < Len ? Tracking : 0.0f);
		}

		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.45f * Progress);
		float x = CenterX - Total * 0.5f;
		for(int i = 0; i < Len && i < (int)std::size(aWidths); i++)
		{
			const char aGlyph[2] = {pText[i], '\0'};
			const vec2 GlyphCenter(x + aWidths[i] * 0.5f, Y + Size * 0.5f);
			const float u = Total > 0.0f ? (GlyphCenter.x - (CenterX - Total * 0.5f)) / Total : 0.5f;
			float Light = LogoSheen((u + SheenOffset) / 3.2f) * Dark;
			float Near = std::clamp(1.0f - distance(Mouse, GlyphCenter) / 150.0f, 0.0f, 1.0f);
			Near *= Near;
			Light = mix(Light, 1.0f, Near * 0.9f);
			TextRender()->TextColor(Light * 0.97f * Tint.r, Light * 0.98f * Tint.g, Light * Tint.b, Progress);
			TextRender()->Text(x, Y + Rise, Size, aGlyph, -1.0f);
			x += aWidths[i] + Tracking;
		}
		return Total;
	};

	const float TitleSize = Height * 0.56f;
	const float SubSize = Height * 0.15f;
	// 667client
	const ColorRGBA White(1.0f, 1.0f, 1.0f, 1.0f);
	DrawSpaced("CECEN", TitleSize, (Decorated ? 0.02f : -0.04f) * TitleSize, Top - Height * 0.04f, 0.0f, Decorated ? 0.8f : 0.62f, White);
	const ColorRGBA SubTint = Decorated ? ColorRGBA(1.0f, 0.3f, 0.32f, 1.0f) : White;
	const float SubWidth = DrawSpaced("CLIENT", SubSize, 0.6f * SubSize, Top + Height * 0.8f, 0.12f, Decorated ? 0.95f : 0.5f, SubTint);
	if(Decorated && SubWidth > 0.0f)
	{
		const float Progress = LogoEaseOut((m_LogoTime - 0.2f) / 0.9f);
		const float LineY = Top + Height * 0.8f + SubSize * 0.62f;
		const float LineLength = Height * 0.55f * Progress;
		const float LineGap = SubSize * 0.9f;
		const ColorRGBA Solid(1.0f, 0.22f, 0.25f, 0.75f * Progress);
		const ColorRGBA Clear = Solid.WithAlpha(0.0f);
		Graphics()->DrawRect4(CenterX - SubWidth * 0.5f - LineGap - LineLength, LineY, LineLength, 1.5f, Clear, Solid, Clear, Solid, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect4(CenterX + SubWidth * 0.5f + LineGap, LineY, LineLength, 1.5f, Solid, Clear, Solid, Clear, IGraphics::CORNER_NONE, 0.0f);
	}
	// 667client

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);
	// 667client
	float Reveal = 1.0f;
	if(g_Config.m_ScStartMenuFox)
		Reveal = RenderStartFox(MainView);
	if(Reveal <= 0.0f)
		return;
	const auto SlideOffset = [&](int Index, float Left) {
		const float Progress = std::clamp(Reveal * 1.6f - Index * 0.12f, 0.0f, 1.0f);
		const float Eased = 1.0f - (1.0f - Progress) * (1.0f - Progress) * (1.0f - Progress);
		return (1.0f - Eased) * (MainView.x + MainView.w - Left + 40.0f);
	};
	// 667client

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;
	const float ExtMenuBottomOffset = 40.0f;

	CUIRect Button;
	int NewPage = -1;

	const auto SetIconMode = [&]() {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	};
	const auto ResetIconMode = [&]() {
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	// 667client
	const bool FoxLayout = g_Config.m_ScStartMenuFox != 0;
	static CButtonContainer s_DiscordButton;
	static CButtonContainer s_TelegramButton;
	static CButtonContainer s_CheckUpdateButton;
	const auto DoExtButtons = [&](const CUIRect &DiscordRect, const CUIRect &TelegramRect, const CUIRect &UpdateRect) {
		const ColorRGBA ExtColor(0.0f, 0.0f, 0.0f, 0.25f);
		const ColorRGBA NoTint(0.0f, 0.0f, 0.0f, 0.0f);
		CMenus &Menus = GameClient()->m_Menus;
		const bool Discord = FoxLayout ? Menus.DoButton_StartMenu(&s_DiscordButton, Localize("Discord"), FontIcon::COMMENT, nullptr, &DiscordRect, nullptr, NoTint) : Menus.DoButton_Menu(&s_DiscordButton, Localize("Discord"), 0, &DiscordRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ExtColor);
		const bool Telegram = FoxLayout ? Menus.DoButton_StartMenu(&s_TelegramButton, Localize("Telegram"), FontIcon::PAPER_PLANE, nullptr, &TelegramRect, nullptr, NoTint) : Menus.DoButton_Menu(&s_TelegramButton, Localize("Telegram"), 0, &TelegramRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ExtColor);
		const bool CheckUpdate = FoxLayout ? Menus.DoButton_StartMenu(&s_CheckUpdateButton, ScLocalize("Check update"), FontIcon::ARROWS_ROTATE, nullptr, &UpdateRect, nullptr, NoTint) : Menus.DoButton_Menu(&s_CheckUpdateButton, ScLocalize("Check update"), 0, &UpdateRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ExtColor);
		if(Discord)
			Client()->ViewLink("https://discord.gg/667");
		if(Telegram)
			Client()->ViewLink("https://t.me/bestddnet");
		if(CheckUpdate)
		{
#if defined(CONF_AUTOUPDATE)
			Updater()->CheckForUpdate();
#endif
		}
	};
	if(!FoxLayout)
	{
		CUIRect ExtMenu, DiscordRect, TelegramRect, UpdateRect;
		MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
		ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);
		ExtMenu.HSplitBottom(ExtMenuBottomOffset, &ExtMenu, nullptr);
		ExtMenu.HSplitBottom(20.0f, &ExtMenu, &DiscordRect);
		ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
		ExtMenu.HSplitBottom(20.0f, &ExtMenu, &TelegramRect);
		ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
		ExtMenu.HSplitBottom(20.0f, &ExtMenu, &UpdateRect);
		DoExtButtons(DiscordRect, TelegramRect, UpdateRect);
	}
	// 667client

	const float LogoH = FoxLayout ? 150.0f : 103.0f; // 667client
	const float LogoW = LogoH * 360.0f / 103.0f; // 667client
	constexpr float LogoGap = 30.0f;
	const float ButtonH = FoxLayout ? 46.0f : 40.0f; // 667client
	const float ButtonGap = FoxLayout ? 8.0f : 5.0f; // 667client
	constexpr int MenuButtonCount = 5;
	const float ButtonsH = MenuButtonCount * ButtonH + (MenuButtonCount - 1) * ButtonGap;
	const float TotalBlockH = LogoH + LogoGap + ButtonsH;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);

	const float BlockStartY = MainView.y + (MainView.h - TotalBlockH) / 2.0f - 50.0f;
	// 667client
	float LogoCenterX = MainView.x + MainView.w / 2.0f;
	float LogoTop = BlockStartY;
	float ButtonsTop = BlockStartY + LogoH + LogoGap;
	if(FoxLayout)
	{
		vec2 FoxCenter;
		float FoxSize;
		StartFoxLayout(MainView, &FoxCenter, &FoxSize);
		LogoCenterX = FoxCenter.x - FoxSize * 0.03f;
		LogoTop = FoxCenter.y - FoxSize * 0.43f - LogoH - 34.0f;
		const CUIRect FoxMenu = StartFoxMenuRect(MainView);
		Menu.x = FoxMenu.x;
		Menu.w = FoxMenu.w;
		ButtonsTop = MainView.y + (MainView.h - ButtonsH) / 2.0f;
	}
	// 667client

	if(g_Config.m_ScStartMenuTextLogo)
	{
		RenderTextLogo(LogoCenterX, LogoTop, LogoH, FoxLayout);
	}
	else
	{
		const float LogoX = LogoCenterX - LogoW / 2.0f;
		const IGraphics::CTextureHandle &LogoTexture = GameClient()->m_Menus.ScLogoTexture();
		Graphics()->TextureSet(LogoTexture.IsValid() && !LogoTexture.IsNullTexture() ? LogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		IGraphics::CQuadItem LogoQuad(LogoX, LogoTop, LogoW, LogoH);
		Graphics()->QuadsDrawTL(&LogoQuad, 1);
		Graphics()->QuadsEnd();
	}

	const auto ScaleButtonRect = [](const CUIRect &Base, float Scale) -> CUIRect {
		CUIRect Out = Base;
		Out.w *= Scale;
		Out.h *= Scale;
		Out.x = Base.x + (Base.w - Out.w) * 0.5f;
		Out.y = Base.y + (Base.h - Out.h) * 0.5f;
		return Out;
	};

	const auto DoStartButton = [&](CButtonContainer *pButton, const char *pText, const char *pIcon, const char *pHotkey, const CUIRect *pRect, const char *pImageName, ColorRGBA Color) {
		if(FoxLayout)
			return GameClient()->m_Menus.DoButton_StartMenu(pButton, pText, pIcon, pHotkey, pRect, pImageName, Color.g > 0.5f ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.12f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		return GameClient()->m_Menus.DoButton_MenuEx(pButton, pText, 0, pRect, BUTTONFLAG_LEFT, pImageName, IGraphics::CORNER_ALL, Rounding, 0.5f, Color, true);
	};

	CUIRect aMenuButtons[MenuButtonCount];
	{
		float Y = ButtonsTop;
		for(int i = 0; i < MenuButtonCount; ++i)
		{
			aMenuButtons[i] = {Menu.x + SlideOffset(i, Menu.x), Y, Menu.w, ButtonH}; // 667client
			Y += ButtonH + ButtonGap;
		}
	}

	static float s_aMenuButtonScale[MenuButtonCount] = {};
	static bool s_MenuButtonScaleInit = false;
	if(!s_MenuButtonScaleInit)
	{
		for(int i = 0; i < MenuButtonCount; ++i)
			s_aMenuButtonScale[i] = 1.0f;
		s_MenuButtonScaleInit = true;
	}

	{
		const bool AnimEnabled = g_Config.m_ScMainMenuAnimation != 0;
		int HoveredIndex = -1;
		if(AnimEnabled)
		{
			for(int i = 0; i < MenuButtonCount; ++i)
			{
				const CUIRect Scaled = ScaleButtonRect(aMenuButtons[i], s_aMenuButtonScale[i]);
				if(Ui()->MouseHovered(&Scaled))
				{
					HoveredIndex = i;
					break;
				}
			}
		}
		const bool AnyHovered = HoveredIndex != -1;
		const float HoverScale = 1.08f;
		const float OtherScale = 0.94f;
		const float Speed = (float)g_Config.m_ScMainMenuAnimationSpeed;
		const float Blend = AnimEnabled ? std::clamp(Client()->RenderFrameTime() * Speed, 0.0f, 1.0f) : 1.0f;
		for(int i = 0; i < MenuButtonCount; ++i)
		{
			const float Target = (AnimEnabled && AnyHovered) ? (i == HoveredIndex ? HoverScale : OtherScale) : 1.0f;
			s_aMenuButtonScale[i] += (Target - s_aMenuButtonScale[i]) * Blend;
		}
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[0], s_aMenuButtonScale[0]);
		static CButtonContainer s_PlayButton;
		if((DoStartButton(&s_PlayButton, Localize("Play", "Start menu"), FontIcon::PLAY, "P", &ScaledButton, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P)))
			NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[1], s_aMenuButtonScale[1]);
		static CButtonContainer s_DemoButton;
		if((DoStartButton(&s_DemoButton, Localize("Demos"), FontIcon::FILM, "D", &ScaledButton, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_D)))
			NewPage = CMenus::PAGE_DEMOS;
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[2], s_aMenuButtonScale[2]);
		static CButtonContainer s_MapEditorButton;
		if((DoStartButton(&s_MapEditorButton, Localize("Editor"), FontIcon::PEN_TO_SQUARE, "E", &ScaledButton, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, GameClient()->Editor()->HasUnsavedData() ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_E)))
		{
			g_Config.m_ClEditor = 1;
			Input()->MouseModeRelative();
		}
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[3], s_aMenuButtonScale[3]);
		static CButtonContainer s_LocalServerButton;
		const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
		if((DoStartButton(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), FontIcon::NETWORK_WIRED, "R", &ScaledButton, g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R))))
		{
			if(LocalServerRunning)
				GameClient()->m_LocalServer.KillServer();
			else
				GameClient()->m_LocalServer.RunServer({});
		}
	}

	const CUIRect SettingsButton = aMenuButtons[4];
	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[4], s_aMenuButtonScale[4]);
		static CButtonContainer s_SettingsButton;
		if(DoStartButton(&s_SettingsButton, Localize("Settings"), FontIcon::GEAR, "S", &ScaledButton, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_S))
		{
			NewPage = CMenus::PAGE_SETTINGS;
			GameClient()->m_Menus.OpenSettingsWindow(ScaledButton); // 667client
		}
	}

	// 667client
	CUIRect UpdateAnchor = SettingsButton;
	CUIRect FoxQuitRect;
	if(FoxLayout)
	{
		CUIRect ExtRow = {Menu.x + SlideOffset(MenuButtonCount, Menu.x), SettingsButton.y + SettingsButton.h + 14.0f, Menu.w, 30.0f};
		UpdateAnchor = ExtRow;
		CUIRect DiscordRect, TelegramRect, UpdateRect;
		const float ExtGap = 8.0f;
		const float ExtW = (ExtRow.w - 2.0f * ExtGap) / 3.0f;
		ExtRow.VSplitLeft(ExtW, &UpdateRect, &ExtRow);
		ExtRow.VSplitLeft(ExtGap, nullptr, &ExtRow);
		ExtRow.VSplitLeft(ExtW, &TelegramRect, &ExtRow);
		ExtRow.VSplitLeft(ExtGap, nullptr, &DiscordRect);
		DoExtButtons(DiscordRect, TelegramRect, UpdateRect);
		FoxQuitRect = {Menu.x + SlideOffset(MenuButtonCount + 1, Menu.x), UpdateAnchor.y + UpdateAnchor.h + ExtGap, Menu.w, 34.0f};
		UpdateAnchor = FoxQuitRect;
	}
	// 667client

#if defined(CONF_AUTOUPDATE)
	{
		char aUpdateBuf[128] = "";
		const IUpdater::EUpdaterState State = Updater()->GetCurrentState();
		const bool NeedUpdate = Updater()->GetLatestVersionString()[0] != '\0';
		const bool ShowDownloadButton = State == IUpdater::VERSION_AVAILABLE;
		const bool ShowRetryButton = NeedUpdate && State == IUpdater::FAIL;
		const bool ShowRestartButton = State == IUpdater::NEED_RESTART;
		const bool ShowUpdateProgress = State == IUpdater::DOWNLOADING;

		if(ShowDownloadButton || ShowRetryButton || ShowRestartButton || ShowUpdateProgress)
		{
			CUIRect UpdateRow = UpdateAnchor; // 667client
			UpdateRow.y += UpdateAnchor.h + ButtonGap; // 667client
			UpdateRow.h = 22.0f;

			CUIRect UpdateLabel, UpdateButton;
			UpdateRow.VSplitRight(120.0f, &UpdateLabel, &UpdateButton);
			UpdateLabel.VSplitRight(10.0f, &UpdateLabel, nullptr);

			if(ShowDownloadButton)
			{
				str_format(aUpdateBuf, sizeof(aUpdateBuf), Localize("Cecen Client %s is out!"), Updater()->GetLatestVersionString());
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			}
			else if(ShowUpdateProgress)
			{
				if(State == IUpdater::GETTING_MANIFEST)
					str_copy(aUpdateBuf, Localize("Preparing update..."));
				else
					str_format(aUpdateBuf, sizeof(aUpdateBuf), Localize("Downloading %d%%"), Updater()->GetCurrentPercent());
			}
			else if(ShowRetryButton)
			{
				str_copy(aUpdateBuf, Localize("Update failed"));
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			}
			else if(ShowRestartButton)
			{
				str_copy(aUpdateBuf, Localize("Update downloaded"));
				TextRender()->TextColor(0.7f, 1.0f, 0.7f, 1.0f);
			}

			Ui()->DoLabel(&UpdateLabel, aUpdateBuf, 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(ShowDownloadButton || ShowRetryButton)
			{
				static CButtonContainer s_MenuUpdateDownload;
				if(GameClient()->m_Menus.DoButton_Menu(&s_MenuUpdateDownload, Localize("Download"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
					Updater()->InitiateUpdate();
			}
			else if(ShowRestartButton)
			{
				static CButtonContainer s_MenuUpdateRestart;
#if defined(CONF_PLATFORM_ANDROID)
				const char *pRestartButtonLabel = Localize("Install");
#else
				const char *pRestartButtonLabel = Localize("Restart");
#endif
				if(GameClient()->m_Menus.DoButton_Menu(&s_MenuUpdateRestart, pRestartButtonLabel, 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
					Updater()->ApplyUpdateAndRestart();
			}
			else
			{
				Ui()->RenderProgressBar(UpdateButton, Updater()->GetCurrentPercent() / 100.0f);
			}
		}
	}
#endif

	{
		CUIRect QuitArea;
		MainView.VMargin(VMargin, &QuitArea);
		QuitArea.HSplitBottom(25.0f, &QuitArea, nullptr);
		QuitArea.HSplitBottom(40.0f, &QuitArea, &Button);
		CUIRect QuitButton = Button;
		QuitButton.w = QuitButton.h;
		QuitButton.x += (Button.w - QuitButton.w) / 2.0f;
		static CButtonContainer s_QuitButton;
		bool UsedEscape = false;
		// 667client
		bool QuitClicked;
		if(FoxLayout)
		{
			QuitClicked = GameClient()->m_Menus.DoButton_StartMenu(&s_QuitButton, Localize("Quit"), FontIcon::POWER_OFF, "Q", &FoxQuitRect, nullptr, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		}
		else
		{
			SetIconMode();
			QuitClicked = GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, FontIcon::POWER_OFF, 0, &QuitButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f));
		}
		if(QuitClicked || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
		// 667client
		{
			ResetIconMode();
			if(g_Config.m_ScConfirmQuit || UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
				GameClient()->m_Menus.ShowQuitPopup();
			else
				Client()->Quit();
		}
		ResetIconMode();
	}

	{
		CUIRect CurVersion, ConsoleButton;
		MainView.HSplitBottom(74.0f, nullptr, &CurVersion);
		CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
		CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
		CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
		ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);

		CUIRect VersionLine1, VersionLine2, VersionLine3;
		CurVersion.HSplitTop(16.0f, &VersionLine1, &CurVersion);
		CurVersion.HSplitTop(2.0f, nullptr, &CurVersion);
		CurVersion.HSplitTop(16.0f, &VersionLine2, &CurVersion);
		CurVersion.HSplitTop(2.0f, nullptr, &CurVersion);
		CurVersion.HSplitTop(16.0f, &VersionLine3, &CurVersion);

		char aDDNetBuf[64];
		char aTClientBuf[64];
		char aSixClientBuf[64];
		str_format(aDDNetBuf, sizeof(aDDNetBuf), "DDNet %s", GAME_RELEASE_VERSION);
		str_format(aTClientBuf, sizeof(aTClientBuf), "TClient %s", TCLIENT_VERSION);
		str_format(aSixClientBuf, sizeof(aSixClientBuf), "Cecen Client %s", SIXCLIENT_VERSION);
		Ui()->DoLabel(&VersionLine1, aDDNetBuf, 14.0f, TEXTALIGN_MR);
		Ui()->DoLabel(&VersionLine2, aTClientBuf, 14.0f, TEXTALIGN_MR);
		Ui()->DoLabel(&VersionLine3, aSixClientBuf, 14.0f, TEXTALIGN_MR);

		static CButtonContainer s_ConsoleButton;
		SetIconMode();
		if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
		ResetIconMode();
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() &&
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
