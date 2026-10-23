#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/client.h>
#include <engine/keys.h>
#include <engine/updater.h>

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <game/client/sc_ui_animations.h>
#include <game/client/components/sixclient/config_presets.h>
#include <game/client/components/sixclient/fast_actions.h>
#include <game/client/components/sixclient/sixclient.h>
#include <game/client/components/sixclient/cursor_trail.h>
#include <game/client/components/sixclient/media_decoder.h>
#include <game/client/components/sixclient/menu_media_background.h>
#include <game/client/components/sixclient/settings_search.h>
#include <game/client/components/sixclient/ui_theme/style.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// 667client
enum
{
	SIXCLIENT_TAB_VISUALS = 0,
	SIXCLIENT_TAB_GAMEPLAY,
	SIXCLIENT_TAB_OTHERS,
	SIXCLIENT_TAB_COSMETICS, // 667client: was Friends, which moved to Cecen Social
	SIXCLIENT_TAB_SENSITIVITY,
	SIXCLIENT_TAB_ASSETS,
	SIXCLIENT_TAB_CONFIGS,
	SIXCLIENT_TAB_INFO,
	SIXCLIENT_TAB_ASSETS_MARKET,
	NUM_SIXCLIENT_TABS,
};

static int s_CurSixClientTab = SIXCLIENT_TAB_VISUALS;
static bool s_ShowSixClientFun = false;

void CMenus::SetSixClientSettingsTab(int Tab)
{
	if(Tab < SIXCLIENT_TAB_VISUALS || Tab >= NUM_SIXCLIENT_TABS)
		Tab = SIXCLIENT_TAB_VISUALS;
	s_CurSixClientTab = Tab;
}

int CMenus::GetSixClientSettingsTab() const
{
	return s_CurSixClientTab;
}

void CMenus::OpenSixClientFun()
{
	s_CurSixClientTab = SIXCLIENT_TAB_INFO;
	s_ShowSixClientFun = true;
}

void CMenus::CloseSixClientFun()
{
	s_ShowSixClientFun = false;
}

static unsigned ScUiThemeParseHex(const char *pValue)
{
	if(pValue[0] == '$')
		++pValue;
	return (unsigned)str_toulong_base(pValue, 16);
}

static bool ScUiThemeParseKvInt(const char *pToken, const char *pKey, int *pOut)
{
	const int KeyLen = str_length(pKey);
	if(str_comp_nocase_num(pToken, pKey, KeyLen) != 0 || pToken[KeyLen] != '=')
		return false;
	*pOut = str_toint(pToken + KeyLen + 1);
	return true;
}

static bool ScUiThemeParseKvColor(const char *pToken, const char *pKey, unsigned *pOut)
{
	const int KeyLen = str_length(pKey);
	if(str_comp_nocase_num(pToken, pKey, KeyLen) != 0 || pToken[KeyLen] != '=')
		return false;
	*pOut = ScUiThemeParseHex(pToken + KeyLen + 1);
	return true;
}

static bool ScUiThemeParseElemLine(const char *pLine, int *pEnable, unsigned *pColor, int *pGradient, unsigned *pGradientColor, int *pAccent, unsigned *pAccentColor, int *pOpacity)
{
	char aTok[128];
	const char *pCursor = pLine;
	bool First = true;
	while((pCursor = str_next_token(pCursor, " \t", aTok, sizeof(aTok))))
	{
		if(First)
		{
			First = false;
			continue;
		}
		ScUiThemeParseKvInt(aTok, "enable", pEnable) ||
			ScUiThemeParseKvColor(aTok, "color", pColor) ||
			ScUiThemeParseKvInt(aTok, "gradient", pGradient) ||
			ScUiThemeParseKvColor(aTok, "gradient_color", pGradientColor) ||
			ScUiThemeParseKvInt(aTok, "accent", pAccent) ||
			ScUiThemeParseKvColor(aTok, "accent_color", pAccentColor) ||
			(pOpacity && ScUiThemeParseKvInt(aTok, "opacity", pOpacity));
	}
	return true;
}

static bool ScUiThemeImportFromText(const char *pText)
{
	if(!pText || !str_startswith(pText, "SC_UI_THEME_EXPORT"))
		return false;

	const char *p = pText;
	while(*p)
	{
		char aLine[512];
		int i = 0;
		while(*p && *p != '\n' && *p != '\r' && i < (int)sizeof(aLine) - 1)
			aLine[i++] = *p++;
		aLine[i] = '\0';
		while(*p == '\n' || *p == '\r')
			++p;
		if(aLine[0] == '\0' || str_startswith(aLine, "SC_UI_THEME_EXPORT"))
			continue;

		if(str_startswith(aLine, "animate_speed="))
		{
			g_Config.m_ScCustomUiGradientAnimateSpeed = std::clamp(str_toint(aLine + 14), 0, 100);
			continue;
		}
		if(str_startswith(aLine, "background="))
		{
			g_Config.m_ScCustomUiBackground = str_toint(aLine + 11) != 0 ? 1 : 0;
			continue;
		}
		if(str_startswith(aLine, "ui_color="))
		{
			g_Config.m_UiColor = ScUiThemeParseHex(aLine + 9);
			continue;
		}

		auto ApplyElem = [&](const char *pName, int *pEnable, unsigned *pColor, int *pGradient, unsigned *pGradientColor, int *pAccent, unsigned *pAccentColor, int *pOpacity = nullptr) {
			if(str_startswith(aLine, pName) && (aLine[str_length(pName)] == ' ' || aLine[str_length(pName)] == '\t'))
				ScUiThemeParseElemLine(aLine, pEnable, pColor, pGradient, pGradientColor, pAccent, pAccentColor, pOpacity);
		};
		ApplyElem("text", &g_Config.m_ScCustomUiText, &g_Config.m_ScCustomUiTextColor, &g_Config.m_ScCustomUiTextGradient, &g_Config.m_ScCustomUiTextGradientColor, &g_Config.m_ScCustomUiTextUseAccent, &g_Config.m_ScCustomUiTextAccentColor);
		ApplyElem("checkbox", &g_Config.m_ScCustomUiCheckbox, &g_Config.m_ScCustomUiCheckboxColor, &g_Config.m_ScCustomUiCheckboxGradient, &g_Config.m_ScCustomUiCheckboxGradientColor, &g_Config.m_ScCustomUiCheckboxUseAccent, &g_Config.m_ScCustomUiCheckboxAccentColor);
		ApplyElem("scrollbar", &g_Config.m_ScCustomUiScrollbar, &g_Config.m_ScCustomUiScrollbarColor, &g_Config.m_ScCustomUiScrollbarGradient, &g_Config.m_ScCustomUiScrollbarGradientColor, &g_Config.m_ScCustomUiScrollbarUseAccent, &g_Config.m_ScCustomUiScrollbarAccentColor);
		ApplyElem("button", &g_Config.m_ScCustomUiButton, &g_Config.m_ScCustomUiButtonColor, &g_Config.m_ScCustomUiButtonGradient, &g_Config.m_ScCustomUiButtonGradientColor, &g_Config.m_ScCustomUiButtonUseAccent, &g_Config.m_ScCustomUiButtonAccentColor);
		ApplyElem("droplist", &g_Config.m_ScCustomUiDropdown, &g_Config.m_ScCustomUiDropdownColor, &g_Config.m_ScCustomUiDropdownGradient, &g_Config.m_ScCustomUiDropdownGradientColor, &g_Config.m_ScCustomUiDropdownUseAccent, &g_Config.m_ScCustomUiDropdownAccentColor);
		ApplyElem("input", &g_Config.m_ScCustomUiEditBox, &g_Config.m_ScCustomUiEditBoxColor, &g_Config.m_ScCustomUiEditBoxGradient, &g_Config.m_ScCustomUiEditBoxGradientColor, &g_Config.m_ScCustomUiEditBoxUseAccent, &g_Config.m_ScCustomUiEditBoxAccentColor);
		ApplyElem("block", &g_Config.m_ScCustomUiBlock, &g_Config.m_ScCustomUiBlockColor, &g_Config.m_ScCustomUiBlockGradient, &g_Config.m_ScCustomUiBlockGradientColor, &g_Config.m_ScCustomUiBlockUseAccent, &g_Config.m_ScCustomUiBlockAccentColor, &g_Config.m_ScCustomUiBlockOpacity);
		ApplyElem("panel", &g_Config.m_ScCustomUiMainPanel, &g_Config.m_ScCustomUiMainPanelColor, &g_Config.m_ScCustomUiMainPanelGradient, &g_Config.m_ScCustomUiMainPanelGradientColor, &g_Config.m_ScCustomUiMainPanelUseAccent, &g_Config.m_ScCustomUiMainPanelAccentColor, &g_Config.m_ScCustomUiMainPanelOpacity);
		ApplyElem("nav", &g_Config.m_ScCustomUiNavTab, &g_Config.m_ScCustomUiNavTabColor, &g_Config.m_ScCustomUiNavTabGradient, &g_Config.m_ScCustomUiNavTabGradientColor, &g_Config.m_ScCustomUiNavTabUseAccent, &g_Config.m_ScCustomUiNavTabAccentColor, &g_Config.m_ScCustomUiNavTabOpacity);
		ApplyElem("badge", &g_Config.m_ScCustomUiBadge, &g_Config.m_ScCustomUiBadgeColor, &g_Config.m_ScCustomUiBadgeGradient, &g_Config.m_ScCustomUiBadgeGradientColor, &g_Config.m_ScCustomUiBadgeUseAccent, &g_Config.m_ScCustomUiBadgeAccentColor);
		ApplyElem("binds", &g_Config.m_ScCustomUiBind, &g_Config.m_ScCustomUiBindColor, &g_Config.m_ScCustomUiBindGradient, &g_Config.m_ScCustomUiBindGradientColor, &g_Config.m_ScCustomUiBindUseAccent, &g_Config.m_ScCustomUiBindAccentColor);
	}
	g_Config.m_ScCustomUiBlockOpacity = std::clamp(g_Config.m_ScCustomUiBlockOpacity, 0, 100);
	g_Config.m_ScCustomUiMainPanelOpacity = std::clamp(g_Config.m_ScCustomUiMainPanelOpacity, 0, 100);
	g_Config.m_ScCustomUiNavTabOpacity = std::clamp(g_Config.m_ScCustomUiNavTabOpacity, 0, 100);
	return true;
}

void CMenus::RenderSettingsSixClientChatMediaBlock(CUIRect &Column)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;

	const bool Expanded = g_Config.m_ScChatMediaPreview != 0;
	const float DomainsHeight = g_Config.m_ScChatMediaContentFilter ? 2.0f * (MarginSmall + LineSize) : 0.0f;
	const float ExpandedTargetHeight = 5.0f * (MarginSmall + LineSize) + DomainsHeight;
	const float ExpandedHeight = SC_MODULE_REVEAL(Expanded, ExpandedTargetHeight, Client()->RenderFrameTime());
	const float HeaderHeight = LineSize + MarginSmall + LineSize;
	const float BlockHeight = HeaderHeight + ExpandedHeight;

	CUIRect Block;
	Column.HSplitTop(BlockHeight, &Block, &Column);
	CUIRect BlockBg = Block;
	BlockBg.w += BlockPadding;
	BlockBg.h += BlockPadding;
	BlockBg.x -= BlockPadding * 0.5f;
	BlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&BlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScChatMediaPreview, &BlockBg);

	CUIRect Content, Label, Button;
	Block.HSplitTop(LineSize, &Label, &Block);
	Ui()->DoLabel(&Label, ScLocalize("Chat Media"), HeadlineFontSize, TEXTALIGN_ML);
	Block.HSplitTop(MarginSmall, nullptr, &Block);

	CChat &Chat = GameClient()->m_Chat;
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatMediaPreview, ScLocalize("Render media previews from chat links"), &g_Config.m_ScChatMediaPreview, &Content, LineSize))
		Chat.RebuildChat();

	if(ExpandedHeight <= 0.5f)
		return;

	CUIRect Visible = Block;
	Visible.h = ExpandedHeight;
	Ui()->ClipEnable(&Visible);

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatMediaPhotos, ScLocalize("Show photos in chat media"), &g_Config.m_ScChatMediaPhotos, &Content, LineSize))
		Chat.RebuildChat();

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatMediaGifs, ScLocalize("Show GIFs in chat media"), &g_Config.m_ScChatMediaGifs, &Content, LineSize))
		Chat.RebuildChat();

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Content, &Block);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatMediaContentFilter, ScLocalize("Content filtering"), &g_Config.m_ScChatMediaContentFilter, &Content, LineSize))
		Chat.RebuildChat();

	if(g_Config.m_ScChatMediaContentFilter)
	{
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Label, &Block);
		Ui()->DoLabel(&Label, ScLocalize("Allowed media domains"), 12.0f, TEXTALIGN_ML);
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Button, &Block);
		static CLineInput DomainsInput(g_Config.m_ScChatMediaAllowedDomains, sizeof(g_Config.m_ScChatMediaAllowedDomains));
		DomainsInput.SetEmptyText("tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz");
		if(Ui()->DoClearableEditBox(&DomainsInput, &Button, 14.0f))
			Chat.RebuildChat();
	}

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Button, &Block);
	if(Ui()->DoScrollbarOption(&g_Config.m_ScChatMediaPreviewMaxWidth, &g_Config.m_ScChatMediaPreviewMaxWidth, &Button, ScLocalize("Media preview width"), 120, 400))
		Chat.RebuildChat();

	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Label, &Block);
	static CButtonContainer HideMediaBindReader, HideMediaBindClear;
	DoLine_KeyReader(Label, HideMediaBindReader, HideMediaBindClear, ScLocalize("Hide media bind"), "toggle_chat_media_hidden");
	Ui()->ClipDisable();
}

static void SetSixClientTabFlag(int32_t &Flags, int Tab, bool Hidden)
{
	if(Hidden)
		Flags |= (1 << Tab);
	else
		Flags &= ~(1 << Tab);
}

static bool IsSixClientTabFlagSet(int32_t Flags, int Tab)
{
	return (Flags & (1 << Tab)) != 0;
}
// 667client

