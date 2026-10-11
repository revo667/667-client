#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/external/json-parser/json.h>
#include <engine/font_icons.h>
#include <engine/friends.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/client/components/menus.h>
#include <game/client/components/sixclient/ui_theme/style.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr const char *FRIEND_SKINS_FILE = "667Client/friend_skins.txt";
	constexpr int MAX_STAT_REQUESTS = 3;
	constexpr int64_t STATS_REFRESH_SECONDS = 600;

	struct SFriendStats
	{
		bool m_Loaded = false;
		bool m_Found = false;
		bool m_Failed = false;
		int m_Points = 0;
		int m_Rank = 0;
		int m_Hours = 0;
		int64_t m_LastActivity = 0;
		int64_t m_FetchedAt = 0;
		std::shared_ptr<IHttpRequest> m_pRequest;
	};

	struct SFriendSkin
	{
		std::string m_Skin = "default";
		bool m_CustomColors = false;
		int m_ColorBody = 0;
		int m_ColorFeet = 0;
	};

	struct SFriendCard
	{
		std::string m_Name;
		std::string m_Clan;
		const CServerInfo *m_pServer = nullptr;
	};

	std::unordered_map<std::string, SFriendStats> gs_Stats;
	std::unordered_map<std::string, SFriendSkin> gs_Skins;
	bool gs_SkinsLoaded = false;
	bool gs_SkinsDirty = false;
	bool gs_RequestedBrowserRefresh = false;

	int64_t DaysFromCivil(int Year, unsigned Month, unsigned Day)
	{
		Year -= Month <= 2;
		const int Era = (Year >= 0 ? Year : Year - 399) / 400;
		const unsigned YearOfEra = (unsigned)(Year - Era * 400);
		const unsigned DayOfYear = (153 * (Month + (Month > 2 ? -3 : 9)) + 2) / 5 + Day - 1;
		const unsigned DayOfEra = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
		return (int64_t)Era * 146097 + (int64_t)DayOfEra - 719468;
	}

	int64_t ParseDateToEpoch(const char *pDate)
	{
		int Year = 0, Month = 0, Day = 0;
		if(sscanf(pDate, "%d-%d-%d", &Year, &Month, &Day) != 3)
			return 0;
		return DaysFromCivil(Year, Month, Day) * 86400;
	}

	void FormatLastActivity(int64_t Timestamp, char *pBuf, int BufSize)
	{
		if(Timestamp <= 0)
		{
			str_copy(pBuf, "-", BufSize);
			return;
		}
		const int64_t Days = (time_timestamp() - Timestamp) / 86400;
		if(Days <= 0)
			str_copy(pBuf, Localize("today"), BufSize);
		else if(Days == 1)
			str_copy(pBuf, Localize("yesterday"), BufSize);
		else if(Days < 30)
			str_format(pBuf, BufSize, Localize("%d days ago"), (int)Days);
		else if(Days < 365)
			str_format(pBuf, BufSize, Localize("%d months ago"), (int)(Days / 30));
		else
			str_format(pBuf, BufSize, Localize("%d years ago"), (int)(Days / 365));
	}

	void ParseStats(SFriendStats &Stats, IHttpRequest *pRequest)
	{
		Stats.m_Loaded = true;
		Stats.m_FetchedAt = time_timestamp();
		json_value *pJson = pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200 ? pRequest->ResultJson() : nullptr;
		if(!pJson)
		{
			Stats.m_Failed = true;
			return;
		}

		const json_value *pPoints = json_object_get(pJson, "points");
		Stats.m_Found = pPoints->type == json_object;
		if(Stats.m_Found)
		{
			const json_value *pValue = json_object_get(pPoints, "points");
			Stats.m_Points = pValue->type == json_integer ? (int)pValue->u.integer : 0;
			const json_value *pRank = json_object_get(pPoints, "rank");
			Stats.m_Rank = pRank->type == json_integer ? (int)pRank->u.integer : 0;
		}

		const json_value *pHours = json_object_get(pJson, "hours_played_past_365_days");
		Stats.m_Hours = pHours->type == json_integer ? (int)pHours->u.integer : 0;

		int64_t LastActivity = 0;
		const json_value *pActivity = json_object_get(pJson, "activity");
		if(pActivity->type == json_array && pActivity->u.array.length > 0)
		{
			const json_value *pLast = pActivity->u.array.values[pActivity->u.array.length - 1];
			const json_value *pDate = json_object_get(pLast, "date");
			if(pDate->type == json_string)
				LastActivity = ParseDateToEpoch(pDate->u.string.ptr);
		}
		const json_value *pFinishes = json_object_get(pJson, "last_finishes");
		if(pFinishes->type == json_array && pFinishes->u.array.length > 0)
		{
			const json_value *pTimestamp = json_object_get(pFinishes->u.array.values[0], "timestamp");
			if(pTimestamp->type == json_double)
				LastActivity = std::max(LastActivity, (int64_t)pTimestamp->u.dbl);
			else if(pTimestamp->type == json_integer)
				LastActivity = std::max(LastActivity, (int64_t)pTimestamp->u.integer);
		}
		Stats.m_LastActivity = LastActivity;
		json_value_free(pJson);
	}

	void LoadSkins(IStorage *pStorage)
	{
		gs_SkinsLoaded = true;
		CLineReader Reader;
		if(!Reader.OpenFile(pStorage->OpenFile(FRIEND_SKINS_FILE, IOFLAG_READ, IStorage::TYPE_SAVE)))
			return;
		while(const char *pLine = Reader.Get())
		{
			char aName[64], aSkin[64];
			int Custom = 0, Body = 0, Feet = 0;
			const char *pTab = str_find(pLine, "\t");
			if(!pTab)
				continue;
			str_truncate(aName, sizeof(aName), pLine, pTab - pLine);
			if(sscanf(pTab + 1, "%63s %d %d %d", aSkin, &Custom, &Body, &Feet) != 4)
				continue;
			SFriendSkin &Skin = gs_Skins[aName];
			Skin.m_Skin = aSkin;
			Skin.m_CustomColors = Custom != 0;
			Skin.m_ColorBody = Body;
			Skin.m_ColorFeet = Feet;
		}
	}

	void SaveSkins(IStorage *pStorage)
	{
		gs_SkinsDirty = false;
		IOHANDLE File = pStorage->OpenFile(FRIEND_SKINS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return;
		for(const auto &[Name, Skin] : gs_Skins)
		{
			char aLine[256];
			str_format(aLine, sizeof(aLine), "%s\t%s %d %d %d\n", Name.c_str(), Skin.m_Skin.c_str(), Skin.m_CustomColors ? 1 : 0, Skin.m_ColorBody, Skin.m_ColorFeet);
			io_write(File, aLine, str_length(aLine));
		}
		io_close(File);
	}
}

