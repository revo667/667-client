/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-b91f3041 */

// 667client: Cecen Client / Cosmetics settings page. Purely visual effects gathered in one
// place: TClient's pet, frozen tee display, tile outlines, rainbow, tee trails and background
// drawing on the left; weapon glow, 3D particles, cursor trail, keystrokes, hook effects and
// weather on the right.

#include <game/client/components/menus.h>
#include <game/client/components/sixclient/ui_theme/style.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>
#include <vector>

void CMenus::RenderSettingsSixClientCosmetics(CUIRect MainView)
{
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 30.0f;
	const float MarginBetweenSections = 30.0f;
	const float SectionPadding = MarginBetweenViews * 0.6666f;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	CUIRect LeftView, RightView;
	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	// The TClient sections only lay out their content; their backgrounds are drawn first, from
	// the size each section had last frame (moved along with the scroll offset).
	static std::vector<CUIRect> s_SectionBoxes;
	static float s_PrevScrollOffset = 0.0f;
	const float ScrollOffset = MainView.y;
	for(CUIRect Section : s_SectionBoxes)
	{
		Section.x -= SectionPadding * 0.5f;
		Section.y -= SectionPadding * 0.5f + (s_PrevScrollOffset - ScrollOffset);
		Section.w += SectionPadding;
		Section.h += SectionPadding;
		SixClientUiTheme::DrawUiBlock(&Section, IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	CUIRect Column = LeftView;
	Column.HSplitTop(10.0f, nullptr, &Column);
	auto &&Section = [&](void (CMenus::*pfnRender)(CUIRect &)) {
		if(!s_SectionBoxes.empty())
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		s_SectionBoxes.push_back(Column);
		(this->*pfnRender)(Column);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	};
	Section(&CMenus::RenderTClientPet);
	Section(&CMenus::RenderTClientFrozenTeeDisplay);
	Section(&CMenus::RenderTClientTileOutlines);
	Section(&CMenus::RenderTClientRainbow);
	Section(&CMenus::RenderTClientTeeTrails);
	Section(&CMenus::RenderTClientBgDraw);
	const float LeftEnd = Column.y;

	// The Cecen Client blocks draw their own background.
	Column = RightView;
	Column.HSplitTop(10.0f, nullptr, &Column);
	RenderSettingsSixClientWeaponGlow(Column);
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);
	RenderSettingsSixClientParticles3D(Column);
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);
	RenderSettingsSixClientCursorTrail(Column);
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);
	RenderSettingsSixClientKeystrokes(Column);
	Column.HSplitTop(MarginBetweenViews, nullptr, &Column);
	RenderSettingsSixClientFx(Column, true);
	const float RightEnd = Column.y;

	CUIRect ScrollEnd;
	ScrollEnd.x = MainView.x;
	ScrollEnd.y = std::max(LeftEnd, RightEnd) + MarginSmall * 2.0f;
	ScrollEnd.w = MainView.w;
	ScrollEnd.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollEnd);
	s_ScrollRegion.End();
}