void CMenus::RenderSettingsSixClient(CUIRect MainView)
{
	// 667client
	const float TopExtend = 20.0f;
	MainView.y -= TopExtend;
	MainView.h += TopExtend;
	// 667client

	static CButtonContainer s_aPageTabs[NUM_SIXCLIENT_TABS] = {};

	MainView.HSplitTop(8.0f, nullptr, &MainView);

	auto IsTabHidden = [&](int Tab) {
		return Tab != SIXCLIENT_TAB_INFO && IsSixClientTabFlagSet(g_Config.m_ScSixClientSettingsTabs, Tab);
	};

	const char *apTabNames[NUM_SIXCLIENT_TABS] = {
		ScLocalize("Visuals"),
		ScLocalize("Gameplay"),
		ScLocalize("Others"),
		ScLocalize("Cosmetics"),
		ScLocalize("Sensitivity"),
		ScLocalize("Assets Editor"),
		ScLocalize("Configs"),
		ScLocalize("Info"),
		ScLocalize("Assets Market"),
	};
	const int aTabOrder[NUM_SIXCLIENT_TABS] = {
		SIXCLIENT_TAB_VISUALS,
		SIXCLIENT_TAB_GAMEPLAY,
		SIXCLIENT_TAB_OTHERS,
		SIXCLIENT_TAB_COSMETICS,
		SIXCLIENT_TAB_SENSITIVITY,
		SIXCLIENT_TAB_ASSETS,
		SIXCLIENT_TAB_ASSETS_MARKET,
		SIXCLIENT_TAB_CONFIGS,
		SIXCLIENT_TAB_INFO,
	};

	CButtonContainer *apVisibleButtons[NUM_SIXCLIENT_TABS];
	const char *apVisibleNames[NUM_SIXCLIENT_TABS];
	int aVisibleValues[NUM_SIXCLIENT_TABS];
	int VisibleCount = 0;
	int FirstVisibleTab = -1;
	for(const int Tab : aTabOrder)
	{
		if(IsTabHidden(Tab))
			continue;
		if(FirstVisibleTab == -1)
			FirstVisibleTab = Tab;
		apVisibleButtons[VisibleCount] = &s_aPageTabs[Tab];
		apVisibleNames[VisibleCount] = apTabNames[Tab];
		aVisibleValues[VisibleCount] = Tab;
		++VisibleCount;
	}

	if(FirstVisibleTab == -1)
	{
		s_CurSixClientTab = SIXCLIENT_TAB_INFO;
		FirstVisibleTab = SIXCLIENT_TAB_INFO;
		VisibleCount = 1;
		apVisibleButtons[0] = &s_aPageTabs[SIXCLIENT_TAB_INFO];
		apVisibleNames[0] = apTabNames[SIXCLIENT_TAB_INFO];
		aVisibleValues[0] = SIXCLIENT_TAB_INFO;
	}

	if(s_CurSixClientTab < SIXCLIENT_TAB_VISUALS || s_CurSixClientTab >= NUM_SIXCLIENT_TABS || IsTabHidden(s_CurSixClientTab))
		s_CurSixClientTab = FirstVisibleTab;

	int SelectedVisibleIndex = 0;
	// 667client
	{
		CUIRect TabBar, TabButton;
		MainView.HSplitTop(24.0f, &TabBar, &MainView);
		const float TabWidth = TabBar.w / (float)VisibleCount;
		for(int i = 0; i < VisibleCount; ++i)
		{
			TabBar.VSplitLeft(TabWidth, &TabButton, &TabBar);
			const int Corners = i == 0 ? IGraphics::CORNER_L : (i == VisibleCount - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(apVisibleButtons[i], apVisibleNames[i], s_CurSixClientTab == aVisibleValues[i], &TabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			{
				s_CurSixClientTab = aVisibleValues[i];
				s_ShowSixClientFun = false;
			}
			if(aVisibleValues[i] == s_CurSixClientTab)
				SelectedVisibleIndex = i;
		}
		MainView.HSplitTop(10.0f, nullptr, &MainView);
	}
	// 667client

	if(s_ShowSixClientFun)
	{
		RenderSettingsSixClientFun(MainView);
		return;
	}

	auto RenderTabContent = [&](int Tab, CUIRect Content) {
		if(Tab == SIXCLIENT_TAB_VISUALS)
			RenderSettingsSixClientVisuals(Content);
		else if(Tab == SIXCLIENT_TAB_GAMEPLAY)
			RenderSettingsSixClientGameplay(Content);
		else if(Tab == SIXCLIENT_TAB_OTHERS)
			RenderSettingsSixClientOthers(Content);
		else if(Tab == SIXCLIENT_TAB_COSMETICS)
			RenderSettingsSixClientCosmetics(Content);
		else if(Tab == SIXCLIENT_TAB_SENSITIVITY)
			RenderSettingsSixClientSensitivity(Content);
		else if(Tab == SIXCLIENT_TAB_ASSETS)
			RenderSettingsSixClientAssetsEditor(Content);
		else if(Tab == SIXCLIENT_TAB_ASSETS_MARKET)
			RenderSettingsSixClientAssetsMarket(Content);
		else if(Tab == SIXCLIENT_TAB_CONFIGS)
			RenderSettingsSixClientConfigPresets(Content);
		else if(Tab == SIXCLIENT_TAB_INFO)
			RenderSettingsSixClientInfo(Content);
	};


	const SCUiAnimations::STabSwitch TabSwitch = SCUiAnimations::AnimateTabSwitch(SelectedVisibleIndex, VisibleCount, Client()->RenderFrameTime(), 0);
	const int FromTab = aVisibleValues[TabSwitch.m_FromIndex];
	const int ToTab = aVisibleValues[TabSwitch.m_ToIndex];
	if(TabSwitch.m_Progress >= 1.0f || FromTab == ToTab)
	{
		RenderTabContent(ToTab, MainView);
		return;
	}

	float FromOffset = 0.0f;
	float ToOffset = 0.0f;
	SCUiAnimations::TabSwitchContentOffsets(TabSwitch.m_Progress, TabSwitch.m_FromIndex, TabSwitch.m_ToIndex, MainView.w, FromOffset, ToOffset);

	CUIRect FromView = MainView;
	CUIRect ToView = MainView;
	FromView.x += FromOffset;
	ToView.x += ToOffset;

	Ui()->ClipEnable(&MainView);
	RenderTabContent(FromTab, FromView);
	RenderTabContent(ToTab, ToView);
	Ui()->ClipDisable();
}

void CMenus::RenderSettingsSixClientVisuals(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const auto DoHighFpsIntakeTooltip = [&](const void *pId, const CUIRect &Row) {
		GameClient()->m_Tooltips.DoToolTip(pId, &Row, ScLocalize("high fps intake"));
		GameClient()->m_Tooltips.SetFadeTime(pId, 0.0f);
		GameClient()->m_Tooltips.SetTextColor(pId, ColorRGBA(1.0f, 0.55f, 0.16f, 1.0f));
	};

	{
		CUIRect HudButtonRow;
		MainView.HSplitTop(24.0f, &HudButtonRow, &MainView);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		static CButtonContainer s_HudEditorButton;
		const bool CanOpen = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		if(DoButton_MenuTab(&s_HudEditorButton, ScLocalize("HUD editor"), 0, &HudButtonRow, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f) && CanOpen)
		{
			SetActive(false);
			GameClient()->m_HudEditor.Activate();
		}
		GameClient()->m_Tooltips.DoToolTip(&s_HudEditorButton, &HudButtonRow, CanOpen ? ScLocalize("Open in HUD editor") : ScLocalize("Join a game first"));
		GameClient()->m_Tooltips.SetFadeTime(&s_HudEditorButton, 0.0f);
	}

	static CScrollRegion s_VisualsScrollRegion;
	CScrollRegionParams VisualsScrollParams;
	VisualsScrollParams.m_ScrollUnit = 60.0f;
	VisualsScrollParams.m_ScrollbarMargin = 5.0f;
	s_VisualsScrollRegion.Begin(&MainView, &VisualsScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	const float VisualsScrollContentX = MainView.x;
	const float VisualsScrollContentW = MainView.w;

	CUIRect Content, Label, Button, LeftView, RightView, Column;

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	Column = LeftView;
	Column.HSplitTop(10.0f, nullptr, &Column);

	const float VisualsQoLBlockHeight =
		LineSize + MarginSmall +
		4.0f * LineSize +
		3.0f * LineSize;
	CUIRect VisualsQoLBlock;
	Column.HSplitTop(VisualsQoLBlockHeight, &VisualsQoLBlock, &Column);

	CUIRect VisualsQoLBlockBg = VisualsQoLBlock;
	VisualsQoLBlockBg.w += BlockPadding;
	VisualsQoLBlockBg.h += BlockPadding;
	VisualsQoLBlockBg.x -= BlockPadding * 0.5f;
	VisualsQoLBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&VisualsQoLBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScServerMapPreview, &VisualsQoLBlockBg);

	VisualsQoLBlock.HSplitTop(LineSize, &Label, &VisualsQoLBlock);
	Ui()->DoLabel(&Label, ScLocalize("Visuals QoL"), HeadlineFontSize, TEXTALIGN_ML);
	VisualsQoLBlock.HSplitTop(MarginSmall, nullptr, &VisualsQoLBlock);

	{
		CUIRect MapPreviewRow;
		VisualsQoLBlock.HSplitTop(LineSize, &MapPreviewRow, &VisualsQoLBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScServerMapPreview, ScLocalize("Map preview in server list"), &g_Config.m_ScServerMapPreview, &MapPreviewRow, LineSize);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScShowCorrectCheckpoint, ScLocalize("Show current checkpoint in hud"), &g_Config.m_ScShowCorrectCheckpoint, &VisualsQoLBlock, LineSize);
	{
		CUIRect SpectatorNamesRow;
		VisualsQoLBlock.HSplitTop(LineSize, &SpectatorNamesRow, &VisualsQoLBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScShowSpectatorNames, ScLocalize("Show spectator nicknames"), &g_Config.m_ScShowSpectatorNames, &SpectatorNamesRow, LineSize);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScShowVotePercentage, ScLocalize("Show vote percentage"), &g_Config.m_ScShowVotePercentage, &VisualsQoLBlock, LineSize);
	VisualsQoLBlock.HSplitTop(LineSize, &Button, &VisualsQoLBlock);
	Ui()->DoScrollbarOption(&g_Config.m_ScWheelScale, &g_Config.m_ScWheelScale, &Button, ScLocalize("Bind/Emote wheel scale"), 50, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
	VisualsQoLBlock.HSplitTop(LineSize, &Button, &VisualsQoLBlock);
	Ui()->DoScrollbarOption(&g_Config.m_ScScoreboardScale, &g_Config.m_ScScoreboardScale, &Button, ScLocalize("Scoreboard scale"), 50, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
	VisualsQoLBlock.HSplitTop(LineSize, &Button, &VisualsQoLBlock);
	Ui()->DoScrollbarOption(&g_Config.m_UiScale, &g_Config.m_UiScale, &Button, ScLocalize("UI scale"), 50, 110, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool CornerIndicatorEnabled = g_Config.m_ScCornerIndicator != 0;
	const bool CornerIndicatorBlocked = GameClient()->m_AspectRatio.IsBlockedByFng();
	const bool CornerIndicatorShowSizes = CornerIndicatorEnabled && g_Config.m_ScCornerIndicatorProximity == 0;
	const bool CornerIndicatorShowCustomColors = CornerIndicatorEnabled && g_Config.m_ScCornerIndicatorCustomColors != 0;
	const float CornerIndicatorColorPickerLineSize = 25.0f;
	const float CornerIndicatorColorPickerSpacing = 5.0f;
	const float CornerIndicatorExpandedTargetHeight =
		7.0f * (MarginSmall + LineSize) +
		(CornerIndicatorShowSizes ? 3.0f * (MarginSmall + LineSize) : 0.0f) +
		(CornerIndicatorShowCustomColors ? 3.0f * (CornerIndicatorColorPickerSpacing + CornerIndicatorColorPickerLineSize) : 0.0f);
	const float CornerIndicatorExpandedHeight = SC_MODULE_REVEAL(CornerIndicatorEnabled, CornerIndicatorExpandedTargetHeight, Client()->RenderFrameTime());
	const float CornerIndicatorBlockedHintHeight = CornerIndicatorBlocked ? (MarginSmall + LineSize) : 0.0f;
	const float CornerIndicatorBlockHeight = LineSize + MarginSmall + LineSize + CornerIndicatorExpandedHeight + CornerIndicatorBlockedHintHeight;
	CUIRect CornerIndicatorBlock;
	Column.HSplitTop(CornerIndicatorBlockHeight, &CornerIndicatorBlock, &Column);

	CUIRect CornerIndicatorBlockBg = CornerIndicatorBlock;
	CornerIndicatorBlockBg.w += BlockPadding;
	CornerIndicatorBlockBg.h += BlockPadding;
	CornerIndicatorBlockBg.x -= BlockPadding * 0.5f;
	CornerIndicatorBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&CornerIndicatorBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScCornerIndicator, &CornerIndicatorBlockBg);

	CUIRect CornerIndicatorView = CornerIndicatorBlock;
	CornerIndicatorView.HSplitTop(LineSize, &Label, &CornerIndicatorView);
	CUIRect CornerIndicatorTitleLabel, CornerIndicatorResetButton;
	Label.VSplitRight(LineSize + 8.0f, &CornerIndicatorTitleLabel, &CornerIndicatorResetButton);
	static CButtonContainer s_CornerIndicatorResetButton;
	const bool CornerIndicatorResetClicked = Ui()->DoButton_FontIcon(&s_CornerIndicatorResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &CornerIndicatorResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_CornerIndicatorResetButton, &CornerIndicatorResetButton, ScLocalize("Reset to defaults"));
	if(CornerIndicatorResetClicked)
	{
		g_Config.m_ScCornerIndicatorOnlyInTeam = DefaultConfig::ScCornerIndicatorOnlyInTeam;
		g_Config.m_ScCornerIndicatorShowAlive = DefaultConfig::ScCornerIndicatorShowAlive;
		g_Config.m_ScCornerIndicatorShowFrozen = DefaultConfig::ScCornerIndicatorShowFrozen;
		g_Config.m_ScCornerIndicatorIndicateLastTeammate = DefaultConfig::ScCornerIndicatorIndicateLastTeammate;
		g_Config.m_ScCornerIndicatorProximity = DefaultConfig::ScCornerIndicatorProximity;
		g_Config.m_ScCornerIndicatorProximityRange = DefaultConfig::ScCornerIndicatorProximityRange;
		g_Config.m_ScCornerIndicatorProximityMinSize = DefaultConfig::ScCornerIndicatorProximityMinSize;
		g_Config.m_ScCornerIndicatorNinjaFrozen = DefaultConfig::ScCornerIndicatorNinjaFrozen;
		g_Config.m_ScCornerIndicatorSizeAlive = DefaultConfig::ScCornerIndicatorSizeAlive;
		g_Config.m_ScCornerIndicatorSizeFrozen = DefaultConfig::ScCornerIndicatorSizeFrozen;
		g_Config.m_ScCornerIndicatorLastTeammateSize = DefaultConfig::ScCornerIndicatorLastTeammateSize;
		g_Config.m_ScCornerIndicatorCustomColors = DefaultConfig::ScCornerIndicatorCustomColors;
		g_Config.m_ScCornerIndicatorColorAlive = DefaultConfig::ScCornerIndicatorColorAlive;
		g_Config.m_ScCornerIndicatorColorFrozen = DefaultConfig::ScCornerIndicatorColorFrozen;
		g_Config.m_ScCornerIndicatorLastTeammateColor = DefaultConfig::ScCornerIndicatorLastTeammateColor;
	}
	Ui()->DoLabel(&CornerIndicatorTitleLabel, ScLocalize("Corner indicator"), HeadlineFontSize, TEXTALIGN_ML);
	CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);

	CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicator, ScLocalize("Enable corner indicator"), &g_Config.m_ScCornerIndicator, &Content, LineSize);

	if(CornerIndicatorExpandedHeight > 0.5f)
	{
		CUIRect Visible = CornerIndicatorView;
		Visible.h = CornerIndicatorExpandedHeight;
		Ui()->ClipEnable(&Visible);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicatorOnlyInTeam, ScLocalize("Only in team"), &g_Config.m_ScCornerIndicatorOnlyInTeam, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicatorShowAlive, ScLocalize("Show alive teammates"), &g_Config.m_ScCornerIndicatorShowAlive, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicatorShowFrozen, ScLocalize("Show frozen teammates"), &g_Config.m_ScCornerIndicatorShowFrozen, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicatorIndicateLastTeammate, ScLocalize("Indicate last teammate"), &g_Config.m_ScCornerIndicatorIndicateLastTeammate, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicatorProximity, ScLocalize("Scale by distance"), &g_Config.m_ScCornerIndicatorProximity, &Content, LineSize);

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicatorNinjaFrozen, ScLocalize("Ninja skin when frozen"), &g_Config.m_ScCornerIndicatorNinjaFrozen, &Content, LineSize);

		if(CornerIndicatorShowSizes)
		{
			CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
			CornerIndicatorView.HSplitTop(LineSize, &Button, &CornerIndicatorView);
			Ui()->DoScrollbarOption(&g_Config.m_ScCornerIndicatorSizeAlive, &g_Config.m_ScCornerIndicatorSizeAlive, &Button, ScLocalize("Alive size"), 12, 128);

			CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
			CornerIndicatorView.HSplitTop(LineSize, &Button, &CornerIndicatorView);
			Ui()->DoScrollbarOption(&g_Config.m_ScCornerIndicatorSizeFrozen, &g_Config.m_ScCornerIndicatorSizeFrozen, &Button, ScLocalize("Frozen size"), 12, 128);

			CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
			CornerIndicatorView.HSplitTop(LineSize, &Button, &CornerIndicatorView);
			Ui()->DoScrollbarOption(&g_Config.m_ScCornerIndicatorLastTeammateSize, &g_Config.m_ScCornerIndicatorLastTeammateSize, &Button, ScLocalize("Last teammate size"), 12, 128);
		}

		CornerIndicatorView.HSplitTop(MarginSmall, nullptr, &CornerIndicatorView);
		CornerIndicatorView.HSplitTop(LineSize, &Content, &CornerIndicatorView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCornerIndicatorCustomColors, ScLocalize("Use custom colors"), &g_Config.m_ScCornerIndicatorCustomColors, &Content, LineSize);

		if(CornerIndicatorShowCustomColors)
		{
			static CButtonContainer s_CornerIndicatorColorAliveButton;
			static CButtonContainer s_CornerIndicatorColorFrozenButton;
			static CButtonContainer s_CornerIndicatorLastTeammateColorButton;
			DoLine_ColorPicker(&s_CornerIndicatorColorAliveButton, CornerIndicatorColorPickerLineSize, 13.0f, CornerIndicatorColorPickerSpacing, &CornerIndicatorView, ScLocalize("Alive color"), &g_Config.m_ScCornerIndicatorColorAlive, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ScCornerIndicatorColorAlive, true)), false, nullptr, true);
			DoLine_ColorPicker(&s_CornerIndicatorColorFrozenButton, CornerIndicatorColorPickerLineSize, 13.0f, CornerIndicatorColorPickerSpacing, &CornerIndicatorView, ScLocalize("Frozen color"), &g_Config.m_ScCornerIndicatorColorFrozen, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ScCornerIndicatorColorFrozen, true)), false, nullptr, true);
			DoLine_ColorPicker(&s_CornerIndicatorLastTeammateColorButton, CornerIndicatorColorPickerLineSize, 13.0f, CornerIndicatorColorPickerSpacing, &CornerIndicatorView, ScLocalize("Last teammate color"), &g_Config.m_ScCornerIndicatorLastTeammateColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ScCornerIndicatorLastTeammateColor, true)), false, nullptr, true);
		}

		Ui()->ClipDisable();
	}

	if(CornerIndicatorBlocked)
	{
		CUIRect HintArea, HintRow;
		CUIRect BlockForHint = CornerIndicatorBlock;
		BlockForHint.HSplitBottom(CornerIndicatorBlockedHintHeight, nullptr, &HintArea);
		HintArea.HSplitTop(MarginSmall, nullptr, &HintRow);
		HintRow.h = LineSize;
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&HintRow, ScLocalize("Looks like you're on a server where this feature is forbidden"), 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	{
		const bool TsEnabled = g_Config.m_ScTeamScoreboard != 0;
		const float TsExpandedTarget = 3.0f * (MarginSmall + LineSize);
		const float TsExpanded = SC_MODULE_REVEAL(TsEnabled, TsExpandedTarget, Client()->RenderFrameTime());
		const float TsBlockHeight = LineSize + MarginSmall + LineSize + TsExpanded;
		CUIRect TsBlock;
		Column.HSplitTop(TsBlockHeight, &TsBlock, &Column);

		CUIRect TsBlockBg = TsBlock;
		TsBlockBg.w += BlockPadding;
		TsBlockBg.h += BlockPadding;
		TsBlockBg.x -= BlockPadding * 0.5f;
		TsBlockBg.y -= BlockPadding * 0.5f;
		SixClientUiTheme::DrawUiBlock(&TsBlockBg, IGraphics::CORNER_ALL, 10.0f);
		SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScTeamScoreboard, &TsBlockBg);

		CUIRect TsView = TsBlock;
		CUIRect TsLabel, TsContent, TsButton;
		TsView.HSplitTop(LineSize, &TsLabel, &TsView);
		Ui()->DoLabel(&TsLabel, ScLocalize("Team scoreboard"), HeadlineFontSize, TEXTALIGN_ML);
		TsView.HSplitTop(MarginSmall, nullptr, &TsView);
		TsView.HSplitTop(LineSize, &TsContent, &TsView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScTeamScoreboard, ScLocalize("Enable team scoreboard"), &g_Config.m_ScTeamScoreboard, &TsContent, LineSize);

		if(TsExpanded > 0.5f)
		{
			CUIRect TsVisible = TsView;
			TsVisible.h = TsExpanded;
			Ui()->ClipEnable(&TsVisible);

			TsView.HSplitTop(MarginSmall, nullptr, &TsView);
			TsView.HSplitTop(LineSize, &TsContent, &TsView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScTeamScoreboardOnlyInTeam, ScLocalize("Only in team"), &g_Config.m_ScTeamScoreboardOnlyInTeam, &TsContent, LineSize);

			TsView.HSplitTop(MarginSmall, nullptr, &TsView);
			TsView.HSplitTop(LineSize, &TsButton, &TsView);
			Ui()->DoScrollbarOption(&g_Config.m_ScTeamScoreboardSize, &g_Config.m_ScTeamScoreboardSize, &TsButton, ScLocalize("Scoreboard size"), 50, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");

			TsView.HSplitTop(MarginSmall, nullptr, &TsView);
			TsView.HSplitTop(LineSize, &TsButton, &TsView);
			Ui()->DoScrollbarOption(&g_Config.m_ScTeamScoreboardY, &g_Config.m_ScTeamScoreboardY, &TsButton, ScLocalize("Vertical offset"), 0, 280);

			Ui()->ClipDisable();
		}
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	RenderSettingsSixClientChatMediaBlock(Column);

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool ChatBubblesExpanded = g_Config.m_ScChatBubbles != 0;
	const bool ChatBubblesShowCustomColors = ChatBubblesExpanded && g_Config.m_ScChatBubbleCustomColors != 0;
	const float ChatBubbleColorPickerLineSize = 25.0f;
	const float ChatBubbleColorPickerSpacing = 5.0f;
	const float ChatBubblesExpandedTargetHeight =
		9.0f * (MarginSmall + LineSize) +
		(ChatBubblesShowCustomColors ? 3.0f * (ChatBubbleColorPickerSpacing + ChatBubbleColorPickerLineSize) : 0.0f);
	const float ChatBubblesExpandedHeight = SC_MODULE_REVEAL(ChatBubblesExpanded, ChatBubblesExpandedTargetHeight, Client()->RenderFrameTime());
	const float ChatBubblesBlockHeight = LineSize + MarginSmall + LineSize + ChatBubblesExpandedHeight;

	CUIRect ChatBubblesBlock;
	Column.HSplitTop(ChatBubblesBlockHeight, &ChatBubblesBlock, &Column);

	CUIRect ChatBubblesBlockBg = ChatBubblesBlock;
	ChatBubblesBlockBg.w += BlockPadding;
	ChatBubblesBlockBg.h += BlockPadding;
	ChatBubblesBlockBg.x -= BlockPadding * 0.5f;
	ChatBubblesBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&ChatBubblesBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScChatBubbles, &ChatBubblesBlockBg);

	MainView = ChatBubblesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect ChatBubblesTitleLabel, ChatBubblesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &ChatBubblesTitleLabel, &ChatBubblesResetButton);
	static CButtonContainer s_ChatBubblesResetButton;
	const bool ChatBubblesResetClicked = Ui()->DoButton_FontIcon(&s_ChatBubblesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ChatBubblesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_ChatBubblesResetButton, &ChatBubblesResetButton, ScLocalize("Reset to defaults"));
	if(ChatBubblesResetClicked)
	{
		g_Config.m_ScChatBubblesSelf = DefaultConfig::ScChatBubblesSelf;
		g_Config.m_ScChatBubblesDemo = DefaultConfig::ScChatBubblesDemo;
		g_Config.m_ScChatBubbleSize = DefaultConfig::ScChatBubbleSize;
		g_Config.m_ScChatBubbleShowTime = DefaultConfig::ScChatBubbleShowTime;
		g_Config.m_ScChatBubbleFadeOut = DefaultConfig::ScChatBubbleFadeOut;
		g_Config.m_ScChatBubbleFadeIn = DefaultConfig::ScChatBubbleFadeIn;
		g_Config.m_ScChatBubbleAnimation = DefaultConfig::ScChatBubbleAnimation;
		g_Config.m_ScChatBubbleCustomColors = DefaultConfig::ScChatBubbleCustomColors;
		g_Config.m_ScChatBubbleBgColor = DefaultConfig::ScChatBubbleBgColor;
		g_Config.m_ScChatBubbleTextColor = DefaultConfig::ScChatBubbleTextColor;
		g_Config.m_ScChatBubbleOutlineColor = DefaultConfig::ScChatBubbleOutlineColor;
		g_Config.m_ScChatBubbleRounding = DefaultConfig::ScChatBubbleRounding;
	}
	ChatBubblesTitleLabel.VSplitRight(MarginSmall, &ChatBubblesTitleLabel, nullptr);
	Ui()->DoLabel(&ChatBubblesTitleLabel, ScLocalize("Chat Bubbles"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatBubbles, ScLocalize("Show chat bubbles above players"), &g_Config.m_ScChatBubbles, &Content, LineSize);

	if(ChatBubblesExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = ChatBubblesExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatBubblesSelf, ScLocalize("Show chat bubbles above you"), &g_Config.m_ScChatBubblesSelf, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatBubblesDemo, ScLocalize("Show chat bubbles in demo"), &g_Config.m_ScChatBubblesDemo, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatBubbleSize, &g_Config.m_ScChatBubbleSize, &Button, ScLocalize("Chat bubble size"), 20, 30);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatBubbleShowTime, &g_Config.m_ScChatBubbleShowTime, &Button, ScLocalize("Show for"), 100, 1000);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatBubbleFadeIn, &g_Config.m_ScChatBubbleFadeIn, &Button, ScLocalize("Fade in"), 15, 100);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatBubbleFadeOut, &g_Config.m_ScChatBubbleFadeOut, &Button, ScLocalize("Fade out"), 15, 100);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatBubbleAnimation, ScLocalize("Stack animation"), &g_Config.m_ScChatBubbleAnimation, &Content, LineSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatBubbleRounding, &g_Config.m_ScChatBubbleRounding, &Button, ScLocalize("Corner rounding"), 0, 30);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatBubbleCustomColors, ScLocalize("Custom colors"), &g_Config.m_ScChatBubbleCustomColors, &Content, LineSize);

		if(ChatBubblesShowCustomColors)
		{
			static CButtonContainer s_ChatBubbleBgColorButton;
			DoLine_ColorPicker(&s_ChatBubbleBgColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, ScLocalize("Background"), &g_Config.m_ScChatBubbleBgColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ScChatBubbleBgColor, true)), false, nullptr, true);

			static CButtonContainer s_ChatBubbleTextColorButton;
			DoLine_ColorPicker(&s_ChatBubbleTextColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, ScLocalize("Text"), &g_Config.m_ScChatBubbleTextColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ScChatBubbleTextColor, true)), false, nullptr, true);

			static CButtonContainer s_ChatBubbleOutlineColorButton;
			DoLine_ColorPicker(&s_ChatBubbleOutlineColorButton, ChatBubbleColorPickerLineSize, 13.0f, ChatBubbleColorPickerSpacing, &MainView, ScLocalize("Outline"), &g_Config.m_ScChatBubbleOutlineColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ScChatBubbleOutlineColor, true)), false, nullptr, true);
		}

		Ui()->ClipDisable();
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float MediaBackgroundBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize;

	CUIRect MediaBackgroundBlock;
	Column.HSplitTop(MediaBackgroundBlockHeight, &MediaBackgroundBlock, &Column);

	CUIRect MediaBackgroundBlockBg = MediaBackgroundBlock;
	MediaBackgroundBlockBg.w += BlockPadding;
	MediaBackgroundBlockBg.h += BlockPadding;
	MediaBackgroundBlockBg.x -= BlockPadding * 0.5f;
	MediaBackgroundBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&MediaBackgroundBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScMenuMediaBackground, &MediaBackgroundBlockBg);

	MainView = MediaBackgroundBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, ScLocalize("Media Background"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	const bool MenuMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMenuMediaBackground, ScLocalize("Enable to main menu"), &g_Config.m_ScMenuMediaBackground, &Content, LineSize);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	const bool GameMediaChanged = DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScGameMediaBackground, ScLocalize("Enable to game background"), &g_Config.m_ScGameMediaBackground, &Content, LineSize);

	auto ReloadMenuMediaFromConfig = [&]() {
		CMenuMediaBackground &MediaBackground = GameClient()->m_SixClient.MenuMediaBackground();
		const bool MediaStatusInGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		if(MediaStatusInGame)
			MediaBackground.ReloadFromConfig(g_Config.m_ScGameMediaBackground, g_Config.m_ScMenuMediaBackgroundPath);
		else
			MediaBackground.ReloadFromConfig(g_Config.m_ScMenuMediaBackground, g_Config.m_ScMenuMediaBackgroundPath);
	};

	if(MenuMediaChanged || GameMediaChanged)
		ReloadMenuMediaFromConfig();

	struct SMenuMediaFileListContext
	{
		std::vector<std::string> *m_pLabels;
		std::vector<std::string> *m_pPaths;
	};

	auto MenuMediaFileListScan = [](const char *pName, int IsDir, int StorageType, void *pUser) {
		(void)StorageType;
		if(IsDir)
			return 0;

		auto *pContext = static_cast<SMenuMediaFileListContext *>(pUser);
		const std::string Ext = MediaDecoder::ExtractExtensionLower(pName);
		const bool SupportedImage = Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "webp" || Ext == "bmp" || Ext == "avif" || Ext == "gif";
		const bool SupportedVideo = Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi";
		if(!SupportedImage && !SupportedVideo)
			return 0;

		pContext->m_pLabels->emplace_back(pName);
		pContext->m_pPaths->emplace_back(std::string("667Client/backgrounds/") + pName);
		return 0;
	};

	Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("667Client/backgrounds", IStorage::TYPE_SAVE);

	static std::vector<std::string> s_vMenuMediaFileLabels;
	static std::vector<std::string> s_vMenuMediaFilePaths;
	s_vMenuMediaFileLabels.clear();
	s_vMenuMediaFilePaths.clear();
	SMenuMediaFileListContext MenuMediaContext{&s_vMenuMediaFileLabels, &s_vMenuMediaFilePaths};
	Storage()->ListDirectory(IStorage::TYPE_SAVE, "667Client/backgrounds", MenuMediaFileListScan, &MenuMediaContext);

	std::vector<int> vSortedIndices(s_vMenuMediaFileLabels.size());
	for(size_t i = 0; i < vSortedIndices.size(); ++i)
		vSortedIndices[i] = (int)i;
	std::sort(vSortedIndices.begin(), vSortedIndices.end(), [&](int Left, int Right) {
		return str_comp_nocase(s_vMenuMediaFileLabels[Left].c_str(), s_vMenuMediaFileLabels[Right].c_str()) < 0;
	});

	static std::vector<std::string> s_vMenuMediaDropDownLabels;
	static std::vector<const char *> s_vMenuMediaDropDownLabelPtrs;
	s_vMenuMediaDropDownLabels.clear();
	s_vMenuMediaDropDownLabelPtrs.clear();
	for(int SortedIndex : vSortedIndices)
		s_vMenuMediaDropDownLabels.push_back(s_vMenuMediaFileLabels[SortedIndex]);
	for(const std::string &LabelString : s_vMenuMediaDropDownLabels)
		s_vMenuMediaDropDownLabelPtrs.push_back(LabelString.c_str());

	int SelectedMediaFile = -1;
	for(size_t i = 0; i < vSortedIndices.size(); ++i)
	{
		const int SortedIndex = vSortedIndices[i];
		if(str_comp(g_Config.m_ScMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str()) == 0)
		{
			SelectedMediaFile = (int)i;
			break;
		}
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	CUIRect MediaPathRow, MediaFileDropDown, MediaReloadButton, MediaFolderButton;
	MainView.HSplitTop(LineSize, &MediaPathRow, &MainView);
	MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaFolderButton);
	MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
	MediaPathRow.VSplitRight(20.0f, &MediaPathRow, &MediaReloadButton);
	MediaPathRow.VSplitRight(MarginSmall, &MediaPathRow, nullptr);
	MediaFileDropDown = MediaPathRow;

	if(s_vMenuMediaDropDownLabelPtrs.empty())
	{
		static CButtonContainer s_MenuMediaEmptyButton;
		DoButton_Menu(&s_MenuMediaEmptyButton, ScLocalize("No media files in backgrounds folder"), -1, &MediaFileDropDown);
	}
	else
	{
		static CUi::SDropDownState s_MenuMediaFileDropDownState;
		static CScrollRegion s_MenuMediaFileDropDownScrollRegion;
		s_MenuMediaFileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_MenuMediaFileDropDownScrollRegion;
		const int NewSelectedMediaFile = Ui()->DoDropDown(&MediaFileDropDown, SelectedMediaFile, s_vMenuMediaDropDownLabelPtrs.data(), s_vMenuMediaDropDownLabelPtrs.size(), s_MenuMediaFileDropDownState);
		if(NewSelectedMediaFile != SelectedMediaFile && NewSelectedMediaFile >= 0 && NewSelectedMediaFile < (int)vSortedIndices.size())
		{
			const int SortedIndex = vSortedIndices[NewSelectedMediaFile];
			str_copy(g_Config.m_ScMenuMediaBackgroundPath, s_vMenuMediaFilePaths[SortedIndex].c_str(), sizeof(g_Config.m_ScMenuMediaBackgroundPath));
			ReloadMenuMediaFromConfig();
		}
	}

	static CButtonContainer s_MenuMediaReloadButton;
	if(Ui()->DoButton_FontIcon(&s_MenuMediaReloadButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &MediaReloadButton, BUTTONFLAG_LEFT))
		ReloadMenuMediaFromConfig();

	static CButtonContainer s_MenuMediaFolderButton;
	if(Ui()->DoButton_FontIcon(&s_MenuMediaFolderButton, FontIcon::FOLDER, 0, &MediaFolderButton, BUTTONFLAG_LEFT))
	{
		Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("667Client/backgrounds", IStorage::TYPE_SAVE);
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "667Client/backgrounds", aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Button, &MainView);
	Ui()->DoScrollbarOption(&g_Config.m_ScGameMediaBackgroundOffset, &g_Config.m_ScGameMediaBackgroundOffset, &Button, ScLocalize("Map offset"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScGameMediaBackgroundOffset, &Button, ScLocalize("0 keeps the image fixed to the screen. Higher values add camera parallax. Move the camera to see the effect."));

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Button, &MainView);
	const bool MediaStatusInGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const CMenuMediaBackground &MediaStatusSource = GameClient()->m_SixClient.MenuMediaBackground();
	const char *pMediaStatusText = MediaStatusSource.StatusText();
	char aMediaStatusBuf[256];
	if(MediaStatusInGame && MediaStatusSource.IsLoaded() && g_Config.m_ScGameMediaBackground && g_Config.m_ClOverlayEntities == 0)
	{
		str_format(aMediaStatusBuf, sizeof(aMediaStatusBuf), "%s %s", pMediaStatusText, ScLocalize("Hidden: overlay entities is 0."));
		pMediaStatusText = aMediaStatusBuf;
		TextRender()->TextColor(ColorRGBA(1.0f, 0.85f, 0.45f, 1.0f));
	}
	else if(MediaStatusSource.HasError())
		TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
	else if(MediaStatusSource.IsLoaded())
		TextRender()->TextColor(ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
	Ui()->DoLabel(&Button, pMediaStatusText, 11.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	{
		const bool RainEnabled = g_Config.m_ScMenuRainBackground != 0;
		const float RainExpanded = SC_MODULE_REVEAL(RainEnabled, MarginSmall + LineSize, Client()->RenderFrameTime());
		const float RainBlockHeight = LineSize + MarginSmall + LineSize + RainExpanded + 2.0f * (MarginSmall + LineSize);
		CUIRect RainBlock;
		Column.HSplitTop(RainBlockHeight, &RainBlock, &Column);

		CUIRect RainBlockBg = RainBlock;
		RainBlockBg.w += BlockPadding;
		RainBlockBg.h += BlockPadding;
		RainBlockBg.x -= BlockPadding * 0.5f;
		RainBlockBg.y -= BlockPadding * 0.5f;
		SixClientUiTheme::DrawUiBlock(&RainBlockBg, IGraphics::CORNER_ALL, 10.0f);
		SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScMenuRainBackground, &RainBlockBg);

		CUIRect RainView = RainBlock;
		CUIRect RainLabel, RainContent;
		RainView.HSplitTop(LineSize, &RainLabel, &RainView);
		Ui()->DoLabel(&RainLabel, ScLocalize("Main menu"), HeadlineFontSize, TEXTALIGN_ML);
		RainView.HSplitTop(MarginSmall, nullptr, &RainView);
		RainView.HSplitTop(LineSize, &RainContent, &RainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMenuRainBackground, ScLocalize("Rain and spotlight in main menu"), &g_Config.m_ScMenuRainBackground, &RainContent, LineSize);

		if(RainExpanded > 0.5f)
		{
			CUIRect RainVisible = RainView;
			RainVisible.h = RainExpanded;
			Ui()->ClipEnable(&RainVisible);
			RainView.HSplitTop(MarginSmall, nullptr, &RainView);
			RainView.HSplitTop(LineSize, &RainContent, &RainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMenuRainTone, ScLocalize("Violet tone"), &g_Config.m_ScMenuRainTone, &RainContent, LineSize);
			Ui()->ClipDisable();
		}

		RainView = RainBlock;
		RainView.HSplitBottom(2.0f * (MarginSmall + LineSize), nullptr, &RainView);
		RainView.HSplitTop(MarginSmall, nullptr, &RainView);
		RainView.HSplitTop(LineSize, &RainContent, &RainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScStartMenuTextLogo, ScLocalize("Cecen Client text logo"), &g_Config.m_ScStartMenuTextLogo, &RainContent, LineSize);
		RainView.HSplitTop(MarginSmall, nullptr, &RainView);
		RainView.HSplitTop(LineSize, &RainContent, &RainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScStartMenuMonoImages, ScLocalize("Black and white menu images"), &g_Config.m_ScStartMenuMonoImages, &RainContent, LineSize);
	}


	const float LeftColumnEndY = Column.y;

	CUIRect RightColumn = RightView;
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	// 667client: settings layout, the classic tabs or the Cecen UI hub and categories
	{
		const float SwitchHeight = 24.0f;
		CUIRect CecenUiBlock;
		RightColumn.HSplitTop(LineSize + MarginSmall + SwitchHeight, &CecenUiBlock, &RightColumn);
		RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);
		CUIRect CecenUiBlockBg = CecenUiBlock;
		CecenUiBlockBg.w += BlockPadding;
		CecenUiBlockBg.h += BlockPadding;
		CecenUiBlockBg.x -= BlockPadding * 0.5f;
		CecenUiBlockBg.y -= BlockPadding * 0.5f;
		SixClientUiTheme::DrawUiBlock(&CecenUiBlockBg, IGraphics::CORNER_ALL, 10.0f);
		SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScUiStyle, &CecenUiBlockBg);

		CecenUiBlock.HSplitTop(LineSize, &Label, &CecenUiBlock);
		Ui()->DoLabel(&Label, ScLocalize("Cecen UI"), HeadlineFontSize, TEXTALIGN_ML);
		CecenUiBlock.HSplitTop(MarginSmall, nullptr, &CecenUiBlock);

		CUIRect Switch, OldButton, NewButton;
		CecenUiBlock.HSplitTop(SwitchHeight, &Switch, &CecenUiBlock);
		Switch.VSplitMid(&OldButton, &NewButton);
		static CButtonContainer s_OldUiButton, s_CecenUiButton;
		if(DoButton_Menu(&s_OldUiButton, ScLocalize("Old UI"), g_Config.m_ScUiStyle == 0, &OldButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_ScUiStyle = 0;
		if(DoButton_Menu(&s_CecenUiButton, ScLocalize("Cecen UI"), g_Config.m_ScUiStyle == 1, &NewButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_ScUiStyle = 1;
		GameClient()->m_Tooltips.DoToolTip(&s_OldUiButton, &OldButton, ScLocalize("Classic settings layout with tabs"));
		GameClient()->m_Tooltips.DoToolTip(&s_CecenUiButton, &NewButton, ScLocalize("Settings hub with categories"));
	}

	const float ColorPickerLineSize = 25.0f;
	const float ColorPickerSpacing = 5.0f;

	const bool UiThemesExpanded = g_Config.m_ScUiThemes != 0;
	const float UiThemesHeaderHeight = LineSize + MarginSmall + LineSize;
	const float UiThemesBackgroundHeight = MarginSmall + LineSize + (g_Config.m_ScCustomUiBackground ? (ColorPickerSpacing + ColorPickerLineSize) : 0.0f);
	const float UiThemesExportImportHeight = MarginSmall + LineSize;
	float UiThemesCustomHeight = 0.0f;
	if(UiThemesExpanded)
	{
		g_Config.m_ScUiThemeElement = std::clamp(g_Config.m_ScUiThemeElement, 0, SixClientUiTheme::UI_THEME_ELEM_NUM - 1);
		const int Elem = g_Config.m_ScUiThemeElement;
		const int aEnabled[] = {
			g_Config.m_ScCustomUiText, g_Config.m_ScCustomUiCheckbox, g_Config.m_ScCustomUiScrollbar, g_Config.m_ScCustomUiButton,
			g_Config.m_ScCustomUiDropdown, g_Config.m_ScCustomUiEditBox, g_Config.m_ScCustomUiBlock, g_Config.m_ScCustomUiMainPanel,
			g_Config.m_ScCustomUiNavTab, g_Config.m_ScCustomUiBadge, g_Config.m_ScCustomUiBind};
		const int aGradient[] = {
			g_Config.m_ScCustomUiTextGradient, g_Config.m_ScCustomUiCheckboxGradient, g_Config.m_ScCustomUiScrollbarGradient, g_Config.m_ScCustomUiButtonGradient,
			g_Config.m_ScCustomUiDropdownGradient, g_Config.m_ScCustomUiEditBoxGradient, g_Config.m_ScCustomUiBlockGradient, g_Config.m_ScCustomUiMainPanelGradient,
			g_Config.m_ScCustomUiNavTabGradient, g_Config.m_ScCustomUiBadgeGradient, g_Config.m_ScCustomUiBindGradient};
		const int aUseAccent[] = {
			g_Config.m_ScCustomUiTextUseAccent, g_Config.m_ScCustomUiCheckboxUseAccent, g_Config.m_ScCustomUiScrollbarUseAccent, g_Config.m_ScCustomUiButtonUseAccent,
			g_Config.m_ScCustomUiDropdownUseAccent, g_Config.m_ScCustomUiEditBoxUseAccent, g_Config.m_ScCustomUiBlockUseAccent, g_Config.m_ScCustomUiMainPanelUseAccent,
			g_Config.m_ScCustomUiNavTabUseAccent, g_Config.m_ScCustomUiBadgeUseAccent, g_Config.m_ScCustomUiBindUseAccent};
		UiThemesCustomHeight = MarginSmall + LineSize;
		UiThemesCustomHeight += MarginSmall + (aEnabled[Elem] ? ColorPickerLineSize : LineSize);
		if(aEnabled[Elem])
		{
			UiThemesCustomHeight += MarginSmall + (aGradient[Elem] ? ColorPickerLineSize : LineSize);
			if(aGradient[Elem])
				UiThemesCustomHeight += MarginSmall + LineSize;
			UiThemesCustomHeight += MarginSmall + (aUseAccent[Elem] ? ColorPickerLineSize : LineSize);
			if(Elem == SixClientUiTheme::UI_THEME_ELEM_BLOCK || Elem == SixClientUiTheme::UI_THEME_ELEM_PANEL || Elem == SixClientUiTheme::UI_THEME_ELEM_NAV)
				UiThemesCustomHeight += MarginSmall + LineSize;
		}
	}
	const float UiThemesExpandedTargetHeight = UiThemesCustomHeight + UiThemesBackgroundHeight + UiThemesExportImportHeight;
	const float UiThemesExpandedHeight = SC_MODULE_REVEAL(UiThemesExpanded, UiThemesExpandedTargetHeight, Client()->RenderFrameTime());
	const float UiThemesBlockHeight = UiThemesHeaderHeight + UiThemesExpandedHeight;
	CUIRect UiThemesBlock;
	RightColumn.HSplitTop(UiThemesBlockHeight, &UiThemesBlock, &RightColumn);

	CUIRect UiThemesBlockBg = UiThemesBlock;
	UiThemesBlockBg.w += BlockPadding;
	UiThemesBlockBg.h += BlockPadding;
	UiThemesBlockBg.x -= BlockPadding * 0.5f;
	UiThemesBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&UiThemesBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScUiThemes, &UiThemesBlockBg);

	MainView = UiThemesBlock;
	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect UiThemesTitleLabel, UiThemesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &UiThemesTitleLabel, &UiThemesResetButton);
	static CButtonContainer s_UiThemesResetButton;
	const bool UiThemesResetClicked = Ui()->DoButton_FontIcon(&s_UiThemesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &UiThemesResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_UiThemesResetButton, &UiThemesResetButton, ScLocalize("Reset to defaults"));
	if(UiThemesResetClicked)
	{
		g_Config.m_ScUiThemeElement = 0;
		g_Config.m_ScCustomUiBackground = 0;
		g_Config.m_ScCustomUiText = 0;
		g_Config.m_ScCustomUiTextColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiTextGradient = 0;
		g_Config.m_ScCustomUiTextGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiTextUseAccent = 0;
		g_Config.m_ScCustomUiTextAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiCheckbox = 0;
		g_Config.m_ScCustomUiCheckboxColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiCheckboxGradient = 0;
		g_Config.m_ScCustomUiCheckboxGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiCheckboxUseAccent = 0;
		g_Config.m_ScCustomUiCheckboxAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiScrollbar = 0;
		g_Config.m_ScCustomUiScrollbarColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiScrollbarGradient = 0;
		g_Config.m_ScCustomUiScrollbarGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiScrollbarUseAccent = 0;
		g_Config.m_ScCustomUiScrollbarAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiButton = 0;
		g_Config.m_ScCustomUiButtonColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiButtonGradient = 0;
		g_Config.m_ScCustomUiButtonGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiButtonUseAccent = 0;
		g_Config.m_ScCustomUiButtonAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiDropdown = 0;
		g_Config.m_ScCustomUiDropdownColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiDropdownGradient = 0;
		g_Config.m_ScCustomUiDropdownGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiDropdownUseAccent = 0;
		g_Config.m_ScCustomUiDropdownAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiEditBox = 0;
		g_Config.m_ScCustomUiEditBoxColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiEditBoxGradient = 0;
		g_Config.m_ScCustomUiEditBoxGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiEditBoxUseAccent = 0;
		g_Config.m_ScCustomUiEditBoxAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiBlock = 0;
		g_Config.m_ScCustomUiBlockColor = 0xFF000000U;
		g_Config.m_ScCustomUiBlockGradient = 0;
		g_Config.m_ScCustomUiBlockGradientColor = 0xFF000000U;
		g_Config.m_ScCustomUiBlockUseAccent = 0;
		g_Config.m_ScCustomUiBlockAccentColor = 0xFF000000U;
		g_Config.m_ScCustomUiBlockOpacity = 25;
		g_Config.m_ScCustomUiMainPanel = 0;
		g_Config.m_ScCustomUiMainPanelColor = 0xFF000000U;
		g_Config.m_ScCustomUiMainPanelGradient = 0;
		g_Config.m_ScCustomUiMainPanelGradientColor = 0xFF000000U;
		g_Config.m_ScCustomUiMainPanelUseAccent = 0;
		g_Config.m_ScCustomUiMainPanelAccentColor = 0xFF000000U;
		g_Config.m_ScCustomUiMainPanelOpacity = 50;
		g_Config.m_ScCustomUiNavTab = 0;
		g_Config.m_ScCustomUiNavTabColor = 0xFF000000U;
		g_Config.m_ScCustomUiNavTabGradient = 0;
		g_Config.m_ScCustomUiNavTabGradientColor = 0xFF000000U;
		g_Config.m_ScCustomUiNavTabUseAccent = 0;
		g_Config.m_ScCustomUiNavTabAccentColor = 0xFF000000U;
		g_Config.m_ScCustomUiNavTabOpacity = 50;
		g_Config.m_ScCustomUiBadge = 0;
		g_Config.m_ScCustomUiBadgeColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiBadgeGradient = 0;
		g_Config.m_ScCustomUiBadgeGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiBadgeUseAccent = 0;
		g_Config.m_ScCustomUiBadgeAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiBind = 0;
		g_Config.m_ScCustomUiBindColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiBindGradient = 0;
		g_Config.m_ScCustomUiBindGradientColor = 0xFFA0A0A0U;
		g_Config.m_ScCustomUiBindUseAccent = 0;
		g_Config.m_ScCustomUiBindAccentColor = 0xFFFFFFFFU;
		g_Config.m_ScCustomUiGradientAnimateSpeed = 35;
		g_Config.m_UiColor = 0xE4A046AFU;
	}
	UiThemesTitleLabel.VSplitRight(MarginSmall, &UiThemesTitleLabel, nullptr);
	Ui()->DoLabel(&UiThemesTitleLabel, ScLocalize("UI Themes"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScUiThemes, ScLocalize("Enable"), &g_Config.m_ScUiThemes, &Content, LineSize);

	if(UiThemesExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = UiThemesExpandedHeight;
		Ui()->ClipEnable(&Visible);

		int *apEnable[] = {
			&g_Config.m_ScCustomUiText, &g_Config.m_ScCustomUiCheckbox, &g_Config.m_ScCustomUiScrollbar, &g_Config.m_ScCustomUiButton,
			&g_Config.m_ScCustomUiDropdown, &g_Config.m_ScCustomUiEditBox, &g_Config.m_ScCustomUiBlock, &g_Config.m_ScCustomUiMainPanel,
			&g_Config.m_ScCustomUiNavTab, &g_Config.m_ScCustomUiBadge, &g_Config.m_ScCustomUiBind};
		unsigned *apColor[] = {
			&g_Config.m_ScCustomUiTextColor, &g_Config.m_ScCustomUiCheckboxColor, &g_Config.m_ScCustomUiScrollbarColor, &g_Config.m_ScCustomUiButtonColor,
			&g_Config.m_ScCustomUiDropdownColor, &g_Config.m_ScCustomUiEditBoxColor, &g_Config.m_ScCustomUiBlockColor, &g_Config.m_ScCustomUiMainPanelColor,
			&g_Config.m_ScCustomUiNavTabColor, &g_Config.m_ScCustomUiBadgeColor, &g_Config.m_ScCustomUiBindColor};
		int *apGradient[] = {
			&g_Config.m_ScCustomUiTextGradient, &g_Config.m_ScCustomUiCheckboxGradient, &g_Config.m_ScCustomUiScrollbarGradient, &g_Config.m_ScCustomUiButtonGradient,
			&g_Config.m_ScCustomUiDropdownGradient, &g_Config.m_ScCustomUiEditBoxGradient, &g_Config.m_ScCustomUiBlockGradient, &g_Config.m_ScCustomUiMainPanelGradient,
			&g_Config.m_ScCustomUiNavTabGradient, &g_Config.m_ScCustomUiBadgeGradient, &g_Config.m_ScCustomUiBindGradient};
		unsigned *apGradientColor[] = {
			&g_Config.m_ScCustomUiTextGradientColor, &g_Config.m_ScCustomUiCheckboxGradientColor, &g_Config.m_ScCustomUiScrollbarGradientColor, &g_Config.m_ScCustomUiButtonGradientColor,
			&g_Config.m_ScCustomUiDropdownGradientColor, &g_Config.m_ScCustomUiEditBoxGradientColor, &g_Config.m_ScCustomUiBlockGradientColor, &g_Config.m_ScCustomUiMainPanelGradientColor,
			&g_Config.m_ScCustomUiNavTabGradientColor, &g_Config.m_ScCustomUiBadgeGradientColor, &g_Config.m_ScCustomUiBindGradientColor};
		int *apUseAccent[] = {
			&g_Config.m_ScCustomUiTextUseAccent, &g_Config.m_ScCustomUiCheckboxUseAccent, &g_Config.m_ScCustomUiScrollbarUseAccent, &g_Config.m_ScCustomUiButtonUseAccent,
			&g_Config.m_ScCustomUiDropdownUseAccent, &g_Config.m_ScCustomUiEditBoxUseAccent, &g_Config.m_ScCustomUiBlockUseAccent, &g_Config.m_ScCustomUiMainPanelUseAccent,
			&g_Config.m_ScCustomUiNavTabUseAccent, &g_Config.m_ScCustomUiBadgeUseAccent, &g_Config.m_ScCustomUiBindUseAccent};
		unsigned *apAccentColor[] = {
			&g_Config.m_ScCustomUiTextAccentColor, &g_Config.m_ScCustomUiCheckboxAccentColor, &g_Config.m_ScCustomUiScrollbarAccentColor, &g_Config.m_ScCustomUiButtonAccentColor,
			&g_Config.m_ScCustomUiDropdownAccentColor, &g_Config.m_ScCustomUiEditBoxAccentColor, &g_Config.m_ScCustomUiBlockAccentColor, &g_Config.m_ScCustomUiMainPanelAccentColor,
			&g_Config.m_ScCustomUiNavTabAccentColor, &g_Config.m_ScCustomUiBadgeAccentColor, &g_Config.m_ScCustomUiBindAccentColor};
		const ColorRGBA aDefaults[] = {
			ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1),
			ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1), ColorRGBA(0, 0, 0, 1),
			ColorRGBA(0, 0, 0, 1), ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1, 1)};

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		const char *apUiThemeElements[] = {
			ScLocalize("Text"), ScLocalize("Checkbox"), ScLocalize("Scrollbar"), ScLocalize("Button"), ScLocalize("Droplist"),
			ScLocalize("Input field"), ScLocalize("Function block"), ScLocalize("UI background"), ScLocalize("Settings tabs"),
			ScLocalize("Badge"), ScLocalize("Binds")};
		static CUi::SDropDownState s_UiThemeElementDropDownState;
		static CScrollRegion s_UiThemeElementDropDownScrollRegion;
		s_UiThemeElementDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_UiThemeElementDropDownScrollRegion;
		g_Config.m_ScUiThemeElement = Ui()->DoDropDown(&Content, g_Config.m_ScUiThemeElement, apUiThemeElements, (int)std::size(apUiThemeElements), s_UiThemeElementDropDownState);
		g_Config.m_ScUiThemeElement = std::clamp(g_Config.m_ScUiThemeElement, 0, SixClientUiTheme::UI_THEME_ELEM_NUM - 1);
		const int Elem = g_Config.m_ScUiThemeElement;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		{
			const float EnableRowH = *apEnable[Elem] ? ColorPickerLineSize : LineSize;
			MainView.HSplitTop(EnableRowH, &Content, &MainView);
			CUIRect CheckArea, ColorPicker;
			Content.VSplitRight(*apEnable[Elem] ? ColorPickerLineSize : 0.0f, &CheckArea, &ColorPicker);
			CUIRect CheckLine = CheckArea;
			CheckLine.HMargin((CheckArea.h - LineSize) * 0.5f, &CheckLine);
			DoButton_CheckBoxAutoVMarginAndSet(apEnable[Elem], ScLocalize("Enable"), apEnable[Elem], &CheckLine, LineSize);
			if(*apEnable[Elem])
			{
				DoButton_ColorPicker(&ColorPicker, apColor[Elem], true, aDefaults[Elem], true);
				SixClientSettingsSearch::DrawItemHighlight(apColor[Elem], &ColorPicker);
			}
		}

		if(*apEnable[Elem])
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			{
				const float GradientRowH = *apGradient[Elem] ? ColorPickerLineSize : LineSize;
				MainView.HSplitTop(GradientRowH, &Content, &MainView);
				CUIRect CheckArea, GradientPicker;
				Content.VSplitRight(*apGradient[Elem] ? ColorPickerLineSize : 0.0f, &CheckArea, &GradientPicker);
				CUIRect CheckLine = CheckArea;
				CheckLine.HMargin((CheckArea.h - LineSize) * 0.5f, &CheckLine);
				const int PrevGradient = *apGradient[Elem];
				DoButton_CheckBoxAutoVMarginAndSet(apGradient[Elem], ScLocalize("Gradient"), apGradient[Elem], &CheckLine, LineSize);
				if(PrevGradient == 0 && *apGradient[Elem] != 0)
					*apGradientColor[Elem] = SixClientUiTheme::DarkenConfigColor(*apColor[Elem]);
				if(*apGradient[Elem])
				{
					DoButton_ColorPicker(&GradientPicker, apGradientColor[Elem], true, aDefaults[Elem], true);
					SixClientSettingsSearch::DrawItemHighlight(apGradientColor[Elem], &GradientPicker);
				}
			}

			if(*apGradient[Elem])
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_ScCustomUiGradientAnimateSpeed, &g_Config.m_ScCustomUiGradientAnimateSpeed, &Button, ScLocalize("Animate speed"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			{
				const float AccentRowH = *apUseAccent[Elem] ? ColorPickerLineSize : LineSize;
				MainView.HSplitTop(AccentRowH, &Content, &MainView);
				CUIRect CheckArea, AccentPicker;
				Content.VSplitRight(*apUseAccent[Elem] ? ColorPickerLineSize : 0.0f, &CheckArea, &AccentPicker);
				CUIRect CheckLine = CheckArea;
				CheckLine.HMargin((CheckArea.h - LineSize) * 0.5f, &CheckLine);
				DoButton_CheckBoxAutoVMarginAndSet(apUseAccent[Elem], ScLocalize("Accent"), apUseAccent[Elem], &CheckLine, LineSize);
				if(*apUseAccent[Elem])
				{
					DoButton_ColorPicker(&AccentPicker, apAccentColor[Elem], true, aDefaults[Elem], true);
					SixClientSettingsSearch::DrawItemHighlight(apAccentColor[Elem], &AccentPicker);
				}
			}

			if(Elem == SixClientUiTheme::UI_THEME_ELEM_BLOCK)
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_ScCustomUiBlockOpacity, &g_Config.m_ScCustomUiBlockOpacity, &Button, ScLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}
			else if(Elem == SixClientUiTheme::UI_THEME_ELEM_PANEL)
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_ScCustomUiMainPanelOpacity, &g_Config.m_ScCustomUiMainPanelOpacity, &Button, ScLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}
			else if(Elem == SixClientUiTheme::UI_THEME_ELEM_NAV)
			{
				MainView.HSplitTop(MarginSmall, nullptr, &MainView);
				MainView.HSplitTop(LineSize, &Button, &MainView);
				Ui()->DoScrollbarOption(&g_Config.m_ScCustomUiNavTabOpacity, &g_Config.m_ScCustomUiNavTabOpacity, &Button, ScLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			}
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		static char s_aSavedMenuMap[sizeof(g_Config.m_ClMenuMap)] = "";
		static bool s_HasSavedMenuMap = false;
		const int PrevBackground = g_Config.m_ScCustomUiBackground;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCustomUiBackground, ScLocalize("Enable colorable background"), &g_Config.m_ScCustomUiBackground, &Content, LineSize);
		if(PrevBackground == 0 && g_Config.m_ScCustomUiBackground != 0)
		{
			str_copy(s_aSavedMenuMap, g_Config.m_ClMenuMap);
			s_HasSavedMenuMap = true;
			str_copy(g_Config.m_ClMenuMap, "");
			GameClient()->m_MenuBackground.LoadMenuBackground(false, false);
		}
		if(PrevBackground != 0 && g_Config.m_ScCustomUiBackground == 0)
		{
			if(s_HasSavedMenuMap)
			{
				str_copy(g_Config.m_ClMenuMap, s_aSavedMenuMap);
				s_HasSavedMenuMap = false;
			}
			GameClient()->m_MenuBackground.LoadMenuBackground();
		}
		if(g_Config.m_ScCustomUiBackground)
		{
			MainView.HSplitTop(ColorPickerSpacing, nullptr, &MainView);
			static CButtonContainer s_BackgroundColorResetId;
			DoLine_ColorPicker(&s_BackgroundColorResetId, ColorPickerLineSize, 13.0f, 0.0f, &MainView, ScLocalize("Background color"), &g_Config.m_UiColor, color_cast<ColorRGBA>(ColorHSLA(0xE4A046AFU, true)), false, nullptr, true);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect ExportButton, ImportButton;
		Button.VSplitMid(&ExportButton, &ImportButton, MarginSmall);
		static CButtonContainer s_UiThemeExportButton;
		static CButtonContainer s_UiThemeImportButton;
		static int64_t s_UiThemeExportFeedbackTime = 0;
		static int64_t s_UiThemeImportFeedbackTime = 0;
		const int64_t Now = time_get();
		const int64_t FeedbackDuration = time_freq();
		const char *pExportLabel = (s_UiThemeExportFeedbackTime != 0 && Now - s_UiThemeExportFeedbackTime < FeedbackDuration) ? ScLocalize("Exported!") : ScLocalize("Export");
		const char *pImportLabel = (s_UiThemeImportFeedbackTime != 0 && Now - s_UiThemeImportFeedbackTime < FeedbackDuration) ? ScLocalize("Imported!") : ScLocalize("Import");
		if(DoButton_Menu(&s_UiThemeExportButton, pExportLabel, 0, &ExportButton, BUTTONFLAG_LEFT))
		{
			char aBuf[4096];
			aBuf[0] = '\0';
			auto AppendLine = [&](const char *pLine) {
				str_append(aBuf, pLine, sizeof(aBuf));
				str_append(aBuf, "\n", sizeof(aBuf));
			};
			auto AppendElem = [&](const char *pName, int Enable, unsigned Color, int Gradient, unsigned GradientColor, int Accent, unsigned AccentColor, int Opacity = -1) {
				char aLine[256];
				if(Opacity >= 0)
					str_format(aLine, sizeof(aLine), "%s enable=%d color=$%08X gradient=%d gradient_color=$%08X accent=%d accent_color=$%08X opacity=%d", pName, Enable, Color, Gradient, GradientColor, Accent, AccentColor, Opacity);
				else
					str_format(aLine, sizeof(aLine), "%s enable=%d color=$%08X gradient=%d gradient_color=$%08X accent=%d accent_color=$%08X", pName, Enable, Color, Gradient, GradientColor, Accent, AccentColor);
				AppendLine(aLine);
			};
			AppendLine("SC_UI_THEME_EXPORT v1");
			{
				char aLine[128];
				str_format(aLine, sizeof(aLine), "animate_speed=%d", g_Config.m_ScCustomUiGradientAnimateSpeed);
				AppendLine(aLine);
				str_format(aLine, sizeof(aLine), "background=%d", g_Config.m_ScCustomUiBackground);
				AppendLine(aLine);
				str_format(aLine, sizeof(aLine), "ui_color=$%08X", g_Config.m_UiColor);
				AppendLine(aLine);
			}
			AppendElem("text", g_Config.m_ScCustomUiText, g_Config.m_ScCustomUiTextColor, g_Config.m_ScCustomUiTextGradient, g_Config.m_ScCustomUiTextGradientColor, g_Config.m_ScCustomUiTextUseAccent, g_Config.m_ScCustomUiTextAccentColor);
			AppendElem("checkbox", g_Config.m_ScCustomUiCheckbox, g_Config.m_ScCustomUiCheckboxColor, g_Config.m_ScCustomUiCheckboxGradient, g_Config.m_ScCustomUiCheckboxGradientColor, g_Config.m_ScCustomUiCheckboxUseAccent, g_Config.m_ScCustomUiCheckboxAccentColor);
			AppendElem("scrollbar", g_Config.m_ScCustomUiScrollbar, g_Config.m_ScCustomUiScrollbarColor, g_Config.m_ScCustomUiScrollbarGradient, g_Config.m_ScCustomUiScrollbarGradientColor, g_Config.m_ScCustomUiScrollbarUseAccent, g_Config.m_ScCustomUiScrollbarAccentColor);
			AppendElem("button", g_Config.m_ScCustomUiButton, g_Config.m_ScCustomUiButtonColor, g_Config.m_ScCustomUiButtonGradient, g_Config.m_ScCustomUiButtonGradientColor, g_Config.m_ScCustomUiButtonUseAccent, g_Config.m_ScCustomUiButtonAccentColor);
			AppendElem("droplist", g_Config.m_ScCustomUiDropdown, g_Config.m_ScCustomUiDropdownColor, g_Config.m_ScCustomUiDropdownGradient, g_Config.m_ScCustomUiDropdownGradientColor, g_Config.m_ScCustomUiDropdownUseAccent, g_Config.m_ScCustomUiDropdownAccentColor);
			AppendElem("input", g_Config.m_ScCustomUiEditBox, g_Config.m_ScCustomUiEditBoxColor, g_Config.m_ScCustomUiEditBoxGradient, g_Config.m_ScCustomUiEditBoxGradientColor, g_Config.m_ScCustomUiEditBoxUseAccent, g_Config.m_ScCustomUiEditBoxAccentColor);
			AppendElem("block", g_Config.m_ScCustomUiBlock, g_Config.m_ScCustomUiBlockColor, g_Config.m_ScCustomUiBlockGradient, g_Config.m_ScCustomUiBlockGradientColor, g_Config.m_ScCustomUiBlockUseAccent, g_Config.m_ScCustomUiBlockAccentColor, g_Config.m_ScCustomUiBlockOpacity);
			AppendElem("panel", g_Config.m_ScCustomUiMainPanel, g_Config.m_ScCustomUiMainPanelColor, g_Config.m_ScCustomUiMainPanelGradient, g_Config.m_ScCustomUiMainPanelGradientColor, g_Config.m_ScCustomUiMainPanelUseAccent, g_Config.m_ScCustomUiMainPanelAccentColor, g_Config.m_ScCustomUiMainPanelOpacity);
			AppendElem("nav", g_Config.m_ScCustomUiNavTab, g_Config.m_ScCustomUiNavTabColor, g_Config.m_ScCustomUiNavTabGradient, g_Config.m_ScCustomUiNavTabGradientColor, g_Config.m_ScCustomUiNavTabUseAccent, g_Config.m_ScCustomUiNavTabAccentColor, g_Config.m_ScCustomUiNavTabOpacity);
			AppendElem("badge", g_Config.m_ScCustomUiBadge, g_Config.m_ScCustomUiBadgeColor, g_Config.m_ScCustomUiBadgeGradient, g_Config.m_ScCustomUiBadgeGradientColor, g_Config.m_ScCustomUiBadgeUseAccent, g_Config.m_ScCustomUiBadgeAccentColor);
			AppendElem("binds", g_Config.m_ScCustomUiBind, g_Config.m_ScCustomUiBindColor, g_Config.m_ScCustomUiBindGradient, g_Config.m_ScCustomUiBindGradientColor, g_Config.m_ScCustomUiBindUseAccent, g_Config.m_ScCustomUiBindAccentColor);
			Input()->SetClipboardText(aBuf);
			s_UiThemeExportFeedbackTime = Now;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_UiThemeExportButton, &ExportButton, ScLocalize("Copy theme to clipboard"));
		if(DoButton_Menu(&s_UiThemeImportButton, pImportLabel, 0, &ImportButton, BUTTONFLAG_LEFT))
		{
			const std::string Clipboard = Input()->GetClipboardText();
			if(ScUiThemeImportFromText(Clipboard.c_str()))
			{
				if(g_Config.m_ScCustomUiBackground)
				{
					str_copy(g_Config.m_ClMenuMap, "");
					GameClient()->m_MenuBackground.LoadMenuBackground(false, false);
				}
				s_UiThemeImportFeedbackTime = Now;
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&s_UiThemeImportButton, &ImportButton, ScLocalize("Paste theme from clipboard"));

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);


	const bool AnimationsExpanded = g_Config.m_ScAnimations != 0;
	const float AnimationsExpandedTargetHeight = 12.0f * (MarginSmall + LineSize);
	const float AnimationsExpandedHeight = SC_MODULE_REVEAL(AnimationsExpanded, AnimationsExpandedTargetHeight, Client()->RenderFrameTime());
	const float AnimationsBlockHeight = LineSize + MarginSmall + LineSize + AnimationsExpandedHeight;

	CUIRect AnimationsBlock;
	RightColumn.HSplitTop(AnimationsBlockHeight, &AnimationsBlock, &RightColumn);

	CUIRect AnimationsBlockBg = AnimationsBlock;
	AnimationsBlockBg.w += BlockPadding;
	AnimationsBlockBg.h += BlockPadding;
	AnimationsBlockBg.x -= BlockPadding * 0.5f;
	AnimationsBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&AnimationsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScAnimations, &AnimationsBlockBg);

	MainView = AnimationsBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect AnimationsTitleLabel, AnimationsResetButton;
	Label.VSplitRight(LineSize + 8.0f, &AnimationsTitleLabel, &AnimationsResetButton);
	static CButtonContainer s_AnimationsResetButton;
	const bool AnimationsResetClicked = Ui()->DoButton_FontIcon(&s_AnimationsResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &AnimationsResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_AnimationsResetButton, &AnimationsResetButton, ScLocalize("Reset to defaults"));
	if(AnimationsResetClicked)
	{
		g_Config.m_ScAnimations = DefaultConfig::ScAnimations;
		g_Config.m_ScModuleUiRevealAnimation = DefaultConfig::ScModuleUiRevealAnimation;
		g_Config.m_ScModuleUiRevealAnimationMs = DefaultConfig::ScModuleUiRevealAnimationMs;
		g_Config.m_ScChatAnimation = DefaultConfig::ScChatAnimation;
		g_Config.m_ScChatAnimationMs = DefaultConfig::ScChatAnimationMs;
		g_Config.m_ScChatOpenAnimation = DefaultConfig::ScChatOpenAnimation;
		g_Config.m_ScChatOpenAnimationMs = DefaultConfig::ScChatOpenAnimationMs;
		g_Config.m_ScChatTypingAnimation = DefaultConfig::ScChatTypingAnimation;
		g_Config.m_ScChatTypingAnimationMs = DefaultConfig::ScChatTypingAnimationMs;
		g_Config.m_ScKillfeedAnimation = DefaultConfig::ScKillfeedAnimation;
		g_Config.m_ScKillfeedAnimationMs = DefaultConfig::ScKillfeedAnimationMs;
		g_Config.m_ScChatAnimationType = DefaultConfig::ScChatAnimationType;
		g_Config.m_ScMainMenuAnimation = DefaultConfig::ScMainMenuAnimation;
		g_Config.m_ScMainMenuAnimationSpeed = DefaultConfig::ScMainMenuAnimationSpeed;
	}
	Ui()->DoLabel(&AnimationsTitleLabel, ScLocalize("Animations"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScAnimations, ScLocalize("Enable animations"), &g_Config.m_ScAnimations, &Content, LineSize);

	if(AnimationsExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = AnimationsExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScModuleUiRevealAnimation, ScLocalize("Settings animation"), &g_Config.m_ScModuleUiRevealAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScModuleUiRevealAnimationMs, &g_Config.m_ScModuleUiRevealAnimationMs, &Button, ScLocalize("Settings (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatAnimation, ScLocalize("Chat message animations"), &g_Config.m_ScChatAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatAnimationMs, &g_Config.m_ScChatAnimationMs, &Button, ScLocalize("Chat message (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatOpenAnimation, ScLocalize("Chat open animation"), &g_Config.m_ScChatOpenAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatOpenAnimationMs, &g_Config.m_ScChatOpenAnimationMs, &Button, ScLocalize("Chat open (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatTypingAnimation, ScLocalize("Chat typing animation"), &g_Config.m_ScChatTypingAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScChatTypingAnimationMs, &g_Config.m_ScChatTypingAnimationMs, &Button, ScLocalize("Chat typing (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScKillfeedAnimation, ScLocalize("Killfeed animation"), &g_Config.m_ScKillfeedAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScKillfeedAnimationMs, &g_Config.m_ScKillfeedAnimationMs, &Button, ScLocalize("Killfeed (ms)"), 1, 500);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMainMenuAnimation, ScLocalize("Main menu animation"), &g_Config.m_ScMainMenuAnimation, &Content, LineSize);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScMainMenuAnimationSpeed, &g_Config.m_ScMainMenuAnimationSpeed, &Button, ScLocalize("Main menu speed"), 1, 50);

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const float MusicPlayerColorPickerLineSize = 25.0f;
	const float MusicPlayerColorPickerLabelSize = 13.0f;
	const float MusicPlayerColorPickerSpacing = 5.0f;
	const bool MusicPlayerEnabled = g_Config.m_ScMusicPlayer != 0;
	const bool MusicPlayerShowStaticColor = MusicPlayerEnabled && g_Config.m_ScMusicPlayerColorMode == 0;
	const bool MusicPlayerShowLyrics = MusicPlayerEnabled && g_Config.m_ScMusicPlayerShowLyrics != 0;
	const float MusicPlayerBaseRows = 7.0f;
	const float MusicPlayerLyricsTargetHeight = MarginSmall + LineSize;
	const float MusicPlayerLyricsExpandedHeight = SC_MODULE_REVEAL(MusicPlayerShowLyrics, MusicPlayerLyricsTargetHeight, Client()->RenderFrameTime());
	const float MusicPlayerExpandedTargetHeight =
		(MarginSmall + LineSize) * MusicPlayerBaseRows +
		(MusicPlayerShowStaticColor ? MusicPlayerColorPickerSpacing + MusicPlayerColorPickerLineSize : 0.0f) +
		MusicPlayerLyricsExpandedHeight;
	const float MusicPlayerExpandedHeight = SC_MODULE_REVEAL(MusicPlayerEnabled, MusicPlayerExpandedTargetHeight, Client()->RenderFrameTime());
	const float MusicPlayerContentHeight = LineSize + MarginSmall + LineSize + MusicPlayerExpandedHeight;

	CUIRect MusicPlayerBlock;
	RightColumn.HSplitTop(MusicPlayerContentHeight, &MusicPlayerBlock, &RightColumn);

	CUIRect MusicPlayerBlockBg = MusicPlayerBlock;
	MusicPlayerBlockBg.w += BlockPadding;
	MusicPlayerBlockBg.h += BlockPadding;
	MusicPlayerBlockBg.x -= BlockPadding * 0.5f;
	MusicPlayerBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&MusicPlayerBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScMusicPlayer, &MusicPlayerBlockBg);

	MainView = MusicPlayerBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect MusicPlayerTitleLabel, MusicPlayerHudEditorButton, MusicPlayerResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Label, &MusicPlayerResetButton);
	Label.VSplitRight(MarginSmall, &Label, nullptr);
	Label.VSplitRight(LineSize + 8.0f, &MusicPlayerTitleLabel, &MusicPlayerHudEditorButton);
	static CButtonContainer s_MusicPlayerResetButton;
	if(Ui()->DoButton_FontIcon(&s_MusicPlayerResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &MusicPlayerResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_ScMusicPlayerColorMode = DefaultConfig::ScMusicPlayerColorMode;
		g_Config.m_ScMusicPlayerStaticColor = DefaultConfig::ScMusicPlayerStaticColor;
		g_Config.m_ScMusicPlayerTextScale = DefaultConfig::ScMusicPlayerTextScale;
		g_Config.m_ScMusicPlayerVisualizerMode = DefaultConfig::ScMusicPlayerVisualizerMode;
		g_Config.m_ScMusicPlayerVisualizerRounding = DefaultConfig::ScMusicPlayerVisualizerRounding;
		g_Config.m_ScMusicPlayerVisualizerColumns = DefaultConfig::ScMusicPlayerVisualizerColumns;
		g_Config.m_ScMusicPlayerShowLyrics = DefaultConfig::ScMusicPlayerShowLyrics;
		g_Config.m_ScMusicPlayerShowCurrentTime = DefaultConfig::ScMusicPlayerShowCurrentTime;
		g_Config.m_ScMusicDock = DefaultConfig::ScMusicDock;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_MusicPlayerResetButton, &MusicPlayerResetButton, ScLocalize("Reset to defaults"));
	static CButtonContainer s_MusicPlayerHudEditorButton;
	const bool MusicPlayerCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool MusicPlayerHudEditorClicked = Ui()->DoButton_FontIcon(&s_MusicPlayerHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, MusicPlayerCanOpenHudEditor ? 0 : -1, &MusicPlayerHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_MusicPlayerHudEditorButton, &MusicPlayerHudEditorButton, MusicPlayerCanOpenHudEditor ? ScLocalize("Open in HUD editor") : ScLocalize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_MusicPlayerHudEditorButton, 0.0f);
	if(MusicPlayerHudEditorClicked && MusicPlayerCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&MusicPlayerTitleLabel, ScLocalize("Music Player"), HeadlineFontSize, TEXTALIGN_ML);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	const CUIRect MusicPlayerEnableRow = Content;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMusicPlayer, ScLocalize("Enable music player"), &g_Config.m_ScMusicPlayer, &Content, LineSize);
	DoHighFpsIntakeTooltip(&g_Config.m_ScMusicPlayer, MusicPlayerEnableRow);

	if(MusicPlayerExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = MusicPlayerExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);

		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerColorModeLabel, MusicPlayerColorModeSelect;
		Button.VSplitLeft(150.0f, &MusicPlayerColorModeLabel, &MusicPlayerColorModeSelect);
		Ui()->DoLabel(&MusicPlayerColorModeLabel, ScLocalize("Color mode"), 14.0f, TEXTALIGN_ML);
		static CUi::SDropDownState s_MusicPlayerColorModeState;
		static CScrollRegion s_MusicPlayerColorModeScrollRegion;
		s_MusicPlayerColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerColorModeScrollRegion;
		const char *apMusicPlayerColorModes[2] = {
			ScLocalize("Static"),
			ScLocalize("Cover"),
		};
		g_Config.m_ScMusicPlayerColorMode = std::clamp(g_Config.m_ScMusicPlayerColorMode, 0, 1);
		g_Config.m_ScMusicPlayerColorMode = Ui()->DoDropDown(&MusicPlayerColorModeSelect, g_Config.m_ScMusicPlayerColorMode, apMusicPlayerColorModes, (int)std::size(apMusicPlayerColorModes), s_MusicPlayerColorModeState);

		if(MusicPlayerShowStaticColor)
		{
			static CButtonContainer s_MusicPlayerStaticColorButton;
			MainView.HSplitTop(MusicPlayerColorPickerSpacing, nullptr, &MainView);
			DoLine_ColorPicker(&s_MusicPlayerStaticColorButton, MusicPlayerColorPickerLineSize, MusicPlayerColorPickerLabelSize, MusicPlayerColorPickerSpacing, &MainView, ScLocalize("Static color"), &g_Config.m_ScMusicPlayerStaticColor, ColorRGBA(0.34f, 0.53f, 0.79f, 1.0f), false);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerVisualizerModeLabel, MusicPlayerVisualizerModeSelect;
		Button.VSplitLeft(150.0f, &MusicPlayerVisualizerModeLabel, &MusicPlayerVisualizerModeSelect);
		Ui()->DoLabel(&MusicPlayerVisualizerModeLabel, ScLocalize("Visualizer mode"), 14.0f, TEXTALIGN_ML);
		static CUi::SDropDownState s_MusicPlayerVisualizerModeState;
		static CScrollRegion s_MusicPlayerVisualizerModeScrollRegion;
		s_MusicPlayerVisualizerModeState.m_SelectionPopupContext.m_pScrollRegion = &s_MusicPlayerVisualizerModeScrollRegion;
		const char *apMusicPlayerVisualizerModes[3] = {
			ScLocalize("Bottom"),
			ScLocalize("Center"),
			ScLocalize("Up"),
		};
		g_Config.m_ScMusicPlayerVisualizerMode = std::clamp(g_Config.m_ScMusicPlayerVisualizerMode, 0, 2);
		g_Config.m_ScMusicPlayerVisualizerMode = Ui()->DoDropDown(&MusicPlayerVisualizerModeSelect, g_Config.m_ScMusicPlayerVisualizerMode, apMusicPlayerVisualizerModes, (int)std::size(apMusicPlayerVisualizerModes), s_MusicPlayerVisualizerModeState);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScMusicPlayerTextScale, &g_Config.m_ScMusicPlayerTextScale, &Button, ScLocalize("Text scale"), 70, 150, &CUi::ms_LinearScrollbarScale, 0u, "%");

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScMusicPlayerVisualizerColumns, &g_Config.m_ScMusicPlayerVisualizerColumns, &Button, ScLocalize("Columns"), 5, 10);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect MusicPlayerRoundingLabel, MusicPlayerRoundingButtons;
		Button.VSplitLeft(150.0f, &MusicPlayerRoundingLabel, &MusicPlayerRoundingButtons);
		Ui()->DoLabel(&MusicPlayerRoundingLabel, ScLocalize("Rounding"), 14.0f, TEXTALIGN_ML);
		static CButtonContainer s_MusicPlayerVisualizerRoundingCube;
		static CButtonContainer s_MusicPlayerVisualizerRoundingSoft;
		if(g_Config.m_ScMusicPlayerVisualizerRounding > 200)
			g_Config.m_ScMusicPlayerVisualizerRounding = 200;
		const int MusicPlayerRoundingPreset = g_Config.m_ScMusicPlayerVisualizerRounding < 100 ? 0 : 1;
		CUIRect MusicPlayerCubeButton, MusicPlayerSoftButton;
		const float MusicPlayerRoundingSpacing = 2.0f;
		const float MusicPlayerRoundingButtonWidth = (MusicPlayerRoundingButtons.w - MusicPlayerRoundingSpacing) / 2.0f;
		MusicPlayerRoundingButtons.VSplitLeft(MusicPlayerRoundingButtonWidth, &MusicPlayerCubeButton, &MusicPlayerSoftButton);
		MusicPlayerSoftButton.VSplitLeft(MusicPlayerRoundingSpacing, nullptr, &MusicPlayerSoftButton);
		if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingCube, ScLocalize("Cube"), MusicPlayerRoundingPreset == 0, &MusicPlayerCubeButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_ScMusicPlayerVisualizerRounding = 0;
		if(DoButton_Menu(&s_MusicPlayerVisualizerRoundingSoft, ScLocalize("Soft"), MusicPlayerRoundingPreset == 1, &MusicPlayerSoftButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_ScMusicPlayerVisualizerRounding = 200;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		{
			CUIRect LyricsRow = Content;
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMusicPlayerShowLyrics, ScLocalize("Show song lyrics"), &g_Config.m_ScMusicPlayerShowLyrics, &LyricsRow, LineSize);
		}

		if(MusicPlayerLyricsExpandedHeight > 0.5f)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMusicPlayerShowCurrentTime, ScLocalize("Show current time"), &g_Config.m_ScMusicPlayerShowCurrentTime, &Content, LineSize);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMusicDock, ScLocalize("Open dock on hover (player, lyrics, quick actions)"), &g_Config.m_ScMusicDock, &Content, LineSize);

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool AspectBlocked = GameClient()->m_AspectRatio.IsBlockedByFng();
	const int AspectMode = g_Config.m_ScCustomAspectRatioMode >= 0 ? g_Config.m_ScCustomAspectRatioMode : (g_Config.m_ScCustomAspectRatio > 0 ? 1 : 0);
	const bool AspectCustomMode = AspectMode == 2;
	const float AspectHeaderHeight = LineSize + MarginSmall + LineSize + MarginSmall + LineSize;
	const float AspectExpandedHeight = AspectCustomMode ? (MarginSmall + LineSize + MarginSmall + LineSize) : 0.0f;
	const float AspectBlockedHintHeight = AspectBlocked ? (MarginSmall + LineSize) : 0.0f;
	const float AspectBlockHeight = AspectHeaderHeight + AspectExpandedHeight + AspectBlockedHintHeight;

	CUIRect AspectBlock;
	RightColumn.HSplitTop(AspectBlockHeight, &AspectBlock, &RightColumn);

	CUIRect AspectBlockBg = AspectBlock;
	AspectBlockBg.w += BlockPadding;
	AspectBlockBg.h += BlockPadding;
	AspectBlockBg.x -= BlockPadding * 0.5f;
	AspectBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&AspectBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScCustomAspectRatioMode, &AspectBlockBg);

	MainView = AspectBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, ScLocalize("Aspect Ratio"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	const auto SplitRowLabelControl = [&](CUIRect &InRow, CUIRect &OutLabel, CUIRect &OutControl) {
		const float LabelWidth = std::clamp(InRow.w * 0.40f, 90.0f, 170.0f);
		InRow.VSplitLeft(LabelWidth, &OutLabel, &OutControl);
	};

	const char *apAspectPresetNames[] = {
		ScLocalize("Off (default)"),
		"5:4",
		"4:3",
		"3:2",
		ScLocalize("Custom"),
	};
	static const int s_aAspectPresetValues[] = {0, 125, 133, 150};
	static CUi::SDropDownState s_AspectPresetState;
	static CScrollRegion s_AspectPresetScrollRegion;
	s_AspectPresetState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetScrollRegion;

	auto GetAspectPresetIndex = [&]() -> int {
		const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
		if(AspectMode <= 0 || g_Config.m_ScCustomAspectRatio == 0)
			return 0;
		if(AspectMode == 2)
			return CustomPresetIndex;

		int BestIndex = 1;
		int BestDiff = absolute(g_Config.m_ScCustomAspectRatio - s_aAspectPresetValues[BestIndex]);
		for(size_t i = 1; i < std::size(s_aAspectPresetValues); ++i)
		{
			const int CurDiff = absolute(g_Config.m_ScCustomAspectRatio - s_aAspectPresetValues[i]);
			if(CurDiff < BestDiff)
			{
				BestDiff = CurDiff;
				BestIndex = (int)i;
			}
		}
		return BestIndex;
	};

	const int CurrentPreset = GetAspectPresetIndex();
	CUIRect PresetLabel, PresetDropDown;
	MainView.HSplitTop(LineSize, &Button, &MainView);
	SplitRowLabelControl(Button, PresetLabel, PresetDropDown);
	Ui()->DoLabel(&PresetLabel, ScLocalize("Preset"), 14.0f, TEXTALIGN_ML);
	const int NewPreset = Ui()->DoDropDown(&PresetDropDown, CurrentPreset, apAspectPresetNames, (int)std::size(apAspectPresetNames), s_AspectPresetState);
	const int CustomPresetIndex = (int)std::size(apAspectPresetNames) - 1;
	if(NewPreset != CurrentPreset)
	{
		if(NewPreset == 0)
		{
			g_Config.m_ScCustomAspectRatioMode = 0;
			g_Config.m_ScCustomAspectRatio = 0;
		}
		else if(NewPreset == CustomPresetIndex)
		{
			g_Config.m_ScCustomAspectRatioMode = 2;
			if(g_Config.m_ScCustomAspectRatio < 100)
				g_Config.m_ScCustomAspectRatio = 178;
			if(g_Config.m_ScCustomAspectRatioNum <= 0 || g_Config.m_ScCustomAspectRatioDen <= 0)
			{
				g_Config.m_ScCustomAspectRatioNum = 16;
				g_Config.m_ScCustomAspectRatioDen = 9;
				g_Config.m_ScCustomAspectRatio = 178;
			}
		}
		else
		{
			g_Config.m_ScCustomAspectRatioMode = 1;
			g_Config.m_ScCustomAspectRatio = s_aAspectPresetValues[NewPreset];
		}
		GameClient()->m_TClient.SetForcedAspect();
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	CUIRect ApplyLabel, ApplyDropDown;
	MainView.HSplitTop(LineSize, &Button, &MainView);
	SplitRowLabelControl(Button, ApplyLabel, ApplyDropDown);
	Ui()->DoLabel(&ApplyLabel, ScLocalize("Apply"), 14.0f, TEXTALIGN_ML);
	const char *apAspectApplyNames[3] = {
		ScLocalize("Game only"),
		ScLocalize("Full"),
		ScLocalize("Game no HUD"),
	};
	static CUi::SDropDownState s_AspectApplyState;
	static CScrollRegion s_AspectApplyScrollRegion;
	s_AspectApplyState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectApplyScrollRegion;
	const int CurrentApplyMode = g_Config.m_ScCustomAspectRatioApplyMode;
	const int NewApplyMode = Ui()->DoDropDown(&ApplyDropDown, CurrentApplyMode, apAspectApplyNames, (int)std::size(apAspectApplyNames), s_AspectApplyState);
	if(NewApplyMode != CurrentApplyMode)
	{
		g_Config.m_ScCustomAspectRatioApplyMode = NewApplyMode;
		GameClient()->m_TClient.SetForcedAspect();
	}

	const int EffectiveAspectMode = g_Config.m_ScCustomAspectRatioMode >= 0 ? g_Config.m_ScCustomAspectRatioMode : (g_Config.m_ScCustomAspectRatio > 0 ? 1 : 0);
	static CLineInputNumber s_CustomAspectNumeratorInput;
	static CLineInputNumber s_CustomAspectDenominatorInput;
	static bool s_CustomAspectInitialized = false;
	static int s_LastSyncedNum = -1;
	static int s_LastSyncedDen = -1;
	if(EffectiveAspectMode == 2)
	{
		const int CfgNum = g_Config.m_ScCustomAspectRatioNum > 0 ? g_Config.m_ScCustomAspectRatioNum : 16;
		const int CfgDen = g_Config.m_ScCustomAspectRatioDen > 0 ? g_Config.m_ScCustomAspectRatioDen : 9;
		if(!s_CustomAspectNumeratorInput.IsActive() && !s_CustomAspectDenominatorInput.IsActive() &&
			(!s_CustomAspectInitialized || s_LastSyncedNum != CfgNum || s_LastSyncedDen != CfgDen))
		{
			s_CustomAspectNumeratorInput.SetInteger(CfgNum);
			s_CustomAspectDenominatorInput.SetInteger(CfgDen);
			s_LastSyncedNum = CfgNum;
			s_LastSyncedDen = CfgDen;
			s_CustomAspectInitialized = true;
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect RatioLabel, RatioControls;
		MainView.HSplitTop(LineSize, &Button, &MainView);
		SplitRowLabelControl(Button, RatioLabel, RatioControls);
		Ui()->DoLabel(&RatioLabel, ScLocalize("Custom size"), 14.0f, TEXTALIGN_ML);

		CUIRect NumeratorRect, SeparatorRect, DenominatorRect;
		const float Gap = std::min(6.0f, RatioControls.w * 0.08f);
		const float SeparatorWidth = std::min(12.0f, RatioControls.w * 0.18f);
		const float FieldWidth = std::max(1.0f, (RatioControls.w - SeparatorWidth - 2.0f * Gap) / 2.0f);
		RatioControls.VSplitLeft(FieldWidth, &NumeratorRect, &RatioControls);
		RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
		RatioControls.VSplitLeft(SeparatorWidth, &SeparatorRect, &RatioControls);
		RatioControls.VSplitLeft(Gap, nullptr, &RatioControls);
		RatioControls.VSplitLeft(FieldWidth, &DenominatorRect, nullptr);

		Ui()->DoEditBox(&s_CustomAspectNumeratorInput, &NumeratorRect, 14.0f);
		Ui()->DoLabel(&SeparatorRect, ":", 14.0f, TEXTALIGN_MC);
		Ui()->DoEditBox(&s_CustomAspectDenominatorInput, &DenominatorRect, 14.0f);

		const int InputNum = std::max(1, s_CustomAspectNumeratorInput.GetInteger());
		const int InputDen = std::max(1, s_CustomAspectDenominatorInput.GetInteger());
		const bool HasPendingCustomChange = InputNum != g_Config.m_ScCustomAspectRatioNum || InputDen != g_Config.m_ScCustomAspectRatioDen;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect ButtonSpace, ApplyButton;
		MainView.HSplitTop(LineSize, &Button, &MainView);
		SplitRowLabelControl(Button, ButtonSpace, ApplyButton);
		(void)ButtonSpace;
		static CButtonContainer s_AspectApplyButton;
		if(DoButton_Menu(&s_AspectApplyButton, ScLocalize("Apply"), HasPendingCustomChange ? 0 : -1, &ApplyButton) && HasPendingCustomChange)
		{
			g_Config.m_ScCustomAspectRatioNum = InputNum;
			g_Config.m_ScCustomAspectRatioDen = InputDen;
			g_Config.m_ScCustomAspectRatio = std::clamp((int)std::lround((double)InputNum * 100.0 / (double)InputDen), 100, 1000);
			s_LastSyncedNum = InputNum;
			s_LastSyncedDen = InputDen;
			GameClient()->m_TClient.SetForcedAspect();
		}
	}
	else
	{
		s_CustomAspectInitialized = false;
		s_LastSyncedNum = -1;
		s_LastSyncedDen = -1;
	}

	if(AspectBlocked)
	{
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&Label, ScLocalize("Looks like you're on a server where this feature is forbidden"), 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);
	RenderSettingsSixClientDiscord(RightColumn);

	const float RightColumnEndY = RightColumn.y;
	CUIRect VisualsScrollContentRect;
	VisualsScrollContentRect.x = VisualsScrollContentX;
	VisualsScrollContentRect.y = std::max(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	VisualsScrollContentRect.w = VisualsScrollContentW;
	VisualsScrollContentRect.h = 0.0f;
	s_VisualsScrollRegion.AddRect(VisualsScrollContentRect);
	s_VisualsScrollRegion.End();
}

void CMenus::DoTickAmountSlider(int *pValue, const CUIRect *pRect, const char *pLabel, int Min, int Max, int Scale)
{
	CUIRect Button = *pRect;
	int Value = std::clamp(*pValue, Min, Max);

	const int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
		Value = std::clamp(Value + Increment, Min, Max);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
		Value = std::clamp(Value - Increment, Min, Max);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %.2f %s", pLabel, Value / (float)Scale, ScLocalize("ticks"));

	CUIRect Label, ScrollBar;
	Button.VSplitMid(&Label, &ScrollBar, std::min(10.0f, Button.w * 0.05f));
	Ui()->DoLabel(&Label, aBuf, Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

	const float Rel = (Value - Min) / (float)(Max - Min);
	// 667client
	const float NewRel = Ui()->DoScrollbarH(pValue, &ScrollBar, Rel, nullptr, nullptr);
	// 667client
	Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
	*pValue = std::clamp(Value, Min, Max);
}

void CMenus::RenderSettingsSixClientGameplay(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float FontSize = 14.0f;
	const float EditBoxFontSize = 12.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const float WheelPreviewHeight = 96.0f;

	static CScrollRegion s_GameplayScrollRegion;
	CScrollRegionParams GameplayScrollParams;
	GameplayScrollParams.m_ScrollUnit = 60.0f;
	GameplayScrollParams.m_ScrollbarMargin = 5.0f;
	s_GameplayScrollRegion.Begin(&MainView, &GameplayScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	const float GameplayScrollContentX = MainView.x;
	const float GameplayScrollContentW = MainView.w;

	CUIRect Content, Label, Button, LeftView, RightView;

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	CUIRect LeftColumn = LeftView;
	LeftColumn.HSplitTop(10.0f, nullptr, &LeftColumn);

	const int InputsMode = g_Config.m_ScInputs;
	const bool InputsEnabled = InputsMode != SC_INPUTS_OFF;
	const float SixInputLines = 8.0f + (GameClient()->m_FastInputLog.Result().m_Valid ? 4.0f : 0.0f);
	const float InputsModeFieldsHeight = (InputsMode == SC_INPUTS_SIX ? SixInputLines : 2.0f) * (MarginSmall + LineSize);
	const float InputsExpandedTargetHeight = (MarginSmall + LineSize) + InputsModeFieldsHeight;
	const float InputsExpandedHeight = SC_MODULE_REVEAL(InputsEnabled, InputsExpandedTargetHeight, Client()->RenderFrameTime());
	const float InputsHeaderHeight = LineSize + MarginSmall + LineSize;
	const float InputsFooterLines = g_Config.m_ClSubTickAiming ? 3.0f : 4.0f;
	const float InputsBlockHeight = InputsHeaderHeight + InputsExpandedHeight + InputsFooterLines * (MarginSmall + LineSize);

	CUIRect InputsBlock;
	LeftColumn.HSplitTop(InputsBlockHeight, &InputsBlock, &LeftColumn);

	CUIRect InputsBlockBg = InputsBlock;
	InputsBlockBg.w += BlockPadding;
	InputsBlockBg.h += BlockPadding;
	InputsBlockBg.x -= BlockPadding * 0.5f;
	InputsBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&InputsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScInputs, &InputsBlockBg);

	InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
	Ui()->DoLabel(&Label, ScLocalize("Inputs"), HeadlineFontSize, TEXTALIGN_ML);
	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);

	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	{
		static CButtonContainer s_InputsEnabledButton;
		static int s_LastNonOffMode = SC_INPUTS_SIX;
		if(InputsEnabled)
			s_LastNonOffMode = InputsMode;
		if(DoButton_CheckBox(&s_InputsEnabledButton, ScLocalize("Enable Inputs"), InputsEnabled, &Content))
			g_Config.m_ScInputs = InputsEnabled ? SC_INPUTS_OFF : s_LastNonOffMode;
	}

	if(InputsExpandedHeight > 0.5f)
	{
		CUIRect Visible = InputsBlock;
		Visible.h = InputsExpandedHeight;
		Ui()->ClipEnable(&Visible);

		InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
		InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
		{
			static CButtonContainer s_InputsSix, s_InputsFast, s_InputsSaiko, s_InputsCloud, s_InputsMeow;
			CUIRect ButtonsRect = Button;
			const float Spacing = 2.0f;
			const float InputButtonWidth = (ButtonsRect.w - Spacing * 4.0f) / 5.0f;

			CUIRect SixButton, FastButton, SaikoButton, CloudButton, MeowButton;
			ButtonsRect.VSplitLeft(InputButtonWidth, &SixButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &FastButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &SaikoButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			ButtonsRect.VSplitLeft(InputButtonWidth, &CloudButton, &ButtonsRect);
			ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			MeowButton = ButtonsRect;

			SixButton.HMargin(2.0f, &SixButton);
			FastButton.HMargin(2.0f, &FastButton);
			SaikoButton.HMargin(2.0f, &SaikoButton);
			CloudButton.HMargin(2.0f, &CloudButton);
			MeowButton.HMargin(2.0f, &MeowButton);

			if(DoButton_Menu(&s_InputsSix, "667", InputsMode == SC_INPUTS_SIX, &SixButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_ScInputs = SC_INPUTS_SIX;
			if(DoButton_Menu(&s_InputsFast, ScLocalize("Fast"), InputsMode == SC_INPUTS_FAST, &FastButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_ScInputs = SC_INPUTS_FAST;
			if(DoButton_Menu(&s_InputsSaiko, ScLocalize("Saiko"), InputsMode == SC_INPUTS_SAIKO, &SaikoButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_ScInputs = SC_INPUTS_SAIKO;
			if(DoButton_Menu(&s_InputsCloud, ScLocalize("Cloud"), InputsMode == SC_INPUTS_CLOUD, &CloudButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_ScInputs = SC_INPUTS_CLOUD;
			if(DoButton_Menu(&s_InputsMeow, ScLocalize("Meow"), InputsMode == SC_INPUTS_MEOW, &MeowButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_ScInputs = SC_INPUTS_MEOW;
			GameClient()->m_Tooltips.DoToolTip(&s_InputsSix, &SixButton, ScLocalize("667 input: predicts your own tee ahead by an amount that follows your connection, up to the max offset. Inputs are sent the moment they change and server corrections are blended out."));
			GameClient()->m_Tooltips.DoToolTip(&s_InputsFast, &FastButton, ScLocalize("Fast input (TClient): predicts your movement a fixed number of milliseconds ahead so it feels more instant."));
			GameClient()->m_Tooltips.DoToolTip(&s_InputsSaiko, &SaikoButton, ScLocalize("Saiko input: sends your input as soon as a key changes instead of waiting for the next tick, and predicts your tee ahead by the offset."));
			GameClient()->m_Tooltips.DoToolTip(&s_InputsCloud, &CloudButton, ScLocalize("Cloud input: keeps its own copy of your input and repredicts every frame from it. Other players are shown at most one tick ahead."));
			GameClient()->m_Tooltips.DoToolTip(&s_InputsMeow, &MeowButton, ScLocalize("Meow input (MeowClient): keeps its own copy of your input, repredicts as soon as it changes and shows your tee ahead by the offset."));
		}

		if(InputsMode == SC_INPUTS_SIX)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_ScSixInputAmount, &Button, ScLocalize("Max prediction offset"), 0, 150);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSixInputAmount, &Button, ScLocalize("The highest prediction offset 667 input may use, in ticks (1.00 = 20 ms). Higher feels more instant, but on a bad connection the tee gets corrected back more often."));

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			Ui()->DoScrollbarOption(&g_Config.m_ScSixInputSmoothing, &g_Config.m_ScSixInputSmoothing, &Button, ScLocalize("Correction smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSixInputSmoothing, &Button, ScLocalize("When the server disagrees with the prediction, the difference is faded out over this many milliseconds instead of snapping. 0 snaps instantly."));

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSixInputAdaptive, &Content, ScLocalize("Lowers the offset automatically while the server keeps correcting your predictions, and raises it again once the connection is stable."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSixInputAdaptive, ScLocalize("Adaptive offset"), &g_Config.m_ScSixInputAdaptive, &Content, LineSize);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSixInputSubTick, &Content, ScLocalize("Starts the response from the exact moment inside the tick the key was pressed, instead of from the start of the next tick."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSixInputSubTick, ScLocalize("Sub-tick input timing"), &g_Config.m_ScSixInputSubTick, &Content, LineSize);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSixInputJumpEase, &Content, ScLocalize("Spreads the position change of a jump over one tick instead of showing it as a single jump."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSixInputJumpEase, ScLocalize("Smooth jump start"), &g_Config.m_ScSixInputJumpEase, &Content, LineSize);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSixInputOthers, &Content, ScLocalize("Also shows other players predicted ahead with 667 input."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSixInputOthers, ScLocalize("667 input others"), &g_Config.m_ScSixInputOthers, &Content, LineSize);

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
			char aSixStatus[128];
			str_format(aSixStatus, sizeof(aSixStatus), ScLocalize("Active offset: %.2f ticks · avg correction: %.1f"), GameClient()->m_SixInput.OffsetTicks(), GameClient()->m_SixInput.AverageError());
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
			Ui()->DoLabel(&Label, aSixStatus, 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			CFastInputLog &InputLog = GameClient()->m_FastInputLog;
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			static CButtonContainer s_CalibrateButton;
			if(InputLog.Calibrating())
			{
				char aCalibrating[128];
				str_format(aCalibrating, sizeof(aCalibrating), ScLocalize("Finding your best input… %d%% (click to cancel)"), (int)(InputLog.CalibrationProgress() * 100.0f));
				if(DoButton_Menu(&s_CalibrateButton, aCalibrating, 1, &Button))
					InputLog.CancelCalibration();
			}
			else if(DoButton_Menu(&s_CalibrateButton, ScLocalize("Find your best input"), 0, &Button))
			{
				InputLog.StartCalibration();
				if(InputLog.Calibrating() && Client()->State() == IClient::STATE_ONLINE)
					SetActive(false);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_CalibrateButton, &Button, ScLocalize("Play normally for two minutes of movement. Cecen Client measures your inputs and connection and suggests the most stable 667 input settings for you."));

			const CFastInputLog::SResult &Result = InputLog.Result();
			if(Result.m_Valid)
			{
				char aLine[256];
				InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
				InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
				str_format(aLine, sizeof(aLine), ScLocalize("Best for you: offset %.2f · smoothing %d ms · adaptive %s · smooth jump %s"), Result.m_Amount / 100.0f, Result.m_Smoothing, Result.m_Adaptive ? ScLocalize("on") : ScLocalize("off"), Result.m_JumpEase ? ScLocalize("on") : ScLocalize("off"));
				TextRender()->TextColor(0.45f, 0.8f, 1.0f, 1.0f);
				Ui()->DoLabel(&Label, aLine, 12.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());

				InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
				InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
				str_format(aLine, sizeof(aLine), ScLocalize("Ping %.0f ms, jitter %.1f ms · worst input jump %.1f units · %.2f corrections/s"), Result.m_PingMs, Result.m_JitterMs, Result.m_ShiftWorst, Result.m_CorrectionsPerSec);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
				Ui()->DoLabel(&Label, aLine, 11.0f, TEXTALIGN_ML);

				InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
				InputsBlock.HSplitTop(LineSize, &Label, &InputsBlock);
				if(!Result.m_EnoughData)
					str_format(aLine, sizeof(aLine), ScLocalize("Only %.0f s of movement was recorded, this is a rough guess."), Result.m_MovingSeconds);
				else if(Result.m_Lost > 0 || Result.m_AimDrift > 2.0f || Result.m_SnapTap)
					str_format(aLine, sizeof(aLine), ScLocalize("Apply also fixes: %d lost presses, %.1f° aim drift%s"), Result.m_Lost, Result.m_AimDrift, Result.m_SnapTap ? ScLocalize(", A+D overlap (Snap Tap)") : "");
				else
					str_format(aLine, sizeof(aLine), ScLocalize("No lost presses, no aim drift, %d server mismatches."), Result.m_ServerMismatches);
				Ui()->DoLabel(&Label, aLine, 11.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());

				InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
				InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
				CUIRect ApplyButton, DismissButton;
				Button.VSplitMid(&ApplyButton, &DismissButton, MarginSmall);
				static CButtonContainer s_ApplyResultButton, s_DismissResultButton;
				if(DoButton_Menu(&s_ApplyResultButton, ScLocalize("Apply"), 0, &ApplyButton))
					InputLog.ApplyResult();
				if(DoButton_Menu(&s_DismissResultButton, ScLocalize("Dismiss"), 0, &DismissButton))
					InputLog.ClearResult();
			}
		}
		else if(InputsMode == SC_INPUTS_FAST)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, ScLocalize("Prediction offset"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_TcFastInputAmount, &Button, ScLocalize("How far ahead your movement is predicted, in ms. Higher feels faster, but the tee jumps back more when the server corrects it."));

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_TcFastInputOthers, &Content, ScLocalize("Also predicts other players ahead by the same amount."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, ScLocalize("Fast Input others"), &g_Config.m_TcFastInputOthers, &Content, LineSize);
		}
		else if(InputsMode == SC_INPUTS_SAIKO)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_ScSaikoInputAmount, &Button, ScLocalize("Prediction offset"), 0, 500);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSaikoInputAmount, &Button, ScLocalize("Prediction offset in ticks (1.00 = 20 ms)."));

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScSaikoInputOthers, &Content, ScLocalize("Also predicts other players ahead with Saiko input."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSaikoInputOthers, ScLocalize("Saiko input others"), &g_Config.m_ScSaikoInputOthers, &Content, LineSize);
		}
		else if(InputsMode == SC_INPUTS_CLOUD)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_ScCloudInputAmount, &Button, ScLocalize("Prediction offset"), 0, 500);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScCloudInputAmount, &Button, ScLocalize("Prediction offset in ticks (1.00 = 20 ms)."));

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScCloudInputOthers, &Content, ScLocalize("Also shows other players one tick ahead."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCloudInputOthers, ScLocalize("Cloud input others"), &g_Config.m_ScCloudInputOthers, &Content, LineSize);
		}
		else if(InputsMode == SC_INPUTS_MEOW)
		{
			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Button, &InputsBlock);
			DoTickAmountSlider(&g_Config.m_ScMeowInputAmount, &Button, ScLocalize("Prediction offset"), 0, 5000, 1000);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScMeowInputAmount, &Button, ScLocalize("Prediction offset in ticks (1.00 = 20 ms)."));

			InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
			InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScMeowInputOthers, &Content, ScLocalize("Also predicts other players ahead with Meow input."));
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMeowInputOthers, ScLocalize("Meow input others"), &g_Config.m_ScMeowInputOthers, &Content, LineSize);
		}

		Ui()->ClipDisable();
	}

	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClSubTickAiming, &Content, ScLocalize("Sends your exact mouse position between ticks, so hook and fire aim where the cursor was when you clicked."));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, ScLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Content, LineSize);

	if(!g_Config.m_ClSubTickAiming)
	{
		InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
		InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_TcFastInputRepredictHook, &Content, ScLocalize("Repredicts when you change the hook direction in the middle of a tick. Hooks are more accurate, but it can cost FPS."));
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputRepredictHook, ScLocalize("Repredict on mid-tick hook direction change (may lag)"), &g_Config.m_TcFastInputRepredictHook, &Content, LineSize);
	}

	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScAutoMargin, &Content, ScLocalize("Adjusts the prediction margin from your ping automatically, so you get fewer corrections without extra delay. Your old margin comes back when you turn it off."));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScAutoMargin, ScLocalize("Auto margin"), &g_Config.m_ScAutoMargin, &Content, LineSize);

	InputsBlock.HSplitTop(MarginSmall, nullptr, &InputsBlock);
	InputsBlock.HSplitTop(LineSize, &Content, &InputsBlock);
	GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScInputDebounce, &Content, ScLocalize("Keeps key presses shorter than one game tick (20 ms), so very fast taps of A, D, jump or hook are never lost."));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScInputDebounce, ScLocalize("Input Debounce"), &g_Config.m_ScInputDebounce, &Content, LineSize);

	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

	const bool SnapTapBlocked = GameClient()->m_SnapTap.IsBlockedByCommunity();
	const bool SnapTapExpanded = g_Config.m_ScSnapTap != 0;
	const float SnapTapHeaderHeight = LineSize + MarginSmall + LineSize;
	const float SnapTapExpandedTargetHeight = MarginSmall + LineSize;
	const float SnapTapExpandedHeight = SC_MODULE_REVEAL(SnapTapExpanded, SnapTapExpandedTargetHeight, Client()->RenderFrameTime());
	const float SnapTapBlockedHintHeight = SnapTapBlocked ? (MarginSmall + LineSize) : 0.0f;
	const float SnapTapBlockHeight = SnapTapHeaderHeight + SnapTapExpandedHeight + SnapTapBlockedHintHeight;

	CUIRect SnapTapBlock;
	LeftColumn.HSplitTop(SnapTapBlockHeight, &SnapTapBlock, &LeftColumn);

	CUIRect SnapTapBlockBg = SnapTapBlock;
	SnapTapBlockBg.w += BlockPadding;
	SnapTapBlockBg.h += BlockPadding;
	SnapTapBlockBg.x -= BlockPadding * 0.5f;
	SnapTapBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&SnapTapBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScSnapTap, &SnapTapBlockBg);

	SnapTapBlock.HSplitTop(LineSize, &Label, &SnapTapBlock);
	Ui()->DoLabel(&Label, ScLocalize("Snap Tap"), HeadlineFontSize, TEXTALIGN_ML);
	SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);

	SnapTapBlock.HSplitTop(LineSize, &Content, &SnapTapBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSnapTap, ScLocalize("Enable"), &g_Config.m_ScSnapTap, &Content, LineSize);

	if(SnapTapExpandedHeight > 0.5f)
	{
		CUIRect Visible = SnapTapBlock;
		Visible.h = SnapTapExpandedHeight;
		Ui()->ClipEnable(&Visible);

		SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);
		SnapTapBlock.HSplitTop(LineSize, &Button, &SnapTapBlock);

		const int Min = 0;
		const int Max = 200;
		int Value = std::clamp(g_Config.m_ScSnapTapDelay, Min, Max);
		const int Increment = std::max(1, (Max - Min) / 35);
		if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
			Value = std::clamp(Value + Increment, Min, Max);
		if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
			Value = std::clamp(Value - Increment, Min, Max);

		char aBuf[256];
		if(Value == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %s", ScLocalize("Delay"), ScLocalize("Off"));
		else
			str_format(aBuf, sizeof(aBuf), "%s: %dms", ScLocalize("Delay"), Value);
		// 667client

		CUIRect DelayLabel, ScrollBar;
		Button.VSplitMid(&DelayLabel, &ScrollBar, std::min(10.0f, Button.w * 0.05f));
		Ui()->DoLabel(&DelayLabel, aBuf, DelayLabel.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

		const float Rel = (Value - Min) / (float)(Max - Min);
		// 667client
		const float NewRel = Ui()->DoScrollbarH(&g_Config.m_ScSnapTapDelay, &ScrollBar, Rel, nullptr, nullptr);
		// 667client
		Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
		g_Config.m_ScSnapTapDelay = std::clamp(Value, Min, Max);

		Ui()->ClipDisable();
	}

	if(SnapTapBlocked)
	{
		SnapTapBlock.HSplitTop(MarginSmall, nullptr, &SnapTapBlock);
		SnapTapBlock.HSplitTop(LineSize, &Label, &SnapTapBlock);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&Label, ScLocalize("Looks like you're on a server where this feature is forbidden"), FontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

	const bool GoresModeExpanded = g_Config.m_ScGoresMode != 0;
	const float GoresModeHeaderHeight = LineSize + MarginSmall + LineSize;
	const float GoresModeExpandedTargetHeight = MarginSmall + LineSize;
	const float GoresModeExpandedHeight = SC_MODULE_REVEAL(GoresModeExpanded, GoresModeExpandedTargetHeight, Client()->RenderFrameTime());
	const float GoresModeBlockHeight = GoresModeHeaderHeight + GoresModeExpandedHeight;

	CUIRect GoresModeBlock;
	LeftColumn.HSplitTop(GoresModeBlockHeight, &GoresModeBlock, &LeftColumn);

	CUIRect GoresModeBlockBg = GoresModeBlock;
	GoresModeBlockBg.w += BlockPadding;
	GoresModeBlockBg.h += BlockPadding;
	GoresModeBlockBg.x -= BlockPadding * 0.5f;
	GoresModeBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&GoresModeBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScGoresMode, &GoresModeBlockBg);

	CUIRect GoresModeView = GoresModeBlock;
	GoresModeView.HSplitTop(LineSize, &Label, &GoresModeView);
	Ui()->DoLabel(&Label, ScLocalize("Gores mode"), HeadlineFontSize, TEXTALIGN_ML);
	GoresModeView.HSplitTop(MarginSmall, nullptr, &GoresModeView);

	GoresModeView.HSplitTop(LineSize, &Content, &GoresModeView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScGoresMode, ScLocalize("Enable gores mode"), &g_Config.m_ScGoresMode, &Content, LineSize);

	if(GoresModeExpandedHeight > 0.5f)
	{
		CUIRect Visible = GoresModeView;
		Visible.h = GoresModeExpandedHeight;
		Ui()->ClipEnable(&Visible);

		GoresModeView.HSplitTop(MarginSmall, nullptr, &GoresModeView);
		GoresModeView.HSplitTop(LineSize, &Content, &GoresModeView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScGoresModeDisableIfWeapons, ScLocalize("Disable if you have shotgun, grenade or laser"), &g_Config.m_ScGoresModeDisableIfWeapons, &Content, LineSize);

		Ui()->ClipDisable();
	}

	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

	const bool OptimizerExpanded = g_Config.m_ScOptimizer != 0;
	const bool OptimizerFpsFogExpanded = OptimizerExpanded && g_Config.m_ScOptimizerFpsFog != 0;
	const float OptimizerHeaderHeight = LineSize + MarginSmall + LineSize;
	const float OptimizerFpsFogTargetHeight = 4.0f * (MarginSmall + LineSize);
	const float OptimizerFpsFogExpandedHeight = SC_MODULE_REVEAL_DUR(OptimizerFpsFogExpanded, OptimizerFpsFogTargetHeight, Client()->RenderFrameTime(), 0.16f);
	const float OptimizerExpandedTargetHeight = 4.0f * (MarginSmall + LineSize);
	const float OptimizerExpandedHeight = SC_MODULE_REVEAL(OptimizerExpanded, OptimizerExpandedTargetHeight, Client()->RenderFrameTime()) + OptimizerFpsFogExpandedHeight;
	const float OptimizerBlockHeight = OptimizerHeaderHeight + OptimizerExpandedHeight;

	CUIRect OptimizerBlock;
	LeftColumn.HSplitTop(OptimizerBlockHeight, &OptimizerBlock, &LeftColumn);

	CUIRect OptimizerBlockBg = OptimizerBlock;
	OptimizerBlockBg.w += BlockPadding;
	OptimizerBlockBg.h += BlockPadding;
	OptimizerBlockBg.x -= BlockPadding * 0.5f;
	OptimizerBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&OptimizerBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScOptimizer, &OptimizerBlockBg);

	CUIRect MainLeft = OptimizerBlock;
	MainLeft.HSplitTop(LineSize, &Label, &MainLeft);
	Ui()->DoLabel(&Label, ScLocalize("Optimizer"), HeadlineFontSize, TEXTALIGN_ML);
	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);

	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScOptimizer, ScLocalize("Enable optimizer"), &g_Config.m_ScOptimizer, &Content, LineSize);

	if(OptimizerExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainLeft;
		Visible.h = OptimizerExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScOptimizerDisableParticles, ScLocalize("Disable all particles render"), &g_Config.m_ScOptimizerDisableParticles, &Content, LineSize);

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScOptimizerDisableQuads, ScLocalize("Disable map quads render"), &g_Config.m_ScOptimizerDisableQuads, &Content, LineSize);

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScOptimizerDisableHighDetail, ScLocalize("Disable high detail"), &g_Config.m_ScOptimizerDisableHighDetail, &Content, LineSize))
			GameClient()->m_Optimizer.OnOptimizerDisableHighDetailToggled();

		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScOptimizerFpsFog, ScLocalize("FPS fog (cull outside limit)"), &g_Config.m_ScOptimizerFpsFog, &Content, LineSize);

		if(OptimizerFpsFogExpandedHeight > 0.5f)
		{
			CUIRect FogVisible = MainLeft;
			FogVisible.h = OptimizerFpsFogExpandedHeight;
			Ui()->ClipEnable(&FogVisible);

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScOptimizerFpsFogRenderRect, ScLocalize("Render FPS fog rectangle"), &g_Config.m_ScOptimizerFpsFogRenderRect, &Content, LineSize);

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScOptimizerFpsFogCullMapTiles, ScLocalize("Cull map tiles outside FPS fog"), &g_Config.m_ScOptimizerFpsFogCullMapTiles, &Content, LineSize);

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Button, &MainLeft);
			static CButtonContainer s_OptimizerFogModeRadius;
			static CButtonContainer s_OptimizerFogModeZoom;
			CUIRect RadiusButton, ZoomButton;
			Button.VSplitMid(&RadiusButton, &ZoomButton, 2.0f);
			RadiusButton.HMargin(2.0f, &RadiusButton);
			ZoomButton.HMargin(2.0f, &ZoomButton);
			if(DoButton_Menu(&s_OptimizerFogModeRadius, ScLocalize("Radius"), g_Config.m_ScOptimizerFpsFogMode == 0, &RadiusButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_ScOptimizerFpsFogMode = 0;
			if(DoButton_Menu(&s_OptimizerFogModeZoom, ScLocalize("Zoom %"), g_Config.m_ScOptimizerFpsFogMode == 1, &ZoomButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_ScOptimizerFpsFogMode = 1;

			MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
			MainLeft.HSplitTop(LineSize, &Button, &MainLeft);
			if(g_Config.m_ScOptimizerFpsFogMode == 0)
				Ui()->DoScrollbarOption(&g_Config.m_ScOptimizerFpsFogRadiusTiles, &g_Config.m_ScOptimizerFpsFogRadiusTiles, &Button, ScLocalize("Radius (tiles)"), 5, 300);
			else
				Ui()->DoScrollbarOption(&g_Config.m_ScOptimizerFpsFogZoomPercent, &g_Config.m_ScOptimizerFpsFogZoomPercent, &Button, ScLocalize("Visible area (%)"), 10, 120, &CUi::ms_LinearScrollbarScale, 0u, "%");

			Ui()->ClipDisable();
		}

		Ui()->ClipDisable();
	}

	LeftColumn.HSplitTop(MarginBetweenViews, nullptr, &LeftColumn);

#if defined(CONF_FAMILY_WINDOWS)
	const float PerformanceOptionRows = 4.0f;
#else
	const float PerformanceOptionRows = 1.0f;
#endif
	const float PerformanceHintSize = 11.0f;
	const float PerformanceBlockHeight = LineSize + PerformanceOptionRows * (MarginSmall + LineSize) + MarginSmall + 3.0f * (PerformanceHintSize + 3.0f) + MarginSmall + LineSize;
	CUIRect PerformanceBlock;
	LeftColumn.HSplitTop(PerformanceBlockHeight, &PerformanceBlock, &LeftColumn);

	CUIRect PerformanceBlockBg = PerformanceBlock;
	PerformanceBlockBg.w += BlockPadding;
	PerformanceBlockBg.h += BlockPadding;
	PerformanceBlockBg.x -= BlockPadding * 0.5f;
	PerformanceBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&PerformanceBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScLowLatencyTimers, &PerformanceBlockBg);

	MainLeft = PerformanceBlock;
	MainLeft.HSplitTop(LineSize, &Label, &MainLeft);
	Ui()->DoLabel(&Label, ScLocalize("Performance"), HeadlineFontSize, TEXTALIGN_ML);

#if defined(CONF_FAMILY_WINDOWS)
	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScHighProcessPriority, ScLocalize("High process priority"), &g_Config.m_ScHighProcessPriority, &Content, LineSize))
		GameClient()->m_SystemTuning.SetDDNetProcessPriority(g_Config.m_ScHighProcessPriority);

	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScLowLatencyTimers, ScLocalize("Low latency timers (1 ms, no power throttling)"), &g_Config.m_ScLowLatencyTimers, &Content, LineSize);

	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMmcss, ScLocalize("Game thread scheduling (MMCSS)"), &g_Config.m_ScMmcss, &Content, LineSize);

	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	if(DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScDiscordNormalProcessPriority, ScLocalize("Lower Discord process priority"), &g_Config.m_ScDiscordNormalProcessPriority, &Content, LineSize))
	{
		if(g_Config.m_ScDiscordNormalProcessPriority)
			GameClient()->m_SystemTuning.StartDiscordPriorityThread();
	}
