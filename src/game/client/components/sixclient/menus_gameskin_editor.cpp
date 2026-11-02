/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-66a34bad */

#include <base/color.h>
#include <base/io.h>
#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/mapimages.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

// Game skin and entities editors: one sheet (a grid of cells) is the base, named parts of it can be
// taken from other sheets of the same kind and tinted, and the result is saved as a new asset.

namespace
{
	struct SCellRect
	{
		int m_X, m_Y, m_W, m_H;
	};

	struct SSheetPart
	{
		const char *m_pName;
		std::vector<SCellRect> m_vRects;
	};

	const std::vector<SSheetPart> &GameSkinParts()
	{
		static const std::vector<SSheetPart> s_vParts = {
			{"Hook", {{2, 0, 3, 1}}},
			{"Cursors", {{0, 4, 2, 2}, {0, 0, 2, 2}, {0, 6, 2, 2}, {0, 8, 2, 2}, {0, 10, 2, 2}, {0, 12, 2, 2}}},
			{"Hammer", {{2, 1, 4, 3}}},
			{"Pistol", {{2, 4, 4, 2}, {6, 4, 14, 2}}},
			{"Shotgun", {{2, 6, 8, 2}, {10, 6, 14, 2}}},
			{"Grenade", {{2, 8, 7, 2}, {10, 8, 2, 2}}},
			{"Laser", {{2, 12, 7, 3}, {10, 12, 2, 2}}},
			{"Ninja", {{2, 10, 8, 2}, {25, 0, 7, 12}}},
			{"Health & Armor", {{21, 0, 4, 4}}},
			{"Pickups", {{10, 2, 4, 2}, {15, 2, 6, 2}, {10, 10, 2, 2}, {6, 2, 2, 2}}},
			{"Flags", {{12, 8, 8, 8}}},
			{"Particles", {{6, 0, 9, 2}}},
			{"Stars", {{15, 0, 6, 2}}},
		};
		return s_vParts;
	}

	// Entities sheets are 16x16 tiles, tile i sits at (i % 16, i / 16); entities (spawns, pickups,
	// lasers, draggers) are stored from ENTITY_OFFSET on.
	SSheetPart TilePart(const char *pName, std::initializer_list<int> Tiles)
	{
		SSheetPart Part{pName, {}};
		for(int Tile : Tiles)
			Part.m_vRects.push_back({Tile % 16, Tile / 16, 1, 1});
		return Part;
	}

	SSheetPart TileRangePart(const char *pName, int First, int Last)
	{
		SSheetPart Part{pName, {}};
		for(int Tile = First; Tile <= Last; Tile++)
			Part.m_vRects.push_back({Tile % 16, Tile / 16, 1, 1});
		return Part;
	}

	const std::vector<SSheetPart> &EntitiesParts()
	{
		static const std::vector<SSheetPart> s_vParts = [] {
			std::vector<SSheetPart> vParts = {
				TilePart("Hookable", {TILE_SOLID}),
				TilePart("Unhookable", {TILE_NOHOOK}),
				TilePart("Death", {TILE_DEATH}),
				TilePart("Freeze", {TILE_FREEZE, TILE_DFREEZE}),
				TilePart("Unfreeze", {TILE_UNFREEZE, TILE_DUNFREEZE}),
				TilePart("Hook through", {TILE_THROUGH_CUT, TILE_THROUGH, TILE_THROUGH_ALL, TILE_THROUGH_DIR}),
				TilePart("Laser blocker", {TILE_NOLASER}),
				TilePart("Jumps", {TILE_JUMP, TILE_WALLJUMP, TILE_REFILL_JUMPS}),
				TilePart("Teleporters", {TILE_TELEINEVIL, TILE_TELEINWEAPON, TILE_TELEINHOOK, TILE_TELEIN, TILE_TELEOUT, TILE_TELECHECK, TILE_TELECHECKOUT, TILE_TELECHECKIN, TILE_TELECHECKINEVIL}),
				TilePart("Speedup", {TILE_SPEED_BOOST_OLD}),
				TilePart("Start & finish", {TILE_START, TILE_FINISH}),
				TileRangePart("Time checkpoints", TILE_TIME_CHECKPOINT_FIRST, TILE_TIME_CHECKPOINT_LAST),
				TilePart("Stoppers", {TILE_STOP, TILE_STOPS, TILE_STOPA}),
				TilePart("Switches", {TILE_SWITCHTIMEDOPEN, TILE_SWITCHTIMEDCLOSE, TILE_SWITCHOPEN, TILE_SWITCHCLOSE}),
				TilePart("Toggles", {TILE_EHOOK_ENABLE, TILE_EHOOK_DISABLE, TILE_HIT_ENABLE, TILE_HIT_DISABLE, TILE_SOLO_ENABLE, TILE_OLDLASER, TILE_NPC, TILE_EHOOK, TILE_NOHIT, TILE_NPH, TILE_UNLOCK_TEAM, TILE_ADD_TIME, TILE_NPC_DISABLE, TILE_UNLIMITED_JUMPS_DISABLE, TILE_JETPACK_DISABLE, TILE_NPH_DISABLE, TILE_SUBTRACT_TIME, TILE_TELE_GUN_ENABLE, TILE_TELE_GUN_DISABLE, TILE_ALLOW_TELE_GUN, TILE_ALLOW_BLUE_TELE_GUN, TILE_NPC_ENABLE, TILE_UNLIMITED_JUMPS_ENABLE, TILE_JETPACK_ENABLE, TILE_NPH_ENABLE, TILE_TELE_GRENADE_ENABLE, TILE_TELE_GRENADE_DISABLE, TILE_TELE_LASER_ENABLE, TILE_TELE_LASER_DISABLE}),
				TilePart("Tune zone", {TILE_TUNE}),
				TileRangePart("Spawns & flags", ENTITY_OFFSET + ENTITY_SPAWN, ENTITY_OFFSET + ENTITY_FLAGSTAND_BLUE),
			};
			SSheetPart Pickups = TileRangePart("Pickups", ENTITY_OFFSET + ENTITY_ARMOR_1, ENTITY_OFFSET + ENTITY_WEAPON_LASER);
			for(int Entity = ENTITY_ARMOR_SHOTGUN; Entity <= ENTITY_ARMOR_LASER; Entity++)
				Pickups.m_vRects.push_back({(ENTITY_OFFSET + Entity) % 16, (ENTITY_OFFSET + Entity) / 16, 1, 1});
			vParts.push_back(Pickups);
			SSheetPart Lasers = TileRangePart("Lasers & plasma", ENTITY_OFFSET + ENTITY_LASER_FAST_CCW, ENTITY_OFFSET + ENTITY_LASER_O_FAST);
			for(int Entity = ENTITY_PLASMAE; Entity <= ENTITY_CRAZY_SHOTGUN; Entity++)
				Lasers.m_vRects.push_back({(ENTITY_OFFSET + Entity) % 16, (ENTITY_OFFSET + Entity) / 16, 1, 1});
			vParts.push_back(Lasers);
			SSheetPart Draggers = TileRangePart("Draggers & door", ENTITY_OFFSET + ENTITY_DRAGGER_WEAK, ENTITY_OFFSET + ENTITY_DRAGGER_STRONG_NW);
			Draggers.m_vRects.push_back({(ENTITY_OFFSET + ENTITY_DOOR) % 16, (ENTITY_OFFSET + ENTITY_DOOR) / 16, 1, 1});
			vParts.push_back(Draggers);
			return vParts;
		}();
		return s_vParts;
	}