void CMenus::RenderSettingsSixClientFriends(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float CardHeight = 104.0f;
	const float CardSpacing = 10.0f;
	const float TeeSize = 64.0f;

	if(!gs_SkinsLoaded)
		LoadSkins(Storage());

	if(ServerBrowser()->NumServers() == 0 && !ServerBrowser()->IsRefreshing() && !gs_RequestedBrowserRefresh)
	{
		gs_RequestedBrowserRefresh = true;
		ServerBrowser()->Refresh(IServerBrowser::TYPE_INTERNET);
	}

	std::vector<SFriendCard> vCards;
	for(int i = 0; i < GameClient()->Friends()->NumFriends(); i++)
	{
		const CFriendInfo *pFriend = GameClient()->Friends()->GetFriend(i);
		if(pFriend->m_aName[0] == '\0')
			continue;
		SFriendCard Card;
		Card.m_Name = pFriend->m_aName;
		Card.m_Clan = pFriend->m_aClan;
		vCards.push_back(Card);
	}

	for(int ServerIndex = 0; ServerIndex < ServerBrowser()->NumServers(); ++ServerIndex)
	{
		const CServerInfo *pServer = ServerBrowser()->Get(ServerIndex);
		if(pServer->m_FriendState == IFriends::FRIEND_NO)
			continue;
		for(const CServerInfo::CClient &Player : pServer->m_vClients)
		{
			if(Player.m_FriendState != IFriends::FRIEND_PLAYER)
				continue;
			for(SFriendCard &Card : vCards)
			{
				if(Card.m_pServer || str_comp(Card.m_Name.c_str(), Player.m_aName) != 0)
					continue;
				Card.m_pServer = pServer;
				SFriendSkin &Skin = gs_Skins[Card.m_Name];
				if(Skin.m_Skin != Player.m_aSkin || Skin.m_CustomColors != Player.m_CustomSkinColors || Skin.m_ColorBody != Player.m_CustomSkinColorBody || Skin.m_ColorFeet != Player.m_CustomSkinColorFeet)
				{
					Skin.m_Skin = Player.m_aSkin[0] ? Player.m_aSkin : "default";
					Skin.m_CustomColors = Player.m_CustomSkinColors;
					Skin.m_ColorBody = Player.m_CustomSkinColorBody;
					Skin.m_ColorFeet = Player.m_CustomSkinColorFeet;
					gs_SkinsDirty = true;
				}
			}
		}
	}
	if(gs_SkinsDirty)
		SaveSkins(Storage());

	std::stable_sort(vCards.begin(), vCards.end(), [](const SFriendCard &A, const SFriendCard &B) {
		if((A.m_pServer != nullptr) != (B.m_pServer != nullptr))
			return A.m_pServer != nullptr;
		return str_comp_nocase(A.m_Name.c_str(), B.m_Name.c_str()) < 0;
	});

	int InFlight = 0;
	for(auto &[Name, Stats] : gs_Stats)
	{
		if(!Stats.m_pRequest)
			continue;
		if(Stats.m_pRequest->Done())
		{
			ParseStats(Stats, Stats.m_pRequest.get());
			Stats.m_pRequest = nullptr;
		}
		else
		{
			InFlight++;
		}
	}
	for(const SFriendCard &Card : vCards)
	{
		if(InFlight >= MAX_STAT_REQUESTS)
			break;
		SFriendStats &Stats = gs_Stats[Card.m_Name];
		if(Stats.m_pRequest || (Stats.m_Loaded && time_timestamp() - Stats.m_FetchedAt < STATS_REFRESH_SECONDS))
			continue;
		char aEscaped[256], aUrl[512];
		EscapeUrl(aEscaped, Card.m_Name.c_str());
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);
		Stats.m_pRequest = HttpGet(aUrl);
		Stats.m_pRequest->Timeout(CTimeout{8000, 0, 500, 5});
		Stats.m_pRequest->LogProgress(HTTPLOG::FAILURE);
		Stats.m_pRequest->FailOnErrorStatus(false);
		Http()->Run(Stats.m_pRequest);
		InFlight++;
	}

	CKogPoints &KogPoints = GameClient()->m_KogPoints;
	const bool OnKog = KogPoints.OnKogServer();
	if(OnKog)
		for(const SFriendCard &Card : vCards)
			KogPoints.Request(Card.m_Name.c_str(), false);

	const int OnlineCount = (int)std::count_if(vCards.begin(), vCards.end(), [](const SFriendCard &Card) { return Card.m_pServer != nullptr; });

	CUIRect Header, Title, RefreshButton;
	MainView.HSplitTop(LineSize + MarginSmall, &Header, &MainView);
	Header.VSplitRight(110.0f, &Title, &RefreshButton);
	RefreshButton.HSplitTop(LineSize, &RefreshButton, nullptr);
	char aTitle[128];
	str_format(aTitle, sizeof(aTitle), Localize("Friends (%d online / %d)"), OnlineCount, (int)vCards.size());
	Ui()->DoLabel(&Title, aTitle, HeadlineFontSize, TEXTALIGN_ML);
	static CButtonContainer s_RefreshButton;
	if(DoButton_Menu(&s_RefreshButton, ServerBrowser()->IsRefreshing() ? Localize("Refreshing…") : Localize("Refresh"), 0, &RefreshButton))
	{
		ServerBrowser()->Refresh(IServerBrowser::TYPE_INTERNET, true);
		if(OnKog)
			for(const SFriendCard &Card : vCards)
				KogPoints.Request(Card.m_Name.c_str(), true);
		for(auto &[Name, Stats] : gs_Stats)
			if(!Stats.m_pRequest)
				Stats.m_Loaded = false;
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	if(vCards.empty())
	{
		CUIRect Empty;
		MainView.HSplitTop(60.0f, &Empty, nullptr);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		Ui()->DoLabel(&Empty, Localize("No friends yet. Add friends from the scoreboard or the server browser."), 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = CardHeight;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	MainView.VSplitRight(10.0f, &MainView, nullptr);

	const int Columns = MainView.w >= 760.0f ? 3 : (MainView.w >= 480.0f ? 2 : 1);
	const float CardWidth = (MainView.w - (Columns - 1) * CardSpacing) / Columns;
	static std::vector<CButtonContainer> s_vJoinButtons;
	static std::vector<CButtonContainer> s_vUnfriendButtons;
	static std::vector<CButtonContainer> s_vProfileButtons;
	if(s_vJoinButtons.size() < vCards.size())
	{
		s_vJoinButtons.resize(vCards.size());
		s_vUnfriendButtons.resize(vCards.size());
		s_vProfileButtons.resize(vCards.size());
	}

	std::string RemoveName, RemoveClan;
	const char *pCurrentAddress = Client()->State() == IClient::STATE_ONLINE ? Client()->ConnectAddressString() : "";

	CUIRect Row;
	for(size_t i = 0; i < vCards.size(); i++)
	{
		if(i % Columns == 0)
		{
			if(i > 0)
				MainView.HSplitTop(CardSpacing, nullptr, &MainView);
			MainView.HSplitTop(CardHeight, &Row, &MainView);
			s_ScrollRegion.AddRect(Row);
		}
		CUIRect Card;
		Row.VSplitLeft(CardWidth, &Card, &Row);
		Row.VSplitLeft(CardSpacing, nullptr, &Row);

		const SFriendCard &Friend = vCards[i];
		const bool Online = Friend.m_pServer != nullptr;
		Card.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, Online ? 0.3f : 0.2f), IGraphics::CORNER_ALL, 10.0f);
		if(Online)
		{
			CUIRect Accent = Card;
			Accent.w = 4.0f;
			Accent.Draw(ColorRGBA(0.35f, 0.85f, 0.45f, 0.9f), IGraphics::CORNER_L, 10.0f);
		}

		CUIRect Content, TeeArea, Info, Buttons;
		Card.Margin(8.0f, &Content);
		Content.VSplitLeft(TeeSize + 8.0f, &TeeArea, &Info);

		const auto SkinIt = gs_Skins.find(Friend.m_Name);
		const SFriendSkin Skin = SkinIt != gs_Skins.end() ? SkinIt->second : SFriendSkin();
		RenderDevSkin(vec2(TeeArea.x + TeeSize * 0.5f, TeeArea.y + TeeArea.h * 0.5f), TeeSize, Skin.m_Skin.c_str(), "default", Skin.m_CustomColors, Skin.m_ColorFeet, Skin.m_ColorBody, Online ? EMOTE_HAPPY : EMOTE_NORMAL, false, true);

		Info.HSplitBottom(LineSize, &Info, &Buttons);
		SLabelProperties Props;
		Props.m_MaxWidth = Info.w;
		Props.m_EllipsisAtEnd = true;

		CUIRect Line;
		Info.HSplitTop(16.0f, &Line, &Info);
		Ui()->DoLabel(&Line, Friend.m_Name.c_str(), 14.0f, TEXTALIGN_ML, Props);
		if(!Friend.m_Clan.empty())
		{
			Info.HSplitTop(12.0f, &Line, &Info);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
			Ui()->DoLabel(&Line, Friend.m_Clan.c_str(), 10.0f, TEXTALIGN_ML, Props);
		}

		char aStatus[160];
		if(Online)
			str_format(aStatus, sizeof(aStatus), "● %s", Friend.m_pServer->m_aName);
		else
			str_format(aStatus, sizeof(aStatus), "○ %s", Localize("Offline"));
		Info.HSplitTop(13.0f, &Line, &Info);
		TextRender()->TextColor(Online ? ColorRGBA(0.45f, 0.95f, 0.55f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f));
		Ui()->DoLabel(&Line, aStatus, 10.0f, TEXTALIGN_ML, Props);

		char aKog[96];
		const CKogPoints::SEntry *pKog = KogPoints.Find(Friend.m_Name.c_str());
		if(pKog && pKog->m_Rank > 0)
			str_format(aKog, sizeof(aKog), "KoG %d %s (#%d)", pKog->m_Points, Localize("pts"), pKog->m_Rank);
		else if(pKog)
			str_format(aKog, sizeof(aKog), "KoG %d %s", pKog->m_Points, Localize("pts"));
		else if(OnKog && KogPoints.IsQueued(Friend.m_Name.c_str()))
			str_format(aKog, sizeof(aKog), "KoG %s", Localize("checking…"));
		else
			str_format(aKog, sizeof(aKog), "KoG %s", Localize("— join a KoG server"));
		Info.HSplitTop(13.0f, &Line, &Info);
		TextRender()->TextColor(1.0f, 0.82f, 0.35f, pKog ? 1.0f : 0.55f);
		Ui()->DoLabel(&Line, aKog, 10.0f, TEXTALIGN_ML, Props);

		const auto StatsIt = gs_Stats.find(Friend.m_Name);
		char aStats[192];
		if(StatsIt == gs_Stats.end() || (!StatsIt->second.m_Loaded && StatsIt->second.m_pRequest))
		{
			str_copy(aStats, Localize("Loading stats…"));
		}
		else if(!StatsIt->second.m_Found)
		{
			str_copy(aStats, StatsIt->second.m_Failed ? Localize("Stats unavailable") : Localize("No DDNet activity"));
		}
		else
		{
			char aLast[64];
			FormatLastActivity(StatsIt->second.m_LastActivity, aLast, sizeof(aLast));
			str_format(aStats, sizeof(aStats), "%d h  ·  %s %s", StatsIt->second.m_Hours, Localize("last active"), aLast);
		}
		Info.HSplitTop(13.0f, &Line, &Info);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.75f);
		Ui()->DoLabel(&Line, aStats, 10.0f, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CUIRect JoinButton, UnfriendButton, ProfileButton;
		Buttons.VSplitRight(72.0f, &Buttons, &UnfriendButton);
		Buttons.VSplitRight(5.0f, &Buttons, nullptr);
		Buttons.VSplitRight(52.0f, &Buttons, &ProfileButton);
		Buttons.VSplitRight(5.0f, &Buttons, nullptr);
		Buttons.VSplitRight(56.0f, &Buttons, &JoinButton);

		if(DoButton_Menu(&s_vProfileButtons[i], "KoG", 0, &ProfileButton))
		{
			char aEscaped[256], aUrl[512];
			EscapeUrl(aEscaped, Friend.m_Name.c_str());
			str_format(aUrl, sizeof(aUrl), "https://kog.tw/#p=players&player=%s", aEscaped);
			Client()->ViewLink(aUrl);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_vProfileButtons[i], &ProfileButton, Localize("Open KoG profile"));

		if(Online)
		{
			const bool AlreadyHere = pCurrentAddress[0] != '\0' && str_find(Friend.m_pServer->m_aAddress, pCurrentAddress) != nullptr;
			if(DoButton_Menu(&s_vJoinButtons[i], AlreadyHere ? Localize("Here") : Localize("Join"), AlreadyHere ? -1 : 0, &JoinButton) && !AlreadyHere)
				Client()->Connect(Friend.m_pServer->m_aAddress);
		}
		if(DoButton_Menu(&s_vUnfriendButtons[i], Localize("Unfriend"), 0, &UnfriendButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.85f, 0.25f, 0.25f, 0.5f)))
		{
			RemoveName = Friend.m_Name;
			RemoveClan = Friend.m_Clan;
		}
	}

	s_ScrollRegion.End();

	if(!RemoveName.empty())
	{
		GameClient()->Friends()->RemoveFriend(RemoveName.c_str(), RemoveClan.c_str());
		Client()->ServerBrowserUpdate();
	}
}
