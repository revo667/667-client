#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_ASSETS_MARKET_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_ASSETS_MARKET_H

#include "teedata_client.h"

#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/components/skins.h>
#include <game/client/skin.h>

#include <memory>
#include <vector>

class IHttpRequest;

class CAssetsMarket : public CComponent
{
	class CPreviewJob;
	class CInstallJob;
	using CSkinLoadData = CSkins::CSkinLoadData;

public:
	enum class EPageState
	{
		IDLE,
		LOADING,
		READY,
		ERROR,
	};

	enum class EInstallState
	{
		NONE,
		INSTALLING,
		INSTALLED,
		FAILED,
	};

	class CEntry
	{
		friend class CAssetsMarket;

	public:
		CTeedataItem m_Item;
		EInstallState m_InstallState = EInstallState::NONE;
		char m_aError[64] = "";
		IGraphics::CTextureHandle m_PreviewTexture;
		std::unique_ptr<CSkin> m_pPreviewSkin;

		bool HasPreview() const { return m_PreviewTexture.IsValid() || m_pPreviewSkin != nullptr; }
		bool PreviewFailed() const { return m_PreviewFailed; }

	private:
		bool m_PreviewRequested = false;
		bool m_PreviewStarted = false;
		bool m_PreviewFailed = false;
		std::shared_ptr<IHttpRequest> m_pPreviewRequest;
		std::shared_ptr<CPreviewJob> m_pPreviewJob;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnShutdown() override;
	void OnUpdate() override;

	void SetQuery(ETeedataAsset Asset, const char *pSearch, ETeedataSort Sort, int Page, bool Debounce);
	void Reload();
	void RequestPreview(int Index);
	void Install(int Index);

	ETeedataAsset Asset() const { return m_Asset; }
	int Page() const { return m_Page; }
	int NumPages() const;
	int TotalCount() const { return m_TotalCount; }
	EPageState PageState() const { return m_PageState; }
	const char *Error() const { return m_aError; }
	std::vector<CEntry> &Entries() { return m_vEntries; }

private:
	enum
	{
		MAX_PREVIEW_REQUESTS = 6,
		MAX_CACHED_PAGES = 12,
		CACHE_LIFETIME_SECONDS = 300,
		SEARCH_DEBOUNCE_MS = 400,
	};

	class CCachedPage
	{
	public:
		char m_aUrl[CTeedataClient::MAX_URL_LENGTH];
		int64_t m_Time;
		CTeedataPage m_Page;
	};

	class CInstall
	{
	public:
		ETeedataAsset m_Asset;
		CTeedataItem m_Item;
		std::shared_ptr<IHttpRequest> m_pRequest;
		std::shared_ptr<CInstallJob> m_pJob;
	};

	ETeedataAsset m_Asset = ETeedataAsset::SKIN;
	char m_aSearch[CTeedataClient::MAX_SEARCH_LENGTH + 1] = "";
	ETeedataSort m_Sort = ETeedataSort::DOWNLOAD;
	int m_Page = 1;
	bool m_QueryDirty = false;
	bool m_BypassCache = false;
	int64_t m_RequestTime = 0;

	char m_aRequestUrl[CTeedataClient::MAX_URL_LENGTH] = "";
	std::shared_ptr<IHttpRequest> m_pPageRequest;
	EPageState m_PageState = EPageState::IDLE;
	char m_aError[128] = "";
	int m_TotalCount = 0;
	std::vector<CEntry> m_vEntries;
	std::vector<CCachedPage> m_vCache;

	std::vector<CInstall> m_vInstalls;
	bool m_SkinsNeedRefresh = false;

	bool Enabled() const;
	bool LoadSkinData(const char *pName, CSkinLoadData &Data) const;
	void StartPageRequest();
	void UpdatePageRequest();
	void ApplyPage(const CTeedataPage &Page);
	void SetError(const char *pError);
	void UpdatePreviews();
	void UpdateInstalls();
	void FinishPreview(CEntry &Entry);
	void FinishInstall(const CInstall &Install, bool Success, const char *pError);
	void UnloadEntry(CEntry &Entry);
	void ClearEntries();
	bool IsInstalling(ETeedataAsset Asset, const char *pName) const;
	bool IsInstalledOnDisk(ETeedataAsset Asset, const char *pName);
};

#endif