	struct SPartEdit
	{
		std::string m_Source;
		int m_Hue = 0;
		int m_Saturation = 100;
		int m_Brightness = 100;

		bool Modified() const { return !m_Source.empty() || m_Hue != 0 || m_Saturation != 100 || m_Brightness != 100; }
		bool HasColorChange() const { return m_Hue != 0 || m_Saturation != 100 || m_Brightness != 100; }
	};

	struct SSkinEntry
	{
		std::string m_Name;
		IGraphics::CTextureHandle m_Texture;
		bool m_TextureLoaded = false;
		CButtonContainer m_Button;
	};

	struct SSheetEditorState
	{
		bool m_Initialized = false;
		std::vector<SSkinEntry> m_vSkins;
		std::map<std::string, CImageInfo> m_Images;
		std::string m_Base = "default";
		std::vector<SPartEdit> m_vEdits;
		int m_Selected = -1;
		bool m_Dirty = true;
		CImageInfo m_Composed;
		IGraphics::CTextureHandle m_PreviewTexture;
		char m_aStatus[256] = "";
		bool m_StatusError = false;

		CLineInputBuffered<64> m_ExportName;
		CButtonContainer m_BaseButton;
		std::vector<CButtonContainer> m_vPartButtons;
		CScrollRegion m_PartScroll;
		CButtonContainer m_SheetButton;
		int m_HueId = 0, m_SatId = 0, m_BrightId = 0;
		CButtonContainer m_ResetPartButton;
		CButtonContainer m_RefreshButton;
		CScrollRegion m_GalleryScroll;
		CButtonContainer m_SaveButton, m_ApplyButton, m_ResetButton;
	};

	// What makes the game skin and the entities editor different
	struct SSheetEditorConfig
	{
		int m_GridX;
		int m_GridY;
		const std::vector<SSheetPart> *m_pParts;
		const char *m_pFolder; // assets/<folder>
		bool (*m_pfnResolve)(IStorage *pStorage, const std::string &Name, char *pOut, int OutSize);
		const char *m_pDefaultExportName;
		const char *m_pGalleryTitle;
		const char *m_pBaseLabel;
		const char *m_pBaseHint;
		const char *m_pLoadError;
		const char *m_pPickError;
	};

	SSheetEditorState gs_GameSkinEditor;
	SSheetEditorState gs_EntitiesEditor;
	int gs_EditorSubTab = 0;

	enum
	{
		EDITOR_TAB_GAMESKIN = 0,
		EDITOR_TAB_DEMO_TEE,
		EDITOR_TAB_ENTITIES,
		EDITOR_TAB_AUDIO,
	};

