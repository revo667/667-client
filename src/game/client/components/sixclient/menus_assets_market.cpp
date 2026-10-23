#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/components/sixclient/assets_market.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

static const char *AssetsMarketCategoryName(ETeedataAsset Asset)
{
	switch(Asset)
	{
	case ETeedataAsset::SKIN: return ScLocalize("Tee");
	case ETeedataAsset::GAMESKIN: return ScLocalize("Game skin");
	case ETeedataAsset::ENTITY: return ScLocalize("Entities");
	case ETeedataAsset::EMOTICON: return ScLocalize("Emoticons");
	case ETeedataAsset::PARTICLE: return ScLocalize("Particles");
	case ETeedataAsset::CURSOR: return ScLocalize("Cursors");
	case ETeedataAsset::FONT: return ScLocalize("Fonts");
	default: return "";
	}
}

static const char *AssetsMarketSortName(ETeedataSort Sort)
{
	switch(Sort)
	{
	case ETeedataSort::DOWNLOAD: return ScLocalize("Downloads");
	case ETeedataSort::TRENDING: return ScLocalize("Trending");
	case ETeedataSort::DATE: return ScLocalize("Newest");
	case ETeedataSort::NAME: return ScLocalize("Name");
	case ETeedataSort::HD: return ScLocalize("HD");
	default: return "";
	}
}

static char *AssetsMarketConfigFor(ETeedataAsset Asset)
{
	switch(Asset)
	{
	case ETeedataAsset::SKIN: return g_Config.m_ClPlayerSkin;
	case ETeedataAsset::GAMESKIN: return g_Config.m_ClAssetGame;
	case ETeedataAsset::ENTITY: return g_Config.m_ClAssetsEntities;
	case ETeedataAsset::EMOTICON: return g_Config.m_ClAssetEmoticons;
	case ETeedataAsset::PARTICLE: return g_Config.m_ClAssetParticles;
	case ETeedataAsset::CURSOR: return g_Config.m_ClAssetCursor;
	default: return nullptr;
	}
}

