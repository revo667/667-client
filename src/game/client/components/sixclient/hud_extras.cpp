#include "hud_extras.h"

#include "ui_animations.h"

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/localization.h>
#include <generated/protocol.h>

#include <algorithm>

bool HudExtras::HasPlayerBelowOnSameX(CGameClient *pGameClient, int ClientId, const vec2 &PosBlocks)
{
	if(!pGameClient || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ClientId || !pGameClient->m_aClients[i].m_Active || !pGameClient->m_Snap.m_aCharacters[i].m_Active)
			continue;
		if(pGameClient->m_Snap.m_apPlayerInfos[i] && pGameClient->m_Snap.m_apPlayerInfos[i]->m_Team == TEAM_SPECTATORS)
			continue;

		vec2 OtherPos;
		if(pGameClient->m_aClients[i].m_SpecCharPresent)
		{
			OtherPos = pGameClient->m_aClients[i].m_SpecChar / 32.0f;
		}
		else
		{
			const int Conn = (i == pGameClient->m_aLocalIds[1]) ? 1 : 0;
			const CNetObj_Character *pPrevChar = &pGameClient->m_Snap.m_aCharacters[i].m_Prev;
			const CNetObj_Character *pCurChar = &pGameClient->m_Snap.m_aCharacters[i].m_Cur;
			const float IntraTick = pGameClient->Client()->IntraGameTick(Conn);
			OtherPos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick) / 32.0f;
		}

		if(absolute(PosBlocks.x - OtherPos.x) < 0.01f && OtherPos.y > PosBlocks.y)
			return true;
	}
	return false;
}