#else
	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
	MainLeft.HSplitTop(LineSize, &Content, &MainLeft);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScLowLatencyTimers, ScLocalize("Low latency timers (no App Nap or timer coalescing)"), &g_Config.m_ScLowLatencyTimers, &Content, LineSize);
#endif

	{
		char aStatus[256];
		CUIRect StatusLine;
		MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.65f);

		char aFpsLimit[32];
		if(g_Config.m_GfxRefreshRate > 0)
			str_format(aFpsLimit, sizeof(aFpsLimit), "%d", g_Config.m_GfxRefreshRate);
		else
			str_copy(aFpsLimit, ScLocalize("unlimited"));
		char aUpdateRate[32];
		if(g_Config.m_ClRefreshRate > 0)
			str_format(aUpdateRate, sizeof(aUpdateRate), "%d Hz", g_Config.m_ClRefreshRate);
		else
			str_copy(aUpdateRate, ScLocalize("unlimited"));
		str_format(aStatus, sizeof(aStatus), "%s: %s  ·  %s: %s  ·  %s: %s", ScLocalize("V-Sync"), g_Config.m_GfxVsync ? ScLocalize("on") : ScLocalize("off"), ScLocalize("FPS limit"), aFpsLimit, ScLocalize("Update rate"), aUpdateRate);
		MainLeft.HSplitTop(PerformanceHintSize + 3.0f, &StatusLine, &MainLeft);
		Ui()->DoLabel(&StatusLine, aStatus, PerformanceHintSize, TEXTALIGN_ML);

		const char *pTimerState = GameClient()->m_SystemTuning.LowLatencyTimersActive() ? ScLocalize("active") : ScLocalize("inactive");
		str_format(aStatus, sizeof(aStatus), "%s: %s  ·  %s: %s  ·  FSAA: %dx", ScLocalize("Backend"), g_Config.m_GfxBackend, ScLocalize("Low latency timers"), pTimerState, g_Config.m_GfxFsaaSamples);
		MainLeft.HSplitTop(PerformanceHintSize + 3.0f, &StatusLine, &MainLeft);
		Ui()->DoLabel(&StatusLine, aStatus, PerformanceHintSize, TEXTALIGN_ML);

