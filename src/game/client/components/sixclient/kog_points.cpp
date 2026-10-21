/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-cd35ae5a */

#include "kog_points.h"

#include <base/io.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/friends.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <vector>
#include <cctype>
#include <cstdlib>

static constexpr const char *KOG_POINTS_FILE = "667Client/kog_points.txt";
static constexpr int64_t KOG_QUERY_INTERVAL_SECONDS = 5;
static constexpr int64_t KOG_RESPONSE_TIMEOUT_SECONDS = 6;
static constexpr int64_t KOG_REFRESH_SECONDS = 1800;
// A name is asked for at most once an hour, automatically or on request.
static constexpr int64_t KOG_RETRY_SECONDS = 3600;
static constexpr int64_t KOG_FORCED_RETRY_SECONDS = 3600;

std::string CKogPoints::Key(const char *pName)
{
	std::string Key = pName;
	std::transform(Key.begin(), Key.end(), Key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Key;
}

void CKogPoints::OnInit()
{
	Load();
}

void CKogPoints::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE)
	{
		m_Queue.clear();
		m_Pending = SPending();
		m_AskFriendsOnJoin = false;
	}
	else if(!m_AskedFriends)
	{
		m_AskFriendsOnJoin = true;
		m_JoinedAt = time_timestamp();
	}
}

bool CKogPoints::OnKogServer() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;
	const CServerInfo &Info = Client()->ServerInfo();
	return str_comp_nocase(Info.m_aCommunityId, "kog") == 0 || str_find_nocase(Info.m_aName, "kog") != nullptr;
}

const CKogPoints::SEntry *CKogPoints::Find(const char *pName) const
{
	const auto It = m_Entries.find(Key(pName));
	return It == m_Entries.end() ? nullptr : &It->second;
}

bool CKogPoints::IsQueued(const char *pName) const
{
	const std::string NameKey = Key(pName);
	if(!m_Pending.m_Name.empty() && Key(m_Pending.m_Name.c_str()) == NameKey)
		return true;
	return std::any_of(m_Queue.begin(), m_Queue.end(), [&](const std::string &Name) { return Key(Name.c_str()) == NameKey; });
}

void CKogPoints::Request(const char *pName, bool Force)
{
	if(!OnKogServer() || IsQueued(pName))
		return;
	const int64_t Now = time_timestamp();
	const SEntry *pEntry = Find(pName);
	if(!Force && pEntry && Now - pEntry->m_UpdatedAt < KOG_REFRESH_SECONDS)
		return;
	// 667client: every ask is a "/points" chat command, so names are not asked again too soon,
	// whether or not the server answered last time.
	const auto Asked = m_LastAsked.find(Key(pName));
	if(Asked != m_LastAsked.end() && Now - Asked->second < (Force ? KOG_FORCED_RETRY_SECONDS : KOG_RETRY_SECONDS))
		return;
	m_Queue.emplace_back(pName);
}

void CKogPoints::NoteManualQuery(const char *pName)
{
	m_Pending.m_Name = pName;
	m_Pending.m_SentAt = time_timestamp();
	m_Pending.m_Hide = false;
}

void CKogPoints::Send(const std::string &Name, bool Hide)
{
	char aCommand[128];
	str_copy(aCommand, "say \"/points ");
	char *pDst = aCommand + str_length(aCommand);
	str_escape(&pDst, Name.c_str(), aCommand + sizeof(aCommand));
	str_append(aCommand, "\"");
	Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);

	m_Pending.m_Name = Name;
	m_Pending.m_SentAt = time_timestamp();
	m_Pending.m_Hide = Hide;
	m_LastSent = m_Pending.m_SentAt;
	m_LastAsked[Key(Name.c_str())] = m_Pending.m_SentAt; // 667client
}

void CKogPoints::OnRender()
{
	const int64_t Now = time_timestamp();
	// A few seconds after joining (the server info decides whether this is KoG), ask for the
	// friends' points, once per game start. Nothing else asks on its own.
	if(m_AskFriendsOnJoin && g_Config.m_ScKogFriendPoints && Now - m_JoinedAt >= 3 && Client()->State() == IClient::STATE_ONLINE)
	{
		m_AskFriendsOnJoin = false;
		if(OnKogServer())
		{
			m_AskedFriends = true;
			for(int i = 0; i < GameClient()->Friends()->NumFriends(); i++)
			{
				const CFriendInfo *pFriend = GameClient()->Friends()->GetFriend(i);
				if(pFriend->m_aName[0] != '\0')
					Request(pFriend->m_aName, true);
			}
		}
	}
	if(!m_Pending.m_Name.empty() && Now - m_Pending.m_SentAt > KOG_RESPONSE_TIMEOUT_SECONDS)
		m_Pending = SPending();

	if(m_Queue.empty() || !m_Pending.m_Name.empty() || Now - m_LastSent < KOG_QUERY_INTERVAL_SECONDS || !OnKogServer())
		return;

	const std::string Name = m_Queue.front();
	m_Queue.pop_front();
	Send(Name, true);
}

