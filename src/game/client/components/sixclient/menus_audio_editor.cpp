/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-e365d2ec */

// Assets Editor > Audio: builds a sound pack (assets/audio/<name>/) from a base pack, with chosen
// sounds swapped in from other installed packs or from the custom sounds the Cecen server hosts.

#include <base/io.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/sound.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/components/menus.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr const char *CACHE_FOLDER = "667Client/sounds";
	constexpr int64_t MAX_SOUND_BYTES = 2 * 1024 * 1024;
	constexpr int64_t MAX_INDEX_BYTES = 512 * 1024;

	struct SAudioSlot
	{
		const char *m_pGroup;
		const char *m_pName;
		int m_SoundId;
	};

	// The sounds a player notices most. Each slot replaces every file of its sound set.
	const SAudioSlot gs_aAudioSlots[] = {
		{"Hook", "Hook (ground)", SOUND_HOOK_ATTACH_GROUND},
		{"Hook", "Drag (hook a player)", SOUND_HOOK_ATTACH_PLAYER},
		{"Hook", "Hook miss", SOUND_HOOK_NOATTACH},
		{"Movement", "Jump", SOUND_PLAYER_JUMP},
		{"Movement", "Air jump", SOUND_PLAYER_AIRJUMP},
		{"Movement", "Land", SOUND_BODY_LAND},
		{"Movement", "Skid", SOUND_PLAYER_SKID},
		{"Hammer", "Hammer swing", SOUND_HAMMER_FIRE},
		{"Hammer", "Hammer hit", SOUND_HAMMER_HIT},
		{"Weapons", "Pistol", SOUND_GUN_FIRE},
		{"Weapons", "Shotgun", SOUND_SHOTGUN_FIRE},
		{"Weapons", "Grenade", SOUND_GRENADE_FIRE},
		{"Weapons", "Explosion", SOUND_GRENADE_EXPLODE},
		{"Weapons", "Laser", SOUND_LASER_FIRE},
		{"Weapons", "Laser bounce", SOUND_LASER_BOUNCE},
		{"Weapons", "Ninja", SOUND_NINJA_FIRE},
		{"Weapons", "Weapon switch", SOUND_WEAPON_SWITCH},
		{"Weapons", "Hit", SOUND_HIT},
		{"Tee", "Spawn", SOUND_PLAYER_SPAWN},
		{"Tee", "Death", SOUND_PLAYER_DIE},
		{"Tee", "Pain", SOUND_PLAYER_PAIN_SHORT},
		{"Chat", "Chat message", SOUND_CHAT_CLIENT},
		{"Chat", "Server message", SOUND_CHAT_SERVER},
		{"Chat", "Highlight", SOUND_CHAT_HIGHLIGHT},
	};
	constexpr int NUM_AUDIO_SLOTS = (int)std::size(gs_aAudioSlots);

	enum class ESourceKind
	{
		BASE,
		PACK,
		CUSTOM,
	};

	struct SSlotEdit
	{
		ESourceKind m_Kind = ESourceKind::BASE;
		std::string m_Value; // pack name or custom sound id
	};

	struct SCustomSound
	{
		std::string m_Id;
		std::string m_File;
		std::string m_Name;
		int m_DurationMs = 0;
		int64_t m_Size = 0;
		bool m_Cached = false;
		bool m_Failed = false;
		std::shared_ptr<IHttpRequest> m_pRequest;
		CButtonContainer m_PickButton;
		CButtonContainer m_PlayButton;
	};

	struct SPackEntry
	{
		std::string m_Name;
		CButtonContainer m_PickButton;
		CButtonContainer m_PlayButton;
	};

	enum class EIndexState
	{
		IDLE,
		LOADING,
		READY,
		FAILED,
	};

	struct SAudioEditorState
	{
		bool m_Initialized = false;
		std::string m_Base = "default";
		SSlotEdit m_aEdits[NUM_AUDIO_SLOTS];
		int m_Selected = 0; // -1 = base pack row
		int m_LibraryTab = 0; // 0 custom sounds, 1 installed packs
		std::vector<SPackEntry> m_vPacks;
		std::vector<SCustomSound> m_vCustom;
		EIndexState m_IndexState = EIndexState::IDLE;
		std::shared_ptr<IHttpRequest> m_pIndexRequest;
		std::string m_PendingPreview; // custom id to play once its download lands
		int m_PreviewSample = -1;
		char m_aStatus[256] = "";
		bool m_StatusError = false;
	};

	SAudioEditorState gs_AudioEditor;
	CLineInputBuffered<64> gs_AudioExportName;
	CLineInputBuffered<64> gs_AudioSearch;

	bool AudioNameValid(const char *pName)
	{
		if(pName[0] == '\0' || str_comp(pName, "default") == 0)
			return false;
		for(const char *p = pName; *p; p++)
		{
			const char c = *p;
			if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' '))
				return false;
		}
		return true;
	}

	bool CustomIdValid(const char *pId)
	{
		if(pId[0] == '\0' || str_length(pId) > 64)
			return false;
		for(const char *p = pId; *p; p++)
			if(!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
				return false;
		return true;
	}

	bool IsWavPack(const unsigned char *pData, size_t Size)
	{
		return Size >= 4 && mem_comp(pData, "wvpk", 4) == 0;
	}

	const char *SoundBaseName(const char *pFilename)
	{
		const char *pSuffix = str_startswith(pFilename, "audio/");
		return pSuffix ? pSuffix : pFilename;
	}

	void CachePath(const SCustomSound &Sound, char *pOut, int OutSize)
	{
		str_format(pOut, OutSize, "%s/%s", CACHE_FOLDER, Sound.m_File.c_str());
	}

	int ListPacksCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		auto *pNames = static_cast<std::vector<std::string> *>(pUser);
		if(!IsDir || pName[0] == '.' || str_comp(pName, "default") == 0)
			return 0;
		if(std::find(pNames->begin(), pNames->end(), pName) == pNames->end())
			pNames->emplace_back(pName);
		return 0;
	}
}