#if defined(CONF_PLATFORM_MACOS)
		const char *pHint = g_Config.m_GfxFullscreen ? ScLocalize("Fullscreen: macOS Game Mode can turn on automatically") : ScLocalize("Windowed: switch to fullscreen to allow macOS Game Mode");
#else
		const char *pHint = g_Config.m_GfxVsync || g_Config.m_GfxFsaaSamples > 0 ? ScLocalize("V-Sync and FSAA add input delay") : ScLocalize("No delay adding graphics options are active");
#endif
		MainLeft.HSplitTop(PerformanceHintSize + 3.0f, &StatusLine, &MainLeft);
		Ui()->DoLabel(&StatusLine, pHint, PerformanceHintSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	MainLeft.HSplitTop(MarginSmall, nullptr, &MainLeft);
	MainLeft.HSplitTop(LineSize, &Button, &MainLeft);
	static CButtonContainer s_LowLatencyPresetButton;
	if(DoButton_Menu(&s_LowLatencyPresetButton, ScLocalize("Apply low latency preset"), 0, &Button))
		GameClient()->m_SystemTuning.ApplyLowLatencyPreset();
	GameClient()->m_Tooltips.DoToolTip(&s_LowLatencyPresetButton, &Button, ScLocalize("Turns off V-Sync, removes the update rate limit and enables the low latency options above"));

	CUIRect RightColumn = RightView;
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	const float CinematicStrengthHeight = g_Config.m_ScCinematicCamera ? LineSize : 0.0f;
	const float BetterSpectateDelayHeight = g_Config.m_ScBetterSpectate ? LineSize : 0.0f;
	const float SpecMovedNotifyTextHeight = g_Config.m_ScSpecMovedNotify ? LineSize : 0.0f;
	const float AutoLockDelayHeight = g_Config.m_ScAutoTeamLock ? LineSize : 0.0f;
#if defined(CONF_AUTOUPDATE)
	const float AutoUpdateHeight = LineSize;
#else
	const float AutoUpdateHeight = 0.0f;
#endif
	const float GameplayQoLBlockHeight =
		LineSize + MarginSmall +
		7.0f * LineSize +
		AutoUpdateHeight +
		CinematicStrengthHeight + BetterSpectateDelayHeight + SpecMovedNotifyTextHeight + AutoLockDelayHeight;
	CUIRect GameplayQoLBlock;
	RightColumn.HSplitTop(GameplayQoLBlockHeight, &GameplayQoLBlock, &RightColumn);

	CUIRect GameplayQoLBlockBg = GameplayQoLBlock;
	GameplayQoLBlockBg.w += BlockPadding;
	GameplayQoLBlockBg.h += BlockPadding;
	GameplayQoLBlockBg.x -= BlockPadding * 0.5f;
	GameplayQoLBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&GameplayQoLBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScAutoUpdate, &GameplayQoLBlockBg);

	GameplayQoLBlock.HSplitTop(LineSize, &Label, &GameplayQoLBlock);
	Ui()->DoLabel(&Label, ScLocalize("Gameplay QoL"), HeadlineFontSize, TEXTALIGN_ML);
	GameplayQoLBlock.HSplitTop(MarginSmall, nullptr, &GameplayQoLBlock);

