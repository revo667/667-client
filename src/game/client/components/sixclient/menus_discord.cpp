#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/components/sixclient/discord_rpc.h>
#include <game/client/components/sixclient/settings_search.h>
#include <game/client/components/sixclient/ui_theme/style.h>
#include <game/client/gameclient.h>
#include <game/client/sc_ui_animations.h>
#include <game/client/ui.h>
#include <game/localization.h>

void CMenus::RenderSettingsSixClientDiscord(CUIRect &Column)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float BlockPadding = 20.0f;
	const float LabelWidth = 120.0f;
	const float HintFontSize = 11.0f;
	const float PreviewHeight = 92.0f;
	const float Row = LineSize + MarginSmall;

	const bool Enabled = g_Config.m_ScDiscordRPC != 0;
	const float ExpandedTarget = 13.0f * Row + 2.0f * (HintFontSize + 2.0f) + MarginSmall + PreviewHeight + MarginSmall;
	const float ExpandedHeight = SC_MODULE_REVEAL(Enabled, ExpandedTarget, Client()->RenderFrameTime());

	CUIRect Block;
	Column.HSplitTop(LineSize + Row + ExpandedHeight, &Block, &Column);
	CUIRect BlockBg = Block;
	BlockBg.w += BlockPadding;
	BlockBg.h += BlockPadding;
	BlockBg.x -= BlockPadding * 0.5f;
	BlockBg.y -= BlockPadding * 0.5f;
	SixClientUiTheme::DrawUiBlock(&BlockBg, IGraphics::CORNER_ALL, 10.0f);
	SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScDiscordRPC, &BlockBg);

	CUIRect Label, Title, ResetButton;
	Block.HSplitTop(LineSize, &Label, &Block);
	Label.VSplitRight(LineSize + 8.0f, &Title, &ResetButton);
	static CButtonContainer s_ResetButton;
	if(Ui()->DoButton_FontIcon(&s_ResetButton, FontIcon::ARROW_ROTATE_LEFT, 0, &ResetButton, BUTTONFLAG_LEFT))
	{
		str_copy(g_Config.m_ScDiscordDetails, DefaultConfig::ScDiscordDetails);
		str_copy(g_Config.m_ScDiscordState, DefaultConfig::ScDiscordState);
		str_copy(g_Config.m_ScDiscordMenuDetails, DefaultConfig::ScDiscordMenuDetails);
		str_copy(g_Config.m_ScDiscordMenuState, DefaultConfig::ScDiscordMenuState);
		str_copy(g_Config.m_ScDiscordLargeImage, DefaultConfig::ScDiscordLargeImage);
		str_copy(g_Config.m_ScDiscordLargeText, DefaultConfig::ScDiscordLargeText);
		str_copy(g_Config.m_ScDiscordSmallImage, DefaultConfig::ScDiscordSmallImage);
		str_copy(g_Config.m_ScDiscordSmallText, DefaultConfig::ScDiscordSmallText);
		g_Config.m_ScDiscordShowTime = DefaultConfig::ScDiscordShowTime;
		g_Config.m_ScDiscordShowParty = DefaultConfig::ScDiscordShowParty;
		g_Config.m_ScDiscordAllowJoin = DefaultConfig::ScDiscordAllowJoin;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_ResetButton, &ResetButton, ScLocalize("Reset to defaults"));
	Ui()->DoLabel(&Title, ScLocalize("Discord Rich Presence"), HeadlineFontSize, TEXTALIGN_ML);

	CUIRect Line;
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(LineSize, &Line, &Block);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScDiscordRPC, ScLocalize("Enable Discord Rich Presence"), &g_Config.m_ScDiscordRPC, &Line, LineSize);

	if(ExpandedHeight <= 0.5f)
		return;

	CUIRect Visible = Block;
	Visible.h = ExpandedHeight;
	Ui()->ClipEnable(&Visible);

	const auto RowSection = [&](const char *pText) {
		CUIRect SectionLine;
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &SectionLine, &Block);
		Ui()->DoLabel(&SectionLine, pText, 14.0f, TEXTALIGN_ML);
	};
	const auto RowInput = [&](CLineInput *pInput, char *pBuf, int BufSize, const char *pLabel, const char *pEmptyText) {
		CUIRect InputLine, InputLabel, InputField;
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &InputLine, &Block);
		InputLine.VSplitLeft(LabelWidth, &InputLabel, &InputField);
		Ui()->DoLabel(&InputLabel, pLabel, 13.0f, TEXTALIGN_ML);
		pInput->SetBuffer(pBuf, BufSize);
		pInput->SetEmptyText(pEmptyText);
		Ui()->DoClearableEditBox(pInput, &InputField, 12.0f);
		SixClientSettingsSearch::DrawItemHighlight(pBuf, &InputField);
	};
	const auto RowCheck = [&](int *pValue, const char *pText) {
		CUIRect CheckLine;
		Block.HSplitTop(MarginSmall, nullptr, &Block);
		Block.HSplitTop(LineSize, &CheckLine, &Block);
		DoButton_CheckBoxAutoVMarginAndSet(pValue, pText, pValue, &CheckLine, LineSize);
	};

	static CLineInput s_DetailsInput, s_StateInput, s_MenuDetailsInput, s_MenuStateInput;
	static CLineInput s_LargeImageInput, s_LargeTextInput, s_SmallImageInput, s_SmallTextInput;

	RowSection(ScLocalize("While playing"));
	RowInput(&s_DetailsInput, g_Config.m_ScDiscordDetails, sizeof(g_Config.m_ScDiscordDetails), ScLocalize("First line"), "{map}");
	RowInput(&s_StateInput, g_Config.m_ScDiscordState, sizeof(g_Config.m_ScDiscordState), ScLocalize("Second line"), "{server}");
	RowSection(ScLocalize("In menus"));
	RowInput(&s_MenuDetailsInput, g_Config.m_ScDiscordMenuDetails, sizeof(g_Config.m_ScDiscordMenuDetails), ScLocalize("First line"), "In menus");
	RowInput(&s_MenuStateInput, g_Config.m_ScDiscordMenuState, sizeof(g_Config.m_ScDiscordMenuState), ScLocalize("Second line"), "");
	RowInput(&s_LargeImageInput, g_Config.m_ScDiscordLargeImage, sizeof(g_Config.m_ScDiscordLargeImage), ScLocalize("Large image"), "https://...");
	RowInput(&s_LargeTextInput, g_Config.m_ScDiscordLargeText, sizeof(g_Config.m_ScDiscordLargeText), ScLocalize("Large image text"), "667 Client {version}");
	RowInput(&s_SmallImageInput, g_Config.m_ScDiscordSmallImage, sizeof(g_Config.m_ScDiscordSmallImage), ScLocalize("Small image"), "https://...");
	RowInput(&s_SmallTextInput, g_Config.m_ScDiscordSmallText, sizeof(g_Config.m_ScDiscordSmallText), ScLocalize("Small image text"), "{player}");
	RowCheck(&g_Config.m_ScDiscordShowTime, ScLocalize("Show elapsed time"));
	RowCheck(&g_Config.m_ScDiscordShowParty, ScLocalize("Show player count"));
	RowCheck(&g_Config.m_ScDiscordAllowJoin, ScLocalize("Let friends join your server"));

	CUIRect Hint;
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(HintFontSize + 2.0f, &Hint, &Block);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
	Ui()->DoLabel(&Hint, ScLocalize("Images accept a https URL or an asset key of the Discord app."), HintFontSize, TEXTALIGN_ML);
	Block.HSplitTop(HintFontSize + 2.0f, &Hint, &Block);
	char aVariables[160];
	str_format(aVariables, sizeof(aVariables), "%s %s", ScLocalize("Variables:"), DISCORD_PRESENCE_VARIABLES);
	Ui()->DoLabel(&Hint, aVariables, HintFontSize, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	const CServerInfo &ServerInfo = Client()->ServerInfo();
	SDiscordPresenceVars Vars;
	Vars.m_pPlayer = Client()->PlayerName();
	Vars.m_pClan = g_Config.m_PlayerClan;
	Vars.m_pSkin = g_Config.m_ClPlayerSkin;
	Vars.m_pMap = Online ? ServerInfo.m_aMap : "Sunny Side Up";
	Vars.m_pServer = Online ? ServerInfo.m_aName : "DDNet GER - Novice";
	Vars.m_pGameType = Online ? ServerInfo.m_aGameType : "DDraceNetwork";
	Vars.m_Players = Online ? GameClient()->m_Snap.m_NumPlayers : 12;
	Vars.m_MaxPlayers = Online ? ServerInfo.m_MaxClients : 64;

	static bool s_PreviewInGame = true;
	char aDetails[128];
	char aState[128];
	SixClientFormatDiscordText(aDetails, sizeof(aDetails), s_PreviewInGame ? g_Config.m_ScDiscordDetails : g_Config.m_ScDiscordMenuDetails, Vars);
	SixClientFormatDiscordText(aState, sizeof(aState), s_PreviewInGame ? g_Config.m_ScDiscordState : g_Config.m_ScDiscordMenuState, Vars);
	if(s_PreviewInGame && g_Config.m_ScDiscordShowParty && aState[0] != '\0')
	{
		char aParty[32];
		str_format(aParty, sizeof(aParty), " (%d of %d)", Vars.m_Players, Vars.m_MaxPlayers);
		str_append(aState, aParty);
	}

	CUIRect Preview;
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(PreviewHeight, &Preview, &Block);
	Preview.Draw(ColorRGBA(0.12f, 0.12f, 0.14f, 0.9f), IGraphics::CORNER_ALL, 8.0f);
	Preview.Margin(8.0f, &Preview);

	CUIRect Header, Body, Image, Text, ModeButton;
	Preview.HSplitTop(14.0f, &Header, &Body);
	Header.VSplitRight(90.0f, &Header, &ModeButton);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
	Ui()->DoLabel(&Header, ScLocalize("Preview"), 10.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	static CButtonContainer s_PreviewModeButton;
	if(DoButton_Menu(&s_PreviewModeButton, s_PreviewInGame ? ScLocalize("Playing") : ScLocalize("Menus"), 0, &ModeButton))
		s_PreviewInGame = !s_PreviewInGame;

	Body.HSplitTop(4.0f, nullptr, &Body);
	Body.VSplitLeft(Body.h, &Image, &Text);
	Image.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ScHookFxColor)).WithAlpha(0.85f), IGraphics::CORNER_ALL, 6.0f);
	Ui()->DoLabel(&Image, "667", 16.0f, TEXTALIGN_MC);
	Text.VSplitLeft(8.0f, nullptr, &Text);

	CUIRect TextLine;
	Text.HSplitTop(14.0f, &TextLine, &Text);
	Ui()->DoLabel(&TextLine, "667 Client", 13.0f, TEXTALIGN_ML);
	if(aDetails[0] != '\0')
	{
		Text.HSplitTop(13.0f, &TextLine, &Text);
		Ui()->DoLabel(&TextLine, aDetails, 11.0f, TEXTALIGN_ML);
	}
	if(aState[0] != '\0')
	{
		Text.HSplitTop(13.0f, &TextLine, &Text);
		Ui()->DoLabel(&TextLine, aState, 11.0f, TEXTALIGN_ML);
	}
	if(g_Config.m_ScDiscordShowTime)
	{
		Text.HSplitTop(13.0f, &TextLine, &Text);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		Ui()->DoLabel(&TextLine, ScLocalize("00:42 elapsed"), 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	Ui()->ClipDisable();
}