void CMenus::RenderSixClientAudioEditor(CUIRect MainView)
{
	SAudioEditorState &State = gs_AudioEditor;
	const float LineSize = 20.0f;
	const float Margin = 5.0f;

	auto SetStatus = [&](bool Error, const char *pText) {
		str_copy(State.m_aStatus, pText, sizeof(State.m_aStatus));
		State.m_StatusError = Error;
	};

	auto FindCustom = [&](const std::string &Id) -> SCustomSound * {
		for(SCustomSound &Sound : State.m_vCustom)
			if(Sound.m_Id == Id)
				return &Sound;
		return nullptr;
	};

	// Reads one file of a pack; "default" and files a pack lacks come from the game's own sounds.
	auto ReadPackFile = [&](const std::string &Pack, const char *pBaseName, void **ppData, unsigned *pSize) {
		char aPath[IO_MAX_PATH_LENGTH];
		if(Pack != "default")
		{
			str_format(aPath, sizeof(aPath), "assets/audio/%s/%s", Pack.c_str(), pBaseName);
			if(Storage()->ReadFile(aPath, IStorage::TYPE_ALL, ppData, pSize))
				return true;
			str_format(aPath, sizeof(aPath), "assets/audio/%s/audio/%s", Pack.c_str(), pBaseName);
			if(Storage()->ReadFile(aPath, IStorage::TYPE_ALL, ppData, pSize))
				return true;
			str_format(aPath, sizeof(aPath), "audio/%s/%s", Pack.c_str(), pBaseName);
			if(Storage()->ReadFile(aPath, IStorage::TYPE_ALL, ppData, pSize))
				return true;
		}
		str_format(aPath, sizeof(aPath), "assets/audio/default/%s", pBaseName);
		if(Storage()->ReadFile(aPath, IStorage::TYPE_ALL, ppData, pSize))
			return true;
		str_format(aPath, sizeof(aPath), "audio/%s", pBaseName);
		return Storage()->ReadFile(aPath, IStorage::TYPE_ALL, ppData, pSize);
	};

	auto PlayBytes = [&](const void *pData, unsigned Size, const char *pName) {
		if(State.m_PreviewSample >= 0)
		{
			Sound()->Stop(State.m_PreviewSample);
			Sound()->UnloadSample(State.m_PreviewSample);
			State.m_PreviewSample = -1;
		}
		// the click of the button that started it would play over the sound
		m_SuppressMenuClickSound = true;
		State.m_PreviewSample = Sound()->LoadWVFromMem(pData, Size, false, pName);
		if(State.m_PreviewSample >= 0)
			Sound()->Play(CSounds::CHN_GUI, State.m_PreviewSample, 0, 1.0f);
	};

	auto StartDownload = [&](SCustomSound &Sound) {
		if(Sound.m_Cached || Sound.m_pRequest)
			return;
		char aUrl[256];
		str_format(aUrl, sizeof(aUrl), "%s/%s", g_Config.m_ScCustomSoundsUrl, Sound.m_File.c_str());
		Sound.m_Failed = false;
		Sound.m_pRequest = HttpGet(aUrl);
		Sound.m_pRequest->Timeout(CTimeout{8000, 15000, 512, 8});
		Sound.m_pRequest->MaxResponseSize(MAX_SOUND_BYTES);
		Sound.m_pRequest->LogProgress(HTTPLOG::FAILURE);
		Http()->Run(Sound.m_pRequest);
	};

	auto PreviewCustom = [&](SCustomSound &Sound) {
		if(!Sound.m_Cached)
		{
			State.m_PendingPreview = Sound.m_Id;
			m_SuppressMenuClickSound = true;
			StartDownload(Sound);
			return;
		}
		char aPath[IO_MAX_PATH_LENGTH];
		CachePath(Sound, aPath, sizeof(aPath));
		void *pData;
		unsigned Size;
		if(Storage()->ReadFile(aPath, IStorage::TYPE_SAVE, &pData, &Size))
		{
			PlayBytes(pData, Size, aPath);
			free(pData);
		}
	};

	auto PreviewPack = [&](const std::string &Pack, int SoundId) {
		if(SoundId < 0 || SoundId >= NUM_SOUNDS || g_pData->m_aSounds[SoundId].m_NumSounds <= 0)
			return;
		const char *pBaseName = SoundBaseName(g_pData->m_aSounds[SoundId].m_aSounds[0].m_pFilename);
		void *pData;
		unsigned Size;
		if(ReadPackFile(Pack, pBaseName, &pData, &Size))
		{
			PlayBytes(pData, Size, pBaseName);
			free(pData);
		}
	};

	auto PreviewSlot = [&](int Slot) {
		const SSlotEdit &Edit = State.m_aEdits[Slot];
		if(Edit.m_Kind == ESourceKind::CUSTOM)
		{
			if(SCustomSound *pSound = FindCustom(Edit.m_Value))
				PreviewCustom(*pSound);
		}
		else
			PreviewPack(Edit.m_Kind == ESourceKind::PACK ? Edit.m_Value : State.m_Base, gs_aAudioSlots[Slot].m_SoundId);
	};

	auto RefreshPacks = [&]() {
		std::vector<std::string> vNames;
		Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/audio", ListPacksCallback, &vNames);
		std::sort(vNames.begin(), vNames.end(), [](const std::string &A, const std::string &B) { return str_comp_nocase(A.c_str(), B.c_str()) < 0; });
		vNames.insert(vNames.begin(), "default");
		State.m_vPacks.clear();
		State.m_vPacks.resize(vNames.size());
		for(size_t i = 0; i < vNames.size(); i++)
			State.m_vPacks[i].m_Name = vNames[i];
	};

	auto RequestIndex = [&]() {
		if(State.m_pIndexRequest)
			return;
		if(!str_startswith(g_Config.m_ScCustomSoundsUrl, "https://"))
		{
			State.m_IndexState = EIndexState::FAILED;
			SetStatus(true, Localize("sc_custom_sounds_url is not a valid https:// address"));
			return;
		}
		State.m_IndexState = EIndexState::LOADING;
		State.m_pIndexRequest = HttpGet(g_Config.m_ScCustomSoundsUrl);
		State.m_pIndexRequest->Timeout(CTimeout{8000, 15000, 512, 8});
		State.m_pIndexRequest->MaxResponseSize(MAX_INDEX_BYTES);
		State.m_pIndexRequest->LogProgress(HTTPLOG::FAILURE);
		Http()->Run(State.m_pIndexRequest);
	};

	if(!State.m_Initialized)
	{
		State.m_Initialized = true;
		State.m_Base = g_Config.m_SndPack[0] != '\0' ? g_Config.m_SndPack : "default";
		gs_AudioExportName.Set("667_sounds");
		RefreshPacks();
		if(std::none_of(State.m_vPacks.begin(), State.m_vPacks.end(), [&](const SPackEntry &Pack) { return Pack.m_Name == State.m_Base; }))
			State.m_Base = "default";
	}
	if(State.m_IndexState == EIndexState::IDLE)
		RequestIndex();
	// no menu clicks on top of a sound that is still playing
	if(State.m_PreviewSample >= 0 && Sound()->IsPlaying(State.m_PreviewSample))
		m_SuppressMenuClickSound = true;

	// Index download
	if(State.m_pIndexRequest && State.m_pIndexRequest->Done())
	{
		std::shared_ptr<IHttpRequest> pRequest = std::move(State.m_pIndexRequest);
		json_value *pJson = pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200 ? pRequest->ResultJson() : nullptr;
		const json_value *pSounds = pJson ? json_object_get(pJson, "sounds") : nullptr;
		if(pSounds && pSounds->type == json_array)
		{
			std::vector<SCustomSound> vSounds(json_array_length(pSounds));
			size_t Count = 0;
			for(int i = 0; i < json_array_length(pSounds); i++)
			{
				const json_value *pEntry = json_array_get(pSounds, i);
				const json_value *pId = json_object_get(pEntry, "id");
				const json_value *pFile = json_object_get(pEntry, "file");
				const json_value *pName = json_object_get(pEntry, "name");
				if(pId->type != json_string || pFile->type != json_string || !CustomIdValid(json_string_get(pId)))
					continue;
				char aExpectedFile[80];
				str_format(aExpectedFile, sizeof(aExpectedFile), "%s.wv", json_string_get(pId));
				if(str_comp(json_string_get(pFile), aExpectedFile) != 0)
					continue;
				SCustomSound &Sound = vSounds[Count++];
				Sound.m_Id = json_string_get(pId);
				Sound.m_File = aExpectedFile;
				Sound.m_Name = pName->type == json_string ? json_string_get(pName) : Sound.m_Id;
				const json_value *pDuration = json_object_get(pEntry, "duration_ms");
				const json_value *pSize = json_object_get(pEntry, "size");
				Sound.m_DurationMs = pDuration->type == json_integer ? json_int_get(pDuration) : 0;
				Sound.m_Size = pSize->type == json_integer ? json_int_get(pSize) : 0;
				char aPath[IO_MAX_PATH_LENGTH];
				CachePath(Sound, aPath, sizeof(aPath));
				IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
				if(File)
				{
					// a size change means the server has a new version of the sound
					Sound.m_Cached = Sound.m_Size <= 0 || io_length(File) == Sound.m_Size;
					io_close(File);
				}
			}
			vSounds.resize(Count);
			std::sort(vSounds.begin(), vSounds.end(), [](const SCustomSound &A, const SCustomSound &B) { return str_comp_nocase(A.m_Name.c_str(), B.m_Name.c_str()) < 0; });
			State.m_vCustom = std::move(vSounds);
			State.m_IndexState = EIndexState::READY;
		}
		else
		{
			State.m_IndexState = EIndexState::FAILED;
			SetStatus(true, Localize("Could not load the custom sounds list."));
		}
		if(pJson)
			json_value_free(pJson);
	}

	// Sound downloads
	for(SCustomSound &Sound : State.m_vCustom)
	{
		if(!Sound.m_pRequest || !Sound.m_pRequest->Done())
			continue;
		std::shared_ptr<IHttpRequest> pRequest = std::move(Sound.m_pRequest);
		unsigned char *pData = nullptr;
		size_t Size = 0;
		if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200)
			pRequest->Result(&pData, &Size);
		bool Written = false;
		if(pData && IsWavPack(pData, Size))
		{
			char aPath[IO_MAX_PATH_LENGTH];
			CachePath(Sound, aPath, sizeof(aPath));
			Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
			Storage()->CreateFolder(CACHE_FOLDER, IStorage::TYPE_SAVE);
			IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(File)
			{
				Written = io_write(File, pData, Size) == Size;
				io_close(File);
			}
		}
		Sound.m_Cached = Written;
		Sound.m_Failed = !Written;
		if(!Written)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), Localize("Could not download %s."), Sound.m_Name.c_str());
			SetStatus(true, aBuf);
		}
		if(State.m_PendingPreview == Sound.m_Id)
		{
			State.m_PendingPreview.clear();
			if(Written)
				PreviewCustom(Sound);
		}
	}

	CUIRect Bottom;
	MainView.HSplitBottom(LineSize + 10.0f, &MainView, &Bottom);
	MainView.HSplitBottom(Margin, &MainView, nullptr);

	CUIRect SlotList, Workspace;
	MainView.VSplitLeft(210.0f, &SlotList, &Workspace);
	Workspace.VSplitLeft(Margin * 2.0f, nullptr, &Workspace);

	// Slot list: base pack, then the sounds by group
	SlotList.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 8.0f);
	SlotList.Margin(Margin, &SlotList);
	{
		static CScrollRegion s_SlotScroll;
		CScrollRegionParams Params;
		Params.m_ScrollUnit = 60.0f;
		Params.m_ScrollbarMargin = 2.0f;
		s_SlotScroll.Begin(&SlotList, &Params);
		SlotList.VSplitRight(6.0f, &SlotList, nullptr);

		static CButtonContainer s_BaseRow, s_BasePlay;
		static CButtonContainer s_aSlotRows[NUM_AUDIO_SLOTS], s_aSlotPlay[NUM_AUDIO_SLOTS];
		auto DoRow = [&](CButtonContainer *pRowButton, CButtonContainer *pPlayButton, int Index, const char *pName, const char *pSource, bool Modified) {
			CUIRect Row, Play, Label, Mark;
			SlotList.HSplitTop(24.0f, &Row, &SlotList);
			SlotList.HSplitTop(2.0f, nullptr, &SlotList);
			if(!s_SlotScroll.AddRect(Row))
				return;
			const bool Selected = State.m_Selected == Index;
			if(Selected || Ui()->MouseHovered(&Row))
				Row.Draw(Selected ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.2f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 5.0f);
			Row.VSplitLeft(22.0f, &Play, &Label);
			Label.VSplitRight(12.0f, &Label, &Mark);
			if(Ui()->DoButton_FontIcon(pPlayButton, FontIcon::PLAY, 0, &Play, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, true, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)))
			{
				if(Index < 0)
					PreviewPack(State.m_Base, SOUND_PLAYER_JUMP);
				else
					PreviewSlot(Index);
			}
			if(Ui()->DoButtonLogic(pRowButton, Selected, &Label, BUTTONFLAG_LEFT))
				State.m_Selected = Index;
			Label.VSplitLeft(4.0f, nullptr, &Label);
			CUIRect Name, Source;
			Label.HSplitMid(&Name, &Source, 0.0f);
			Ui()->DoLabel(&Name, pName, 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
			Ui()->DoLabel(&Source, pSource, 8.5f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			if(Modified)
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->TextColor(0.45f, 0.8f, 1.0f, 1.0f);
				Ui()->DoLabel(&Mark, FontIcon::CIRCLE, 6.0f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
		};

		DoRow(&s_BaseRow, &s_BasePlay, -1, Localize("Base pack"), State.m_Base.c_str(), false);
		const char *pLastGroup = nullptr;
		for(int i = 0; i < NUM_AUDIO_SLOTS; i++)
		{
			if(pLastGroup == nullptr || str_comp(pLastGroup, gs_aAudioSlots[i].m_pGroup) != 0)
			{
				pLastGroup = gs_aAudioSlots[i].m_pGroup;
				CUIRect Header;
				SlotList.HSplitTop(18.0f, &Header, &SlotList);
				if(s_SlotScroll.AddRect(Header))
				{
					Header.VSplitLeft(4.0f, nullptr, &Header);
					TextRender()->TextColor(0.45f, 0.8f, 1.0f, 0.85f);
					Ui()->DoLabel(&Header, Localize(pLastGroup), 10.0f, TEXTALIGN_BL);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
			const SSlotEdit &Edit = State.m_aEdits[i];
			std::string Source;
			if(Edit.m_Kind == ESourceKind::CUSTOM)
			{
				const SCustomSound *pSound = FindCustom(Edit.m_Value);
				Source = pSound ? pSound->m_Name : Edit.m_Value;
			}
			else
				Source = Edit.m_Kind == ESourceKind::PACK ? Edit.m_Value : State.m_Base;
			DoRow(&s_aSlotRows[i], &s_aSlotPlay[i], i, Localize(gs_aAudioSlots[i].m_pName), Source.c_str(), Edit.m_Kind != ESourceKind::BASE);
		}
		s_SlotScroll.End();
	}

	// Selected slot
	CUIRect Header, Library;
	Workspace.HSplitTop(LineSize * 2.0f + Margin * 3.0f, &Header, &Library);
	Library.HSplitTop(Margin * 2.0f, nullptr, &Library);
	Header.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 8.0f);
	Header.Margin(Margin * 1.5f, &Header);
	{
		const bool BaseSelected = State.m_Selected < 0;
		CUIRect Title, Hint, ResetButton;
		Header.HSplitTop(LineSize, &Title, &Hint);
		Title.VSplitRight(100.0f, &Title, &ResetButton);
		Ui()->DoLabel(&Title, BaseSelected ? Localize("Base pack") : Localize(gs_aAudioSlots[State.m_Selected].m_pName), 15.0f, TEXTALIGN_ML);
		if(!BaseSelected)
		{
			static CButtonContainer s_ResetSlot;
			if(DoButton_Menu(&s_ResetSlot, Localize("Reset"), 0, &ResetButton) && State.m_aEdits[State.m_Selected].m_Kind != ESourceKind::BASE)
				State.m_aEdits[State.m_Selected] = SSlotEdit();
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.55f);
		Ui()->DoLabel(&Hint, BaseSelected ? Localize("Every sound starts from this pack. Pick one under Installed packs.") : Localize("Pick a custom sound or a pack's version of this sound. Every file of the sound uses it."), 10.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	// Library: custom sounds or installed packs
	Library.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 8.0f);
	Library.Margin(Margin, &Library);
	{
		CUIRect TabBar, CustomTab, PacksTab, Search, Refresh;
		Library.HSplitTop(LineSize, &TabBar, &Library);
		Library.HSplitTop(Margin, nullptr, &Library);
		TabBar.VSplitRight(LineSize, &TabBar, &Refresh);
		TabBar.VSplitRight(Margin, &TabBar, nullptr);
		TabBar.VSplitRight(std::min(180.0f, TabBar.w * 0.4f), &TabBar, &Search);
		TabBar.VSplitRight(Margin, &TabBar, nullptr);
		TabBar.VSplitMid(&CustomTab, &PacksTab);

		char aCustomTitle[64];
		char aPacksTitle[64];
		str_format(aCustomTitle, sizeof(aCustomTitle), "%s (%d)", Localize("Custom sounds"), (int)State.m_vCustom.size());
		str_format(aPacksTitle, sizeof(aPacksTitle), "%s (%d)", Localize("Installed packs"), (int)State.m_vPacks.size());
		static CButtonContainer s_CustomTab, s_PacksTab, s_RefreshButton;
		if(DoButton_MenuTab(&s_CustomTab, aCustomTitle, State.m_LibraryTab == 0, &CustomTab, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
			State.m_LibraryTab = 0;
		if(DoButton_MenuTab(&s_PacksTab, aPacksTitle, State.m_LibraryTab == 1, &PacksTab, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
			State.m_LibraryTab = 1;
		Ui()->DoEditBox_Search(&gs_AudioSearch, &Search, 12.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		if(Ui()->DoButton_FontIcon(&s_RefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &Refresh, BUTTONFLAG_LEFT))
		{
			RefreshPacks();
			if(State.m_IndexState != EIndexState::LOADING)
			{
				State.m_IndexState = EIndexState::IDLE;
				RequestIndex();
			}
		}

		const char *pFilter = gs_AudioSearch.GetString();
		const bool BaseSelected = State.m_Selected < 0;
		const int SlotSoundId = BaseSelected ? SOUND_PLAYER_JUMP : gs_aAudioSlots[State.m_Selected].m_SoundId;

		static CScrollRegion s_LibraryScroll;
		CScrollRegionParams Params;
		Params.m_ScrollUnit = 60.0f;
		Params.m_ScrollbarMargin = 2.0f;
		s_LibraryScroll.Begin(&Library, &Params);
		Library.VSplitRight(8.0f, &Library, nullptr);

		const float CellW = 150.0f;
		const float CellH = 34.0f;
		const int Columns = std::max(1, (int)((Library.w + Margin) / (CellW + Margin)));
		const float RealW = (Library.w - (Columns - 1) * Margin) / Columns;
		int Shown = 0;
		CUIRect Row;
		auto NextCell = [&](CUIRect &Cell) {
			if(Shown % Columns == 0)
			{
				if(Shown > 0)
					Library.HSplitTop(Margin, nullptr, &Library);
				Library.HSplitTop(CellH, &Row, &Library);
			}
			Row.VSplitLeft(RealW, &Cell, &Row);
			Row.VSplitLeft(Margin, nullptr, &Row);
			Shown++;
		};
		auto DoCell = [&](CUIRect Cell, CButtonContainer *pPick, CButtonContainer *pPlay, const char *pName, const char *pDetail, bool Active, bool Busy) {
			const bool Hovered = Ui()->MouseHovered(&Cell);
			Cell.Draw(Active ? ColorRGBA(0.45f, 0.8f, 1.0f, 0.25f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 0.12f : 0.05f), IGraphics::CORNER_ALL, 6.0f);
			CUIRect Play, Label;
			Cell.VSplitLeft(28.0f, &Play, &Label);
			Play.Margin(3.0f, &Play);
			const bool PlayClicked = Ui()->DoButton_FontIcon(pPlay, Busy ? FontIcon::ARROW_ROTATE_RIGHT : FontIcon::PLAY, 0, &Play, BUTTONFLAG_LEFT);
			const bool Picked = Ui()->DoButtonLogic(pPick, Active, &Label, BUTTONFLAG_LEFT);
			CUIRect Name, Detail;
			Label.HSplitMid(&Name, &Detail, 0.0f);
			Ui()->DoLabel(&Name, pName, 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
			Ui()->DoLabel(&Detail, pDetail, 8.5f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			return std::pair<bool, bool>(PlayClicked, Picked);
		};

		if(State.m_LibraryTab == 0)
		{
			if(State.m_IndexState == EIndexState::LOADING || State.m_IndexState == EIndexState::FAILED)
			{
				CUIRect Label;
				Library.HSplitTop(LineSize * 2.0f, &Label, &Library);
				s_LibraryScroll.AddRect(Label);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
				Ui()->DoLabel(&Label, State.m_IndexState == EIndexState::LOADING ? Localize("Loading custom sounds...") : Localize("Custom sounds are not available right now."), 12.0f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			for(SCustomSound &Sound : State.m_vCustom)
			{
				if(pFilter[0] != '\0' && !str_find_nocase(Sound.m_Name.c_str(), pFilter))
					continue;
				CUIRect Cell;
				NextCell(Cell);
				if(!s_LibraryScroll.AddRect(Cell))
					continue;
				const SSlotEdit *pEdit = BaseSelected ? nullptr : &State.m_aEdits[State.m_Selected];
				const bool Active = pEdit && pEdit->m_Kind == ESourceKind::CUSTOM && pEdit->m_Value == Sound.m_Id;
				char aDetail[64];
				str_format(aDetail, sizeof(aDetail), "%.2f s%s", Sound.m_DurationMs / 1000.0f, Sound.m_Failed ? " · failed" : "");
				const auto [PlayClicked, Picked] = DoCell(Cell, &Sound.m_PickButton, &Sound.m_PlayButton, Sound.m_Name.c_str(), aDetail, Active, Sound.m_pRequest != nullptr);
				if(PlayClicked)
					PreviewCustom(Sound);
				if(Picked)
				{
					if(BaseSelected)
						SetStatus(true, Localize("Select a sound on the left first."));
					else
					{
						State.m_aEdits[State.m_Selected] = {ESourceKind::CUSTOM, Sound.m_Id};
						StartDownload(Sound);
						PreviewCustom(Sound);
					}
				}
			}
		}
		else
		{
			for(SPackEntry &Pack : State.m_vPacks)
			{
				if(pFilter[0] != '\0' && !str_find_nocase(Pack.m_Name.c_str(), pFilter))
					continue;
				CUIRect Cell;
				NextCell(Cell);
				if(!s_LibraryScroll.AddRect(Cell))
					continue;
				bool Active;
				if(BaseSelected)
					Active = Pack.m_Name == State.m_Base;
				else
				{
					const SSlotEdit &Edit = State.m_aEdits[State.m_Selected];
					Active = Edit.m_Kind == ESourceKind::PACK ? Edit.m_Value == Pack.m_Name : (Edit.m_Kind == ESourceKind::BASE && Pack.m_Name == State.m_Base);
				}
				const char *pDetail = Pack.m_Name == State.m_Base ? Localize("base pack") : Localize("sound pack");
				const auto [PlayClicked, Picked] = DoCell(Cell, &Pack.m_PickButton, &Pack.m_PlayButton, Pack.m_Name.c_str(), pDetail, Active, false);
				if(PlayClicked)
					PreviewPack(Pack.m_Name, SlotSoundId);
				if(Picked)
				{
					if(BaseSelected)
					{
						State.m_Base = Pack.m_Name;
						for(SSlotEdit &Edit : State.m_aEdits)
							if(Edit.m_Kind == ESourceKind::PACK && Edit.m_Value == Pack.m_Name)
								Edit = SSlotEdit();
					}
					else
						State.m_aEdits[State.m_Selected] = Pack.m_Name == State.m_Base ? SSlotEdit() : SSlotEdit{ESourceKind::PACK, Pack.m_Name};
					PreviewPack(Pack.m_Name, SlotSoundId);
				}
			}
		}
		s_LibraryScroll.End();
	}

	// Save
	CUIRect NameBox, SaveButton, ApplyButton, ResetButton, Status;
	Bottom.HMargin(5.0f, &Bottom);
	Bottom.VSplitLeft(170.0f, &NameBox, &Bottom);
	Bottom.VSplitLeft(Margin, nullptr, &Bottom);
	Bottom.VSplitLeft(90.0f, &SaveButton, &Bottom);
	Bottom.VSplitLeft(Margin, nullptr, &Bottom);
	Bottom.VSplitLeft(120.0f, &ApplyButton, &Bottom);
	Bottom.VSplitLeft(Margin, nullptr, &Bottom);
	Bottom.VSplitLeft(90.0f, &ResetButton, &Bottom);
	Bottom.VSplitLeft(Margin * 2.0f, nullptr, &Status);

	Ui()->DoEditBox(&gs_AudioExportName, &NameBox, 12.0f);
	const char *pExportName = gs_AudioExportName.GetString();
	char aPackFolder[IO_MAX_PATH_LENGTH];
	str_format(aPackFolder, sizeof(aPackFolder), "assets/audio/%s", pExportName);
	const bool Exists = AudioNameValid(pExportName) && Storage()->FolderExists(aPackFolder, IStorage::TYPE_SAVE);

	auto Save = [&]() {
		if(!AudioNameValid(pExportName))
		{
			SetStatus(true, Localize("Use only letters, numbers, spaces, - and _ in the name."));
			return false;
		}
		// every custom sound in use has to be on disk first
		int Missing = 0;
		for(const SSlotEdit &Edit : State.m_aEdits)
		{
			if(Edit.m_Kind != ESourceKind::CUSTOM)
				continue;
			SCustomSound *pSound = FindCustom(Edit.m_Value);
			if(pSound == nullptr)
			{
				SetStatus(true, Localize("A chosen custom sound is no longer available. Pick another one."));
				return false;
			}
			if(!pSound->m_Cached)
			{
				StartDownload(*pSound);
				Missing++;
			}
		}
		if(Missing > 0)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Localize("Downloading %d sound(s), save again in a moment."), Missing);
			SetStatus(true, aBuf);
			return false;
		}

		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("assets/audio", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aPackFolder, IStorage::TYPE_SAVE);

		// A pack has to hold every game sound, a missing file plays as silence
		int Written = 0;
		int Failed = 0;
		for(int SoundId = 0; SoundId < NUM_SOUNDS; SoundId++)
		{
			const SSlotEdit *pEdit = nullptr;
			for(int Slot = 0; Slot < NUM_AUDIO_SLOTS; Slot++)
				if(gs_aAudioSlots[Slot].m_SoundId == SoundId && State.m_aEdits[Slot].m_Kind != ESourceKind::BASE)
					pEdit = &State.m_aEdits[Slot];

			void *pCustomData = nullptr;
			unsigned CustomSize = 0;
			if(pEdit && pEdit->m_Kind == ESourceKind::CUSTOM)
			{
				char aPath[IO_MAX_PATH_LENGTH];
				CachePath(*FindCustom(pEdit->m_Value), aPath, sizeof(aPath));
				if(!Storage()->ReadFile(aPath, IStorage::TYPE_SAVE, &pCustomData, &CustomSize))
					pCustomData = nullptr;
			}

			const CDataSoundset &Set = g_pData->m_aSounds[SoundId];
			for(int i = 0; i < Set.m_NumSounds; i++)
			{
				const char *pBaseName = SoundBaseName(Set.m_aSounds[i].m_pFilename);
				void *pData = nullptr;
				unsigned Size = 0;
				bool Owned = false;
				if(pCustomData)
				{
					pData = pCustomData;
					Size = CustomSize;
				}
				else
				{
					const std::string &Pack = pEdit && pEdit->m_Kind == ESourceKind::PACK ? pEdit->m_Value : State.m_Base;
					Owned = ReadPackFile(Pack, pBaseName, &pData, &Size);
					if(!Owned)
					{
						Failed++;
						continue;
					}
				}
				char aOutPath[IO_MAX_PATH_LENGTH];
				str_format(aOutPath, sizeof(aOutPath), "%s/%s", aPackFolder, pBaseName);
				IOHANDLE File = Storage()->OpenFile(aOutPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
				if(File && io_write(File, pData, Size) == Size)
					Written++;
				else
					Failed++;
				if(File)
					io_close(File);
				if(Owned)
					free(pData);
			}
			free(pCustomData);
		}

		if(Failed > 0)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Localize("Saved with %d missing file(s)."), Failed);
			SetStatus(true, aBuf);
		}
		else
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), Localize("Saved %d files to %s"), Written, aPackFolder);
			SetStatus(false, aBuf);
		}
		if(std::none_of(State.m_vPacks.begin(), State.m_vPacks.end(), [&](const SPackEntry &Pack) { return Pack.m_Name == pExportName; }))
		{
			SPackEntry Entry;
			Entry.m_Name = pExportName;
			State.m_vPacks.push_back(std::move(Entry));
			std::sort(State.m_vPacks.begin() + 1, State.m_vPacks.end(), [](const SPackEntry &A, const SPackEntry &B) { return str_comp_nocase(A.m_Name.c_str(), B.m_Name.c_str()) < 0; });
		}
		if(std::none_of(m_vAudioPackList.begin(), m_vAudioPackList.end(), [&](const SCustomAudioPack &Item) { return str_comp(Item.m_aName, pExportName) == 0; }))
		{
			SCustomAudioPack Item;
			str_copy(Item.m_aName, pExportName);
			m_vAudioPackList.push_back(Item);
		}
		return Failed == 0;
	};

	static CButtonContainer s_SaveButton, s_ApplyButton, s_ResetButton;
	if(DoButton_Menu(&s_SaveButton, Exists ? Localize("Overwrite") : Localize("Save"), 0, &SaveButton))
	{
		if(Save() && str_comp(g_Config.m_SndPack, pExportName) == 0)
			GameClient()->m_Sounds.Clear();
	}
	if(DoButton_Menu(&s_ApplyButton, Localize("Save & Apply"), 0, &ApplyButton) && Save())
	{
		str_copy(g_Config.m_SndPack, pExportName);
		GameClient()->m_Sounds.Clear();
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), Localize("Saved and applied %s"), pExportName);
		SetStatus(false, aBuf);
	}
	if(DoButton_Menu(&s_ResetButton, Localize("Reset all"), 0, &ResetButton))
	{
		for(SSlotEdit &Edit : State.m_aEdits)
			Edit = SSlotEdit();
		SetStatus(false, "");
	}

	if(State.m_aStatus[0])
	{
		if(State.m_StatusError)
			TextRender()->TextColor(1.0f, 0.45f, 0.45f, 1.0f);
		else
			TextRender()->TextColor(0.5f, 0.9f, 0.55f, 1.0f);
		Ui()->DoLabel(&Status, State.m_aStatus, 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}