#if defined(CONF_AUTOUPDATE)
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScAutoUpdate, ScLocalize("Automatic update"), &g_Config.m_ScAutoUpdate, &GameplayQoLBlock, LineSize);
#endif
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCinematicCamera, ScLocalize("Cinematic camera"), &g_Config.m_ScCinematicCamera, &GameplayQoLBlock, LineSize);
	if(g_Config.m_ScCinematicCamera)
	{
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		Ui()->DoScrollbarOption(&g_Config.m_ScCinematicCameraStrength, &g_Config.m_ScCinematicCameraStrength, &Button, ScLocalize("Strength"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScExtendZoom, ScLocalize("Extend zoom (0.5 steps)"), &g_Config.m_ScExtendZoom, &GameplayQoLBlock, LineSize);
	{
		CUIRect BetterSpectateRow;
		GameplayQoLBlock.HSplitTop(LineSize, &BetterSpectateRow, &GameplayQoLBlock);
		if(DoButton_CheckBox(&g_Config.m_ScBetterSpectate, ScLocalize("Better spectate"), g_Config.m_ScBetterSpectate, &BetterSpectateRow))
		{
			g_Config.m_ScBetterSpectate ^= 1;
			GameClient()->m_SpecPauseRadio.SyncSpectateBinds(g_Config.m_ScBetterSpectate != 0);
		}
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScBetterSpectate, &BetterSpectateRow, ScLocalize("Replace your say /pause bind (default: Q) with +specpause (pause/spec radio)"));
	}
	if(g_Config.m_ScBetterSpectate)
	{
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		Ui()->DoScrollbarOption(&g_Config.m_ScSpecPauseShowDelay, &g_Config.m_ScSpecPauseShowDelay, &Button, ScLocalize("Show delay"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSpecMovedNotify, ScLocalize("Notify when moved in spec"), &g_Config.m_ScSpecMovedNotify, &GameplayQoLBlock, LineSize);
	if(g_Config.m_ScSpecMovedNotify)
	{
		static CLineInput s_SpecMovedNotifyTextInput;
		s_SpecMovedNotifyTextInput.SetBuffer(g_Config.m_ScSpecMovedNotifyText, sizeof(g_Config.m_ScSpecMovedNotifyText));
		s_SpecMovedNotifyTextInput.SetEmptyText("you moved in game");
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		CUIRect TextLabel, TextField;
		Button.VSplitMid(&TextLabel, &TextField, std::min(10.0f, Button.w * 0.05f));
		Ui()->DoLabel(&TextLabel, ScLocalize("Notification text"), 14.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_SpecMovedNotifyTextInput, &TextField, 14.0f);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScAutoTeamLock, ScLocalize("Lock team automatically after joining"), &g_Config.m_ScAutoTeamLock, &GameplayQoLBlock, LineSize);
	if(g_Config.m_ScAutoTeamLock)
	{
		GameplayQoLBlock.HSplitTop(LineSize, &Button, &GameplayQoLBlock);
		Ui()->DoScrollbarOption(&g_Config.m_ScAutoTeamLockDelay, &g_Config.m_ScAutoTeamLockDelay, &Button, ScLocalize("Auto lock delay"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
	}
	{
		CUIRect ConfirmQuitRow;
		GameplayQoLBlock.HSplitTop(LineSize, &ConfirmQuitRow, &GameplayQoLBlock);
		if(DoButton_CheckBox(&g_Config.m_ScConfirmQuit, ScLocalize("Confirm before quitting"), g_Config.m_ScConfirmQuit, &ConfirmQuitRow))
			g_Config.m_ScConfirmQuit ^= 1;
	}
	{
		CUIRect EgoTilesAntiLagRow;
		GameplayQoLBlock.HSplitTop(LineSize, &EgoTilesAntiLagRow, &GameplayQoLBlock);
		if(DoButton_CheckBox(&g_Config.m_TcEgoTilesAntiLag, ScLocalize("Moving/ego tiles prediction"), g_Config.m_TcEgoTilesAntiLag, &EgoTilesAntiLagRow))
			g_Config.m_TcEgoTilesAntiLag ^= 1;
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool FastActionsExpanded = g_Config.m_ScFastActions != 0;
	const float FastActionsHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FastActionsExpandedTargetHeight = MarginSmall + WheelPreviewHeight + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize + MarginSmall + LineSize * 0.8f + MarginSmall + LineSize;
	const float FastActionsExpandedHeight = SC_MODULE_REVEAL(FastActionsExpanded, FastActionsExpandedTargetHeight, Client()->RenderFrameTime());
	const float FastActionsBlockHeight = FastActionsHeaderHeight + FastActionsExpandedHeight;

	CUIRect FastActionsBlock;
	RightColumn.HSplitTop(FastActionsBlockHeight, &FastActionsBlock, &RightColumn);

	CUIRect FastActionsBlockBg = FastActionsBlock;
	FastActionsBlockBg.w += BlockPadding;
	FastActionsBlockBg.h += BlockPadding;
	FastActionsBlockBg.x -= BlockPadding * 0.5f;
	FastActionsBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&FastActionsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScFastActions, &FastActionsBlockBg);

	MainView = FastActionsBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	Ui()->DoLabel(&Label, ScLocalize("Fast Actions"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFastActions, ScLocalize("Enable Fast Actions"), &g_Config.m_ScFastActions, &Content, LineSize);

	if(FastActionsExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = FastActionsExpandedHeight;
		Ui()->ClipEnable(&Visible);

		static char s_aBindName[FAST_ACTIONS_MAX_NAME] = "";
		static char s_aBindCommand[FAST_ACTIONS_MAX_CMD] = "";
		static int s_SelectedBindIndex = 0;
		static int s_LastSelectedBindIndex = -1;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		CUIRect WheelPreview;
		MainView.HSplitTop(WheelPreviewHeight, &WheelPreview, &MainView);
		const vec2 Center = WheelPreview.Center();
		const float LineInset = 18.0f;
		const float LineHalfWidth = std::max(40.0f, WheelPreview.w / 2.0f - LineInset);
		const float LineHeight = std::min(WheelPreview.h * 0.78f, 44.0f);
		const float SelectBandHalfHeight = LineHeight * 1.2f;
		const float LabelW = 52.0f;
		const float LabelH = 52.0f;
		const float TextHalfRange = std::max(0.0f, LineHalfWidth - LabelW / 2.0f - 2.0f);

		Graphics()->DrawRect(Center.x - LineHalfWidth, Center.y - LineHeight / 2.0f, LineHalfWidth * 2.0f, LineHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 8.0f);

		int HoveringIndex = -1;
		const vec2 MouseDelta = Ui()->MousePos() - Center;
		const int SegmentCount = static_cast<int>(GameClient()->m_FastActions.m_vBinds.size());
		const auto IsLegacySlotName = [](const char *pName, int SlotIndex) {
			if(pName[0] == '\0')
				return false;
			char aSlotName[16];
			str_format(aSlotName, sizeof(aSlotName), "%d", SlotIndex + 1);
			return str_comp(pName, aSlotName) == 0;
		};
		const bool HoverInsideLine = absolute(MouseDelta.x) <= LineHalfWidth && absolute(MouseDelta.y) <= SelectBandHalfHeight;
		if(HoverInsideLine && SegmentCount > 0)
		{
			const float HoverPos01 = TextHalfRange > 0.0f ? (MouseDelta.x + TextHalfRange) / (2.0f * TextHalfRange) : 0.5f;
			HoveringIndex = std::clamp((int)std::round(HoverPos01 * (SegmentCount - 1)), 0, SegmentCount - 1);

			if(Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(2))
			{
				s_SelectedBindIndex = HoveringIndex;
				const CFastActions::CBind &Bind = GameClient()->m_FastActions.m_vBinds[HoveringIndex];
				if(IsLegacySlotName(Bind.m_aName, HoveringIndex))
					s_aBindName[0] = '\0';
				else
					str_copy(s_aBindName, Bind.m_aName);
				str_copy(s_aBindCommand, GameClient()->m_FastActions.m_vBinds[HoveringIndex].m_aCommand);
			}
		}

		s_SelectedBindIndex = std::clamp(s_SelectedBindIndex, 0, std::max(0, SegmentCount - 1));
		if(s_SelectedBindIndex != s_LastSelectedBindIndex &&
			s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			const CFastActions::CBind &Bind = GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex];
			if(IsLegacySlotName(Bind.m_aName, s_SelectedBindIndex))
				s_aBindName[0] = '\0';
			else
				str_copy(s_aBindName, Bind.m_aName);
			str_copy(s_aBindCommand, GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand);
			s_LastSelectedBindIndex = s_SelectedBindIndex;
		}

		for(int i = 0; i < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()); i++)
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
			float SegmentFontSize = FontSize * 1.1f;
			if(i == s_SelectedBindIndex)
			{
				SegmentFontSize = FontSize * 1.7f;
				TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
			}
			else if(i == HoveringIndex)
			{
				SegmentFontSize = FontSize * 1.35f;
			}

			const float Pos01 = GameClient()->m_FastActions.m_vBinds.size() <= 1 ? 0.5f : (float)i / (float)(GameClient()->m_FastActions.m_vBinds.size() - 1);
			const vec2 Pos = vec2(Center.x - TextHalfRange + Pos01 * (TextHalfRange * 2.0f), Center.y);
			const CUIRect Rect = CUIRect{Pos.x - LabelW / 2.0f, Pos.y - LabelH / 2.0f, LabelW, LabelH};
			char aBindPreviewText[16];
			str_format(aBindPreviewText, sizeof(aBindPreviewText), "%d", i + 1);
			Ui()->DoLabel(&Rect, aBindPreviewText, SegmentFontSize, TEXTALIGN_MC);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		char aSlotLabel[64];
		str_format(aSlotLabel, sizeof(aSlotLabel), "%s %d", ScLocalize("Selected slot"), s_SelectedBindIndex + 1);
		Ui()->DoLabel(&Button, aSlotLabel, FontSize, TEXTALIGN_ML);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Button.VSplitLeft(150.0f, &Label, &Button);
		Ui()->DoLabel(&Label, ScLocalize("Name:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindNameInput;
		s_BindNameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
		s_BindNameInput.SetEmptyText(ScLocalize("Name (optional)"));
		Ui()->DoEditBox(&s_BindNameInput, &Button, EditBoxFontSize);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Button.VSplitLeft(150.0f, &Label, &Button);
		Ui()->DoLabel(&Label, ScLocalize("Command:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindCommandInput;
		s_BindCommandInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
		s_BindCommandInput.SetEmptyText(ScLocalize("Command"));
		Ui()->DoEditBox(&s_BindCommandInput, &Button, EditBoxFontSize);

		if(s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aName, s_aBindName);
			str_copy(GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
		}

		static CButtonContainer s_FastActionsClearButton;
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		if(DoButton_Menu(&s_FastActionsClearButton, ScLocalize("Clear command"), 0, &Button) &&
			s_SelectedBindIndex < static_cast<int>(GameClient()->m_FastActions.m_vBinds.size()))
		{
			GameClient()->m_FastActions.m_vBinds[s_SelectedBindIndex].m_aCommand[0] = '\0';
			s_aBindCommand[0] = '\0';
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize * 0.8f, &Label, &MainView);
		Ui()->DoLabel(&Label, ScLocalize("In game: hold bind key, press 1..6, release key to execute"), FontSize * 0.8f, TEXTALIGN_ML);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Label, &MainView);
		static CButtonContainer s_FastActionsReaderButton;
		static CButtonContainer s_FastActionsClearKeyButton;
		DoLine_KeyReader(Label, s_FastActionsReaderButton, s_FastActionsClearKeyButton, ScLocalize("Fast Actions key"), "+fa");

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	{
		const bool FastPracticeHudEnabled = g_Config.m_ScFastPracticeHud != 0;
		const int FastPracticeBindCount = 7;
		const float FastPracticeExpandedTarget = (MarginSmall + LineSize) * 3.0f + (MarginSmall + LineSize) * (1 + FastPracticeBindCount) + MarginSmall + LineSize * 0.8f;
		const float FastPracticeExpanded = SC_MODULE_REVEAL(FastPracticeHudEnabled, FastPracticeExpandedTarget, Client()->RenderFrameTime());
		const float FastPracticeBlockHeight = LineSize + MarginSmall + LineSize + FastPracticeExpanded;
		CUIRect FastPracticeBlock;
		RightColumn.HSplitTop(FastPracticeBlockHeight, &FastPracticeBlock, &RightColumn);

		CUIRect FastPracticeBlockBg = FastPracticeBlock;
		FastPracticeBlockBg.w += BlockPadding;
		FastPracticeBlockBg.h += BlockPadding;
		FastPracticeBlockBg.x -= BlockPadding * 0.5f;
		FastPracticeBlockBg.y -= BlockPadding * 0.5f;
		SixClientUiTheme::DrawUiBlock(&FastPracticeBlockBg, IGraphics::CORNER_ALL, 10.0f);
		SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScFastPracticeHud, &FastPracticeBlockBg);

		CUIRect FastPracticeView = FastPracticeBlock;
		FastPracticeView.HSplitTop(LineSize, &Label, &FastPracticeView);
		Ui()->DoLabel(&Label, ScLocalize("Fast Practice"), HeadlineFontSize, TEXTALIGN_ML);
		FastPracticeView.HSplitTop(MarginSmall, nullptr, &FastPracticeView);
		FastPracticeView.HSplitTop(LineSize, &Content, &FastPracticeView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFastPracticeHud, ScLocalize("Show practice overlay"), &g_Config.m_ScFastPracticeHud, &Content, LineSize);

		if(FastPracticeExpanded > 0.5f)
		{
			CUIRect FastPracticeVisible = FastPracticeView;
			FastPracticeVisible.h = FastPracticeExpanded;
			Ui()->ClipEnable(&FastPracticeVisible);

			FastPracticeView.HSplitTop(MarginSmall, nullptr, &FastPracticeView);
			FastPracticeView.HSplitTop(LineSize, &Content, &FastPracticeView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFastPracticeShowBinds, ScLocalize("Show binds on overlay"), &g_Config.m_ScFastPracticeShowBinds, &Content, LineSize);

			FastPracticeView.HSplitTop(MarginSmall, nullptr, &FastPracticeView);
			FastPracticeView.HSplitTop(LineSize, &Button, &FastPracticeView);
			Ui()->DoScrollbarOption(&g_Config.m_ScFastPracticeHudSize, &g_Config.m_ScFastPracticeHudSize, &Button, ScLocalize("Overlay size"), 50, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");

			FastPracticeView.HSplitTop(MarginSmall, nullptr, &FastPracticeView);
			FastPracticeView.HSplitTop(LineSize, &Button, &FastPracticeView);
			Ui()->DoScrollbarOption(&g_Config.m_ScFastPracticeHudY, &g_Config.m_ScFastPracticeHudY, &Button, ScLocalize("Vertical offset"), 0, 280);

			FastPracticeView.HSplitTop(MarginSmall, nullptr, &FastPracticeView);
			FastPracticeView.HSplitTop(LineSize * 0.8f, &Label, &FastPracticeView);
			Ui()->DoLabel(&Label, ScLocalize("Practice binds (shown in-game while practice is on)"), FontSize * 0.8f, TEXTALIGN_ML);

			struct SPracticeBindRow
			{
				const char *m_pLabel;
				const char *m_pCommand;
			};
			static const SPracticeBindRow s_aPracticeBinds[] = {
				{"Toggle practice", "fast_practice_toggle"},
				{"/r", "say /r"},
				{"/invincible", "say /invincible"},
				{"/telecursor", "say /telecursor"},
				{"/tp", "say /tp"},
				{"/lasttp", "say /lasttp"},
				{"/infjump", "say /infjump"},
			};
			static CButtonContainer s_aPracticeBindReaders[std::size(s_aPracticeBinds)];
			static CButtonContainer s_aPracticeBindClears[std::size(s_aPracticeBinds)];

			for(size_t i = 0; i < std::size(s_aPracticeBinds); i++)
			{
				FastPracticeView.HSplitTop(MarginSmall, nullptr, &FastPracticeView);
				FastPracticeView.HSplitTop(LineSize, &Label, &FastPracticeView);
				DoLine_KeyReader(Label, s_aPracticeBindReaders[i], s_aPracticeBindClears[i], ScLocalize(s_aPracticeBinds[i].m_pLabel), s_aPracticeBinds[i].m_pCommand);
			}

			Ui()->ClipDisable();
		}
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);

	const bool FinishPredictionEnabled = g_Config.m_ScFinishPrediction != 0;
	const bool FinishPredictionShowTimeOptions = FinishPredictionEnabled && g_Config.m_ScFinishPredictionShowTime != 0;
	const float FinishPredictionTimeExpandedHeight = SC_MODULE_REVEAL(FinishPredictionShowTimeOptions, (MarginSmall + LineSize) * 2.0f, Client()->RenderFrameTime());
	const float FinishPredictionExpandedTargetHeight = (MarginSmall + LineSize) + FinishPredictionTimeExpandedHeight + (MarginSmall + LineSize) * 3.0f;
	const float FinishPredictionExpandedHeight = SC_MODULE_REVEAL(FinishPredictionEnabled, FinishPredictionExpandedTargetHeight, Client()->RenderFrameTime());
	const float FinishPredictionBlockHeight = LineSize + MarginSmall + LineSize + FinishPredictionExpandedHeight;
	CUIRect FinishPredictionBlock;
	RightColumn.HSplitTop(FinishPredictionBlockHeight, &FinishPredictionBlock, &RightColumn);

	CUIRect FinishPredictionBlockBg = FinishPredictionBlock;
	FinishPredictionBlockBg.w += BlockPadding;
	FinishPredictionBlockBg.h += BlockPadding;
	FinishPredictionBlockBg.x -= BlockPadding * 0.5f;
	FinishPredictionBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&FinishPredictionBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScFinishPrediction, &FinishPredictionBlockBg);

	CUIRect FinishPredictionView = FinishPredictionBlock;
	FinishPredictionView.HSplitTop(LineSize, &Label, &FinishPredictionView);
	CUIRect FinishPredictionTitleLabel, FinishPredictionHudEditorButton;
	Label.VSplitRight(LineSize + 8.0f, &FinishPredictionTitleLabel, &FinishPredictionHudEditorButton);
	static CButtonContainer s_FinishPredictionHudEditorButton;
	const bool FinishPredictionCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool FinishPredictionHudEditorClicked = Ui()->DoButton_FontIcon(&s_FinishPredictionHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, FinishPredictionCanOpenHudEditor ? 0 : -1, &FinishPredictionHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_FinishPredictionHudEditorButton, &FinishPredictionHudEditorButton, FinishPredictionCanOpenHudEditor ? ScLocalize("Open in HUD editor") : ScLocalize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_FinishPredictionHudEditorButton, 0.0f);
	if(FinishPredictionHudEditorClicked && FinishPredictionCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&FinishPredictionTitleLabel, ScLocalize("Finish Prediction"), HeadlineFontSize, TEXTALIGN_ML);
	FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);

	FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFinishPrediction, ScLocalize("Enable finish prediction"), &g_Config.m_ScFinishPrediction, &Content, LineSize);
	HudLayout::SetEnabled(HudLayout::MODULE_FINISH_PREDICTION, g_Config.m_ScFinishPrediction != 0);

	if(FinishPredictionExpandedHeight > 0.5f)
	{
		CUIRect Visible = FinishPredictionView;
		Visible.h = FinishPredictionExpandedHeight;
		Ui()->ClipEnable(&Visible);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFinishPredictionShowTime, ScLocalize("Show time"), &g_Config.m_ScFinishPredictionShowTime, &Content, LineSize);

		if(FinishPredictionTimeExpandedHeight > 0.5f)
		{
			CUIRect TimeVisible = FinishPredictionView;
			TimeVisible.h = FinishPredictionTimeExpandedHeight;
			Ui()->ClipEnable(&TimeVisible);

			FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
			FinishPredictionView.HSplitTop(LineSize, &Button, &FinishPredictionView);
			static CButtonContainer s_FinishPredictionRemainingButton;
			static CButtonContainer s_FinishPredictionFinishTimeButton;
			CUIRect Left, Right;
			Button.VSplitMid(&Left, &Right);
			if(DoButton_Menu(&s_FinishPredictionRemainingButton, ScLocalize("Time left"), g_Config.m_ScFinishPredictionTimeMode == 0, &Left, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_ScFinishPredictionTimeMode = 0;
			if(DoButton_Menu(&s_FinishPredictionFinishTimeButton, ScLocalize("Finish time"), g_Config.m_ScFinishPredictionTimeMode == 1, &Right, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_ScFinishPredictionTimeMode = 1;

			FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
			FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFinishPredictionShowMillis, ScLocalize("Show milliseconds"), &g_Config.m_ScFinishPredictionShowMillis, &Content, LineSize);

			Ui()->ClipDisable();
		}

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFinishPredictionShowPercentage, ScLocalize("Show percentage"), &g_Config.m_ScFinishPredictionShowPercentage, &Content, LineSize);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFinishPredictionShowAlways, ScLocalize("Show always"), &g_Config.m_ScFinishPredictionShowAlways, &Content, LineSize);

		FinishPredictionView.HSplitTop(MarginSmall, nullptr, &FinishPredictionView);
		FinishPredictionView.HSplitTop(LineSize, &Content, &FinishPredictionView);
		const int PrevConsiderFreezeWalls = g_Config.m_ScFinishPredictionConsiderFreezeWalls;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScFinishPredictionConsiderFreezeWalls, ScLocalize("Consider freeze walls"), &g_Config.m_ScFinishPredictionConsiderFreezeWalls, &Content, LineSize);
		if(PrevConsiderFreezeWalls != g_Config.m_ScFinishPredictionConsiderFreezeWalls)
			GameClient()->m_FinishPrediction.RequestPathRebuild();

		Ui()->ClipDisable();
	}

	RightColumn.HSplitTop(MarginBetweenViews, nullptr, &RightColumn);
	{
		const float RunAnalysisBlockHeight = LineSize + 5.0f * (MarginSmall + LineSize);
		CUIRect RunAnalysisBlock;
		RightColumn.HSplitTop(RunAnalysisBlockHeight, &RunAnalysisBlock, &RightColumn);
		CUIRect RunAnalysisBlockBg = RunAnalysisBlock;
		RunAnalysisBlockBg.w += BlockPadding;
		RunAnalysisBlockBg.h += BlockPadding;
		RunAnalysisBlockBg.x -= BlockPadding * 0.5f;
		RunAnalysisBlockBg.y -= BlockPadding * 0.5f;
		SixClientUiTheme::DrawUiBlock(&RunAnalysisBlockBg, IGraphics::CORNER_ALL, 10.0f);
		SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScRunReport, &RunAnalysisBlockBg);

		CUIRect RunView = RunAnalysisBlock;
		RunView.HSplitTop(LineSize, &Label, &RunView);
		Ui()->DoLabel(&Label, ScLocalize("Run Analysis"), HeadlineFontSize, TEXTALIGN_ML);

		RunView.HSplitTop(MarginSmall, nullptr, &RunView);
		RunView.HSplitTop(LineSize, &Content, &RunView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScRunReport, ScLocalize("Report after every run"), &g_Config.m_ScRunReport, &Content, LineSize);

		RunView.HSplitTop(MarginSmall, nullptr, &RunView);
		RunView.HSplitTop(LineSize, &Button, &RunView);
		Ui()->DoScrollbarOption(&g_Config.m_ScRunReportTime, &g_Config.m_ScRunReportTime, &Button, ScLocalize("Report duration"), 2, 30, &CUi::ms_LinearScrollbarScale, 0, "s");

		RunView.HSplitTop(MarginSmall, nullptr, &RunView);
		RunView.HSplitTop(LineSize, &Content, &RunView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScRunFeedback, ScLocalize("Live hook and jump timing vs PB"), &g_Config.m_ScRunFeedback, &Content, LineSize);

		RunView.HSplitTop(MarginSmall, nullptr, &RunView);
		RunView.HSplitTop(LineSize, &Content, &RunView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScRunStatsSave, ScLocalize("Save attempt statistics per map"), &g_Config.m_ScRunStatsSave, &Content, LineSize);

		RunView.HSplitTop(MarginSmall, nullptr, &RunView);
		RunView.HSplitTop(LineSize, &Label, &RunView);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
		Ui()->DoLabel(&Label, ScLocalize("Needs race ghosts enabled. Type sc_run_stats for your map stats."), FontSize * 0.8f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	const float LeftColumnEndY = LeftColumn.y;
	const float RightColumnEndY = RightColumn.y;
	CUIRect GameplayScrollContentRect;
	GameplayScrollContentRect.x = GameplayScrollContentX;
	GameplayScrollContentRect.y = std::max(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	GameplayScrollContentRect.w = GameplayScrollContentW;
	GameplayScrollContentRect.h = 0.0f;
	s_GameplayScrollRegion.AddRect(GameplayScrollContentRect);
	s_GameplayScrollRegion.End();
}

void CMenus::RenderSettingsSixClientOthers(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float EditBoxFontSize = 12.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;

	static CScrollRegion s_OthersScrollRegion;
	CScrollRegionParams OthersScrollParams;
	OthersScrollParams.m_ScrollUnit = 60.0f;
	OthersScrollParams.m_ScrollbarMargin = 5.0f;
	s_OthersScrollRegion.Begin(&MainView, &OthersScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	const float OthersScrollContentX = MainView.x;
	const float OthersScrollContentW = MainView.w;

	CUIRect Content, Label, Button, LeftView, RightView, Column;

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	Column = LeftView;
	CUIRect RightColumn = RightView;
	Column.HSplitTop(10.0f, nullptr, &Column);
	RightColumn.HSplitTop(10.0f, nullptr, &RightColumn);

	// 667client: beta reports
	{
		CUIRect BetaBlock;
		Column.HSplitTop(LineSize + MarginSmall + LineSize, &BetaBlock, &Column);
		CUIRect BetaBlockBg = BetaBlock;
		BetaBlockBg.w += BlockPadding;
		BetaBlockBg.h += BlockPadding;
		BetaBlockBg.x -= BlockPadding * 0.5f;
		BetaBlockBg.y -= BlockPadding * 0.5f;
		SixClientUiTheme::DrawUiBlock(&BetaBlockBg, IGraphics::CORNER_ALL, 10.0f);
		SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScBetaReports, &BetaBlockBg);

		BetaBlock.HSplitTop(LineSize, &Label, &BetaBlock);
		Ui()->DoLabel(&Label, ScLocalize("Beta"), HeadlineFontSize, TEXTALIGN_ML);
		BetaBlock.HSplitTop(MarginSmall, nullptr, &BetaBlock);
		CUIRect BetaRow;
		BetaBlock.HSplitTop(LineSize, &BetaRow, &BetaBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScBetaReports, ScLocalize("Send crash, freeze and stutter reports"), &g_Config.m_ScBetaReports, &BetaRow, LineSize);
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ScBetaReports, &BetaRow, ScLocalize("While Cecen Client is in beta, crashes, freezes, stutters and lag are sent to the Cecen team with your name, the server, the map, your system and a few settings, so they can be fixed"));
		Column.HSplitTop(MarginBetweenViews, nullptr, &Column);
	}
	// 667client

	const float ChatQoLBlockHeight = LineSize + MarginSmall + 5.0f * LineSize;
	CUIRect ChatQoLBlock;
	Column.HSplitTop(ChatQoLBlockHeight, &ChatQoLBlock, &Column);

	CUIRect ChatQoLBlockBg = ChatQoLBlock;
	ChatQoLBlockBg.w += BlockPadding;
	ChatQoLBlockBg.h += BlockPadding;
	ChatQoLBlockBg.x -= BlockPadding * 0.5f;
	ChatQoLBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&ChatQoLBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ClChatReset, &ChatQoLBlockBg);

	ChatQoLBlock.HSplitTop(LineSize, &Label, &ChatQoLBlock);
	Ui()->DoLabel(&Label, ScLocalize("Chat QoL"), HeadlineFontSize, TEXTALIGN_ML);
	ChatQoLBlock.HSplitTop(MarginSmall, nullptr, &ChatQoLBlock);

	{
		CUIRect SaveDraftRow;
		ChatQoLBlock.HSplitTop(LineSize, &SaveDraftRow, &ChatQoLBlock);
		if(DoButton_CheckBox(&g_Config.m_ClChatReset, ScLocalize("Save unsent messages"), g_Config.m_ClChatReset == 0, &SaveDraftRow))
			g_Config.m_ClChatReset ^= 1;
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScSilentTyping, ScLocalize("Silent typing"), &g_Config.m_ScSilentTyping, &ChatQoLBlock, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatAltCommandLayout, ScLocalize("Commands in other layout"), &g_Config.m_ScChatAltCommandLayout, &ChatQoLBlock, LineSize);
	{
		CUIRect MessageActionsRow;
		ChatQoLBlock.HSplitTop(LineSize, &MessageActionsRow, &ChatQoLBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScChatMessageActions, ScLocalize("Reply/copy message menu"), &g_Config.m_ScChatMessageActions, &MessageActionsRow, LineSize);
	}
	{
		CUIRect NotifySavesRow;
		ChatQoLBlock.HSplitTop(LineSize, &NotifySavesRow, &ChatQoLBlock);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScNotifySavesOnMap, ScLocalize("Notify about saves on map"), &g_Config.m_ScNotifySavesOnMap, &NotifySavesRow, LineSize);
	}

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const float BrowserUtilsBlockHeight = 5.0f * LineSize + 4.0f * MarginSmall;

	CUIRect BrowserUtilsBlock;
	Column.HSplitTop(BrowserUtilsBlockHeight, &BrowserUtilsBlock, &Column);

	CUIRect BrowserUtilsBlockBg = BrowserUtilsBlock;
	BrowserUtilsBlockBg.w += BlockPadding;
	BrowserUtilsBlockBg.h += BlockPadding;
	BrowserUtilsBlockBg.x -= BlockPadding * 0.5f;
	BrowserUtilsBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&BrowserUtilsBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScAutoServerListRefresh, &BrowserUtilsBlockBg);

	BrowserUtilsBlock.HSplitTop(LineSize, &Label, &BrowserUtilsBlock);
	Ui()->DoLabel(&Label, ScLocalize("Browser Utils"), HeadlineFontSize, TEXTALIGN_ML);
	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);

	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScAutoServerListRefresh, ScLocalize("Auto server list refresh"), &g_Config.m_ScAutoServerListRefresh, &Content, LineSize);

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Button, &BrowserUtilsBlock);
	Ui()->DoScrollbarOption(&g_Config.m_ScAutoServerListRefreshSeconds, &g_Config.m_ScAutoServerListRefreshSeconds, &Button, ScLocalize("Seconds"), 1, 300, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, " s");

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScUseShortKogServerName, ScLocalize("Short EGO/KoG server name"), &g_Config.m_ScUseShortKogServerName, &Content, LineSize);

	BrowserUtilsBlock.HSplitTop(MarginSmall, nullptr, &BrowserUtilsBlock);
	BrowserUtilsBlock.HSplitTop(LineSize, &Content, &BrowserUtilsBlock);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScShowFinishedMapOnEgo, ScLocalize("Show finished map on EGO"), &g_Config.m_ScShowFinishedMapOnEgo, &Content, LineSize);

	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

	const bool FocusModeEnabled = g_Config.m_ClFocusMode != 0;
	const bool FocusModeAutoReplyEnabled = g_Config.m_ClFocusModeAutoReply != 0;
	const float FocusModeHeaderHeight = LineSize + MarginSmall + LineSize;
	const float FocusModeExpandedTargetHeight = (9.0f + (FocusModeAutoReplyEnabled ? 1.0f : 0.0f)) * (MarginSmall + LineSize);
	const float FocusModeExpandedHeight = SC_MODULE_REVEAL(FocusModeEnabled, FocusModeExpandedTargetHeight, Client()->RenderFrameTime());
	const float FocusModeBlockHeight = FocusModeHeaderHeight + FocusModeExpandedHeight;

	CUIRect FocusModeBlock;
	Column.HSplitTop(FocusModeBlockHeight, &FocusModeBlock, &Column);

	CUIRect FocusModeBlockBg = FocusModeBlock;
	FocusModeBlockBg.w += BlockPadding;
	FocusModeBlockBg.h += BlockPadding;
	FocusModeBlockBg.x -= BlockPadding * 0.5f;
	FocusModeBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&FocusModeBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ClFocusMode, &FocusModeBlockBg);

	{
		CUIRect FocusMain = FocusModeBlock;
		FocusMain.HSplitTop(LineSize, &Label, &FocusMain);
		Ui()->DoLabel(&Label, ScLocalize("Focus Mode"), HeadlineFontSize, TEXTALIGN_ML);
		FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);

		FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusMode, ScLocalize("Enable Focus Mode"), &g_Config.m_ClFocusMode, &Content, LineSize);

		if(FocusModeExpandedHeight > 0.5f)
		{
			CUIRect Visible = FocusMain;
			Visible.h = FocusModeExpandedHeight;
			Ui()->ClipEnable(&Visible);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideNames, ScLocalize("Hide Player Names"), &g_Config.m_ClFocusModeHideNames, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideEffects, ScLocalize("Hide Visual Effects"), &g_Config.m_ClFocusModeHideEffects, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideHud, ScLocalize("Hide HUD"), &g_Config.m_ClFocusModeHideHud, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideSongPlayer, ScLocalize("Hide Song Player"), &g_Config.m_ClFocusModeHideSongPlayer, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideUI, ScLocalize("Hide Unnecessary UI"), &g_Config.m_ClFocusModeHideUI, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideChat, ScLocalize("Hide Chat"), &g_Config.m_ClFocusModeHideChat, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeHideScoreboard, ScLocalize("Hide Scoreboard"), &g_Config.m_ClFocusModeHideScoreboard, &Content, LineSize);

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFocusModeAutoReply, ScLocalize("Auto reply when chat is hidden"), &g_Config.m_ClFocusModeAutoReply, &Content, LineSize);

			if(FocusModeAutoReplyEnabled)
			{
				FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
				FocusMain.HSplitTop(LineSize, &Content, &FocusMain);
				static CLineInput s_FocusModeAutoReplyMessage(g_Config.m_ClFocusModeAutoReplyMessage, sizeof(g_Config.m_ClFocusModeAutoReplyMessage));
				s_FocusModeAutoReplyMessage.SetEmptyText("my chat is hidden");
				Ui()->DoEditBox(&s_FocusModeAutoReplyMessage, &Content, EditBoxFontSize);
			}

			FocusMain.HSplitTop(MarginSmall, nullptr, &FocusMain);
			FocusMain.HSplitTop(LineSize, &Label, &FocusMain);
			static CButtonContainer s_FocusModeBindReader;
			static CButtonContainer s_FocusModeBindClear;
			DoLine_KeyReader(Label, s_FocusModeBindReader, s_FocusModeBindClear, ScLocalize("Focus mode bind"), "toggle p_focus_mode 0 1");

			Ui()->ClipDisable();
		}
	}

	CUIRect OthersScrollContentRect;
	OthersScrollContentRect.x = OthersScrollContentX;
	OthersScrollContentRect.y = std::max(Column.y, RightColumn.y) + MarginSmall * 2.0f;
	OthersScrollContentRect.w = OthersScrollContentW;
	OthersScrollContentRect.h = 0.0f;
	s_OthersScrollRegion.AddRect(OthersScrollContentRect);
	s_OthersScrollRegion.End();
}