	int ListSheetsCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		auto *pNames = static_cast<std::vector<std::string> *>(pUser);
		if(pName[0] == '.')
			return 0;
		std::string Name;
		if(IsDir)
			Name = pName;
		else if(str_endswith(pName, ".png"))
			Name.assign(pName, str_length(pName) - 4);
		else
			return 0;
		if(Name == "default" || std::find(pNames->begin(), pNames->end(), Name) != pNames->end())
			return 0;
		pNames->push_back(Name);
		return 0;
	}

	bool ExportNameValid(const char *pName)
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

	void RgbToHsv(float r, float g, float b, float &h, float &s, float &v)
	{
		const float Max = std::max(r, std::max(g, b));
		const float Min = std::min(r, std::min(g, b));
		const float Delta = Max - Min;
		v = Max;
		s = Max > 0.0f ? Delta / Max : 0.0f;
		if(Delta <= 0.0f)
			h = 0.0f;
		else if(Max == r)
			h = std::fmod((g - b) / Delta, 6.0f);
		else if(Max == g)
			h = (b - r) / Delta + 2.0f;
		else
			h = (r - g) / Delta + 4.0f;
		h /= 6.0f;
		if(h < 0.0f)
			h += 1.0f;
	}

	void HsvToRgb(float h, float s, float v, float &r, float &g, float &b)
	{
		const float H = h * 6.0f;
		const float C = v * s;
		const float X = C * (1.0f - std::fabs(std::fmod(H, 2.0f) - 1.0f));
		const float m = v - C;
		float R = 0, G = 0, B = 0;
		if(H < 1.0f)
			R = C, G = X;
		else if(H < 2.0f)
			R = X, G = C;
		else if(H < 3.0f)
			G = C, B = X;
		else if(H < 4.0f)
			G = X, B = C;
		else if(H < 5.0f)
			R = X, B = C;
		else
			R = C, B = X;
		r = R + m;
		g = G + m;
		b = B + m;
	}

	void CopyRegion(CImageInfo &Dst, const CImageInfo &Src, const SCellRect &Rect, int GridX, int GridY)
	{
		const int DstCellW = Dst.m_Width / GridX;
		const int DstCellH = Dst.m_Height / GridY;
		const int SrcCellW = Src.m_Width / GridX;
		const int SrcCellH = Src.m_Height / GridY;
		const int DstX = Rect.m_X * DstCellW, DstY = Rect.m_Y * DstCellH;
		const int DstW = Rect.m_W * DstCellW, DstH = Rect.m_H * DstCellH;
		const int SrcX = Rect.m_X * SrcCellW, SrcY = Rect.m_Y * SrcCellH;
		const int SrcW = Rect.m_W * SrcCellW, SrcH = Rect.m_H * SrcCellH;
		if(DstW <= 0 || DstH <= 0 || SrcW <= 0 || SrcH <= 0)
			return;
		for(int y = 0; y < DstH; y++)
		{
			const int SampleY = SrcY + (int)((int64_t)y * SrcH / DstH);
			for(int x = 0; x < DstW; x++)
			{
				const int SampleX = SrcX + (int)((int64_t)x * SrcW / DstW);
				const size_t DstOff = ((size_t)(DstY + y) * Dst.m_Width + DstX + x) * 4;
				const size_t SrcOff = ((size_t)SampleY * Src.m_Width + SampleX) * 4;
				mem_copy(&Dst.m_pData[DstOff], &Src.m_pData[SrcOff], 4);
			}
		}
	}

	void AdjustRegion(CImageInfo &Image, const SCellRect &Rect, const SPartEdit &Edit, int GridX, int GridY)
	{
		const int CellW = Image.m_Width / GridX;
		const int CellH = Image.m_Height / GridY;
		const float HueShift = Edit.m_Hue / 360.0f;
		const float Sat = Edit.m_Saturation / 100.0f;
		const float Bright = Edit.m_Brightness / 100.0f;
		for(int y = Rect.m_Y * CellH; y < (Rect.m_Y + Rect.m_H) * CellH; y++)
		{
			for(int x = Rect.m_X * CellW; x < (Rect.m_X + Rect.m_W) * CellW; x++)
			{
				uint8_t *pPixel = &Image.m_pData[((size_t)y * Image.m_Width + x) * 4];
				if(pPixel[3] == 0)
					continue;
				float h, s, v;
				RgbToHsv(pPixel[0] / 255.0f, pPixel[1] / 255.0f, pPixel[2] / 255.0f, h, s, v);
				h = std::fmod(h + HueShift + 1.0f, 1.0f);
				s = std::clamp(s * Sat, 0.0f, 1.0f);
				v = std::clamp(v * Bright, 0.0f, 1.0f);
				float r, g, b;
				HsvToRgb(h, s, v, r, g, b);
				pPixel[0] = (uint8_t)round_to_int(r * 255.0f);
				pPixel[1] = (uint8_t)round_to_int(g * 255.0f);
				pPixel[2] = (uint8_t)round_to_int(b * 255.0f);
			}
		}
	}

	void DrawSubset(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const CUIRect &Rect, const SCellRect &Cells, int GridX, int GridY, float Alpha = 1.0f)
	{
		if(!Texture.IsValid())
			return;
		const float Aspect = (float)Cells.m_W / Cells.m_H;
		float W = Rect.w, H = Rect.w / Aspect;
		if(H > Rect.h)
		{
			H = Rect.h;
			W = H * Aspect;
		}
		pGraphics->WrapClamp();
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		pGraphics->QuadsSetSubset((float)Cells.m_X / GridX, (float)Cells.m_Y / GridY, (float)(Cells.m_X + Cells.m_W) / GridX, (float)(Cells.m_Y + Cells.m_H) / GridY);
		const IGraphics::CQuadItem Quad(Rect.x + (Rect.w - W) / 2.0f, Rect.y + (Rect.h - H) / 2.0f, W, H);
		pGraphics->QuadsDrawTL(&Quad, 1);
		pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
	}

	CUIRect CellsToRect(const CUIRect &Sheet, const SCellRect &Cells, int GridX, int GridY)
	{
		CUIRect Rect;
		Rect.x = Sheet.x + Sheet.w * Cells.m_X / GridX;
		Rect.y = Sheet.y + Sheet.h * Cells.m_Y / GridY;
		Rect.w = Sheet.w * Cells.m_W / GridX;
		Rect.h = Sheet.h * Cells.m_H / GridY;
		return Rect;
	}

	void DrawCheckerboard(IGraphics *pGraphics, const CUIRect &Rect, float Size)
	{
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		for(float y = 0.0f; y < Rect.h; y += Size)
		{
			for(float x = 0.0f; x < Rect.w; x += Size)
			{
				const bool Dark = ((int)(x / Size) + (int)(y / Size)) % 2 == 0;
				pGraphics->SetColor(Dark ? 0.16f : 0.22f, Dark ? 0.16f : 0.22f, Dark ? 0.18f : 0.24f, 1.0f);
				const IGraphics::CQuadItem Quad(Rect.x + x, Rect.y + y, std::min(Size, Rect.w - x), std::min(Size, Rect.h - y));
				pGraphics->QuadsDrawTL(&Quad, 1);
			}
		}
		pGraphics->QuadsEnd();
	}
}

static bool ResolveGameSkinPath(IStorage *pStorage, const std::string &Name, char *pOut, int OutSize)
{
	if(Name == "default")
	{
		str_copy(pOut, "game.png", OutSize);
		return pStorage->FileExists(pOut, IStorage::TYPE_ALL);
	}
	str_format(pOut, OutSize, "assets/game/%s.png", Name.c_str());
	if(pStorage->FileExists(pOut, IStorage::TYPE_ALL))
		return true;
	str_format(pOut, OutSize, "assets/game/%s/game.png", Name.c_str());
	return pStorage->FileExists(pOut, IStorage::TYPE_ALL);
}

