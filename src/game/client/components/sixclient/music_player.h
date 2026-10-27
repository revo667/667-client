#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_MUSIC_PLAYER_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_MUSIC_PLAYER_H

#include "music_player_lyrics.h"

#include <base/color.h>

#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <cstdint>
#include <memory>
#include <string>

class CSpotifyConnect;
class ITextRender;

float MusicPlayerLegacyHudAnchorOffsetCanvasX(ITextRender *pTextRender, int LayoutScale, float HudWidth, float TextScale, bool *pOutReady);

class CMusicPlayer : public CComponent
{
public:
	struct SHudReservation
	{
		CUIRect m_Rect{};
		bool m_Visible = false;
		bool m_Active = false;
		float m_PushAmount = 0.0f;
	};

	struct SDockInfo
	{
		bool m_Valid = false;
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		bool m_Playing = false;
		bool m_CanPrev = false;
		bool m_CanPlayPause = false;
		bool m_CanNext = false;
		bool m_HasArt = false;
		IGraphics::CTextureHandle m_ArtTexture;
		ColorRGBA m_Accent = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		ColorRGBA m_PaletteMid = ColorRGBA(0.15f, 0.16f, 0.18f, 1.0f);
		ColorRGBA m_PaletteDark = ColorRGBA(0.07f, 0.08f, 0.09f, 1.0f);
		bool m_HudVisible = false;
		CUIRect m_HudRect{};
		CMusicPlayerLyrics::EDisplayState m_LyricsState = CMusicPlayerLyrics::EDisplayState::Idle;
		CMusicPlayerLyrics::SLineWindow m_Lyrics;
	};

	struct SSpotifyTrack
	{
		bool m_Valid = false;
		bool m_Playing = false;
		bool m_Buffering = false;
		bool m_FromConnect = false;
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		std::string m_Uri;
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
	};

	CMusicPlayer();
	~CMusicPlayer() override;

	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnShutdown() override;
	void OnWindowResize() override;

	SHudReservation HudReservation() const;
	vec2 GetHudPushOffsetForRect(const CUIRect &Rect, float CanvasWidth, float CanvasHeight, float Padding = 0.0f) const;
	CUIRect GetHudEditorRect(bool ForcePreview = false) const;
	bool GetDockInfo(SDockInfo &Out) const;
	void DockPrevious();
	void DockPlayPause();
	void DockNext();
	bool GetSpotifyTrack(SSpotifyTrack &Out) const;
	void PlaySpotifyTrack(const char *pUri, int64_t PositionMs);
	void SeekSpotifyTo(int64_t PositionMs);
	void SetSpotifyPlaying(bool Playing);
	bool SpotifyConnectReady() const;
	CSpotifyConnect &SpotifyConnect() { return *m_pSpotifyConnect; }
	void RenderHudEditor(bool ForcePreview);

private:
	class CImpl;
	std::unique_ptr<CImpl> m_pImpl;
	std::unique_ptr<CSpotifyConnect> m_pSpotifyConnect;
	bool m_SpotifyHelperChecked = false;
	std::string m_SpotifyHelperPath;

	void RenderMusicPlayer(bool ForcePreview);
	void RenderMusicPlayerCard(bool ForcePreview);
	void EnsureImpl();
	void UpdateSpotifyConnect();
	void UpdateLayout();
};

#endif
