/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-4d0288d6 */

#include "menu_rain_background.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/image.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr float VIRTUAL_HEIGHT = 1080.0f;
constexpr float LIGHT_RADIUS = 140.0f;
constexpr float MAX_TILT = 38.0f;
constexpr float BEAM_SPREAD = 12.0f;
constexpr float BEAM_FULL = 0.126f;
constexpr float BEAM_END = 0.8f;
constexpr int CIRCLE_SEGMENTS = 48;
constexpr int GRAIN_SIZE = 128;
constexpr float GRAIN_TILE = 160.0f;
constexpr float RIPPLE_DURATION = 0.75f;
constexpr float RIPPLE_RADIUS = 110.0f;
constexpr float RAIN_FADE = 1.5f;
constexpr float ATMOSPHERE_FADE = 2.4f;
constexpr int SPARKS_PER_BURST = 14;

struct SBeam
{
	float m_Left;
	ColorRGBA m_Color;
	float m_Follow;
	float m_Sway;
	float m_Delay;
};

const SBeam BEAMS[] = {
	{0.5f, ColorRGBA(0.975f, 0.975f, 0.975f, 0.15f), 0.07f, 15.0f, 0.0f},
	{0.14f, ColorRGBA(0.80f, 0.82f, 0.86f, 0.11f), 0.04f, 21.0f, -7.0f},
	{0.86f, ColorRGBA(0.80f, 0.82f, 0.86f, 0.11f), 0.045f, 19.0f, -12.0f},
	{0.32f, ColorRGBA(0.90f, 0.90f, 0.90f, 0.07f), 0.028f, 25.0f, -3.0f},
	{0.68f, ColorRGBA(0.90f, 0.90f, 0.90f, 0.07f), 0.032f, 23.0f, -9.0f},
};

float SmoothStep(float u)
{
	u = std::clamp(u, 0.0f, 1.0f);
	return u * u * (3.0f - 2.0f * u);
}

float PingPong(float Time, float Period)
{
	float p = std::fmod(Time / Period, 1.0f);
	if(p < 0.0f)
		p += 1.0f;
	return p < 0.5f ? SmoothStep(p * 2.0f) : 1.0f - SmoothStep((p - 0.5f) * 2.0f);
}

float EaseOut(float u)
{
	u = std::clamp(u, 0.0f, 1.0f);
	return 1.0f - (1.0f - u) * (1.0f - u);
}

float EaseOutQuint(float u)
{
	u = std::clamp(u, 0.0f, 1.0f);
	const float Inv = 1.0f - u;
	return 1.0f - Inv * Inv * Inv * Inv * Inv;
}

vec2 BeamDir(float Degrees)
{
	const float Rad = Degrees * pi / 180.0f;
	return vec2(-std::sin(Rad), std::cos(Rad));
}
} // namespace

CMenuRainBackground::SPalette CMenuRainBackground::Palette()
{
	if(g_Config.m_ScMenuRainTone == 1)
	{
		return {
			ColorRGBA(178.0f / 255.0f, 132.0f / 255.0f, 1.0f, 1.0f),
			ColorRGBA(0.20f, 0.085f, 0.335f, 0.18f),
			ColorRGBA(0.55f, 0.30f, 0.85f, 1.0f),
			ColorRGBA(0.87f, 0.78f, 1.0f, 1.0f),
		};
	}
	return {
		ColorRGBA(196.0f / 255.0f, 202.0f / 255.0f, 214.0f / 255.0f, 1.0f),
		ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f),
		ColorRGBA(0.80f, 0.81f, 0.83f, 1.0f),
		ColorRGBA(0.975f, 0.975f, 0.975f, 1.0f),
	};
}

void CMenuRainBackground::OnInit()
{
	CImageInfo Image;
	Image.m_Width = GRAIN_SIZE;
	Image.m_Height = GRAIN_SIZE;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.Allocate();
	for(int i = 0; i < GRAIN_SIZE * GRAIN_SIZE; i++)
	{
		const float n = random_float();
		uint8_t *pPixel = Image.m_pData + i * 4;
		if(n < 0.5f)
		{
			pPixel[0] = pPixel[1] = pPixel[2] = 0;
			pPixel[3] = (uint8_t)((0.5f - n) * 2.0f * 0.10f * 255.0f);
		}
		else
		{
			pPixel[0] = pPixel[1] = pPixel[2] = 255;
			pPixel[3] = (uint8_t)((n - 0.5f) * 2.0f * 0.035f * 255.0f);
		}
	}
	m_GrainTexture = Graphics()->LoadTextureRawMove(Image, 0, "667_menu_grain");
}

