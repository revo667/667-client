#include <algorithm>

#include <engine/font_icons.h>
#include <engine/shared/config.h>

#include <game/client/sc_ui_animations.h>
#include <game/client/components/sixclient/fx/fgf_weather.h>
#include <game/client/components/sixclient/settings_search.h>
#include <game/client/components/sixclient/ui_theme/style.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

void CMenus::RenderSettingsSixClientFx(CUIRect &Column, bool HookColumn)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginBetweenViews = 30.0f;
	const float BlockPadding = MarginBetweenViews * 0.6666f;
	const float Row = MarginSmall + LineSize;

	const auto RowCheck = [&](CUIRect &View, int *pValue, const char *pText) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		CUIRect Line;
		View.HSplitTop(LineSize, &Line, &View);
		DoButton_CheckBoxAutoVMarginAndSet(pValue, pText, pValue, &Line, LineSize);
	};
	const auto RowSlider = [&](CUIRect &View, int *pValue, const char *pText, int Min, int Max, const char *pSuffix) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		CUIRect Line;
		View.HSplitTop(LineSize, &Line, &View);
		Ui()->DoScrollbarOption(pValue, pValue, &Line, pText, Min, Max, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE, pSuffix);
	};
	const auto RowColor = [&](CUIRect &View, CButtonContainer *pResetId, const char *pText, unsigned *pColor, unsigned DefaultColor) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		DoLine_ColorPicker(pResetId, LineSize, 13.0f, 0.0f, &View, pText, pColor, color_cast<ColorRGBA>(ColorHSLA(DefaultColor)), false);
	};
	const auto RowDropDown = [&](CUIRect &View, const char *pLabel, int &Value, const char **pItems, int Num, CUi::SDropDownState &State, CScrollRegion &Scroll) {
		View.HSplitTop(MarginSmall, nullptr, &View);
		CUIRect Line, Label, Select;
		View.HSplitTop(LineSize, &Line, &View);
		Line.VSplitLeft(110.0f, &Label, &Select);
		Ui()->DoLabel(&Label, pLabel, 14.0f, TEXTALIGN_ML);
		State.m_SelectionPopupContext.m_pScrollRegion = &Scroll;
		Value = Ui()->DoDropDown(&Select, Value, pItems, Num, State);
	};
	const auto DrawFxTitle = [&](CUIRect Title, const char *pText, CButtonContainer *pAuthor) {
		Ui()->DoLabel(&Title, pText, HeadlineFontSize, TEXTALIGN_ML);
	};

	if(HookColumn)
	{
		const float HoldTarget = 3.0f * Row;
		const float HoldHeight = SC_MODULE_REVEAL(g_Config.m_ScGatheredEnergy != 0, HoldTarget, Client()->RenderFrameTime());
		const float EffectRows = 6.0f + (g_Config.m_ScAirJump != 0 ? 1.0f : 0.0f) + (g_Config.m_ScHammerSlash != 0 ? 1.0f : 0.0f);
		const float EffectsBlockHeight = LineSize + EffectRows * Row + HoldHeight;
		CUIRect EffectsBlock;
		Column.HSplitTop(EffectsBlockHeight, &EffectsBlock, &Column);
		CUIRect EffectsBg = EffectsBlock;
		EffectsBg.w += BlockPadding;
		EffectsBg.h += BlockPadding;
		EffectsBg.x -= BlockPadding * 0.5f;
		EffectsBg.y -= BlockPadding * 0.5f;
		SixClientUiTheme::DrawUiBlock(&EffectsBg, IGraphics::CORNER_ALL, 10.0f);
		SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScHookTether, &EffectsBg);

		CUIRect EffectsLabel;
		EffectsBlock.HSplitTop(LineSize, &EffectsLabel, &EffectsBlock);
		CUIRect EffectsTitle, EffectsResetButton;
		EffectsLabel.VSplitRight(LineSize + 8.0f, &EffectsTitle, &EffectsResetButton);
		static CButtonContainer s_EffectsReset;
		if(Ui()->DoButton_FontIcon(&s_EffectsReset, FontIcon::ARROW_ROTATE_LEFT, 0, &EffectsResetButton, BUTTONFLAG_LEFT))
		{
			g_Config.m_ScHookTether = DefaultConfig::ScHookTether;
			g_Config.m_ScHookLassoBox = DefaultConfig::ScHookLassoBox;
			g_Config.m_ScHookFxColor = DefaultConfig::ScHookFxColor;
			g_Config.m_ScAirJump = DefaultConfig::ScAirJump;
			g_Config.m_ScAirJumpHideParticles = DefaultConfig::ScAirJumpHideParticles;
			g_Config.m_ScHammerSlash = DefaultConfig::ScHammerSlash;
			g_Config.m_ScHammerHideParticles = DefaultConfig::ScHammerHideParticles;
			g_Config.m_ScGatheredEnergyAura = DefaultConfig::ScGatheredEnergyAura;
			g_Config.m_ScGatheredEnergyBurst = DefaultConfig::ScGatheredEnergyBurst;
			g_Config.m_ScGatheredEnergyAmount = DefaultConfig::ScGatheredEnergyAmount;
		}
		Ui()->DoToolTip(&s_EffectsReset, &EffectsResetButton, ScLocalize("Reset to defaults"));
		static CButtonContainer s_EffectsAuthor;
		DrawFxTitle(EffectsTitle, ScLocalize("Hook effects"), &s_EffectsAuthor);
		static CButtonContainer s_EffectsColor;
		RowColor(EffectsBlock, &s_EffectsColor, ScLocalize("Effect color"), &g_Config.m_ScHookFxColor, DefaultConfig::ScHookFxColor);
		RowCheck(EffectsBlock, &g_Config.m_ScAirJump, ScLocalize("Air jump ring"));
		if(g_Config.m_ScAirJump)
			RowCheck(EffectsBlock, &g_Config.m_ScAirJumpHideParticles, ScLocalize("Disable jump particles"));
		RowCheck(EffectsBlock, &g_Config.m_ScHammerSlash, ScLocalize("Hammer slash"));
		if(g_Config.m_ScHammerSlash)
			RowCheck(EffectsBlock, &g_Config.m_ScHammerHideParticles, ScLocalize("Disable hammer particles"));
		RowCheck(EffectsBlock, &g_Config.m_ScHookTether, ScLocalize("Hook lasso"));
		RowCheck(EffectsBlock, &g_Config.m_ScHookLassoBox, ScLocalize("Hook box"));
		RowCheck(EffectsBlock, &g_Config.m_ScGatheredEnergy, ScLocalize("Hook hold"));
		if(HoldHeight > 0.5f)
		{
			CUIRect VisibleEffects = EffectsBlock;
			VisibleEffects.h = HoldHeight;
			Ui()->ClipEnable(&VisibleEffects);
			RowCheck(EffectsBlock, &g_Config.m_ScGatheredEnergyAura, ScLocalize("Hold aura"));
			RowCheck(EffectsBlock, &g_Config.m_ScGatheredEnergyBurst, ScLocalize("Hold burst"));
			RowSlider(EffectsBlock, &g_Config.m_ScGatheredEnergyAmount, ScLocalize("Energy motes"), 25, 300, "%");
			Ui()->ClipDisable();
		}

		Column.HSplitTop(MarginBetweenViews, nullptr, &Column);

		{
			const bool Expanded = g_Config.m_ScAtmosphere != 0;
			float WeatherExpandedRows = 5.0f;
			if(g_Config.m_ScWeather == FGF_WEATHER_SNOW || g_Config.m_ScWeather == FGF_WEATHER_RAIN)
				WeatherExpandedRows = 6.0f;
			const float WeatherExpandedTarget = WeatherExpandedRows * Row;
			const float ExpandedHeight = SC_MODULE_REVEAL(Expanded, WeatherExpandedTarget, Client()->RenderFrameTime());
			const float WeatherBlockHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect WeatherBlock;
			Column.HSplitTop(WeatherBlockHeight, &WeatherBlock, &Column);
			CUIRect WeatherBg = WeatherBlock;
			WeatherBg.w += BlockPadding;
			WeatherBg.h += BlockPadding;
			WeatherBg.x -= BlockPadding * 0.5f;
			WeatherBg.y -= BlockPadding * 0.5f;
			SixClientUiTheme::DrawUiBlock(&WeatherBg, IGraphics::CORNER_ALL, 10.0f);
			SixClientSettingsSearch::DrawBlockHighlight(&g_Config.m_ScAtmosphere, &WeatherBg);

			CUIRect WeatherLabel;
			WeatherBlock.HSplitTop(LineSize, &WeatherLabel, &WeatherBlock);
			CUIRect WeatherTitle, WeatherResetButton;
			WeatherLabel.VSplitRight(LineSize + 8.0f, &WeatherTitle, &WeatherResetButton);
			static CButtonContainer s_WeatherReset;
			if(Ui()->DoButton_FontIcon(&s_WeatherReset, FontIcon::ARROW_ROTATE_LEFT, 0, &WeatherResetButton, BUTTONFLAG_LEFT))
			{
				g_Config.m_ScWeather = DefaultConfig::ScWeather;
				g_Config.m_ScWeatherAmount = DefaultConfig::ScWeatherAmount;
				g_Config.m_ScWeatherSnowSize = DefaultConfig::ScWeatherSnowSize;
				g_Config.m_ScWeatherSnowSpeed = DefaultConfig::ScWeatherSnowSpeed;
				g_Config.m_ScWeatherSnowColor = DefaultConfig::ScWeatherSnowColor;
				g_Config.m_ScWeatherSnowflakes = DefaultConfig::ScWeatherSnowflakes;
				g_Config.m_ScWeatherRainSize = DefaultConfig::ScWeatherRainSize;
				g_Config.m_ScWeatherRainSpeed = DefaultConfig::ScWeatherRainSpeed;
				g_Config.m_ScWeatherRainTilt = DefaultConfig::ScWeatherRainTilt;
				g_Config.m_ScWeatherRainColor = DefaultConfig::ScWeatherRainColor;
				g_Config.m_ScWeatherFireflySize = DefaultConfig::ScWeatherFireflySize;
				g_Config.m_ScWeatherFireflySpeed = DefaultConfig::ScWeatherFireflySpeed;
				g_Config.m_ScWeatherFireflyColor = DefaultConfig::ScWeatherFireflyColor;
				g_Config.m_ScAmbientDust = DefaultConfig::ScAmbientDust;
				g_Config.m_ScAmbientDustAmount = DefaultConfig::ScAmbientDustAmount;
				g_Config.m_ScAmbientDustColor = DefaultConfig::ScAmbientDustColor;
				g_Config.m_ScWeatherDustSize = DefaultConfig::ScWeatherDustSize;
				g_Config.m_ScWeatherDustSpeed = DefaultConfig::ScWeatherDustSpeed;
			}
			Ui()->DoToolTip(&s_WeatherReset, &WeatherResetButton, ScLocalize("Reset to defaults"));
			static CButtonContainer s_WeatherAuthor;
			DrawFxTitle(WeatherTitle, ScLocalize("Weather"), &s_WeatherAuthor);
			WeatherBlock.HSplitTop(MarginSmall, nullptr, &WeatherBlock);
			CUIRect Enable;
			WeatherBlock.HSplitTop(LineSize, &Enable, &WeatherBlock);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ScAtmosphere, ScLocalize("Enable"), &g_Config.m_ScAtmosphere, &Enable, LineSize);
			if(ExpandedHeight > 0.5f)
			{
				CUIRect WeatherVisible = WeatherBlock;
				WeatherVisible.h = ExpandedHeight;
				Ui()->ClipEnable(&WeatherVisible);
				static CUi::SDropDownState s_WeatherState;
				static CScrollRegion s_WeatherScroll;
				const char *apWeather[] = {
					ScLocalize("Snow"),
					ScLocalize("Rain"),
					ScLocalize("Fireflies"),
					ScLocalize("Dust"),
				};
				g_Config.m_ScWeather = std::clamp(g_Config.m_ScWeather, 0, (int)FGF_WEATHER_DUST);
				RowDropDown(WeatherBlock, ScLocalize("Weather"), g_Config.m_ScWeather, apWeather, (int)std::size(apWeather), s_WeatherState, s_WeatherScroll);
				if(g_Config.m_ScWeather == FGF_WEATHER_DUST)
				{
					RowSlider(WeatherBlock, &g_Config.m_ScAmbientDustAmount, ScLocalize("Amount"), 25, 200, "%");
					RowSlider(WeatherBlock, &g_Config.m_ScWeatherDustSize, ScLocalize("Size"), 25, 300, "%");
					RowSlider(WeatherBlock, &g_Config.m_ScWeatherDustSpeed, ScLocalize("Speed"), 25, 300, "%");
					static CButtonContainer s_DustColor;
					RowColor(WeatherBlock, &s_DustColor, ScLocalize("Dust color"), &g_Config.m_ScAmbientDustColor, DefaultConfig::ScAmbientDustColor);
				}
				else
				{
					RowSlider(WeatherBlock, &g_Config.m_ScWeatherAmount, ScLocalize("Amount"), 25, 300, "%");
					if(g_Config.m_ScWeather == FGF_WEATHER_RAIN)
					{
						RowSlider(WeatherBlock, &g_Config.m_ScWeatherRainSize, ScLocalize("Size"), 25, 300, "%");
						RowSlider(WeatherBlock, &g_Config.m_ScWeatherRainSpeed, ScLocalize("Speed"), 25, 300, "%");
						RowSlider(WeatherBlock, &g_Config.m_ScWeatherRainTilt, ScLocalize("Tilt"), -75, 75, "°");
						static CButtonContainer s_RainColor;
						RowColor(WeatherBlock, &s_RainColor, ScLocalize("Rain color"), &g_Config.m_ScWeatherRainColor, DefaultConfig::ScWeatherRainColor);
					}
					else if(g_Config.m_ScWeather == FGF_WEATHER_FIREFLIES)
					{
						RowSlider(WeatherBlock, &g_Config.m_ScWeatherFireflySize, ScLocalize("Size"), 25, 300, "%");
						RowSlider(WeatherBlock, &g_Config.m_ScWeatherFireflySpeed, ScLocalize("Speed"), 25, 300, "%");
						static CButtonContainer s_FireflyColor;
						RowColor(WeatherBlock, &s_FireflyColor, ScLocalize("Firefly color"), &g_Config.m_ScWeatherFireflyColor, DefaultConfig::ScWeatherFireflyColor);
					}
					else
					{
						RowSlider(WeatherBlock, &g_Config.m_ScWeatherSnowSize, ScLocalize("Size"), 25, 300, "%");
						RowSlider(WeatherBlock, &g_Config.m_ScWeatherSnowSpeed, ScLocalize("Speed"), 25, 300, "%");
						static CUi::SDropDownState s_SnowState;
						static CScrollRegion s_SnowScroll;
						const char *apSnow[] = {
							ScLocalize("Dots"),
							ScLocalize("Snowflakes"),
						};
						g_Config.m_ScWeatherSnowflakes = std::clamp(g_Config.m_ScWeatherSnowflakes, 0, 1);
						RowDropDown(WeatherBlock, ScLocalize("Snow"), g_Config.m_ScWeatherSnowflakes, apSnow, (int)std::size(apSnow), s_SnowState, s_SnowScroll);
						static CButtonContainer s_SnowColor;
						RowColor(WeatherBlock, &s_SnowColor, ScLocalize("Snow color"), &g_Config.m_ScWeatherSnowColor, DefaultConfig::ScWeatherSnowColor);
					}
				}
				Ui()->ClipDisable();
			}
		}

		Column.HSplitTop(MarginBetweenViews, nullptr, &Column);
	}
}