// The DDNet sheet of an entities pack, the one DDNet servers use
static bool ResolveEntitiesPath(IStorage *pStorage, const std::string &Name, char *pOut, int OutSize)
{
	if(Name == "default")
	{
		str_copy(pOut, "editor/entities_clear/ddnet.png", OutSize);
		return pStorage->FileExists(pOut, IStorage::TYPE_ALL);
	}
	str_format(pOut, OutSize, "assets/entities/%s/ddnet.png", Name.c_str());
	if(pStorage->FileExists(pOut, IStorage::TYPE_ALL))
		return true;
	str_format(pOut, OutSize, "assets/entities/%s.png", Name.c_str());
	return pStorage->FileExists(pOut, IStorage::TYPE_ALL);
}

void CMenus::RenderSettingsSixClientAssetsEditor(CUIRect MainView)
{
	static CButtonContainer s_GameSkinTab, s_EntitiesTab, s_AudioTab, s_DemoTeeTab;
	CUIRect TabBar, GameSkinButton, EntitiesButton, AudioButton, DemoTeeButton;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / 4.0f;
	TabBar.VSplitLeft(TabWidth, &GameSkinButton, &TabBar);
	TabBar.VSplitLeft(TabWidth, &EntitiesButton, &TabBar);
	TabBar.VSplitLeft(TabWidth, &AudioButton, &DemoTeeButton);
	if(DoButton_MenuTab(&s_GameSkinTab, Localize("Game Skin"), gs_EditorSubTab == EDITOR_TAB_GAMESKIN, &GameSkinButton, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 4.0f))
		gs_EditorSubTab = EDITOR_TAB_GAMESKIN;
	if(DoButton_MenuTab(&s_EntitiesTab, Localize("Entities"), gs_EditorSubTab == EDITOR_TAB_ENTITIES, &EntitiesButton, IGraphics::CORNER_NONE, nullptr, nullptr, nullptr, nullptr, 4.0f))
		gs_EditorSubTab = EDITOR_TAB_ENTITIES;
	if(DoButton_MenuTab(&s_AudioTab, Localize("Audio"), gs_EditorSubTab == EDITOR_TAB_AUDIO, &AudioButton, IGraphics::CORNER_NONE, nullptr, nullptr, nullptr, nullptr, 4.0f))
		gs_EditorSubTab = EDITOR_TAB_AUDIO;
	if(DoButton_MenuTab(&s_DemoTeeTab, Localize("Demo Tee"), gs_EditorSubTab == EDITOR_TAB_DEMO_TEE, &DemoTeeButton, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 4.0f))
		gs_EditorSubTab = EDITOR_TAB_DEMO_TEE;
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	if(gs_EditorSubTab == EDITOR_TAB_GAMESKIN)
	{
		RenderSixClientGameSkinEditor(MainView);
		return;
	}
	if(gs_EditorSubTab == EDITOR_TAB_ENTITIES)
	{
		RenderSixClientEntitiesEditor(MainView);
		return;
	}
	if(gs_EditorSubTab == EDITOR_TAB_AUDIO)
	{
		RenderSixClientAudioEditor(MainView);
		return;
	}

	MainView.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 10.0f);
	CUIRect Content = MainView;
	Content.Margin(20.0f, &Content);
	CUIRect Icon, Title, Line;
	Content.HSplitTop(44.0f, &Icon, &Content);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.7f);
	Ui()->DoLabel(&Icon, FontIcon::USER, 32.0f, TEXTALIGN_ML);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(8.0f, nullptr, &Content);
	Content.HSplitTop(24.0f, &Title, &Content);
	Ui()->DoLabel(&Title, Localize("Demo Tee Editor"), 20.0f, TEXTALIGN_ML);
	Content.HSplitTop(10.0f, nullptr, &Content);

	auto DrawLine = [&](const char *pText) {
		Content.HSplitTop(18.0f, &Line, &Content);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.75f);
		Ui()->DoLabel(&Line, pText, 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};
	DrawLine(Localize("Edit tee name, clan, skin and colors while watching a demo."));
	DrawLine(Localize("1. Open a demo and keep the demo menu open."));
	DrawLine(Localize("2. Click a tee to open the editor popup."));
	DrawLine(Localize("3. Save a cut to export the edited demo (0.6 demos)."));
	Content.HSplitTop(16.0f, nullptr, &Content);
	DrawLine(Localize("Edited names are marked with *...* when exported."));
	DrawLine(Localize("0.7 demos: preview only, export of tee edits is not supported."));
}

void CMenus::RenderSixClientGameSkinEditor(CUIRect MainView)
{
	RenderSixClientSheetEditor(MainView, false);
}

void CMenus::RenderSixClientEntitiesEditor(CUIRect MainView)
{
	RenderSixClientSheetEditor(MainView, true);
}

