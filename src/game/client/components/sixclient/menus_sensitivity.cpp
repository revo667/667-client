/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-896a9cbe */

#include <base/math.h>
#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/components/sixclient/ui_theme/style.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderSettingsSixClientSensitivity(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float FontSize = 14.0f;
	const float BlockPadding = 10.0f;

	CUIRect LeftView, RightView;
	MainView.VSplitMid(&LeftView, &RightView, 20.0f);
	LeftView.HSplitTop(BlockPadding / 2.0f, nullptr, &LeftView);
	RightView.HSplitTop(BlockPadding / 2.0f, nullptr, &RightView);

	auto DrawBlock = [&](CUIRect &Column, float Height) {
		CUIRect Block;
		Column.HSplitTop(Height, &Block, &Column);
		Column.HSplitTop(20.0f, nullptr, &Column);
		CUIRect Bg = Block;
		Bg.x -= BlockPadding / 2.0f;
		Bg.y -= BlockPadding / 2.0f;
		Bg.w += BlockPadding;
		Bg.h += BlockPadding;
		SixClientUiTheme::DrawUiBlock(&Bg, IGraphics::CORNER_ALL, 10.0f);
		return Block;
	};
	auto DimLabel = [&](CUIRect &View, const char *pText) {
		CUIRect Label;
		View.HSplitTop(MarginSmall, nullptr, &View);
		View.HSplitTop(LineSize, &Label, &View);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.6f);
		Ui()->DoLabel(&Label, pText, FontSize * 0.85f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	auto ValueRow = [&](CUIRect &View, int *pValue, CLineInputNumber *pInput, int *pLastSynced, const char *pLabel, int Min, int SliderMax, int Max) {
		CUIRect Row, Slider, Field;
		View.HSplitTop(MarginSmall, nullptr, &View);
		View.HSplitTop(LineSize, &Row, &View);
		Row.VSplitRight(70.0f, &Slider, &Field);
		Slider.VSplitRight(MarginSmall, &Slider, nullptr);
		if(!pInput->IsActive() && *pLastSynced != *pValue)
		{
			pInput->SetInteger(*pValue);
			*pLastSynced = *pValue;
		}
		Ui()->DoScrollbarOption(pValue, pValue, &Slider, pLabel, Min, SliderMax, &CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		if(Ui()->DoEditBox(pInput, &Field, FontSize))
		{
			const int Typed = pInput->GetInteger();
			if(Typed >= Min && Typed <= Max)
			{
				*pValue = Typed;
				*pLastSynced = Typed;
			}
		}
	};

	const bool DyncamSens = g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens;
	int *pSens = DyncamSens ? &g_Config.m_ClDyncamMousesens : &g_Config.m_InpMousesens;
	const float MaxDistance = GameClient()->m_Controls.GetMaxMouseDistance();
	const bool Online = Client()->State() == IClient::STATE_ONLINE;

	{
		CUIRect View = DrawBlock(LeftView, LineSize + 10.0f * (MarginSmall + LineSize));
		CUIRect Label, Button;
		View.HSplitTop(LineSize, &Label, &View);
		Ui()->DoLabel(&Label, ScLocalize("Sensitivity"), HeadlineFontSize, TEXTALIGN_ML);

		static CLineInputNumber s_SensInput, s_DpiInput;
		static int s_SensSynced = -1, s_DpiSynced = -1;
		ValueRow(View, pSens, &s_SensInput, &s_SensSynced, DyncamSens ? ScLocalize("Dyncam sensitivity") : ScLocalize("Mouse sensitivity"), 1, 1000, 100000);
		ValueRow(View, &g_Config.m_ScMouseDpi, &s_DpiInput, &s_DpiSynced, ScLocalize("Mouse DPI"), 0, 32000, 64000);

		const float Sens = std::max(1, *pSens) / 100.0f;
		const int Dpi = g_Config.m_ScMouseDpi;
		char aLine[128];
		if(Dpi > 0)
		{
			const float CountsPerUnit = 1.0f / Sens;
			const float CmPerUnit = CountsPerUnit / Dpi * 2.54f;
			str_format(aLine, sizeof(aLine), ScLocalize("eDPI %d  (DPI × sensitivity)"), round_to_int(Dpi * Sens));
			DimLabel(View, aLine);
			str_format(aLine, sizeof(aLine), ScLocalize("Full aim range (%.0f units each side): %.1f cm of mouse travel"), MaxDistance, MaxDistance * 2.0f * CmPerUnit);
			DimLabel(View, aLine);
			str_format(aLine, sizeof(aLine), ScLocalize("One tile of aim movement: %.2f cm"), 32.0f * CmPerUnit);
			DimLabel(View, aLine);
		}
		else
		{
			DimLabel(View, ScLocalize("Set your mouse DPI to see eDPI and mouse travel in cm."));
			DimLabel(View, "");
			DimLabel(View, "");
		}
		DimLabel(View, DyncamSens ? ScLocalize("Dyncam is on, so the dyncam sensitivity is used.") : ScLocalize("Aim moves the same distance on both axes, aspect ratio does not change it."));

		View.HSplitTop(MarginSmall * 2.0f, nullptr, &View);
		View.HSplitTop(LineSize, &Button, &View);
		static CButtonContainer s_PsaButton;
		if(DoButton_Menu(&s_PsaButton, ScLocalize("Find my sensitivity (PSA)"), Online ? 0 : -1, &Button) && Online)
		{
			GameClient()->m_AimTrainer.Activate(CAimTrainer::MODE_PSA);
			return;
		}
		DimLabel(View, ScLocalize("Plays 7 rounds with your real sensitivity changed between a lower and a higher value."));
		DimLabel(View, ScLocalize("Your current sensitivity is saved and restored if you stop the test."));
	}

	{
		CUIRect View = DrawBlock(RightView, LineSize + 4.0f * (MarginSmall + LineSize));
		CUIRect Label, Button;
		View.HSplitTop(LineSize, &Label, &View);
		Ui()->DoLabel(&Label, ScLocalize("Aim Trainer"), HeadlineFontSize, TEXTALIGN_ML);

		View.HSplitTop(MarginSmall, nullptr, &View);
		View.HSplitTop(LineSize, &Button, &View);
		CUIRect FlickButton, TimedButton;
		Button.VSplitMid(&FlickButton, &TimedButton, MarginSmall);
		static CButtonContainer s_FlickButton, s_TimedButton;
		if(DoButton_Menu(&s_FlickButton, ScLocalize("Flick (30 targets)"), Online ? 0 : -1, &FlickButton) && Online)
		{
			GameClient()->m_AimTrainer.Activate(CAimTrainer::MODE_FLICK);
			return;
		}
		if(DoButton_Menu(&s_TimedButton, ScLocalize("Timed (30 seconds)"), Online ? 0 : -1, &TimedButton) && Online)
		{
			GameClient()->m_AimTrainer.Activate(CAimTrainer::MODE_TIMED);
			return;
		}

		DimLabel(View, Online ? ScLocalize("Runs on top of the game. Esc stops it.") : ScLocalize("Join a server first, the trainer runs on top of the game."));

		char aLine[128];
		char aFlick[32] = "-";
		char aTimed[32] = "-";
		if(g_Config.m_ScAimBestFlickMs > 0)
			str_format(aFlick, sizeof(aFlick), "%d ms", g_Config.m_ScAimBestFlickMs);
		if(g_Config.m_ScAimBestTimed > 0)
			str_format(aTimed, sizeof(aTimed), "%d", g_Config.m_ScAimBestTimed);
		str_format(aLine, sizeof(aLine), ScLocalize("Best flick: %s per target  ·  Best timed: %s hits"), aFlick, aTimed);
		DimLabel(View, aLine);
	}
}