void CMenus::RenderSettingsSixClientAssetsMarket(CUIRect MainView)
{
	const float LineSize = 22.0f;
	const float MarginSmall = 5.0f;
	const float FontSize = 13.0f;

	CAssetsMarket &Market = GameClient()->m_AssetsMarket;

	static int s_Asset = (int)ETeedataAsset::SKIN;
	static int s_Sort = (int)ETeedataSort::DOWNLOAD;
	static int s_Page = 1;
	static CLineInputBuffered<CTeedataClient::MAX_SEARCH_LENGTH + 1> s_SearchInput;
	bool SearchChanged = false;

	CUIRect Categories, Toolbar, Footer, Button, Label;
	MainView.HSplitTop(LineSize + 2.0f, &Categories, &MainView);
	MainView.HSplitTop(MarginSmall * 2.0f, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Toolbar, &MainView);
	MainView.HSplitTop(MarginSmall * 2.0f, nullptr, &MainView);
	MainView.HSplitBottom(LineSize, &MainView, &Footer);
	MainView.HSplitBottom(MarginSmall * 2.0f, &MainView, nullptr);

	{
		static CButtonContainer s_aCategoryButtons[(int)ETeedataAsset::NUM];
		const float TabWidth = Categories.w / (float)ETeedataAsset::NUM;
		for(int i = 0; i < (int)ETeedataAsset::NUM; ++i)
		{
			Categories.VSplitLeft(TabWidth, &Button, &Categories);
			const int Corners = i == 0 ? IGraphics::CORNER_L : (i == (int)ETeedataAsset::NUM - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&s_aCategoryButtons[i], AssetsMarketCategoryName((ETeedataAsset)i), s_Asset == i, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			{
				s_Asset = i;
				s_Page = 1;
			}
		}
	}

	{
		CUIRect Search, Sorts, Refresh, Enable;
		Toolbar.VSplitLeft(220.0f, &Search, &Toolbar);
		Toolbar.VSplitLeft(MarginSmall * 2.0f, nullptr, &Toolbar);
		Toolbar.VSplitRight(200.0f, &Toolbar, &Enable);
		Toolbar.VSplitRight(MarginSmall * 2.0f, &Toolbar, nullptr);
		Toolbar.VSplitRight(LineSize, &Sorts, &Refresh);
		Sorts.VSplitRight(MarginSmall, &Sorts, nullptr);

		if(Ui()->DoEditBox_Search(&s_SearchInput, &Search, 12.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
		{
			s_Page = 1;
			SearchChanged = true;
		}

		static CButtonContainer s_aSortButtons[(int)ETeedataSort::NUM];
		const float SortWidth = std::min(Sorts.w / (float)ETeedataSort::NUM, 90.0f);
		for(int i = 0; i < (int)ETeedataSort::NUM; ++i)
		{
			Sorts.VSplitLeft(SortWidth, &Button, &Sorts);
			const int Corners = i == 0 ? IGraphics::CORNER_L : (i == (int)ETeedataSort::NUM - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&s_aSortButtons[i], AssetsMarketSortName((ETeedataSort)i), s_Sort == i, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			{
				s_Sort = i;
				s_Page = 1;
			}
		}

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		static CButtonContainer s_RefreshButton;
		if(DoButton_Menu(&s_RefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &Refresh))
			Market.Reload();
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScAssetsMarket, ScLocalize("Load from teedata.net"), &g_Config.m_ScAssetsMarket, &Enable, LineSize);
	}

	const ETeedataAsset Asset = (ETeedataAsset)s_Asset;
	Market.SetQuery(Asset, s_SearchInput.GetString(), (ETeedataSort)s_Sort, s_Page, SearchChanged);

	{
		CUIRect Prev, Next, Attribution, Website;
		Footer.VSplitLeft(LineSize * 2.0f, &Prev, &Footer);
		Footer.VSplitLeft(150.0f, &Label, &Footer);
		Footer.VSplitLeft(LineSize * 2.0f, &Next, &Footer);
		Footer.VSplitRight(120.0f, &Attribution, &Website);
		Attribution.VSplitRight(MarginSmall * 2.0f, &Attribution, nullptr);

		const bool Ready = Market.PageState() == CAssetsMarket::EPageState::READY;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		static CButtonContainer s_PrevButton;
		if(DoButton_Menu(&s_PrevButton, FontIcon::CHEVRON_LEFT, 0, &Prev) && Ready && s_Page > 1)
			--s_Page;
		static CButtonContainer s_NextButton;
		if(DoButton_Menu(&s_NextButton, FontIcon::CHEVRON_RIGHT, 0, &Next) && Ready && s_Page < Market.NumPages())
			++s_Page;
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		char aPage[64];
		if(Market.PageState() == CAssetsMarket::EPageState::LOADING)
			str_copy(aPage, ScLocalize("Loading…"));
		else
			str_format(aPage, sizeof(aPage), ScLocalize("Page %d / %d"), Market.Page(), Market.NumPages());
		Ui()->DoLabel(&Label, aPage, FontSize, TEXTALIGN_MC);

		SLabelProperties Props;
		Props.m_MaxWidth = Attribution.w;
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		Ui()->DoLabel(&Attribution, ScLocalize("Assets by their authors on teedata.net, licensed CC BY 4.0"), 11.0f, TEXTALIGN_MR, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		static CButtonContainer s_WebsiteButton;
		if(DoButton_Menu(&s_WebsiteButton, "teedata.net", 0, &Website))
			Client()->ViewLink("https://teedata.net");
	}

	std::vector<CAssetsMarket::CEntry> &vEntries = Market.Entries();
	auto &&CenterMessage = [&](const char *pMessage, CButtonContainer *pButton, const char *pButtonText) {
		CUIRect Message = MainView;
		Message.HMargin((Message.h - (pButton ? 2.0f * LineSize + MarginSmall * 2.0f : LineSize)) / 2.0f, &Message);
		Message.HSplitTop(LineSize, &Label, &Message);
		Ui()->DoLabel(&Label, pMessage, 16.0f, TEXTALIGN_MC);
		if(pButton == nullptr)
			return;
		Message.HSplitTop(MarginSmall * 2.0f, nullptr, &Message);
		Message.VMargin((Message.w - 160.0f) / 2.0f, &Button);
		if(DoButton_Menu(pButton, pButtonText, 0, &Button))
			Market.Reload();
	};

	if(!g_Config.m_ScAssetsMarket)
	{
		CenterMessage(ScLocalize("The Assets Market is turned off. Enable \"Load from teedata.net\" to browse assets."), nullptr, nullptr);
		return;
	}
	if(Market.PageState() == CAssetsMarket::EPageState::ERROR)
	{
		static CButtonContainer s_RetryButton;
		CenterMessage(Market.Error(), &s_RetryButton, ScLocalize("Retry"));
		return;
	}
	if(vEntries.empty())
	{
		CenterMessage(Market.PageState() == CAssetsMarket::EPageState::READY ? ScLocalize("No assets found") : ScLocalize("Loading assets…"), nullptr, nullptr);
		return;
	}

	static std::vector<char> s_vItemIds;
	static std::vector<CButtonContainer> s_vActionButtons;
	if(s_vItemIds.size() < vEntries.size())
	{
		s_vItemIds.resize(vEntries.size());
		s_vActionButtons.resize(vEntries.size());
	}

	const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");
	const char *pInUse = AssetsMarketConfigFor(Asset);

	static CListBox s_ListBox;
	s_ListBox.DoStart(150.0f, vEntries.size(), std::max(1, (int)(MainView.w / 180.0f)), 1, -1, &MainView, true, IGraphics::CORNER_ALL, true);
	for(size_t i = 0; i < vEntries.size(); ++i)
	{
		CAssetsMarket::CEntry &Entry = vEntries[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&s_vItemIds[i], false);
		if(!Item.m_Visible)
			continue;
		Market.RequestPreview(i);

		CUIRect Card, Preview, Name, Author, Action;
		Item.m_Rect.Margin(MarginSmall, &Card);
		Card.HSplitBottom(LineSize, &Card, &Action);
		Card.HSplitBottom(MarginSmall, &Card, nullptr);
		Card.HSplitBottom(14.0f, &Card, &Author);
		Card.HSplitBottom(16.0f, &Preview, &Name);
		Preview.Margin(2.0f, &Preview);

		if(Asset == ETeedataAsset::SKIN)
		{
			CTeeRenderInfo Info;
			Info.Apply(Entry.m_pPreviewSkin ? Entry.m_pPreviewSkin.get() : pDefaultSkin);
			Info.m_Size = std::min(Preview.h, 64.0f);
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &Info, OffsetToMid);
			const vec2 TeeRenderPos = vec2(Preview.x + Preview.w / 2.0f, Preview.y + Preview.h / 2.0f + OffsetToMid.y);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, Entry.m_pPreviewSkin ? 1.0f : 0.25f);
		}
		else if(Entry.m_PreviewTexture.IsValid())
		{
			const float Aspect = Entry.m_Item.m_Width > 0 && Entry.m_Item.m_Height > 0 ? Entry.m_Item.m_Width / (float)Entry.m_Item.m_Height : 1.0f;
			float Width = std::min(Preview.w, Preview.h * Aspect);
			float Height = Width / Aspect;
			if(Asset == ETeedataAsset::CURSOR)
			{
				Width = std::min(Width, 48.0f);
				Height = Width / Aspect;
			}
			Graphics()->TextureSet(Entry.m_PreviewTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			const IGraphics::CQuadItem QuadItem(Preview.x + (Preview.w - Width) / 2.0f, Preview.y + (Preview.h - Height) / 2.0f, Width, Height);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}
		else
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
			RenderFontIcon(Preview, Entry.PreviewFailed() ? FontIcon::TRIANGLE_EXCLAMATION : FontIcon::IMAGE, 24.0f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		SLabelProperties Props;
		Props.m_MaxWidth = Name.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Name, Entry.m_Item.m_aName, 12.0f, TEXTALIGN_MC, Props);

		char aAuthor[96];
		if(Entry.m_Item.m_Downloads >= 1000)
			str_format(aAuthor, sizeof(aAuthor), "%s · %.1fk", Entry.m_Item.m_aAuthor[0] ? Entry.m_Item.m_aAuthor : "?", Entry.m_Item.m_Downloads / 1000.0f);
		else
			str_format(aAuthor, sizeof(aAuthor), "%s · %d", Entry.m_Item.m_aAuthor[0] ? Entry.m_Item.m_aAuthor : "?", Entry.m_Item.m_Downloads);
		Props.m_MaxWidth = Author.w;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
		Ui()->DoLabel(&Author, aAuthor, 10.0f, TEXTALIGN_MC, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		const bool InUse = pInUse != nullptr && str_comp(pInUse, Entry.m_Item.m_aName) == 0;
		const char *pAction;
		switch(Entry.m_InstallState)
		{
		case CAssetsMarket::EInstallState::INSTALLING: pAction = ScLocalize("Installing…"); break;
		case CAssetsMarket::EInstallState::INSTALLED: pAction = pInUse == nullptr ? ScLocalize("Installed") : (InUse ? ScLocalize("In use") : ScLocalize("Use")); break;
		case CAssetsMarket::EInstallState::FAILED: pAction = ScLocalize("Retry"); break;
		default: pAction = ScLocalize("Install"); break;
		}
		if(DoButton_Menu(&s_vActionButtons[i], pAction, InUse, &Action))
		{
			if(Entry.m_InstallState == CAssetsMarket::EInstallState::NONE || Entry.m_InstallState == CAssetsMarket::EInstallState::FAILED)
			{
				Market.Install(i);
			}
			else if(Entry.m_InstallState == CAssetsMarket::EInstallState::INSTALLED && pInUse != nullptr && !InUse)
			{
				const char *pName = Entry.m_Item.m_aName;
				switch(Asset)
				{
				case ETeedataAsset::SKIN:
					str_copy(g_Config.m_ClPlayerSkin, pName);
					GameClient()->m_Skins.SkinList().ForceRefresh();
					SetNeedSendInfo();
					break;
				case ETeedataAsset::GAMESKIN:
					str_copy(g_Config.m_ClAssetGame, pName);
					GameClient()->LoadGameSkin(pName);
					break;
				case ETeedataAsset::ENTITY:
					str_copy(g_Config.m_ClAssetsEntities, pName);
					GameClient()->m_MapImages.ChangeEntitiesPath(pName);
					break;
				case ETeedataAsset::EMOTICON:
					str_copy(g_Config.m_ClAssetEmoticons, pName);
					GameClient()->LoadEmoticonsSkin(pName);
					break;
				case ETeedataAsset::PARTICLE:
					str_copy(g_Config.m_ClAssetParticles, pName);
					GameClient()->LoadParticlesSkin(pName);
					break;
				case ETeedataAsset::CURSOR:
					str_copy(g_Config.m_ClAssetCursor, pName);
					GameClient()->LoadCursorAsset(pName);
					break;
				default:
					break;
				}
			}
		}
		if(Entry.m_InstallState == CAssetsMarket::EInstallState::FAILED && Entry.m_aError[0] != '\0')
			GameClient()->m_Tooltips.DoToolTip(&s_vActionButtons[i], &Action, Entry.m_aError);
	}
	s_ListBox.DoEnd();

	if(Market.PageState() == CAssetsMarket::EPageState::READY)
		s_Page = std::clamp(s_Page, 1, Market.NumPages());
}
