/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-a1882842 */

#include <base/str.h>
#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/components/sixclient/discord_rpc.h>
#include <game/client/components/sixclient/media_decoder.h>
#include <game/client/components/sixclient/settings_search.h>
#include <game/client/components/sixclient/ui_theme/style.h>
#include <game/client/gameclient.h>
#include <game/client/sc_ui_animations.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <memory>

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
	const float ExpandedTarget = 14.0f * Row + 2.0f * (HintFontSize + 2.0f) + MarginSmall + PreviewHeight + MarginSmall;
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
		str_copy(g_Config.m_ScDiscordClientId, DefaultConfig::ScDiscordClientId);
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

	static CLineInput s_ClientIdInput, s_DetailsInput, s_StateInput, s_MenuDetailsInput, s_MenuStateInput;
	static CLineInput s_LargeImageInput, s_LargeTextInput, s_SmallImageInput, s_SmallTextInput;

	RowInput(&s_ClientIdInput, g_Config.m_ScDiscordClientId, sizeof(g_Config.m_ScDiscordClientId), ScLocalize("Client ID"), "");
	RowSection(ScLocalize("While playing"));
	RowInput(&s_DetailsInput, g_Config.m_ScDiscordDetails, sizeof(g_Config.m_ScDiscordDetails), ScLocalize("First line"), "{map}");
	RowInput(&s_StateInput, g_Config.m_ScDiscordState, sizeof(g_Config.m_ScDiscordState), ScLocalize("Second line"), "{server}");
	RowSection(ScLocalize("In menus"));
	RowInput(&s_MenuDetailsInput, g_Config.m_ScDiscordMenuDetails, sizeof(g_Config.m_ScDiscordMenuDetails), ScLocalize("First line"), "In menus");
	RowInput(&s_MenuStateInput, g_Config.m_ScDiscordMenuState, sizeof(g_Config.m_ScDiscordMenuState), ScLocalize("Second line"), "");
	RowInput(&s_LargeImageInput, g_Config.m_ScDiscordLargeImage, sizeof(g_Config.m_ScDiscordLargeImage), ScLocalize("Large image"), "https://...");
	RowInput(&s_LargeTextInput, g_Config.m_ScDiscordLargeText, sizeof(g_Config.m_ScDiscordLargeText), ScLocalize("Large image text"), "Cecen Client {version}");
	RowInput(&s_SmallImageInput, g_Config.m_ScDiscordSmallImage, sizeof(g_Config.m_ScDiscordSmallImage), ScLocalize("Small image"), "https://...");
	RowInput(&s_SmallTextInput, g_Config.m_ScDiscordSmallText, sizeof(g_Config.m_ScDiscordSmallText), ScLocalize("Small image text"), "{player}");
	RowCheck(&g_Config.m_ScDiscordShowTime, ScLocalize("Show elapsed time"));
	RowCheck(&g_Config.m_ScDiscordShowParty, ScLocalize("Show player count"));
	RowCheck(&g_Config.m_ScDiscordAllowJoin, ScLocalize("Let friends join your server"));

	CUIRect Hint;
	Block.HSplitTop(MarginSmall, nullptr, &Block);
	Block.HSplitTop(HintFontSize + 2.0f, &Hint, &Block);
	const auto IsDiscordAttachment = [](const char *pUrl) {
		return str_find_nocase(pUrl, "discordapp.com/attachments/") || str_find_nocase(pUrl, "discordapp.net/attachments/");
	};
	const auto IsPageLink = [](const char *pUrl) {
		if(!str_startswith_nocase(pUrl, "http://") && !str_startswith_nocase(pUrl, "https://"))
			return false;
		char aPath[512];
		str_copy(aPath, pUrl, sizeof(aPath));
		if(char *pQuery = (char *)str_find(aPath, "?"))
			*pQuery = '\0';
		const char *apExtensions[] = {".png", ".jpg", ".jpeg", ".gif", ".webp"};
		for(const char *pExtension : apExtensions)
			if(str_endswith_nocase(aPath, pExtension))
				return false;
		return true;
	};
	if(IsDiscordAttachment(g_Config.m_ScDiscordLargeImage) || IsDiscordAttachment(g_Config.m_ScDiscordSmallImage))
	{
		TextRender()->TextColor(1.0f, 0.45f, 0.45f, 1.0f);
		Ui()->DoLabel(&Hint, ScLocalize("Discord attachment links expire and show as ?. Use a direct i.imgur.com / GitHub / jsDelivr image URL."), HintFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
	}
	else if(IsPageLink(g_Config.m_ScDiscordLargeImage) || IsPageLink(g_Config.m_ScDiscordSmallImage))
	{
		TextRender()->TextColor(1.0f, 0.45f, 0.45f, 1.0f);
		Ui()->DoLabel(&Hint, ScLocalize("Use a direct image link ending in .png or .jpg, like https://i.imgur.com/abc.png"), HintFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
	}
	else if(g_Config.m_ScDiscordLargeImage[0] != '\0' &&
		!str_startswith_nocase(g_Config.m_ScDiscordLargeImage, "http://") &&
		!str_startswith_nocase(g_Config.m_ScDiscordLargeImage, "https://"))
	{
		TextRender()->TextColor(1.0f, 0.75f, 0.35f, 1.0f);
		Ui()->DoLabel(&Hint, ScLocalize("Asset keys only work if uploaded in Discord Developer Portal. For a custom logo, paste a direct https:// image URL."), HintFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
	}
	else
	{
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		Ui()->DoLabel(&Hint, ScLocalize("Images accept a https URL or an asset key of the Discord app."), HintFontSize, TEXTALIGN_ML);
	}
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

	struct SDiscordPreviewImage
	{
		char m_aSource[512] = "";
		IGraphics::CTextureHandle m_DefaultTexture;
		IGraphics::CTextureHandle m_CustomTexture;
		std::shared_ptr<IHttpRequest> m_pRequest;
		bool m_DefaultLoaded = false;
		bool m_CustomFailed = false;
	};
	static SDiscordPreviewImage s_PreviewImage;

	if(!s_PreviewImage.m_DefaultLoaded)
	{
		s_PreviewImage.m_DefaultTexture = Graphics()->LoadTexture("667Client/discord_rpc.png", IStorage::TYPE_ALL);
		s_PreviewImage.m_DefaultLoaded = true;
	}

	char aLargeImage[512];
	SixClientResolveDiscordImage(aLargeImage, sizeof(aLargeImage), g_Config.m_ScDiscordLargeImage);
	const bool IsHttpImage = str_startswith_nocase(aLargeImage, "http://") || str_startswith_nocase(aLargeImage, "https://");
	const bool UseDefaultImage = str_comp(aLargeImage, SIXCLIENT_DISCORD_DEFAULT_LARGE_IMAGE) == 0 ||
		str_comp(g_Config.m_ScDiscordLargeImage, SIXCLIENT_DISCORD_LEGACY_LARGE_IMAGE_KEY) == 0 ||
		g_Config.m_ScDiscordLargeImage[0] == '\0' ||
		!IsHttpImage;

	if(str_comp(s_PreviewImage.m_aSource, aLargeImage) != 0)
	{
		str_copy(s_PreviewImage.m_aSource, aLargeImage);
		if(s_PreviewImage.m_CustomTexture.IsValid())
			Graphics()->UnloadTexture(&s_PreviewImage.m_CustomTexture);
		s_PreviewImage.m_pRequest.reset();
		s_PreviewImage.m_CustomFailed = false;

		if(IsHttpImage)
		{
			s_PreviewImage.m_pRequest = HttpGet(aLargeImage);
			s_PreviewImage.m_pRequest->Timeout(CTimeout{2000, 8000, 500, 5});
			s_PreviewImage.m_pRequest->MaxResponseSize(8 * 1024 * 1024);
			s_PreviewImage.m_pRequest->LogProgress(HTTPLOG::NONE);
			Http()->Run(s_PreviewImage.m_pRequest);
		}
	}

	if(s_PreviewImage.m_pRequest)
	{
		const EHttpState State = s_PreviewImage.m_pRequest->State();
		if(State == EHttpState::DONE)
		{
			std::shared_ptr<IHttpRequest> pFinished = std::move(s_PreviewImage.m_pRequest);
			s_PreviewImage.m_pRequest.reset();
			if(pFinished->StatusCode() >= 200 && pFinished->StatusCode() < 400)
			{
				unsigned char *pData = nullptr;
				size_t DataSize = 0;
				pFinished->Result(&pData, &DataSize);
				CImageInfo Decoded;
				if(pData && DataSize > 0 && MediaDecoder::DecodeImageToRgba(Graphics(), pData, DataSize, "discord_preview", Decoded))
				{
					if(s_PreviewImage.m_CustomTexture.IsValid())
						Graphics()->UnloadTexture(&s_PreviewImage.m_CustomTexture);
					s_PreviewImage.m_CustomTexture = Graphics()->LoadTextureRawMove(Decoded, 0, "discord_preview");
					s_PreviewImage.m_CustomFailed = !s_PreviewImage.m_CustomTexture.IsValid() || s_PreviewImage.m_CustomTexture.IsNullTexture();
				}
				else
				{
					s_PreviewImage.m_CustomFailed = true;
				}
			}
			else
			{
				s_PreviewImage.m_CustomFailed = true;
			}
		}
		else if(State == EHttpState::ERROR || State == EHttpState::ABORTED)
		{
			s_PreviewImage.m_pRequest.reset();
			s_PreviewImage.m_CustomFailed = true;
		}
	}

	const bool HasCustomTexture = !UseDefaultImage && s_PreviewImage.m_CustomTexture.IsValid() && !s_PreviewImage.m_CustomTexture.IsNullTexture();
	const IGraphics::CTextureHandle &PreviewTexture = HasCustomTexture ? s_PreviewImage.m_CustomTexture : s_PreviewImage.m_DefaultTexture;
	if(PreviewTexture.IsValid() && !PreviewTexture.IsNullTexture())
	{
		Graphics()->TextureSet(PreviewTexture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem QuadItem(Image.x, Image.y, Image.w, Image.h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}
	else
	{
		Image.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ScHookFxColor)).WithAlpha(0.85f), IGraphics::CORNER_ALL, 6.0f);
		Ui()->DoLabel(&Image, "CC", 16.0f, TEXTALIGN_MC);
	}
	Text.VSplitLeft(8.0f, nullptr, &Text);

	CUIRect TextLine;
	Text.HSplitTop(14.0f, &TextLine, &Text);
	Ui()->DoLabel(&TextLine, "Cecen Client", 13.0f, TEXTALIGN_ML);
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