void CMenuRainBackground::OnShutdown()
{
	Graphics()->UnloadTexture(&m_GrainTexture);
}

void CMenuRainBackground::ResetDrops()
{
	const int Count = (int)(m_Width * m_Height / 9000.0f);
	m_vDrops.resize(Count);
	for(SDrop &Drop : m_vDrops)
	{
		Drop.m_X = random_float(m_Width);
		Drop.m_Y = random_float(m_Height);
		Drop.m_Len = random_float(10.0f, 32.0f);
		Drop.m_Speed = random_float(2.3f, 5.9f);
		Drop.m_Opacity = random_float(0.12f, 0.57f);
	}
}

float CMenuRainBackground::DropLight(const SDrop &Drop) const
{
	const float Dist = distance(vec2(Drop.m_X, Drop.m_Y + Drop.m_Len * 0.5f), m_SmoothPointer);
	return Dist < LIGHT_RADIUS ? 1.0f - Dist / LIGHT_RADIUS : 0.0f;
}

void CMenuRainBackground::SpawnBurst(vec2 Pos)
{
	m_vRipples.push_back({Pos, 0.0f});
	for(int i = 0; i < SPARKS_PER_BURST; i++)
	{
		const float Angle = (float)i / SPARKS_PER_BURST * 2.0f * pi + random_float(0.4f);
		const float Speed = random_float(1.4f, 3.6f);
		const float Life = random_float(30.0f, 60.0f);
		m_vSparks.push_back({Pos, vec2(std::cos(Angle), std::sin(Angle)) * Speed, Life, Life, random_float(0.7f, 2.0f)});
	}
}

void CMenuRainBackground::Update(float Dt)
{
	const float Step = Dt * 60.0f;

	m_SmoothPointer += (m_Pointer - m_SmoothPointer) * (1.0f - std::pow(0.86f, Step));

	for(int i = 0; i < (int)std::size(BEAMS); i++)
	{
		const vec2 Origin(BEAMS[i].m_Left * m_Width, -0.1f * m_Height);
		const float Deg = -std::atan2(m_SmoothPointer.x - Origin.x, m_SmoothPointer.y - Origin.y) * 180.0f / pi;
		const float Target = std::clamp(Deg, -MAX_TILT, MAX_TILT);
		m_aBeamAngles[i] += (Target - m_aBeamAngles[i]) * (1.0f - std::pow(1.0f - BEAMS[i].m_Follow, Step));
	}

	for(SDrop &Drop : m_vDrops)
	{
		const float Light = DropLight(Drop);
		Drop.m_Y += Drop.m_Speed * Step;
		Drop.m_X -= 0.15f * Step;
		if(Light > 0.0f)
		{
			const float Dx = Drop.m_X - m_SmoothPointer.x;
			const float Dist = distance(vec2(Drop.m_X, Drop.m_Y + Drop.m_Len * 0.5f), m_SmoothPointer);
			if(Dist > 0.0f)
				Drop.m_X += Dx / Dist * Light * Light * 3.2f * Step;
		}
		if(Drop.m_Y > m_Height)
		{
			Drop.m_Y = -Drop.m_Len;
			Drop.m_X = random_float(m_Width);
		}
	}

	const float Decay = std::pow(0.97f, Step);
	for(SSpark &Spark : m_vSparks)
	{
		Spark.m_Life -= Step;
		Spark.m_Pos += Spark.m_Vel * Step;
		Spark.m_Vel.x *= Decay;
		Spark.m_Vel.y = Spark.m_Vel.y * Decay + 0.012f * Step;
	}
	m_vSparks.erase(std::remove_if(m_vSparks.begin(), m_vSparks.end(), [](const SSpark &Spark) { return Spark.m_Life <= 0.0f; }), m_vSparks.end());

	for(SRipple &Ripple : m_vRipples)
		Ripple.m_Age += Dt;
	m_vRipples.erase(std::remove_if(m_vRipples.begin(), m_vRipples.end(), [](const SRipple &Ripple) { return Ripple.m_Age >= RIPPLE_DURATION; }), m_vRipples.end());
}