void CMenus::RenderSettingsSixClientInfo(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 30.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = HeadlineFontSize;

	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	CUIRect GamesButton;
	MainView.HSplitTop(24.0f, &GamesButton, &MainView);
	static CButtonContainer s_GamesButton;
	if(DoButton_MenuTab(&s_GamesButton, ScLocalize("Mini games"), 0, &GamesButton, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
		OpenSixClientFun();
	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	// ── Cecen Client Links ───────────────────────────────────────────────────
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, ScLocalize("Cecen Client Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, ScLocalize("Discord"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/667");
	if(DoButtonLineSize_Menu(&s_WebsiteButton, ScLocalize("Website"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://www.revo667.com");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
	Ui()->DoLabel(&Label, "© 2026 667.wtf · discord.gg/667 · contact@revo667.com", 11.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

#if defined(CONF_AUTOUPDATE)
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	static CButtonContainer s_CheckUpdateButton;
	if(DoButtonLineSize_Menu(&s_CheckUpdateButton, ScLocalize("Check update"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Updater()->CheckForUpdate();
#endif

	// ── Config Files (anchored to the bottom of the left column) ──────────
	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 2.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, ScLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect SixClientConfig;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	SixClientConfig = Button;

	static CButtonContainer s_Config;
	if(DoButtonLineSize_Menu(&s_Config, ScLocalize("Cecen Client Settings"), 0, &SixClientConfig, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::SIXCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	// ── Cecen Client Developers ─────────────────────────────────────────────
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, ScLocalize("Cecen Client Developers"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 64.0f;
	const float DevNameFontSize = 24.0f;
	const float CardSize = TeeSize + MarginSmall * 2.0f;
	CUIRect TeeRect, DevCardRect;
	static CButtonContainer s_LinkButton1;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(DevNameFontSize, "revo667"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = DevNameFontSize;
		Button.h = DevNameFontSize;
		Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "revo667", DevNameFontSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton1, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/revo667");
		RenderDevSkin(TeeRect.Center(), TeeSize, "greyfox", "default", true, 364436, 14931191, 0, false, true);
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Ui()->DoLabel(&Label, "rain cakir", DevNameFontSize, TEXTALIGN_ML);
		RenderDevSkin(TeeRect.Center(), TeeSize, "kir4i", "default", false, 0, 0, 0, false, true);
	}

	// ── Hide Settings Tabs ────────────────────────────────────────────────
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, ScLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const char *apTabNames[NUM_SIXCLIENT_TABS] = {
		ScLocalize("Visuals"),
		ScLocalize("Gameplay"),
		ScLocalize("Others"),
		ScLocalize("Cosmetics"),
		ScLocalize("Sensitivity"),
		ScLocalize("Assets Editor"),
		ScLocalize("Configs"),
		ScLocalize("Info"),
		ScLocalize("Assets Market"),
	};
	const int aTabOrder[NUM_SIXCLIENT_TABS] = {
		SIXCLIENT_TAB_VISUALS,
		SIXCLIENT_TAB_GAMEPLAY,
		SIXCLIENT_TAB_OTHERS,
		SIXCLIENT_TAB_COSMETICS,
		SIXCLIENT_TAB_SENSITIVITY,
		SIXCLIENT_TAB_ASSETS,
		SIXCLIENT_TAB_ASSETS_MARKET,
		SIXCLIENT_TAB_CONFIGS,
		SIXCLIENT_TAB_INFO,
	};

	CUIRect LeftSettings, RightSettings;
	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);

	static CButtonContainer s_aShowTabButtons[NUM_SIXCLIENT_TABS] = {};
	int HideableTabCount = 0;
	int HideableVisibleIndex = 0;
	for(const int Tab : aTabOrder)
	{
		if(Tab == SIXCLIENT_TAB_INFO)
			continue;

		++HideableTabCount;
		int Hidden = IsSixClientTabFlagSet(g_Config.m_ScSixClientSettingsTabs, Tab);
		CUIRect *pColumn = HideableVisibleIndex % 2 == 0 ? &LeftSettings : &RightSettings;
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabButtons[Tab], apTabNames[Tab], &Hidden, pColumn, LineSize);
		SetSixClientTabFlag(g_Config.m_ScSixClientSettingsTabs, Tab, Hidden);
		++HideableVisibleIndex;
	}
	const int HideableRows = (HideableTabCount + 1) / 2;
	RightView.HSplitTop(LineSize * (HideableRows + 0.5f), nullptr, &RightView);
}

void CMenus::RenderSettingsSixClientConfigPresets(CUIRect MainView)
{
	const float FontSize = 14.0f;
	const float EditBoxFontSize = 12.0f;
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;

	CScConfigPresets &Presets = GameClient()->m_ScConfigPresets;
	static int s_Selected = -1;
	static CLineInputBuffered<CScConfigPresets::MAX_NAME_LENGTH> s_NameInput;
	if(s_Selected >= (int)Presets.m_vPresets.size())
		s_Selected = (int)Presets.m_vPresets.size() - 1;

	CUIRect Label, Button;
	const bool HasSelection = s_Selected >= 0 && s_Selected < (int)Presets.m_vPresets.size();
	{
		CUIRect Top, NameColumn, Actions;
		MainView.HSplitTop(30.0f + MarginSmall + 30.0f, &Top, &MainView);
		Top.VSplitLeft(300.0f, &NameColumn, &Actions);
		CUIRect NameCard;
		NameColumn.HSplitTop(60.0f, &NameCard, &NameColumn);
		NameCard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
		NameCard.Margin(5.0f, &NameCard);
		{
			CUIRect Icon, Text, NameRow;
			NameCard.VSplitLeft(50.0f, &Icon, &Text);
			RenderFontIcon(Icon, FontIcon::FILE, 28.0f, TEXTALIGN_MC);
			Text.VSplitLeft(5.0f, nullptr, &Text);
			const float RowHeight = Text.h / 2.0f;
			Text.HSplitTop(RowHeight, &NameRow, &Text);
			NameRow.VMargin(2.0f, &NameRow);
			Ui()->DoEditBox(&s_NameInput, &NameRow, EditBoxFontSize);
			s_NameInput.SetEmptyText(ScLocalize("Name"));
			char aTime[64];
			str_timestamp_ex(time_timestamp(), aTime, sizeof(aTime), "%d.%m.%Y %H:%M");
			Ui()->DoLabel(&Text, aTime, RowHeight * 0.55f, TEXTALIGN_ML);
		}

		Actions.VSplitLeft(20.0f, nullptr, &Actions);
		CUIRect Row, LeftButton, RightButton;
		Actions.HSplitTop(30.0f, &Row, &Actions);
		Row.VSplitMid(&LeftButton, &RightButton, MarginSmall);
		static CButtonContainer s_LoadButton;
		if(DoButton_Menu(&s_LoadButton, ScLocalize("Load"), 0, &LeftButton) && HasSelection)
			Presets.Load(Presets.m_vPresets[s_Selected].m_aName);
		static CButtonContainer s_SaveButton;
		if(DoButton_Menu(&s_SaveButton, ScLocalize("Save"), 0, &RightButton))
		{
			const char *pName = s_NameInput.GetString();
			if(Presets.SaveNew(pName))
			{
				for(int i = 0; i < (int)Presets.m_vPresets.size(); ++i)
				{
					if(str_comp(Presets.m_vPresets[i].m_aName, pName) == 0)
						s_Selected = i;
				}
				s_NameInput.Clear();
			}
		}
		Actions.HSplitTop(MarginSmall, nullptr, &Actions);
		Actions.HSplitTop(30.0f, &Row, &Actions);
		Row.VSplitMid(&LeftButton, &RightButton, MarginSmall);
		static CButtonContainer s_DeleteButton;
		if(DoButton_Menu(&s_DeleteButton, ScLocalize("Delete"), 0, &LeftButton) && HasSelection)
		{
			Presets.Delete(Presets.m_vPresets[s_Selected].m_aName);
			if(s_Selected >= (int)Presets.m_vPresets.size())
				s_Selected = (int)Presets.m_vPresets.size() - 1;
		}
		static CButtonContainer s_OverrideButton;
		if(DoButton_Menu(&s_OverrideButton, ScLocalize("Override"), 0, &RightButton) && HasSelection)
			Presets.Override(Presets.m_vPresets[s_Selected].m_aName);
	}

	{
		CUIRect Bottom, Saved;
		MainView.HSplitBottom(LineSize + MarginSmall, &MainView, &Bottom);
		Bottom.HSplitTop(MarginSmall, nullptr, &Bottom);
		Bottom.VSplitLeft(170.0f, &Button, &Bottom);
		static CButtonContainer s_FolderButton;
		char aFolder[64];
		str_format(aFolder, sizeof(aFolder), "%s %s", ScLocalize("Configs"), ScLocalize("Folder"));
		if(DoButton_Menu(&s_FolderButton, aFolder, 0, &Button))
			Presets.OpenFolder();
		Bottom.VSplitRight(120.0f, nullptr, &Saved);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), ScLocalize("Saved: %d"), (int)Presets.m_vPresets.size());
		Ui()->DoLabel(&Saved, aBuf, FontSize, TEXTALIGN_MR);
	}

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	static CListBox s_ListBox;
	s_ListBox.DoStart(64.0f, Presets.m_vPresets.size(), MainView.w / 220.0f, 3, s_Selected, &MainView, true, IGraphics::CORNER_ALL, true);
	static std::vector<int> s_Ids;
	if(s_Ids.size() < Presets.m_vPresets.size())
		s_Ids.resize(Presets.m_vPresets.size());
	for(int i = 0; i < (int)Presets.m_vPresets.size(); ++i)
	{
		const CListboxItem Item = s_ListBox.DoNextItem(&s_Ids[i], s_Selected == i);
		if(!Item.m_Visible)
			continue;

		CUIRect Icon, Text;
		Item.m_Rect.VSplitLeft(50.0f, &Icon, &Text);
		Text.VSplitLeft(5.0f, nullptr, &Text);
		RenderFontIcon(Icon, FontIcon::FILE, 28.0f, TEXTALIGN_MC);

		char aTime[64];
		str_timestamp_ex(Presets.m_vPresets[i].m_Time, aTime, sizeof(aTime), "%d.%m.%Y %H:%M");
		char aChanges[64];
		str_format(aChanges, sizeof(aChanges), ScLocalize("Changes: %d"), Presets.m_vPresets[i].m_Changes);
		const float RowHeight = Text.h / 3.0f;
		Text.HSplitTop(RowHeight, &Label, &Text);
		Ui()->DoLabel(&Label, Presets.m_vPresets[i].m_aName, RowHeight * 0.8f, TEXTALIGN_ML);
		Text.HSplitTop(RowHeight, &Label, &Text);
		Ui()->DoLabel(&Label, aTime, RowHeight * 0.7f, TEXTALIGN_ML);
		Ui()->DoLabel(&Text, aChanges, RowHeight * 0.7f, TEXTALIGN_ML);
	}
	s_Selected = s_ListBox.DoEnd();
}

void CMenus::RenderSettingsSixClientWeaponGlow(CUIRect &Column)
{
	constexpr float LineSize = 20.0f;
	constexpr float MarginSmall = 5.0f;
	constexpr float PreviewHeight = 106.0f;
	static int s_Weapon = 0;
	static CButtonContainer s_aWeaponButtons[3];
	static CButtonContainer s_aModeButtons[4];
	static CButtonContainer s_aResetColor[3];
	static CButtonContainer s_ResetButton;
	const char *apTitles[] = {ScLocalize("Laser Glow"), ScLocalize("Shotgun Glow"), ScLocalize("Rocket Glow")};
	const char *apWeapons[] = {ScLocalize("Laser"), ScLocalize("Shotgun"), ScLocalize("Rocket")};
	const char *apModes[] = {ScLocalize("Classic"), ScLocalize("Prism"), ScLocalize("Pulse"), ScLocalize("Crystal")};
	int *apEnabled[] = {&g_Config.m_ScLaserGlow, &g_Config.m_ScShotgunGlow, &g_Config.m_ScRocketGlow};
	int *apPower[] = {&g_Config.m_ScLaserGlowPower, &g_Config.m_ScShotgunGlowPower, &g_Config.m_ScRocketGlowPower};
	int *apMode[] = {&g_Config.m_ScLaserGlowMode, &g_Config.m_ScShotgunGlowMode, &g_Config.m_ScRocketGlowMode};
	unsigned *apColor[] = {&g_Config.m_ScLaserGlowColor, &g_Config.m_ScShotgunGlowColor, &g_Config.m_ScRocketGlowColor};
	const unsigned aDefaultColors[] = {DefaultConfig::ScLaserGlowColor, DefaultConfig::ScShotgunGlowColor, DefaultConfig::ScRocketGlowColor};
	const float NukeHeight = s_Weapon == 2 ? (g_Config.m_ScMemeNukeExplosion ? 4 : 2) * (LineSize + MarginSmall) : 0.0f;
	const float Height = 6 * (LineSize + MarginSmall) + PreviewHeight + MarginSmall + NukeHeight;
	CUIRect Block;
	Column.HSplitTop(Height, &Block, &Column);
	CUIRect Background = Block;
	Background.Margin(-10.0f, &Background);
	SixClientUiTheme::DrawUiBlock(&Background, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScLaserGlow, &Background);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScShotgunGlow, &Background);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScRocketGlow, &Background);
	CUIRect Row, Button;
	Block.HSplitTop(LineSize, &Row, &Block);
	CUIRect ResetButton;
	Row.VSplitRight(LineSize + 8.0f, &Row, &ResetButton);
	const bool ResetClicked = Ui()->DoButton_FontIcon(&s_ResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_ResetButton, &ResetButton, ScLocalize("Reset to defaults"));
	Ui()->DoLabel(&Row, apTitles[s_Weapon], 20.0f, TEXTALIGN_ML);
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	const float WeaponWidth = Row.w / 3.0f;
	int NextWeapon = s_Weapon;
	for(int i = 0; i < 3; ++i)
	{
		Row.VSplitLeft(WeaponWidth, &Button, &Row);
		Button.HMargin(2.0f, &Button);
		const int Corners = i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_Menu(&s_aWeaponButtons[i], apWeapons[i], s_Weapon == i, &Button, BUTTONFLAG_LEFT, nullptr, Corners))
			NextWeapon = i;
	}
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	DoButton_CheckBoxAutoVMarginAndSet(apEnabled[s_Weapon], ScLocalize("Enable"), apEnabled[s_Weapon], &Row, LineSize);
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	Ui()->DoScrollbarOption(apPower[s_Weapon], apPower[s_Weapon], &Row, ScLocalize("Power"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	DoLine_ColorPicker(&s_aResetColor[s_Weapon], LineSize, 15.0f, MarginSmall, &Block, ScLocalize("Color"), apColor[s_Weapon],
		color_cast<ColorRGBA>(ColorHSLA(aDefaultColors[s_Weapon])), false, nullptr, false);
	CUIRect Preview;
	Block.HSplitTop(PreviewHeight, &Preview, &Block);
	Ui()->ClipEnable(&Preview);
	if(s_Weapon == 0)
		DoLaserPreview(&Preview, ColorHSLA(g_Config.m_ClLaserRifleOutlineColor), ColorHSLA(g_Config.m_ClLaserRifleInnerColor), LASERTYPE_RIFLE);
	else if(s_Weapon == 1)
		DoLaserPreview(&Preview, ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor), ColorHSLA(g_Config.m_ClLaserShotgunInnerColor), LASERTYPE_SHOTGUN);
	else
		DoRocketPreview(&Preview);
	Ui()->ClipDisable();
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Row, &Block);
	const float ModeWidth = Row.w / 4.0f;
	for(int i = 0; i < 4; ++i)
	{
		Row.VSplitLeft(ModeWidth, &Button, &Row);
		Button.HMargin(2.0f, &Button);
		const int Corners = i == 0 ? IGraphics::CORNER_L : (i == 3 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_Menu(&s_aModeButtons[i], apModes[i], *apMode[s_Weapon] == i, &Button, BUTTONFLAG_LEFT, nullptr, Corners))
			*apMode[s_Weapon] = i;
	}
	if(s_Weapon == 2)
	{
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Row, &Block);
		Ui()->DoLabel(&Row, ScLocalize("Nuke effects"), 16.0f, TEXTALIGN_ML);
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &Row, &Block);
		const bool NukeExpanded = g_Config.m_ScMemeNukeExplosion != 0;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScMemeNukeExplosion, ScLocalize("Enable Nuke effects"), &g_Config.m_ScMemeNukeExplosion, &Row, LineSize);
		if(NukeExpanded)
		{
			Block.HSplitTop(MarginSmall, nullptr, &Block);
			Block.HSplitTop(LineSize, &Row, &Block);
			Ui()->DoScrollbarOption(&g_Config.m_ScMemeNukeShake, &g_Config.m_ScMemeNukeShake, &Row, ScLocalize("Screen shake"), 0, 200, &CUi::ms_LinearScrollbarScale, 0u, "%");
			Block.HSplitTop(MarginSmall, nullptr, &Block);
			Block.HSplitTop(LineSize, &Row, &Block);
			Ui()->DoScrollbarOption(&g_Config.m_ScMemeNukeScale, &g_Config.m_ScMemeNukeScale, &Row, ScLocalize("Nuke size"), 20, 300, &CUi::ms_LinearScrollbarScale, 0u, "%");
		}
	}
	if(ResetClicked)
	{
		const int aDefaultEnabled[] = {DefaultConfig::ScLaserGlow, DefaultConfig::ScShotgunGlow, DefaultConfig::ScRocketGlow};
		const int aDefaultPower[] = {DefaultConfig::ScLaserGlowPower, DefaultConfig::ScShotgunGlowPower, DefaultConfig::ScRocketGlowPower};
		const int aDefaultModes[] = {DefaultConfig::ScLaserGlowMode, DefaultConfig::ScShotgunGlowMode, DefaultConfig::ScRocketGlowMode};
		*apEnabled[s_Weapon] = aDefaultEnabled[s_Weapon];
		*apPower[s_Weapon] = aDefaultPower[s_Weapon];
		*apMode[s_Weapon] = aDefaultModes[s_Weapon];
		*apColor[s_Weapon] = aDefaultColors[s_Weapon];
		if(s_Weapon == 2)
		{
			g_Config.m_ScMemeNukeExplosion = DefaultConfig::ScMemeNukeExplosion;
			g_Config.m_ScMemeNukeShake = DefaultConfig::ScMemeNukeShake;
			g_Config.m_ScMemeNukeScale = DefaultConfig::ScMemeNukeScale;
		}
	}
	s_Weapon = NextWeapon;
}

void CMenus::DoRocketPreview(const CUIRect *pRect)
{
	CUIRect Section = *pRect;

	const vec2 From = vec2(Section.x + 36.0f, Section.y + Section.h * 0.5f);
	const vec2 Pos = vec2(Section.x + Section.w - 36.0f, Section.y + Section.h * 0.5f);

	if(g_Config.m_ScRocketGlow)
	{
		const float Power = (float)g_Config.m_ScRocketGlowPower;
		const int Mode = std::clamp(g_Config.m_ScRocketGlowMode, 0, 3);
		const int NumPoints = 16;
		vec2 aPoints[16];
		float aAlphas[16];
		const float GlobalTime = Client()->GlobalTime();

		for(int i = 0; i < NumPoints; ++i)
		{
			float t = (float)i / (float)(NumPoints - 1);
			float Arc = std::sin(t * pi) * 10.0f * std::sin(GlobalTime * 3.0f + t * 2.0f);
			aPoints[i] = mix(Pos, From, t) + vec2(0.0f, Arc);
			aAlphas[i] = 1.0f;
		}
		GameClient()->m_WeaponVfx.RenderRocketTrajectory(aPoints, aAlphas, NumPoints, Mode, Power);
	}

	Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponGrenade);
	Graphics()->SelectSprite(SPRITE_WEAPON_GRENADE_BODY);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->DrawSprite(From.x, From.y, 48.0f);
	Graphics()->QuadsEnd();

	if(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[WEAPON_GRENADE].IsValid())
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[WEAPON_GRENADE]);
		Graphics()->SelectSprite(SPRITE_WEAPON_GRENADE_PROJ);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DrawSprite(Pos.x, Pos.y, 24.0f);
		Graphics()->QuadsEnd();
	}
}