int HudExtras::GetCheckpointId(CGameClient *pGameClient)
{
	if(!pGameClient)
		return -1;

	int PlayerId = pGameClient->m_Snap.m_LocalClientId;
	if(pGameClient->m_Snap.m_SpecInfo.m_Active && pGameClient->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		PlayerId = pGameClient->m_Snap.m_SpecInfo.m_SpectatorId;

	if(PlayerId < 0 || PlayerId >= MAX_CLIENTS)
		return -1;

	const auto &Char = pGameClient->m_Snap.m_aCharacters[PlayerId];
	if(!Char.m_Active || !Char.m_HasExtendedData)
		return -1;
	return Char.m_ExtendedData.m_TeleCheckpoint;
}

bool HudExtras::GetSpectatorCountState(CGameClient *pGameClient, IClient *pClient, int &LastSpectatorCountTick, SSpectatorCountState &State, bool ForcePreview)
{
	State = SSpectatorCountState{};
	if(!pGameClient || !pClient)
		return false;

	if(ForcePreview)
	{
		State.m_Count = 5;
		str_copy(State.m_aCountBuf, "5");
		if(g_Config.m_ScShowSpectatorNames)
		{
			str_copy(State.m_aaNameLines[0], "revo667");
			str_copy(State.m_aaNameLines[1], "+4");
			State.m_NumNameLines = 2;
		}
		return true;
	}
	if(!g_Config.m_ClShowhudSpectatorCount)
		return false;

	int aSpectatorIds[MAX_CLIENTS];
	bool aSpectatorAdded[MAX_CLIENTS] = {};
	int NumSpectatorIds = 0;
	bool HasExactSpectatorNames = false;
	bool HasReliableFallbackSpectatorNames = false;
	auto AddSpectator = [&](int ClientId) {
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || aSpectatorAdded[ClientId])
			return;
		aSpectatorAdded[ClientId] = true;
		aSpectatorIds[NumSpectatorIds] = ClientId;
		++NumSpectatorIds;
	};

	int Count = 0;
	// The count is computed for the connection we are snapping, which is the dummy while cl_dummy is 1.
	// The other local tee is a valid watcher, so only the active one is skipped.
	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	if(pClient->IsSixup())
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == LocalId)
				continue;
			if(pClient->m_TranslationContext.m_aClients[i].m_PlayerFlags7 & protocol7::PLAYERFLAG_WATCHING)
				AddSpectator(i);
		}
		Count = NumSpectatorIds;
		HasExactSpectatorNames = true;
	}
	else
	{
		const CNetObj_SpectatorCount *pSpectatorCount = pGameClient->m_Snap.m_pSpectatorCount;
		if(!pSpectatorCount)
		{
			LastSpectatorCountTick = pClient->GameTick(g_Config.m_ClDummy);
			return false;
		}
		Count = pSpectatorCount->m_NumSpectators;

		if(pGameClient->m_Snap.m_NumSpectatorWatchers > 0)
		{
			// Server snaps one item per watcher (id = client id + 1).
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(pGameClient->m_Snap.m_aSpectatorWatchers[i])
					AddSpectator(i);
			}
			HasExactSpectatorNames = true;
		}
		else if(Count > 0)
		{
			// Stock DDNet only snaps the total, so infer names from the players the server can
			// be counting: spectating or paused and not AFK. If they match the total the names
			// are certain; otherwise only name likely watchers and leave the rest as "+N".
			const int LocalTeam = LocalId >= 0 && LocalId < MAX_CLIENTS ? pGameClient->m_Teams.Team(LocalId) : TEAM_FLOCK;
			vec2 LocalPos = vec2(0.0f, 0.0f);
			bool HasLocalPos = false;
			if(LocalId >= 0 && LocalId < MAX_CLIENTS && pGameClient->m_Snap.m_aCharacters[LocalId].m_Active)
			{
				const CNetObj_Character *pCur = &pGameClient->m_Snap.m_aCharacters[LocalId].m_Cur;
				LocalPos = vec2(pCur->m_X, pCur->m_Y);
				HasLocalPos = true;
			}

			int aCandidateIds[MAX_CLIENTS];
			int aCandidateScore[MAX_CLIENTS];
			int NumCandidates = 0;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(i == LocalId)
					continue;

				const CNetObj_PlayerInfo *pInfo = pGameClient->m_Snap.m_apPlayerInfos[i];
				const CGameClient::CClientData &ClientData = pGameClient->m_aClients[i];
				if(!pInfo || !ClientData.m_Active || ClientData.m_Afk)
					continue;
				if(pInfo->m_Team != TEAM_SPECTATORS && !ClientData.m_Spec && !ClientData.m_Paused)
					continue;

				int Score = 0;
				// Paused teammates are usually watching their partner.
				if(LocalTeam != TEAM_FLOCK && pGameClient->m_Teams.Team(i) == LocalTeam)
					Score += 2;
				// SpecChar is the paused tee's own position: parked next to us is a weak hint.
				if(ClientData.m_SpecCharPresent && HasLocalPos && distance(ClientData.m_SpecChar, LocalPos) < 800.0f)
					Score += 1;
				aCandidateIds[NumCandidates] = i;
				aCandidateScore[NumCandidates] = Score;
				++NumCandidates;
			}

			if(NumCandidates == Count)
			{
				for(int i = 0; i < NumCandidates; i++)
					AddSpectator(aCandidateIds[i]);
			}
			else if(NumCandidates > Count)
			{
				// Insertion sort keeps client id order among equal scores, so names don't flicker.
				for(int i = 1; i < NumCandidates; i++)
				{
					for(int j = i; j > 0 && aCandidateScore[j] > aCandidateScore[j - 1]; j--)
					{
						std::swap(aCandidateScore[j], aCandidateScore[j - 1]);
						std::swap(aCandidateIds[j], aCandidateIds[j - 1]);
					}
				}
				for(int i = 0; i < Count && aCandidateScore[i] > 0; i++)
					AddSpectator(aCandidateIds[i]);
			}
			HasReliableFallbackSpectatorNames = NumSpectatorIds > 0;
		}
	}

	if(Count == 0)
	{
		LastSpectatorCountTick = pClient->GameTick(g_Config.m_ClDummy);
		return false;
	}
	if(pClient->GameTick(g_Config.m_ClDummy) < LastSpectatorCountTick + pClient->GameTickSpeed())
		return false;

	if(g_Config.m_ScShowSpectatorNames && (HasExactSpectatorNames || HasReliableFallbackSpectatorNames) && NumSpectatorIds > 0)
	{
		const int MaxVisibleNames = 5;
		const int VisibleNames = std::min(NumSpectatorIds, MaxVisibleNames);

		int ShownNames = 0;
		for(int i = 0; i < NumSpectatorIds && ShownNames < VisibleNames; i++)
		{
			const char *pName = pGameClient->m_aClients[aSpectatorIds[i]].m_aName;
			if(pName[0] == '\0')
				continue;
			str_copy(State.m_aaNameLines[State.m_NumNameLines], pName);
			++State.m_NumNameLines;
			++ShownNames;
		}

		const int Remaining = std::max(0, Count - ShownNames);
		if(Remaining > 0 && State.m_NumNameLines < 6)
		{
			str_format(State.m_aaNameLines[State.m_NumNameLines], sizeof(State.m_aaNameLines[State.m_NumNameLines]), "+%d", Remaining);
			++State.m_NumNameLines;
		}
	}

	State.m_Count = Count;
	str_format(State.m_aCountBuf, sizeof(State.m_aCountBuf), "%d", Count);
	return true;
}
