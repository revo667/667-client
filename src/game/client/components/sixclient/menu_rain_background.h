#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_MENU_RAIN_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_MENU_RAIN_BACKGROUND_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include <cstdint>
#include <vector>

class CMenuRainBackground : public CComponent
{
	struct SDrop
	{
		float m_X;
		float m_Y;
		float m_Len;
		float m_Speed;
		float m_Opacity;
	};
	struct SSpark
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Life;
		float m_MaxLife;
		float m_Radius;
	};
	struct SRipple
	{
		vec2 m_Pos;
		float m_Age;
	};
	struct SGradientStop
	{
		float m_T;
		ColorRGBA m_Color;
	};
	struct SPalette
	{
		ColorRGBA m_Rain;
		ColorRGBA m_Halo;
		ColorRGBA m_FxA;
		ColorRGBA m_FxC;
	};

	std::vector<SDrop> m_vDrops;
	std::vector<SSpark> m_vSparks;
	std::vector<SRipple> m_vRipples;
	float m_aBeamAngles[5] = {};
	vec2 m_Pointer = vec2(0.0f, 0.0f);
	vec2 m_SmoothPointer = vec2(0.0f, 0.0f);
	bool m_PointerInit = false;
	float m_Width = 0.0f;
	float m_Height = 0.0f;
	float m_Time = 0.0f;
	float m_FadeTime = 0.0f;
	int64_t m_LastRenderTime = 0;
	IGraphics::CTextureHandle m_GrainTexture;

	static SPalette Palette();
	float DropLight(const SDrop &Drop) const;
	void ResetDrops();
	void SpawnBurst(vec2 Pos);
	void Update(float Dt);
	void DrawRadialGradient(vec2 Center, vec2 Radius, const SGradientStop *pStops, int NumStops, float Alpha);
	void DrawBlobs(float Alpha);
	void DrawBeams(float Alpha);
	void DrawRain(const SPalette &Pal, float Alpha);
	void DrawRipples(const SPalette &Pal);
	void DrawSparks();
	void DrawGrain(float Alpha);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	bool Render();
};

#endif