void CMenus::RenderSettingsSixClientParticles3D(CUIRect &Column)
{
	CUIRect MainView; // scratch rect, as in the Visuals tab these blocks came from
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	CUIRect Content, Label, Button;

	const float Particles3DColorPickerLineSize = 25.0f;
	const float Particles3DColorPickerLabelSize = 13.0f;
	const float Particles3DColorPickerSpacing = 5.0f;
	const bool Particles3DEnabled = g_Config.m_Bc3dParticles != 0;
	const bool Particles3DShowCustomColor = Particles3DEnabled && g_Config.m_Bc3dParticlesColorMode == 1;
	const float Particles3DExpandedTargetHeight =
		(MarginSmall + LineSize) * 5.0f + (Particles3DShowCustomColor ? MarginSmall + Particles3DColorPickerLineSize + Particles3DColorPickerSpacing : 0.0f);
	const float Particles3DExpandedHeight = SC_MODULE_REVEAL(Particles3DEnabled, Particles3DExpandedTargetHeight, Client()->RenderFrameTime());
	const float Particles3DContentHeight = LineSize + MarginSmall + LineSize + Particles3DExpandedHeight;

	CUIRect Particles3DBlock;
	Column.HSplitTop(Particles3DContentHeight, &Particles3DBlock, &Column);

	CUIRect Particles3DBlockBg = Particles3DBlock;
	Particles3DBlockBg.w += BlockPadding;
	Particles3DBlockBg.h += BlockPadding;
	Particles3DBlockBg.x -= BlockPadding * 0.5f;
	Particles3DBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&Particles3DBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_Bc3dParticles, &Particles3DBlockBg);

	MainView = Particles3DBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect Particles3DTitleLabel, Particles3DResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Particles3DTitleLabel, &Particles3DResetButton);
	static CButtonContainer s_3DParticlesResetButton;
	if(Ui()->DoButton_FontIcon(&s_3DParticlesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &Particles3DResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_Bc3dParticlesType = DefaultConfig::Bc3dParticlesType;
		g_Config.m_Bc3dParticlesDensity = DefaultConfig::Bc3dParticlesDensity;
		g_Config.m_Bc3dParticlesSizeMax = DefaultConfig::Bc3dParticlesSizeMax;
		g_Config.m_Bc3dParticlesSpeed = DefaultConfig::Bc3dParticlesSpeed;
		g_Config.m_Bc3dParticlesDepth = DefaultConfig::Bc3dParticlesDepth;
		g_Config.m_Bc3dParticlesAlpha = DefaultConfig::Bc3dParticlesAlpha;
		g_Config.m_Bc3dParticlesPushRadius = DefaultConfig::Bc3dParticlesPushRadius;
		g_Config.m_Bc3dParticlesPushStrength = DefaultConfig::Bc3dParticlesPushStrength;
		g_Config.m_Bc3dParticlesCollide = DefaultConfig::Bc3dParticlesCollide;
		g_Config.m_Bc3dParticlesViewMargin = DefaultConfig::Bc3dParticlesViewMargin;
		g_Config.m_Bc3dParticlesColorMode = DefaultConfig::Bc3dParticlesColorMode;
		g_Config.m_Bc3dParticlesColor = DefaultConfig::Bc3dParticlesColor;
		g_Config.m_Bc3dParticlesGlow = DefaultConfig::Bc3dParticlesGlow;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_3DParticlesResetButton, &Particles3DResetButton, ScLocalize("Reset to defaults"));
	Ui()->DoLabel(&Particles3DTitleLabel, ScLocalize("3D Particles"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticles, ScLocalize("Enable 3D Particles"), &g_Config.m_Bc3dParticles, &Content, LineSize);

	if(Particles3DExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = Particles3DExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesDensity, &g_Config.m_Bc3dParticlesDensity, &Button, ScLocalize("Density"), 1, 600);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect Particles3DTypeLabel, Particles3DTypeSelect;
		Button.VSplitLeft(150.0f, &Particles3DTypeLabel, &Particles3DTypeSelect);
		Ui()->DoLabel(&Particles3DTypeLabel, ScLocalize("Particle type"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_3DParticlesTypeState;
		static CScrollRegion s_3DParticlesTypeScrollRegion;
		s_3DParticlesTypeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesTypeScrollRegion;
		const char *Ap3DParticleTypes[3] = {
			ScLocalize("Cube"),
			ScLocalize("Heart"),
			ScLocalize("Mixed"),
		};
		g_Config.m_Bc3dParticlesType = Ui()->DoDropDown(&Particles3DTypeSelect, g_Config.m_Bc3dParticlesType - 1, Ap3DParticleTypes, (int)std::size(Ap3DParticleTypes), s_3DParticlesTypeState) + 1;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_Bc3dParticlesSizeMax, &g_Config.m_Bc3dParticlesSizeMax, &Button, ScLocalize("Size"), 2, 200);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		CUIRect Particles3DColorModeLabel, Particles3DColorModeSelect;
		Button.VSplitLeft(150.0f, &Particles3DColorModeLabel, &Particles3DColorModeSelect);
		Ui()->DoLabel(&Particles3DColorModeLabel, ScLocalize("Color mode"), 14.0f, TEXTALIGN_ML);

		static CUi::SDropDownState s_3DParticlesColorModeState;
		static CScrollRegion s_3DParticlesColorModeScrollRegion;
		s_3DParticlesColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_3DParticlesColorModeScrollRegion;
		const char *Ap3DParticleColorModes[2] = {
			ScLocalize("Custom"),
			ScLocalize("Random"),
		};
		g_Config.m_Bc3dParticlesColorMode = Ui()->DoDropDown(&Particles3DColorModeSelect, g_Config.m_Bc3dParticlesColorMode - 1, Ap3DParticleColorModes, (int)std::size(Ap3DParticleColorModes), s_3DParticlesColorModeState) + 1;

		if(Particles3DShowCustomColor)
		{
			static CButtonContainer s_3DParticlesColorButton;
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			DoLine_ColorPicker(&s_3DParticlesColorButton, Particles3DColorPickerLineSize, Particles3DColorPickerLabelSize, Particles3DColorPickerSpacing, &MainView, ScLocalize("Color"), &g_Config.m_Bc3dParticlesColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		}

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_Bc3dParticlesGlow, ScLocalize("Glow"), &g_Config.m_Bc3dParticlesGlow, &Content, LineSize);

		Ui()->ClipDisable();
	}

}

void CMenus::RenderSettingsSixClientCursorTrail(CUIRect &Column)
{
	CUIRect MainView; // scratch rect, as in the Visuals tab these blocks came from
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	CUIRect Content, Label, Button;

	const bool CursorTrailExpanded = g_Config.m_ScCursorTrail != 0;
	const bool CursorTrailCustom = CursorTrailExpanded && g_Config.m_ScCursorTrailMode == 1;
	const float CursorTrailTargetHeight = 6.0f * (MarginSmall + LineSize) + (CursorTrailCustom ? MarginSmall + LineSize : 0.0f);
	const float CursorTrailExpandedHeight = SC_MODULE_REVEAL(CursorTrailExpanded, CursorTrailTargetHeight, Client()->RenderFrameTime());
	const float CursorTrailBlockHeight = 2.0f * LineSize + MarginSmall + CursorTrailExpandedHeight;
	CUIRect CursorTrailBlock;
	Column.HSplitTop(CursorTrailBlockHeight, &CursorTrailBlock, &Column);
	CUIRect CursorTrailBlockBg = CursorTrailBlock;
	CursorTrailBlockBg.w += BlockPadding;
	CursorTrailBlockBg.h += BlockPadding;
	CursorTrailBlockBg.x -= BlockPadding * 0.5f;
	CursorTrailBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&CursorTrailBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScCursorTrail, &CursorTrailBlockBg);
	MainView = CursorTrailBlock;
	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect CursorTrailTitleLabel, CursorTrailResetButton;
	Label.VSplitRight(LineSize + 8.0f, &CursorTrailTitleLabel, &CursorTrailResetButton);
	static CButtonContainer s_CursorTrailResetButton;
	if(Ui()->DoButton_FontIcon(&s_CursorTrailResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &CursorTrailResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_ScCursorTrailMode = DefaultConfig::ScCursorTrailMode;
		g_Config.m_ScCursorTrailTrailImage[0] = '\0';
		g_Config.m_ScCursorTrailTrailSize = DefaultConfig::ScCursorTrailTrailSize;
		g_Config.m_ScCursorTrailNumberOfFrames = DefaultConfig::ScCursorTrailNumberOfFrames;
		g_Config.m_ScCursorTrailOpacity = DefaultConfig::ScCursorTrailOpacity;
		g_Config.m_ScCursorTrailSamplingFps = DefaultConfig::ScCursorTrailSamplingFps;
		g_Config.m_ScCursorTrailDisableMovement = DefaultConfig::ScCursorTrailDisableMovement;
		GameClient()->m_CursorTrail.Reload();
	}
	Ui()->DoLabel(&CursorTrailTitleLabel, ScLocalize("Cursor Trail"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Content, &MainView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCursorTrail, ScLocalize("Enable"), &g_Config.m_ScCursorTrail, &Content, LineSize);

	if(CursorTrailExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = CursorTrailExpandedHeight;
		Ui()->ClipEnable(&Visible);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		static CUi::SDropDownState s_CursorTrailModeState;
		static CScrollRegion s_CursorTrailModeScrollRegion;
		s_CursorTrailModeState.m_SelectionPopupContext.m_pScrollRegion = &s_CursorTrailModeScrollRegion;
		const char *apCursorTrailModes[] = {ScLocalize("Cursor"), ScLocalize("Custom")};
		CUIRect ModeLabel, ModeRow;
		Content.VSplitLeft(110.0f, &ModeLabel, &ModeRow);
		Ui()->DoLabel(&ModeLabel, ScLocalize("Mode"), 12.0f, TEXTALIGN_ML);
		g_Config.m_ScCursorTrailMode = Ui()->DoDropDown(&ModeRow, g_Config.m_ScCursorTrailMode, apCursorTrailModes, std::size(apCursorTrailModes), s_CursorTrailModeState);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScCursorTrailTrailSize, &g_Config.m_ScCursorTrailTrailSize, &Button, ScLocalize("Trail size"), 10, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScCursorTrailNumberOfFrames, &g_Config.m_ScCursorTrailNumberOfFrames, &Button, ScLocalize("Number of frames"), 1, 10);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScCursorTrailOpacity, &g_Config.m_ScCursorTrailOpacity, &Button, ScLocalize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScCursorTrailSamplingFps, &g_Config.m_ScCursorTrailSamplingFps, &Button, ScLocalize("Sampling FPS"), 24, 120);
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScCursorTrailDisableMovement, ScLocalize("Disable movement"), &g_Config.m_ScCursorTrailDisableMovement, &Content, LineSize);
		if(CursorTrailCustom)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			struct SCursorTrailFileListContext
			{
				std::vector<std::string> *m_pLabels;
				std::vector<std::string> *m_pPaths;
			};
			auto CursorTrailFileListScan = [](const char *pName, int IsDir, int StorageType, void *pUser) {
				(void)StorageType;
				if(IsDir)
					return 0;
				const std::string Ext = MediaDecoder::ExtractExtensionLower(pName);
				if(Ext != "png" && Ext != "jpg" && Ext != "jpeg" && Ext != "webp" && Ext != "bmp" && Ext != "avif")
					return 0;
				auto *pContext = static_cast<SCursorTrailFileListContext *>(pUser);
				pContext->m_pLabels->emplace_back(pName);
				pContext->m_pPaths->emplace_back(std::string("667Client/CTrails/") + pName);
				return 0;
			};
			Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("667Client/CTrails", IStorage::TYPE_SAVE);
			static std::vector<std::string> s_vCursorTrailFileLabels, s_vCursorTrailFilePaths;
			s_vCursorTrailFileLabels.clear();
			s_vCursorTrailFilePaths.clear();
			SCursorTrailFileListContext CursorTrailFileContext{&s_vCursorTrailFileLabels, &s_vCursorTrailFilePaths};
			Storage()->ListDirectory(IStorage::TYPE_SAVE, "667Client/CTrails", CursorTrailFileListScan, &CursorTrailFileContext);
			std::vector<int> vSortedCursorTrailIndices(s_vCursorTrailFileLabels.size());
			for(size_t i = 0; i < vSortedCursorTrailIndices.size(); ++i)
				vSortedCursorTrailIndices[i] = (int)i;
			std::sort(vSortedCursorTrailIndices.begin(), vSortedCursorTrailIndices.end(), [&](int Left, int Right) {
				return str_comp_nocase(s_vCursorTrailFileLabels[Left].c_str(), s_vCursorTrailFileLabels[Right].c_str()) < 0;
			});
			static std::vector<std::string> s_vCursorTrailDropDownLabels;
			static std::vector<const char *> s_vCursorTrailDropDownLabelPtrs;
			s_vCursorTrailDropDownLabels.clear();
			s_vCursorTrailDropDownLabelPtrs.clear();
			for(int Index : vSortedCursorTrailIndices)
				s_vCursorTrailDropDownLabels.push_back(s_vCursorTrailFileLabels[Index]);
			for(const std::string &LabelString : s_vCursorTrailDropDownLabels)
				s_vCursorTrailDropDownLabelPtrs.push_back(LabelString.c_str());
			int SelectedCursorTrailFile = -1;
			for(size_t i = 0; i < vSortedCursorTrailIndices.size(); ++i)
				if(str_comp(g_Config.m_ScCursorTrailTrailImage, s_vCursorTrailFilePaths[vSortedCursorTrailIndices[i]].c_str()) == 0)
					SelectedCursorTrailFile = (int)i;
			CUIRect CursorTrailPathRow, CursorTrailFolderButton, CursorTrailReloadButton;
			MainView.HSplitTop(LineSize, &CursorTrailPathRow, &MainView);
			CursorTrailPathRow.VSplitRight(20.0f, &CursorTrailPathRow, &CursorTrailFolderButton);
			CursorTrailPathRow.VSplitRight(MarginSmall, &CursorTrailPathRow, nullptr);
			CursorTrailPathRow.VSplitRight(20.0f, &CursorTrailPathRow, &CursorTrailReloadButton);
			CursorTrailPathRow.VSplitRight(MarginSmall, &CursorTrailPathRow, nullptr);
			if(s_vCursorTrailDropDownLabelPtrs.empty())
			{
				static CButtonContainer s_CursorTrailEmptyButton;
				CUIRect CursorTrailPathLabel, CursorTrailEmptyRow;
				CursorTrailPathRow.VSplitLeft(110.0f, &CursorTrailPathLabel, &CursorTrailEmptyRow);
				Ui()->DoLabel(&CursorTrailPathLabel, ScLocalize("Trail image"), 12.0f, TEXTALIGN_ML);
				DoButton_Menu(&s_CursorTrailEmptyButton, ScLocalize("No images in CTrails folder"), -1, &CursorTrailEmptyRow);
			}
			else
			{
				static CUi::SDropDownState s_CursorTrailFileDropDownState;
				static CScrollRegion s_CursorTrailFileDropDownScrollRegion;
				s_CursorTrailFileDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_CursorTrailFileDropDownScrollRegion;
				CUIRect CursorTrailPathLabel, CursorTrailDropDown;
				CursorTrailPathRow.VSplitLeft(110.0f, &CursorTrailPathLabel, &CursorTrailDropDown);
				Ui()->DoLabel(&CursorTrailPathLabel, ScLocalize("Trail image"), 12.0f, TEXTALIGN_ML);
				const int NewSelectedCursorTrailFile = Ui()->DoDropDown(&CursorTrailDropDown, SelectedCursorTrailFile, s_vCursorTrailDropDownLabelPtrs.data(), s_vCursorTrailDropDownLabelPtrs.size(), s_CursorTrailFileDropDownState);
				if(NewSelectedCursorTrailFile != SelectedCursorTrailFile && NewSelectedCursorTrailFile >= 0 && NewSelectedCursorTrailFile < (int)vSortedCursorTrailIndices.size())
				{
					const int SortedIndex = vSortedCursorTrailIndices[NewSelectedCursorTrailFile];
					str_copy(g_Config.m_ScCursorTrailTrailImage, s_vCursorTrailFilePaths[SortedIndex].c_str(), sizeof(g_Config.m_ScCursorTrailTrailImage));
					GameClient()->m_CursorTrail.Reload();
				}
			}
			static CButtonContainer s_CursorTrailFolderButton, s_CursorTrailReloadButton;
			if(Ui()->DoButton_FontIcon(&s_CursorTrailReloadButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &CursorTrailReloadButton, BUTTONFLAG_LEFT))
				GameClient()->m_CursorTrail.Reload();
			if(Ui()->DoButton_FontIcon(&s_CursorTrailFolderButton, FontIcon::FOLDER, 0, &CursorTrailFolderButton, BUTTONFLAG_LEFT))
			{
				Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
				Storage()->CreateFolder("667Client/CTrails", IStorage::TYPE_SAVE);
				char aBuf[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, "667Client/CTrails", aBuf, sizeof(aBuf));
				Client()->ViewFile(aBuf);
			}
		}
		Ui()->ClipDisable();
	}
}