bool CKogPoints::ParseLine(const char *pLine, const char *pName, int &Points, int &Rank)
{
	const char *pNameStart = str_find_nocase(pLine, pName);
	if(!pNameStart)
		return false;

	const char *pCursor = pLine;
	while(*pCursor == ' ')
		pCursor++;
	if(std::isdigit((unsigned char)*pCursor))
	{
		const int LeadingRank = std::atoi(pCursor);
		while(std::isdigit((unsigned char)*pCursor))
			pCursor++;
		if(*pCursor == '.')
		{
			pCursor++;
			while(*pCursor == ' ')
				pCursor++;
			const char *pPointsLabel = str_find_nocase(pNameStart, "points:");
			if(pCursor == pNameStart && pPointsLabel)
			{
				Rank = LeadingRank;
				Points = std::atoi(pPointsLabel + str_length("points:"));
				return true;
			}
		}
	}

	std::string Text = std::string(pLine, pNameStart - pLine) + " " + std::string(pNameStart + str_length(pName));
	std::string Lower = Key(Text.c_str());

	struct SNumber
	{
		size_t m_Start;
		size_t m_End;
		int m_Value;
	};
	std::vector<SNumber> vNumbers;
	for(size_t i = 0; i < Text.size();)
	{
		if(std::isdigit((unsigned char)Text[i]))
		{
			size_t End = i;
			std::string Digits;
			while(End < Text.size() && (std::isdigit((unsigned char)Text[End]) || ((Text[End] == ',' || Text[End] == '.') && End + 1 < Text.size() && std::isdigit((unsigned char)Text[End + 1]))))
			{
				if(std::isdigit((unsigned char)Text[End]))
					Digits.push_back(Text[End]);
				End++;
			}
			vNumbers.push_back({i, End, std::atoi(Digits.c_str())});
			i = End;
		}
		else
		{
			i++;
		}
	}
	if(vNumbers.empty())
		return false;

	Rank = 0;
	int PointsIndex = -1;
	const size_t PointWord = Lower.find("point") != std::string::npos ? Lower.find("point") : Lower.find("pts");
	for(size_t i = 0; i < vNumbers.size(); i++)
	{
		const SNumber &Number = vNumbers[i];
		const bool HashBefore = Number.m_Start > 0 && Text[Number.m_Start - 1] == '#';
		const size_t RankWord = Lower.rfind("rank", Number.m_Start);
		if(HashBefore || (RankWord != std::string::npos && Number.m_Start - RankWord <= 7))
		{
			Rank = Number.m_Value;
			continue;
		}
		if(PointWord != std::string::npos)
		{
			const bool Before = Number.m_End <= PointWord && PointWord - Number.m_End <= 3;
			const bool After = Number.m_Start >= PointWord && Number.m_Start - PointWord <= 10;
			if(Before || After)
			{
				PointsIndex = (int)i;
				break;
			}
		}
		if(PointsIndex < 0)
			PointsIndex = (int)i;
	}
	if(PointsIndex < 0)
		return false;
	Points = vNumbers[PointsIndex].m_Value;
	return true;
}

bool CKogPoints::ConsumeServerLine(const char *pLine)
{
	if(m_Pending.m_Name.empty() || time_timestamp() - m_Pending.m_SentAt > KOG_RESPONSE_TIMEOUT_SECONDS)
		return false;

	int Points = 0, Rank = 0;
	if(!ParseLine(pLine, m_Pending.m_Name.c_str(), Points, Rank))
		return false;

	log_info("kog_points", "%s -> %d points, rank %d (\"%s\")", m_Pending.m_Name.c_str(), Points, Rank, pLine);
	SEntry &Entry = m_Entries[Key(m_Pending.m_Name.c_str())];
	Entry.m_Points = Points;
	Entry.m_Rank = Rank;
	Entry.m_UpdatedAt = time_timestamp();
	const bool Hide = m_Pending.m_Hide;
	m_Pending = SPending();
	Save();
	return Hide;
}

void CKogPoints::Load()
{
	if(m_Loaded)
		return;
	m_Loaded = true;
	CLineReader Reader;
	if(!Reader.OpenFile(Storage()->OpenFile(KOG_POINTS_FILE, IOFLAG_READ, IStorage::TYPE_SAVE)))
		return;
	while(const char *pLine = Reader.Get())
	{
		const char *pTab = str_find(pLine, "\t");
		if(!pTab)
			continue;
		SEntry Entry;
		long long UpdatedAt = 0;
		if(sscanf(pTab + 1, "%d %d %lld", &Entry.m_Points, &Entry.m_Rank, &UpdatedAt) != 3)
			continue;
		Entry.m_UpdatedAt = UpdatedAt;
		m_Entries[std::string(pLine, pTab - pLine)] = Entry;
	}
}

void CKogPoints::Save()
{
	IOHANDLE File = Storage()->OpenFile(KOG_POINTS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	for(const auto &[Name, Entry] : m_Entries)
	{
		char aLine[256];
		str_format(aLine, sizeof(aLine), "%s\t%d %d %lld\n", Name.c_str(), Entry.m_Points, Entry.m_Rank, (long long)Entry.m_UpdatedAt);
		io_write(File, aLine, str_length(aLine));
	}
	io_close(File);
}