void CMenuRainBackground::DrawRadialGradient(vec2 Center, vec2 Radius, const SGradientStop *pStops, int NumStops, float Alpha)
{
	Graphics()->QuadsBegin();
	for(int s = 0; s + 1 < NumStops; s++)
	{
		const SGradientStop &Inner = pStops[s];
		const SGradientStop &Outer = pStops[s + 1];
		const ColorRGBA InnerColor = Inner.m_Color.WithMultipliedAlpha(Alpha);
		const ColorRGBA OuterColor = Outer.m_Color.WithMultipliedAlpha(Alpha);
		for(int i = 0; i < CIRCLE_SEGMENTS; i++)
		{
			const float A0 = (float)i / CIRCLE_SEGMENTS * 2.0f * pi;
			const float A1 = (float)(i + 1) / CIRCLE_SEGMENTS * 2.0f * pi;
			const vec2 D0(std::cos(A0) * Radius.x, std::sin(A0) * Radius.y);
			const vec2 D1(std::cos(A1) * Radius.x, std::sin(A1) * Radius.y);
			const IGraphics::CFreeformItem Item(Center + D0 * Inner.m_T, Center + D1 * Inner.m_T, Center + D0 * Outer.m_T, Center + D1 * Outer.m_T);
			Graphics()->SetColor4(InnerColor, InnerColor, OuterColor, OuterColor);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	Graphics()->QuadsEnd();
}

void CMenuRainBackground::DrawBlobs(float Alpha)
{
	const float Vmax = std::max(m_Width, m_Height) / 100.0f;
	const float Vw = m_Width / 100.0f;
	const float Vh = m_Height / 100.0f;
	const float BoxRadius = 29.0f * Vmax;

	const float UA = PingPong(m_Time, 26.0f);
	const SGradientStop aStopsA[] = {{0.0f, ColorRGBA(0.48f, 0.48f, 0.48f, 0.07f)}, {0.65f, ColorRGBA(0.48f, 0.48f, 0.48f, 0.0f)}};
	const vec2 CenterA(11.0f * Vmax + 9.0f * Vw * UA, 7.0f * Vmax + 7.0f * Vh * UA);
	const float RadiusA = BoxRadius * (1.0f + 0.15f * UA);
	DrawRadialGradient(CenterA, vec2(RadiusA, RadiusA), aStopsA, 2, Alpha);

	const float UB = PingPong(m_Time, 32.0f);
	const SGradientStop aStopsB[] = {{0.0f, ColorRGBA(0.60f, 0.62f, 0.66f, 0.05f)}, {0.65f, ColorRGBA(0.60f, 0.62f, 0.66f, 0.0f)}};
	const vec2 CenterB(m_Width - 11.0f * Vmax - 8.0f * Vw * UB, m_Height - 3.0f * Vmax - 6.0f * Vh * UB);
	const float RadiusB = BoxRadius * (1.1f - 0.15f * UB);
	DrawRadialGradient(CenterB, vec2(RadiusB, RadiusB), aStopsB, 2, Alpha);
}

void CMenuRainBackground::DrawBeams(float Alpha)
{
	Graphics()->BlendAdditive();
	Graphics()->QuadsBegin();
	for(int i = 0; i < (int)std::size(BEAMS); i++)
	{
		const SBeam &Beam = BEAMS[i];
		const float Sway = PingPong(m_Time - Beam.m_Delay, Beam.m_Sway);
		const float Angle = m_aBeamAngles[i] + (-6.0f + 12.0f * Sway);
		const float Opacity = 0.8f + 0.2f * Sway;

		const vec2 Apex(Beam.m_Left * m_Width, -0.1f * m_Height);
		const vec2 DirC = BeamDir(Angle);
		const vec2 DirL = BeamDir(Angle - BEAM_SPREAD);
		const vec2 DirR = BeamDir(Angle + BEAM_SPREAD);
		const float D1 = BEAM_FULL * m_Height;
		const float D2 = BEAM_END * m_Height;

		const ColorRGBA Lit = Beam.m_Color.WithMultipliedAlpha(Opacity * Alpha);
		const ColorRGBA Off = Beam.m_Color.WithAlpha(0.0f);

		const vec2 C1 = Apex + DirC * D1;
		const vec2 L1 = Apex + DirL * D1;
		const vec2 R1 = Apex + DirR * D1;
		const vec2 C2 = Apex + DirC * D2;
		const vec2 L2 = Apex + DirL * D2;
		const vec2 R2 = Apex + DirR * D2;

		const IGraphics::CFreeformItem aItems[] = {
			IGraphics::CFreeformItem(Apex, Apex, L1, C1),
			IGraphics::CFreeformItem(Apex, Apex, C1, R1),
			IGraphics::CFreeformItem(L1, C1, L2, C2),
			IGraphics::CFreeformItem(C1, R1, C2, R2),
		};
		Graphics()->SetColor4(Lit, Lit, Off, Lit);
		Graphics()->QuadsDrawFreeform(&aItems[0], 1);
		Graphics()->SetColor4(Lit, Lit, Lit, Off);
		Graphics()->QuadsDrawFreeform(&aItems[1], 1);
		Graphics()->SetColor4(Off, Lit, Off, Off);
		Graphics()->QuadsDrawFreeform(&aItems[2], 1);
		Graphics()->SetColor4(Lit, Off, Off, Off);
		Graphics()->QuadsDrawFreeform(&aItems[3], 1);
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CMenuRainBackground::DrawRain(const SPalette &Pal, float Alpha)
{
	Graphics()->QuadsBegin();
	for(const SDrop &Drop : m_vDrops)
	{
		const float Light = DropLight(Drop);
		const ColorRGBA Color(
			Pal.m_Rain.r + (1.0f - Pal.m_Rain.r) * Light,
			Pal.m_Rain.g + (1.0f - Pal.m_Rain.g) * Light,
			Pal.m_Rain.b + (1.0f - Pal.m_Rain.b) * Light,
			std::min(1.0f, Drop.m_Opacity + Light * 0.75f) * Alpha);
		const float HalfWidth = (1.1f + Light * 0.9f) * 0.5f;
		const float BottomX = Drop.m_X - 1.2f;
		const float BottomY = Drop.m_Y + Drop.m_Len;
		const IGraphics::CFreeformItem Item(
			Drop.m_X - HalfWidth, Drop.m_Y,
			Drop.m_X + HalfWidth, Drop.m_Y,
			BottomX - HalfWidth, BottomY,
			BottomX + HalfWidth, BottomY);
		Graphics()->SetColor(Color);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}
	Graphics()->QuadsEnd();
}

void CMenuRainBackground::DrawRipples(const SPalette &Pal)
{
	Graphics()->BlendAdditive();
	for(const SRipple &Ripple : m_vRipples)
	{
		const float Progress = EaseOutQuint(Ripple.m_Age / RIPPLE_DURATION);
		const float Radius = RIPPLE_RADIUS * Progress;
		const float Fade = 1.0f - Progress;
		if(Radius < 1.0f || Fade <= 0.0f)
			continue;

		const SGradientStop aFill[] = {{0.0f, Pal.m_FxA.WithAlpha(0.22f)}, {0.6f, Pal.m_FxA.WithAlpha(0.0f)}};
		DrawRadialGradient(Ripple.m_Pos, vec2(Radius, Radius), aFill, 2, Fade);

		const float Inset = std::max(0.0f, 1.0f - 18.0f / Radius);
		const float Ring = 0.75f / Radius;
		const float Glow = 1.0f + 24.0f / Radius;
		const SGradientStop aEdge[] = {
			{Inset, Pal.m_FxC.WithAlpha(0.0f)},
			{1.0f - Ring, Pal.m_FxC.WithAlpha(0.3f)},
			{1.0f - Ring, Pal.m_FxC.WithAlpha(0.7f)},
			{1.0f + Ring, Pal.m_FxC.WithAlpha(0.7f)},
			{1.0f + Ring, Pal.m_FxA.WithAlpha(0.45f)},
			{Glow, Pal.m_FxA.WithAlpha(0.0f)},
		};
		DrawRadialGradient(Ripple.m_Pos, vec2(Radius, Radius), aEdge, (int)std::size(aEdge), Fade);
	}
	Graphics()->BlendNormal();
}

void CMenuRainBackground::DrawSparks()
{
	Graphics()->BlendAdditive();
	for(const SSpark &Spark : m_vSparks)
	{
		const float K = Spark.m_Life / Spark.m_MaxLife;
		const SGradientStop aStops[] = {
			{0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f * K)},
			{0.35f, ColorRGBA(200.0f / 255.0f, 206.0f / 255.0f, 218.0f / 255.0f, 0.45f * K)},
			{1.0f, ColorRGBA(160.0f / 255.0f, 166.0f / 255.0f, 180.0f / 255.0f, 0.0f)},
		};
		const float Radius = Spark.m_Radius * 5.0f;
		DrawRadialGradient(Spark.m_Pos, vec2(Radius, Radius), aStops, 3, 1.0f);
	}
	Graphics()->BlendNormal();
}

void CMenuRainBackground::DrawGrain(float Alpha)
{
	if(!m_GrainTexture.IsValid())
		return;
	Graphics()->WrapNormal();
	Graphics()->TextureSet(m_GrainTexture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->QuadsSetSubset(0.0f, 0.0f, m_Width / GRAIN_TILE, m_Height / GRAIN_TILE);
	const IGraphics::CQuadItem Quad(0.0f, 0.0f, m_Width, m_Height);
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();
	Graphics()->TextureClear();
}

bool CMenuRainBackground::Render()
{
	if(!g_Config.m_ScMenuRainBackground)
		return false;

	const int64_t Now = time_get();
	float Dt = Client()->RenderFrameTime();
	if(m_LastRenderTime == 0 || Now - m_LastRenderTime > time_freq() / 2)
	{
		m_FadeTime = 0.0f;
		m_PointerInit = false;
		Dt = 0.0f;
	}
	m_LastRenderTime = Now;
	Dt = std::clamp(Dt, 0.0f, 0.1f);

	const float Height = VIRTUAL_HEIGHT;
	const float Width = VIRTUAL_HEIGHT * Graphics()->ScreenAspect();
	if(Width != m_Width || Height != m_Height || m_vDrops.empty())
	{
		m_Width = Width;
		m_Height = Height;
		ResetDrops();
	}

	const CUIRect *pScreen = Ui()->Screen();
	const vec2 Mouse = Ui()->MousePos();
	m_Pointer = vec2(Mouse.x / pScreen->w * Width, Mouse.y / pScreen->h * Height);
	if(!m_PointerInit)
	{
		m_SmoothPointer = m_Pointer;
		m_PointerInit = true;
	}
	if(Ui()->MouseButtonClicked(0))
		SpawnBurst(m_Pointer);

	m_Time += Dt;
	m_FadeTime += Dt;
	Update(Dt);

	const SPalette Pal = Palette();
	const float RainAlpha = 0.7f * EaseOut(m_FadeTime / RAIN_FADE);
	const float AtmosphereAlpha = EaseOut(m_FadeTime / ATMOSPHERE_FADE);

	const CScreenRect PreviousScreen = Graphics()->GetScreen();
	Graphics()->MapScreenToSize(Width, Height);
	Graphics()->TextureClear();
	Graphics()->BlendNormal();
	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), IGraphics::CORNER_NONE, 0.0f);

	const SGradientStop aHalo[] = {{0.0f, Pal.m_Halo}, {0.6f, Pal.m_Halo.WithAlpha(0.0f)}};
	DrawRadialGradient(vec2(Width * 0.5f, Height * 0.5f), vec2(Width / std::sqrt(2.0f), Height / std::sqrt(2.0f)), aHalo, 2, 1.0f);

	DrawBeams(AtmosphereAlpha);
	DrawBlobs(AtmosphereAlpha);

	const SGradientStop aGlowWide[] = {{0.0f, ColorRGBA(0.86f, 0.87f, 0.89f, 0.08f)}, {0.65f, ColorRGBA(0.86f, 0.87f, 0.89f, 0.0f)}};
	DrawRadialGradient(m_SmoothPointer, vec2(260.0f, 260.0f), aGlowWide, 2, AtmosphereAlpha);
	const SGradientStop aGlowTight[] = {{0.0f, ColorRGBA(0.95f, 0.91f, 0.99f, 0.07f)}, {0.7f, ColorRGBA(0.95f, 0.91f, 0.99f, 0.0f)}};
	DrawRadialGradient(m_Pointer, vec2(70.0f, 70.0f), aGlowTight, 2, AtmosphereAlpha);

	DrawRain(Pal, RainAlpha);
	DrawRipples(Pal);
	DrawSparks();
	DrawGrain(AtmosphereAlpha);

	Graphics()->BlendNormal();
	Graphics()->MapScreen(PreviousScreen);
	return true;
}