void CMenus::RenderSixClientSheetEditor(CUIRect MainView, bool Entities)
{
	static const SSheetEditorConfig s_GameSkinConfig = {
		32, 16, &GameSkinParts(), "game", ResolveGameSkinPath, "667_gameskin",
		"Game skins", "Base skin",
		"Pick the skin every part starts from. Select a part on the left or in the preview to mix it from another skin.",
		"Failed to load the base game skin.", "This game skin could not be loaded."};
	static const SSheetEditorConfig s_EntitiesConfig = {
		16, 16, &EntitiesParts(), "entities", ResolveEntitiesPath, "667_entities",
		"Entities", "Base entities",
		"Pick the entities every group starts from. Select a group on the left or a tile in the preview to take it from other entities.",
		"Failed to load the base entities.", "These entities could not be loaded."};
	const SSheetEditorConfig &Config = Entities ? s_EntitiesConfig : s_GameSkinConfig;
	SSheetEditorState &State = Entities ? gs_EntitiesEditor : gs_GameSkinEditor;
	const std::vector<SSheetPart> &vParts = *Config.m_pParts;
	const int GridX = Config.m_GridX;
	const int GridY = Config.m_GridY;
	const float SheetAspect = (float)GridX / GridY;
	const float LineSize = 20.0f;
	const float Margin = 5.0f;

	auto SetStatus = [&](bool Error, const char *pText) {
		str_copy(State.m_aStatus, pText, sizeof(State.m_aStatus));
		State.m_StatusError = Error;
	};

	auto GetImage = [&](const std::string &Name) -> const CImageInfo * {
		auto It = State.m_Images.find(Name);
		if(It != State.m_Images.end())
			return It->second.m_pData ? &It->second : nullptr;
		CImageInfo &Image = State.m_Images[Name];
		char aPath[IO_MAX_PATH_LENGTH];
		if(!Config.m_pfnResolve(Storage(), Name, aPath, sizeof(aPath)) || !Graphics()->LoadPng(Image, aPath, IStorage::TYPE_ALL))
			return nullptr;
		ConvertToRgba(Image);
		if((int)Image.m_Width < GridX || (int)Image.m_Height < GridY || Image.m_Width % GridX != 0 || Image.m_Height % GridY != 0)
		{
			Image.Free();
			return nullptr;
		}
		return &Image;
	};

	auto RefreshSkinList = [&]() {
		for(SSkinEntry &Entry : State.m_vSkins)
			Graphics()->UnloadTexture(&Entry.m_Texture);
		for(auto &[Name, Image] : State.m_Images)
			Image.Free();
		State.m_Images.clear();
		std::vector<std::string> vNames;
		char aFolder[IO_MAX_PATH_LENGTH];
		str_format(aFolder, sizeof(aFolder), "assets/%s", Config.m_pFolder);
		Storage()->ListDirectory(IStorage::TYPE_ALL, aFolder, ListSheetsCallback, &vNames);
		std::sort(vNames.begin(), vNames.end(), [](const std::string &A, const std::string &B) { return str_comp_nocase(A.c_str(), B.c_str()) < 0; });
		vNames.insert(vNames.begin(), "default");
		State.m_vSkins.clear();
		State.m_vSkins.resize(vNames.size());
		for(size_t i = 0; i < vNames.size(); i++)
			State.m_vSkins[i].m_Name = vNames[i];
		State.m_Dirty = true;
	};

	if(!State.m_Initialized)
	{
		State.m_Initialized = true;
		State.m_vEdits.assign(vParts.size(), SPartEdit());
		State.m_vPartButtons.resize(vParts.size());
		const char *pCurrent = Entities ? g_Config.m_ClAssetsEntities : g_Config.m_ClAssetGame;
		State.m_Base = pCurrent[0] ? pCurrent : "default";
		State.m_ExportName.Set(Config.m_pDefaultExportName);
		RefreshSkinList();
	}

	if(State.m_Dirty)
	{
		State.m_Dirty = false;
		State.m_Composed.Free();
		const CImageInfo *pBase = GetImage(State.m_Base);
		if(pBase == nullptr && State.m_Base != "default")
		{
			State.m_Base = "default";
			pBase = GetImage(State.m_Base);
		}
		if(pBase != nullptr)
		{
			State.m_Composed = pBase->DeepCopy();
			for(size_t i = 0; i < vParts.size(); i++)
			{
				const SPartEdit &Edit = State.m_vEdits[i];
				if(!Edit.m_Source.empty() && Edit.m_Source != State.m_Base)
				{
					if(const CImageInfo *pSource = GetImage(Edit.m_Source))
						for(const SCellRect &Rect : vParts[i].m_vRects)
							CopyRegion(State.m_Composed, *pSource, Rect, GridX, GridY);
				}
				if(Edit.HasColorChange())
					for(const SCellRect &Rect : vParts[i].m_vRects)
						AdjustRegion(State.m_Composed, Rect, Edit, GridX, GridY);
			}
			Graphics()->UnloadTexture(&State.m_PreviewTexture);
			CImageInfo Upload = State.m_Composed.DeepCopy();
			State.m_PreviewTexture = Graphics()->LoadTextureRawMove(Upload, 0, Entities ? "entities_editor_preview" : "gameskin_editor_preview");
		}
		else
		{
			SetStatus(true, Localize(Config.m_pLoadError));
		}
	}

	CUIRect Bottom;
	MainView.HSplitBottom(LineSize + 10.0f, &MainView, &Bottom);
	MainView.HSplitBottom(Margin, &MainView, nullptr);

	CUIRect PartList, Workspace;
	MainView.VSplitLeft(170.0f, &PartList, &Workspace);
	Workspace.VSplitLeft(Margin * 2.0f, nullptr, &Workspace);

	PartList.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 8.0f);
	PartList.Margin(Margin, &PartList);
	{
		CScrollRegionParams PartScrollParams;
		PartScrollParams.m_ScrollUnit = 60.0f;
		PartScrollParams.m_ScrollbarMargin = 2.0f;
		State.m_PartScroll.Begin(&PartList, &PartScrollParams);
		PartList.VSplitRight(6.0f, &PartList, nullptr);
		auto DoPartRow = [&](CButtonContainer *pButton, int Index, const char *pName, bool Modified) {
			CUIRect Row, IconRect, Label, Mark;
			PartList.HSplitTop(24.0f, &Row, &PartList);
			PartList.HSplitTop(2.0f, nullptr, &PartList);
			if(!State.m_PartScroll.AddRect(Row))
				return;
			const bool Selected = State.m_Selected == Index;
			const bool Hovered = Ui()->MouseHovered(&Row);
			if(Selected || Hovered)
				Row.Draw(Selected ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.2f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 5.0f);
			if(Ui()->DoButtonLogic(pButton, Selected, &Row, BUTTONFLAG_LEFT))
				State.m_Selected = Index;
			Row.VSplitLeft(24.0f, &IconRect, &Label);
			Label.VSplitRight(14.0f, &Label, &Mark);
			IconRect.Margin(2.0f, &IconRect);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			if(Index < 0)
				Ui()->DoLabel(&IconRect, FontIcon::IMAGE, 11.0f, TEXTALIGN_MC);
			else
				DrawSubset(Graphics(), State.m_PreviewTexture, IconRect, vParts[Index].m_vRects.front(), GridX, GridY);
			if(Modified)
			{
				TextRender()->TextColor(0.45f, 0.8f, 1.0f, 1.0f);
				Ui()->DoLabel(&Mark, FontIcon::CIRCLE, 6.0f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			Ui()->DoLabel(&Label, pName, 12.0f, TEXTALIGN_ML);
		};
		DoPartRow(&State.m_BaseButton, -1, Localize(Config.m_pBaseLabel), false);
		CUIRect Separator;
		PartList.HSplitTop(4.0f, &Separator, &PartList);
		if(State.m_PartScroll.AddRect(Separator))
		{
			Separator.HMargin(1.5f, &Separator);
			Separator.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f), IGraphics::CORNER_NONE, 0.0f);
		}
		for(size_t i = 0; i < vParts.size(); i++)
			DoPartRow(&State.m_vPartButtons[i], (int)i, Localize(vParts[i].m_pName), State.m_vEdits[i].Modified());
		State.m_PartScroll.End();
	}

	CUIRect Preview, Lower;
	const float PreviewHeight = std::min(Workspace.w / 2.0f, Workspace.h * 0.55f);
	Workspace.HSplitTop(PreviewHeight, &Preview, &Lower);
	Lower.HSplitTop(Margin * 2.0f, nullptr, &Lower);

	Preview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
	CUIRect Sheet;
	Preview.Margin(Margin, &Sheet);
	{
		float W = Sheet.w, H = Sheet.w / SheetAspect;
		if(H > Sheet.h)
		{
			H = Sheet.h;
			W = H * SheetAspect;
		}
		Sheet = {Sheet.x + (Sheet.w - W) / 2.0f, Sheet.y + (Sheet.h - H) / 2.0f, W, H};
	}
	DrawCheckerboard(Graphics(), Sheet, 8.0f);
	DrawSubset(Graphics(), State.m_PreviewTexture, Sheet, {0, 0, GridX, GridY}, GridX, GridY);

	int HoveredPart = -1;
	if(Ui()->MouseHovered(&Sheet))
	{
		const int CellX = (int)((Ui()->MouseX() - Sheet.x) / Sheet.w * GridX);
		const int CellY = (int)((Ui()->MouseY() - Sheet.y) / Sheet.h * GridY);
		for(size_t i = 0; i < vParts.size() && HoveredPart < 0; i++)
			for(const SCellRect &Rect : vParts[i].m_vRects)
				if(CellX >= Rect.m_X && CellX < Rect.m_X + Rect.m_W && CellY >= Rect.m_Y && CellY < Rect.m_Y + Rect.m_H)
				{
					HoveredPart = (int)i;
					break;
				}
	}
	if(Ui()->DoButtonLogic(&State.m_SheetButton, 0, &Sheet, BUTTONFLAG_LEFT) && HoveredPart >= 0)
		State.m_Selected = HoveredPart;

	auto OutlinePart = [&](int Part, ColorRGBA Color) {
		for(const SCellRect &Cells : vParts[Part].m_vRects)
		{
			const CUIRect Rect = CellsToRect(Sheet, Cells, GridX, GridY);
			Rect.Draw(Color, IGraphics::CORNER_ALL, 3.0f);
		}
	};
	if(HoveredPart >= 0 && HoveredPart != State.m_Selected)
		OutlinePart(HoveredPart, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f));
	if(State.m_Selected >= 0)
		OutlinePart(State.m_Selected, ColorRGBA(0.45f, 0.8f, 1.0f, 0.25f));
	if(HoveredPart >= 0)
	{
		CUIRect Tip;
		Preview.Margin(Margin, &Tip);
		Tip.HSplitBottom(18.0f, nullptr, &Tip);
		Tip.VSplitRight(140.0f, nullptr, &Tip);
		Tip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&Tip, Localize(vParts[HoveredPart].m_pName), 11.0f, TEXTALIGN_MC);
	}

	CUIRect Controls, Gallery;
	Lower.VSplitLeft(std::min(240.0f, Lower.w * 0.4f), &Controls, &Gallery);
	Gallery.VSplitLeft(Margin * 2.0f, nullptr, &Gallery);

	const bool BaseSelected = State.m_Selected < 0;
	const std::string &ActiveSource = BaseSelected ? State.m_Base : (State.m_vEdits[State.m_Selected].m_Source.empty() ? State.m_Base : State.m_vEdits[State.m_Selected].m_Source);

	Controls.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 8.0f);
	Controls.Margin(Margin * 1.5f, &Controls);
	{
		CUIRect Label;
		Controls.HSplitTop(LineSize, &Label, &Controls);
		Ui()->DoLabel(&Label, BaseSelected ? Localize(Config.m_pBaseLabel) : Localize(vParts[State.m_Selected].m_pName), 16.0f, TEXTALIGN_ML);
		Controls.HSplitTop(LineSize, &Label, &Controls);
		char aSource[128];
		str_format(aSource, sizeof(aSource), "%s: %s", Localize("Source"), ActiveSource.c_str());
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		Ui()->DoLabel(&Label, aSource, 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Controls.HSplitTop(Margin, nullptr, &Controls);

		if(BaseSelected)
		{
			CUIRect Hint;
			Controls.HSplitTop(LineSize * 3.0f, &Hint, &Controls);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
			SLabelProperties Props;
			Props.m_MaxWidth = Hint.w;
			Ui()->DoLabel(&Hint, Localize(Config.m_pBaseHint), 11.0f, TEXTALIGN_TL, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else
		{
			SPartEdit &Edit = State.m_vEdits[State.m_Selected];
			const SPartEdit Before = Edit;
			CUIRect Slider;
			Controls.HSplitTop(LineSize, &Slider, &Controls);
			Ui()->DoScrollbarOption(&State.m_HueId, &Edit.m_Hue, &Slider, Localize("Hue"), -180, 180, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "°");
			Controls.HSplitTop(LineSize, &Slider, &Controls);
			Ui()->DoScrollbarOption(&State.m_SatId, &Edit.m_Saturation, &Slider, Localize("Saturation"), 0, 200, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			Controls.HSplitTop(LineSize, &Slider, &Controls);
			Ui()->DoScrollbarOption(&State.m_BrightId, &Edit.m_Brightness, &Slider, Localize("Brightness"), 0, 200, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, "%");
			if(Edit.m_Hue != Before.m_Hue || Edit.m_Saturation != Before.m_Saturation || Edit.m_Brightness != Before.m_Brightness)
				State.m_Dirty = true;

			Controls.HSplitTop(Margin * 2.0f, nullptr, &Controls);
			CUIRect Button;
			Controls.HSplitTop(LineSize, &Button, &Controls);
			if(DoButton_Menu(&State.m_ResetPartButton, Localize("Reset part"), 0, &Button) && Edit.Modified())
			{
				Edit = SPartEdit();
				State.m_Dirty = true;
			}

			CUIRect Zoom;
			Controls.HSplitTop(Margin * 2.0f, nullptr, &Controls);
			Controls.HSplitTop(std::min(Controls.h, 80.0f), &Zoom, &Controls);
			if(Zoom.h > 20.0f)
			{
				Zoom.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 6.0f);
				Zoom.Margin(4.0f, &Zoom);
				DrawSubset(Graphics(), State.m_PreviewTexture, Zoom, vParts[State.m_Selected].m_vRects.front(), GridX, GridY);
			}
		}
	}

	Gallery.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 8.0f);
	Gallery.Margin(Margin, &Gallery);
	{
		CUIRect Header, RefreshButton;
		Gallery.HSplitTop(LineSize, &Header, &Gallery);
		Header.VSplitRight(LineSize, &Header, &RefreshButton);
		char aTitle[64];
		str_format(aTitle, sizeof(aTitle), "%s (%d)", Localize(Config.m_pGalleryTitle), (int)State.m_vSkins.size());
		Ui()->DoLabel(&Header, aTitle, 12.0f, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&State.m_RefreshButton, FontIcon::ARROW_ROTATE_RIGHT, 0, &RefreshButton, BUTTONFLAG_LEFT))
			RefreshSkinList();
		Gallery.HSplitTop(Margin, nullptr, &Gallery);

		const SCellRect PreviewCells = BaseSelected ? SCellRect{0, 0, GridX, GridY} : vParts[State.m_Selected].m_vRects.front();
		CScrollRegionParams Params;
		Params.m_ScrollUnit = 60.0f;
		Params.m_ScrollbarMargin = 2.0f;
		State.m_GalleryScroll.Begin(&Gallery, &Params);
		Gallery.VSplitRight(8.0f, &Gallery, nullptr);

		const float CellW = BaseSelected ? 120.0f : 92.0f;
		const float CellH = BaseSelected ? 76.0f : 70.0f;
		const int Columns = std::max(1, (int)((Gallery.w + Margin) / (CellW + Margin)));
		const float RealW = (Gallery.w - (Columns - 1) * Margin) / Columns;
		CUIRect Row;
		std::string Picked;
		int LoadsThisFrame = 0;
		for(size_t i = 0; i < State.m_vSkins.size(); i++)
		{
			if(i % Columns == 0)
			{
				if(i > 0)
					Gallery.HSplitTop(Margin, nullptr, &Gallery);
				Gallery.HSplitTop(CellH, &Row, &Gallery);
			}
			CUIRect Cell;
			Row.VSplitLeft(RealW, &Cell, &Row);
			Row.VSplitLeft(Margin, nullptr, &Row);
			if(!State.m_GalleryScroll.AddRect(Cell))
				continue;

			SSkinEntry &Entry = State.m_vSkins[i];
			if(!Entry.m_TextureLoaded && LoadsThisFrame < 4)
			{
				Entry.m_TextureLoaded = true;
				LoadsThisFrame++;
				char aPath[IO_MAX_PATH_LENGTH];
				if(Config.m_pfnResolve(Storage(), Entry.m_Name, aPath, sizeof(aPath)))
					Entry.m_Texture = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			}

			const bool Active = Entry.m_Name == ActiveSource;
			const bool Hovered = Ui()->MouseHovered(&Cell);
			Cell.Draw(Active ? ColorRGBA(0.45f, 0.8f, 1.0f, 0.25f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 0.12f : 0.05f), IGraphics::CORNER_ALL, 6.0f);
			if(Ui()->DoButtonLogic(&Entry.m_Button, Active, &Cell, BUTTONFLAG_LEFT))
				Picked = Entry.m_Name;

			CUIRect Image, Name;
			Cell.Margin(4.0f, &Image);
			Image.HSplitBottom(14.0f, &Image, &Name);
			DrawSubset(Graphics(), Entry.m_Texture, Image, PreviewCells, GridX, GridY);
			Ui()->DoLabel(&Name, Entry.m_Name.c_str(), 10.0f, TEXTALIGN_MC);
		}
		State.m_GalleryScroll.End();

		if(!Picked.empty())
		{
			if(BaseSelected)
			{
				if(Picked != State.m_Base)
				{
					State.m_Base = Picked;
					for(SPartEdit &Edit : State.m_vEdits)
						if(Edit.m_Source == Picked)
							Edit.m_Source.clear();
					State.m_Dirty = true;
				}
			}
			else
			{
				SPartEdit &Edit = State.m_vEdits[State.m_Selected];
				const std::string NewSource = Picked == State.m_Base ? "" : Picked;
				if(NewSource != Edit.m_Source)
				{
					Edit.m_Source = NewSource;
					State.m_Dirty = true;
				}
			}
			if(State.m_Dirty && GetImage(Picked) == nullptr)
				SetStatus(true, Localize(Config.m_pPickError));
		}
	}

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

	Ui()->DoEditBox(&State.m_ExportName, &NameBox, 12.0f);
	const char *pExportName = State.m_ExportName.GetString();
	char aExportPath[IO_MAX_PATH_LENGTH];
	str_format(aExportPath, sizeof(aExportPath), "assets/%s/%s.png", Config.m_pFolder, pExportName);
	if(Entities && ExportNameValid(pExportName))
	{
		// a folder pack wins over a single file of the same name, so write into the folder then
		char aPackFolder[IO_MAX_PATH_LENGTH];
		str_format(aPackFolder, sizeof(aPackFolder), "assets/entities/%s", pExportName);
		if(Storage()->FolderExists(aPackFolder, IStorage::TYPE_SAVE))
			str_format(aExportPath, sizeof(aExportPath), "%s/ddnet.png", aPackFolder);
	}
	const bool Exists = ExportNameValid(pExportName) && Storage()->FileExists(aExportPath, IStorage::TYPE_SAVE);

	auto Save = [&]() {
		if(!ExportNameValid(pExportName))
		{
			SetStatus(true, Localize("Use only letters, numbers, spaces, - and _ in the name."));
			return false;
		}
		if(State.m_Composed.m_pData == nullptr)
		{
			SetStatus(true, Localize("Nothing to save."));
			return false;
		}
		char aFolder[IO_MAX_PATH_LENGTH];
		str_format(aFolder, sizeof(aFolder), "assets/%s", Config.m_pFolder);
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aFolder, IStorage::TYPE_SAVE);
		IOHANDLE File = Storage()->OpenFile(aExportPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File || !CImageLoader::SavePng(File, aExportPath, State.m_Composed))
		{
			SetStatus(true, Localize("Failed to write the PNG file."));
			return false;
		}
		const bool Known = std::any_of(State.m_vSkins.begin(), State.m_vSkins.end(), [&](const SSkinEntry &Entry) { return Entry.m_Name == pExportName; });
		if(Known)
		{
			for(SSkinEntry &Entry : State.m_vSkins)
				if(Entry.m_Name == pExportName)
				{
					Graphics()->UnloadTexture(&Entry.m_Texture);
					Entry.m_TextureLoaded = false;
				}
			auto It = State.m_Images.find(pExportName);
			if(It != State.m_Images.end())
			{
				It->second.Free();
				State.m_Images.erase(It);
			}
		}
		else
		{
			SSkinEntry Entry;
			Entry.m_Name = pExportName;
			State.m_vSkins.push_back(std::move(Entry));
			std::sort(State.m_vSkins.begin() + 1, State.m_vSkins.end(), [](const SSkinEntry &A, const SSkinEntry &B) { return str_comp_nocase(A.m_Name.c_str(), B.m_Name.c_str()) < 0; });
		}
		if(Entities)
		{
			if(!m_vEntitiesList.empty() && std::none_of(m_vEntitiesList.begin(), m_vEntitiesList.end(), [&](const SCustomEntities &Item) { return str_comp(Item.m_aName, pExportName) == 0; }))
			{
				SCustomEntities Item;
				str_copy(Item.m_aName, pExportName);
				const IGraphics::CTextureHandle Texture = Graphics()->LoadTexture(aExportPath, IStorage::TYPE_ALL);
				for(auto &Image : Item.m_aImages)
					Image.m_Texture = Texture;
				Item.m_RenderTexture = Texture;
				m_vEntitiesList.push_back(Item);
				std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
			}
		}
		else if(!m_vGameList.empty() && std::none_of(m_vGameList.begin(), m_vGameList.end(), [&](const SCustomGame &Item) { return str_comp(Item.m_aName, pExportName) == 0; }))
		{
			SCustomGame Item;
			str_copy(Item.m_aName, pExportName);
			Item.m_RenderTexture = Graphics()->LoadTexture(aExportPath, IStorage::TYPE_ALL);
			m_vGameList.push_back(Item);
			std::sort(m_vGameList.begin(), m_vGameList.end());
		}
		char aStatus[256];
		str_format(aStatus, sizeof(aStatus), Localize("Saved to %s"), aExportPath);
		SetStatus(false, aStatus);
		return true;
	};

	auto ApplyAsset = [&]() {
		if(Entities)
		{
			str_copy(g_Config.m_ClAssetsEntities, pExportName);
			GameClient()->m_MapImages.ChangeEntitiesPath(pExportName);
		}
		else
		{
			str_copy(g_Config.m_ClAssetGame, pExportName);
			GameClient()->LoadGameSkin(pExportName);
		}
	};

	if(DoButton_Menu(&State.m_SaveButton, Exists ? Localize("Overwrite") : Localize("Save"), 0, &SaveButton) && Save())
	{
		// reload it when it is the asset in use
		if(str_comp(Entities ? g_Config.m_ClAssetsEntities : g_Config.m_ClAssetGame, pExportName) == 0)
			ApplyAsset();
	}
	if(DoButton_Menu(&State.m_ApplyButton, Localize("Save & Apply"), 0, &ApplyButton) && Save())
	{
		ApplyAsset();
		char aStatus[256];
		str_format(aStatus, sizeof(aStatus), Localize("Saved and applied %s"), pExportName);
		SetStatus(false, aStatus);
	}
	if(DoButton_Menu(&State.m_ResetButton, Localize("Reset all"), 0, &ResetButton))
	{
		State.m_vEdits.assign(vParts.size(), SPartEdit());
		State.m_Dirty = true;
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