void CMenus::RenderSettingsSixClientKeystrokes(CUIRect &Column)
{
	CUIRect MainView; // scratch rect, as in the Visuals tab these blocks came from
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	CUIRect Content, Label, Button;
	const auto DoHighFpsIntakeTooltip = [&](const void *pId, const CUIRect &Row) {
		GameClient()->m_Tooltips.DoToolTip(pId, &Row, ScLocalize("high fps intake"));
		GameClient()->m_Tooltips.SetFadeTime(pId, 0.0f);
		GameClient()->m_Tooltips.SetTextColor(pId, ColorRGBA(1.0f, 0.55f, 0.16f, 1.0f));
	};

	const bool KeystrokesIsMinecraft = g_Config.m_ScKeystrokesStyle == 1;
	const bool KeystrokesKeyboardExpanded = g_Config.m_ScKeystrokesKeyboard != 0;
	const bool KeystrokesMouseExpanded = !KeystrokesIsMinecraft && g_Config.m_ScKeystrokesMouse != 0;
	const float KeystrokesMcOptionsRows = 2.0f + (g_Config.m_ScKeystrokesMcLayout == 1 ? 3.0f : 0.0f);
	const float KeystrokesClassicPresetHeight = SC_MODULE_REVEAL(!KeystrokesIsMinecraft && KeystrokesKeyboardExpanded, MarginSmall + LineSize, Client()->RenderFrameTime());
	const float KeystrokesMouseExpandedHeight = SC_MODULE_REVEAL(KeystrokesMouseExpanded, MarginSmall + LineSize, Client()->RenderFrameTime());
	const float KeystrokesMinecraftExpandedHeight = SC_MODULE_REVEAL(KeystrokesIsMinecraft && KeystrokesKeyboardExpanded, (MarginSmall + LineSize) * KeystrokesMcOptionsRows, Client()->RenderFrameTime());
	const float KeystrokesClassicBodyHeight = LineSize + KeystrokesClassicPresetHeight + MarginSmall + LineSize + KeystrokesMouseExpandedHeight;
	const float KeystrokesMinecraftBodyHeight = LineSize + KeystrokesMinecraftExpandedHeight;
	const float KeystrokesBodyHeight = KeystrokesIsMinecraft ? KeystrokesMinecraftBodyHeight : KeystrokesClassicBodyHeight;
	const float KeystrokesBlockHeight = LineSize + MarginSmall + LineSize + MarginSmall + KeystrokesBodyHeight;

	CUIRect KeystrokesBlock;
	Column.HSplitTop(KeystrokesBlockHeight, &KeystrokesBlock, &Column);

	CUIRect KeystrokesBlockBg = KeystrokesBlock;
	KeystrokesBlockBg.w += BlockPadding;
	KeystrokesBlockBg.h += BlockPadding;
	KeystrokesBlockBg.x -= BlockPadding * 0.5f;
	KeystrokesBlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&KeystrokesBlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScKeystrokesKeyboard, &KeystrokesBlockBg);

	MainView = KeystrokesBlock;

	MainView.HSplitTop(LineSize, &Label, &MainView);
	CUIRect KeystrokesTitleLabel, KeystrokesHudEditorButton, KeystrokesResetButton;
	Label.VSplitRight(LineSize + 8.0f, &Label, &KeystrokesResetButton);
	Label.VSplitRight(MarginSmall, &Label, nullptr);
	Label.VSplitRight(LineSize + 8.0f, &KeystrokesTitleLabel, &KeystrokesHudEditorButton);
	static CButtonContainer s_KeystrokesResetButton;
	if(Ui()->DoButton_FontIcon(&s_KeystrokesResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &KeystrokesResetButton, BUTTONFLAG_LEFT))
	{
		g_Config.m_ScKeystrokesStyle = DefaultConfig::ScKeystrokesStyle;
		g_Config.m_ScKeystrokesKeyboardPreset = DefaultConfig::ScKeystrokesKeyboardPreset;
		g_Config.m_ScKeystrokesMousePreset = DefaultConfig::ScKeystrokesMousePreset;
		g_Config.m_ScKeystrokesMcLayout = DefaultConfig::ScKeystrokesMcLayout;
		g_Config.m_ScKeystrokesMcShowLmb = DefaultConfig::ScKeystrokesMcShowLmb;
		g_Config.m_ScKeystrokesMcShowRmb = DefaultConfig::ScKeystrokesMcShowRmb;
		g_Config.m_ScKeystrokesMcShowSpace = DefaultConfig::ScKeystrokesMcShowSpace;
		g_Config.m_ScKeystrokesMcPressedOpacity = DefaultConfig::ScKeystrokesMcPressedOpacity;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_KeystrokesResetButton, &KeystrokesResetButton, ScLocalize("Reset to defaults"));
	static CButtonContainer s_KeystrokesHudEditorButton;
	const bool KeystrokesCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const bool KeystrokesHudEditorClicked = Ui()->DoButton_FontIcon(&s_KeystrokesHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, KeystrokesCanOpenHudEditor ? 0 : -1, &KeystrokesHudEditorButton, BUTTONFLAG_LEFT);
	GameClient()->m_Tooltips.DoToolTip(&s_KeystrokesHudEditorButton, &KeystrokesHudEditorButton, KeystrokesCanOpenHudEditor ? ScLocalize("Open in HUD editor") : ScLocalize("Join a game first"));
	GameClient()->m_Tooltips.SetFadeTime(&s_KeystrokesHudEditorButton, 0.0f);
	if(KeystrokesHudEditorClicked && KeystrokesCanOpenHudEditor)
	{
		SetActive(false);
		GameClient()->m_HudEditor.Activate();
	}
	Ui()->DoLabel(&KeystrokesTitleLabel, ScLocalize("Keystrokes"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Button, &MainView);
	static CButtonContainer s_KeystrokesStyleClassic;
	static CButtonContainer s_KeystrokesStyleMinecraft;
	CUIRect ClassicStyleButton, MinecraftStyleButton;
	Button.VSplitMid(&ClassicStyleButton, &MinecraftStyleButton, 2.0f);
	ClassicStyleButton.HMargin(2.0f, &ClassicStyleButton);
	MinecraftStyleButton.HMargin(2.0f, &MinecraftStyleButton);
	if(DoButton_Menu(&s_KeystrokesStyleClassic, ScLocalize("Classic"), g_Config.m_ScKeystrokesStyle == 0, &ClassicStyleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		g_Config.m_ScKeystrokesStyle = 0;
	if(DoButton_Menu(&s_KeystrokesStyleMinecraft, ScLocalize("Minecraft"), g_Config.m_ScKeystrokesStyle == 1, &MinecraftStyleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		g_Config.m_ScKeystrokesStyle = 1;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitTop(LineSize, &Content, &MainView);
	const CUIRect KeystrokesKeyboardEnableRow = Content;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScKeystrokesKeyboard, ScLocalize("Enable keyboard"), &g_Config.m_ScKeystrokesKeyboard, &Content, LineSize);
	DoHighFpsIntakeTooltip(&g_Config.m_ScKeystrokesKeyboard, KeystrokesKeyboardEnableRow);
	if(g_Config.m_ScKeystrokesKeyboard && !HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD))
		HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD, true);

	if(!KeystrokesIsMinecraft && KeystrokesClassicPresetHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = KeystrokesClassicPresetHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);

		static CButtonContainer s_KeyboardPresetFull;
		static CButtonContainer s_KeyboardPresetMinimal;
		static CButtonContainer s_KeyboardPresetMicro;
		CUIRect FullButton, Rest, MinimalButton, MicroButton;
		const float KeyboardPresetSpacing = 2.0f;
		const float KeyboardPresetButtonWidth = (Button.w - KeyboardPresetSpacing * 2.0f) / 3.0f;
		Button.VSplitLeft(KeyboardPresetButtonWidth, &FullButton, &Rest);
		Rest.VSplitLeft(KeyboardPresetSpacing, nullptr, &Rest);
		Rest.VSplitLeft(KeyboardPresetButtonWidth, &MinimalButton, &Rest);
		Rest.VSplitLeft(KeyboardPresetSpacing, nullptr, &Rest);
		MicroButton = Rest;
		FullButton.HMargin(2.0f, &FullButton);
		MinimalButton.HMargin(2.0f, &MinimalButton);
		MicroButton.HMargin(2.0f, &MicroButton);
		if(DoButton_Menu(&s_KeyboardPresetFull, ScLocalize("Full"), g_Config.m_ScKeystrokesKeyboardPreset == 1, &FullButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		{
			g_Config.m_ScKeystrokesKeyboardPreset = 1;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KeyboardPresetMinimal, ScLocalize("Minimal"), g_Config.m_ScKeystrokesKeyboardPreset == 0, &MinimalButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
		{
			g_Config.m_ScKeystrokesKeyboardPreset = 0;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KeyboardPresetMicro, ScLocalize("Micro"), g_Config.m_ScKeystrokesKeyboardPreset == 2, &MicroButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		{
			g_Config.m_ScKeystrokesKeyboardPreset = 2;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}

		Ui()->ClipDisable();
	}

	if(KeystrokesIsMinecraft && KeystrokesMinecraftExpandedHeight > 0.5f)
	{
		CUIRect Visible = MainView;
		Visible.h = KeystrokesMinecraftExpandedHeight;
		Ui()->ClipEnable(&Visible);

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		static CButtonContainer s_McLayoutFull;
		static CButtonContainer s_McLayoutOnlyAd;
		CUIRect McFullButton, McOnlyAdButton;
		Button.VSplitMid(&McFullButton, &McOnlyAdButton, 2.0f);
		McFullButton.HMargin(2.0f, &McFullButton);
		McOnlyAdButton.HMargin(2.0f, &McOnlyAdButton);
		if(DoButton_Menu(&s_McLayoutFull, ScLocalize("Full"), g_Config.m_ScKeystrokesMcLayout == 0, &McFullButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_ScKeystrokesMcLayout = 0;
		if(DoButton_Menu(&s_McLayoutOnlyAd, ScLocalize("Only A/D"), g_Config.m_ScKeystrokesMcLayout == 1, &McOnlyAdButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_ScKeystrokesMcLayout = 1;

		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_ScKeystrokesMcPressedOpacity, &g_Config.m_ScKeystrokesMcPressedOpacity, &Button, ScLocalize("Pressed opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

		if(g_Config.m_ScKeystrokesMcLayout == 1)
		{
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScKeystrokesMcShowLmb, ScLocalize("Enable LMB"), &g_Config.m_ScKeystrokesMcShowLmb, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScKeystrokesMcShowRmb, ScLocalize("Enable RMB"), &g_Config.m_ScKeystrokesMcShowRmb, &Content, LineSize);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Content, &MainView);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScKeystrokesMcShowSpace, ScLocalize("Enable Space"), &g_Config.m_ScKeystrokesMcShowSpace, &Content, LineSize);
		}

		Ui()->ClipDisable();
	}

	if(!KeystrokesIsMinecraft)
	{
		MainView.HSplitTop(MarginSmall, nullptr, &MainView);
		MainView.HSplitTop(LineSize, &Content, &MainView);
		const CUIRect KeystrokesMouseEnableRow = Content;
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScKeystrokesMouse, ScLocalize("Enable mouse"), &g_Config.m_ScKeystrokesMouse, &Content, LineSize);
		DoHighFpsIntakeTooltip(&g_Config.m_ScKeystrokesMouse, KeystrokesMouseEnableRow);
		if(g_Config.m_ScKeystrokesMouse && !HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE))
			HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE, true);

		if(KeystrokesMouseExpandedHeight > 0.5f)
		{
			CUIRect Visible = MainView;
			Visible.h = KeystrokesMouseExpandedHeight;
			Ui()->ClipEnable(&Visible);

			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			MainView.HSplitTop(LineSize, &Button, &MainView);

			static CButtonContainer s_MousePresetArrow;
			static CButtonContainer s_MousePresetDotDot;
			static CButtonContainer s_MousePresetNothing;
			CUIRect ArrowButton, Rest, DotDotButton, NothingButton;
			const float MousePresetSpacing = 2.0f;
			const float MousePresetButtonWidth = (Button.w - MousePresetSpacing * 2.0f) / 3.0f;
			Button.VSplitLeft(MousePresetButtonWidth, &ArrowButton, &Rest);
			Rest.VSplitLeft(MousePresetSpacing, nullptr, &Rest);
			Rest.VSplitLeft(MousePresetButtonWidth, &DotDotButton, &Rest);
			Rest.VSplitLeft(MousePresetSpacing, nullptr, &Rest);
			NothingButton = Rest;
			ArrowButton.HMargin(2.0f, &ArrowButton);
			DotDotButton.HMargin(2.0f, &DotDotButton);
			NothingButton.HMargin(2.0f, &NothingButton);
			if(DoButton_Menu(&s_MousePresetArrow, ScLocalize("Arrow"), g_Config.m_ScKeystrokesMousePreset == 1, &ArrowButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_ScKeystrokesMousePreset = 1;
			if(DoButton_Menu(&s_MousePresetDotDot, ScLocalize("Dot Dot"), g_Config.m_ScKeystrokesMousePreset == 2, &DotDotButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_ScKeystrokesMousePreset = 2;
			if(DoButton_Menu(&s_MousePresetNothing, ScLocalize("Nothing"), g_Config.m_ScKeystrokesMousePreset == 3, &NothingButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_ScKeystrokesMousePreset = 3;

			Ui()->ClipDisable();
		}
	}

}
