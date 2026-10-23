#include "assets_market.h"

#include <base/io.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

class CAssetsMarket::CPreviewJob : public IJob
{
public:
	CPreviewJob(const CAssetsMarket *pMarket, ETeedataAsset Asset, const char *pName, std::shared_ptr<IHttpRequest> pRequest) :
		m_Asset(Asset),
		m_pMarket(pMarket),
		m_pRequest(std::move(pRequest))
	{
		str_copy(m_aName, pName);
		Abortable(true);
	}

	~CPreviewJob() override
	{
		m_Data.m_Info.Free();
		m_Data.m_InfoGrayscale.Free();
	}

	ETeedataAsset m_Asset;
	CSkinLoadData m_Data;
	bool m_Success = false;

protected:
	void Run() override
	{
		unsigned char *pData;
		size_t DataSize;
		m_pRequest->Result(&pData, &DataSize);
		if(!CTeedataClient::IsPng(pData, DataSize) || !m_pMarket->Graphics()->LoadPng(m_Data.m_Info, pData, DataSize, m_aName))
			return;
		if(State() == IJob::STATE_ABORTED)
			return;
		m_Success = m_Asset != ETeedataAsset::SKIN || m_pMarket->LoadSkinData(m_aName, m_Data);
	}

private:
	const CAssetsMarket *m_pMarket;
	char m_aName[CTeedataItem::MAX_NAME_LENGTH];
	std::shared_ptr<IHttpRequest> m_pRequest;
};

class CAssetsMarket::CInstallJob : public IJob
{
public:
	CInstallJob(const CAssetsMarket *pMarket, ETeedataAsset Asset, const CTeedataItem &Item, std::shared_ptr<IHttpRequest> pRequest) :
		m_pMarket(pMarket),
		m_Asset(Asset),
		m_Item(Item),
		m_pRequest(std::move(pRequest))
	{
		Abortable(true);
	}

	bool m_Success = false;
	char m_aError[64] = "";

protected:
	void Run() override
	{
		unsigned char *pData;
		size_t DataSize;
		m_pRequest->Result(&pData, &DataSize);
		if(!CTeedataClient::ValidateFile(pData, DataSize, m_Item))
		{
			str_copy(m_aError, "File failed verification");
			return;
		}

		CSkinLoadData Data;
		bool Valid = m_pMarket->Graphics()->LoadPng(Data.m_Info, pData, DataSize, m_Item.m_aName);
		if(Valid && m_Asset == ETeedataAsset::SKIN)
			Valid = m_pMarket->LoadSkinData(m_Item.m_aName, Data);
		Data.m_Info.Free();
		Data.m_InfoGrayscale.Free();
		if(!Valid)
		{
			str_copy(m_aError, "Unsupported image");
			return;
		}
		if(State() == IJob::STATE_ABORTED)
			return;

		if(!WriteFile(pData, DataSize))
		{
			str_copy(m_aError, "Could not save file");
			return;
		}
		m_Success = true;
	}

private:
	const CAssetsMarket *m_pMarket;
	ETeedataAsset m_Asset;
	CTeedataItem m_Item;
	std::shared_ptr<IHttpRequest> m_pRequest;

	bool WriteFile(const unsigned char *pData, size_t DataSize) const
	{
		IStorage *pStorage = m_pMarket->Storage();
		char aPath[IO_MAX_PATH_LENGTH];
		if(!CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), m_Asset, m_Item.m_aName))
			return false;

		char aFolder[IO_MAX_PATH_LENGTH];
		str_copy(aFolder, CTeedataClient::InstallFolder(m_Asset));
		for(char *pSeparator = aFolder; (pSeparator = (char *)str_find(pSeparator, "/")) != nullptr; ++pSeparator)
		{
			*pSeparator = '\0';
			pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE);
			*pSeparator = '/';
		}
		pStorage->CreateFolder(aFolder, IStorage::TYPE_SAVE);

		if(pStorage->FileExists(aPath, IStorage::TYPE_SAVE))
			return true;

		char aTmpPath[IO_MAX_PATH_LENGTH];
		str_format(aTmpPath, sizeof(aTmpPath), "%s.tmp", aPath);
		IOHANDLE File = pStorage->OpenFile(aTmpPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Written = io_write(File, pData, DataSize) == DataSize;
		const bool Closed = io_close(File) == 0;
		if(!Written || !Closed || !pStorage->RenameFile(aTmpPath, aPath, IStorage::TYPE_SAVE))
		{
			pStorage->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
			return false;
		}
		return true;
	}
};

bool CAssetsMarket::Enabled() const
{
	return g_Config.m_ScAssetsMarket != 0;
}

bool CAssetsMarket::LoadSkinData(const char *pName, CSkinLoadData &Data) const
{
	return GameClient()->m_Skins.LoadSkinData(pName, Data);
}

int CAssetsMarket::NumPages() const
{
	return std::max(1, (m_TotalCount + CTeedataClient::ITEMS_PER_PAGE - 1) / CTeedataClient::ITEMS_PER_PAGE);
}

void CAssetsMarket::SetQuery(ETeedataAsset Asset, const char *pSearch, ETeedataSort Sort, int Page, bool Debounce)
{
	Page = std::max(Page, 1);
	if(m_PageState != EPageState::IDLE && Asset == m_Asset && Sort == m_Sort && Page == m_Page && str_comp(pSearch, m_aSearch) == 0)
		return;

	if(Asset != m_Asset)
	{
		ClearEntries();
		m_TotalCount = 0;
	}
	m_Asset = Asset;
	str_copy(m_aSearch, pSearch);
	m_Sort = Sort;
	m_Page = Page;
	m_QueryDirty = true;
	m_RequestTime = time_get() + (Debounce ? time_freq() * SEARCH_DEBOUNCE_MS / 1000 : 0);
	if(m_pPageRequest)
	{
		m_pPageRequest->Abort();
		m_pPageRequest = nullptr;
	}
	m_PageState = EPageState::LOADING;
}

void CAssetsMarket::Reload()
{
	if(m_pPageRequest)
	{
		m_pPageRequest->Abort();
		m_pPageRequest = nullptr;
	}
	m_QueryDirty = true;
	m_BypassCache = true;
	m_RequestTime = time_get();
	m_PageState = EPageState::LOADING;
}

void CAssetsMarket::SetError(const char *pError)
{
	str_copy(m_aError, pError);
	m_PageState = EPageState::ERROR;
	log_error("assets_market", "%s", pError);
}

void CAssetsMarket::StartPageRequest()
{
	m_QueryDirty = false;

	char aUrl[CTeedataClient::MAX_URL_LENGTH];
	if(!CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), g_Config.m_ScAssetsMarketUrl, m_Asset, m_aSearch, m_Sort, m_Page))
	{
		SetError(ScLocalize("sc_assets_market_url is not a valid https:// address"));
		return;
	}

	const int64_t Now = time_get();
	m_vCache.erase(std::remove_if(m_vCache.begin(), m_vCache.end(), [&](const CCachedPage &Cached) {
		return Now - Cached.m_Time > time_freq() * CACHE_LIFETIME_SECONDS || (m_BypassCache && str_comp(Cached.m_aUrl, aUrl) == 0);
	}),
		m_vCache.end());
	m_BypassCache = false;

	for(const CCachedPage &Cached : m_vCache)
	{
		if(str_comp(Cached.m_aUrl, aUrl) == 0)
		{
			ApplyPage(Cached.m_Page);
			return;
		}
	}

	str_copy(m_aRequestUrl, aUrl);
	m_pPageRequest = HttpGet(aUrl);
	m_pPageRequest->Timeout(CTimeout{8000, 20000, 500, 10});
	m_pPageRequest->MaxResponseSize(CTeedataClient::MAX_PAGE_RESPONSE_SIZE);
	m_pPageRequest->LogProgress(HTTPLOG::FAILURE);
	m_pPageRequest->FailOnErrorStatus(false);
	Http()->Run(m_pPageRequest);
	m_PageState = EPageState::LOADING;
}

void CAssetsMarket::UpdatePageRequest()
{
	if(m_QueryDirty && time_get() >= m_RequestTime)
		StartPageRequest();
	if(!m_pPageRequest || !m_pPageRequest->Done())
		return;

	const std::shared_ptr<IHttpRequest> pRequest = std::move(m_pPageRequest);
	m_pPageRequest = nullptr;
	if(pRequest->State() != EHttpState::DONE)
	{
		if(pRequest->State() != EHttpState::ABORTED)
			SetError(ScLocalize("Could not reach teedata.net. Check your connection and try again."));
		return;
	}
	if(pRequest->StatusCode() == 429)
	{
		SetError(ScLocalize("teedata.net is receiving too many requests, try again in a minute"));
		return;
	}
	if(pRequest->StatusCode() != 200)
	{
		char aError[128];
		str_format(aError, sizeof(aError), ScLocalize("teedata.net answered with HTTP %d"), pRequest->StatusCode());
		SetError(aError);
		return;
	}

	json_value *pJson = pRequest->ResultJson();
	CTeedataPage Page;
	const bool Parsed = CTeedataClient::ParsePage(pJson, m_Asset, Page);
	if(pJson)
		json_value_free(pJson);
	if(!Parsed)
	{
		SetError(ScLocalize("teedata.net sent a response that could not be read"));
		return;
	}

	if(m_vCache.size() >= MAX_CACHED_PAGES)
		m_vCache.erase(m_vCache.begin());
	CCachedPage &Cached = m_vCache.emplace_back();
	str_copy(Cached.m_aUrl, m_aRequestUrl);
	Cached.m_Time = time_get();
	Cached.m_Page = Page;

	ApplyPage(Page);
}

bool CAssetsMarket::IsInstalling(ETeedataAsset Asset, const char *pName) const
{
	return std::any_of(m_vInstalls.begin(), m_vInstalls.end(), [&](const CInstall &Install) {
		return Install.m_Asset == Asset && str_comp(Install.m_Item.m_aName, pName) == 0;
	});
}

bool CAssetsMarket::IsInstalledOnDisk(ETeedataAsset Asset, const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), Asset, pName))
		return false;
	if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
		return true;
	if(Asset == ETeedataAsset::SKIN || Asset == ETeedataAsset::FONT)
		return false;
	aPath[str_length(aPath) - str_length(".png")] = '\0';
	return Storage()->FolderExists(aPath, IStorage::TYPE_ALL);
}

void CAssetsMarket::ApplyPage(const CTeedataPage &Page)
{
	ClearEntries();
	m_TotalCount = Page.m_TotalCount;
	m_vEntries.resize(Page.m_vItems.size());
	for(size_t i = 0; i < Page.m_vItems.size(); ++i)
	{
		CEntry &Entry = m_vEntries[i];
		Entry.m_Item = Page.m_vItems[i];
		if(IsInstalling(m_Asset, Entry.m_Item.m_aName))
			Entry.m_InstallState = EInstallState::INSTALLING;
		else if(IsInstalledOnDisk(m_Asset, Entry.m_Item.m_aName))
			Entry.m_InstallState = EInstallState::INSTALLED;
	}
	m_aError[0] = '\0';
	m_PageState = EPageState::READY;
}

void CAssetsMarket::UnloadEntry(CEntry &Entry)
{
	if(Entry.m_pPreviewRequest)
	{
		Entry.m_pPreviewRequest->Abort();
		Entry.m_pPreviewRequest = nullptr;
	}
	if(Entry.m_pPreviewJob)
	{
		Entry.m_pPreviewJob->Abort();
		Entry.m_pPreviewJob = nullptr;
	}
	if(Entry.m_PreviewTexture.IsValid())
		Graphics()->UnloadTexture(&Entry.m_PreviewTexture);
	if(Entry.m_pPreviewSkin)
	{
		Entry.m_pPreviewSkin->m_OriginalSkin.Unload(Graphics());
		Entry.m_pPreviewSkin = nullptr;
	}
}

void CAssetsMarket::ClearEntries()
{
	for(CEntry &Entry : m_vEntries)
		UnloadEntry(Entry);
	m_vEntries.clear();
}

void CAssetsMarket::RequestPreview(int Index)
{
	if(Index >= 0 && Index < (int)m_vEntries.size())
		m_vEntries[Index].m_PreviewRequested = true;
}

void CAssetsMarket::FinishPreview(CEntry &Entry)
{
	const std::shared_ptr<CPreviewJob> pJob = std::move(Entry.m_pPreviewJob);
	Entry.m_pPreviewJob = nullptr;
	if(pJob->State() != IJob::STATE_DONE || !pJob->m_Success)
	{
		Entry.m_PreviewFailed = true;
		return;
	}

	if(pJob->m_Asset != ETeedataAsset::SKIN)
	{
		Entry.m_PreviewTexture = Graphics()->LoadTextureRawMove(pJob->m_Data.m_Info, 0, Entry.m_Item.m_aName);
		Entry.m_PreviewFailed = !Entry.m_PreviewTexture.IsValid();
		return;
	}

	const CImageInfo &Info = pJob->m_Data.m_Info;
	auto pSkin = std::make_unique<CSkin>(Entry.m_Item.m_aName);
	pSkin->m_OriginalSkin.m_Body = Graphics()->LoadSpriteTexture(Info, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY]);
	pSkin->m_OriginalSkin.m_BodyOutline = Graphics()->LoadSpriteTexture(Info, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	pSkin->m_OriginalSkin.m_Feet = Graphics()->LoadSpriteTexture(Info, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT]);
	pSkin->m_OriginalSkin.m_FeetOutline = Graphics()->LoadSpriteTexture(Info, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	pSkin->m_OriginalSkin.m_Hands = Graphics()->LoadSpriteTexture(Info, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND]);
	pSkin->m_OriginalSkin.m_HandsOutline = Graphics()->LoadSpriteTexture(Info, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND_OUTLINE]);
	for(size_t i = 0; i < std::size(pSkin->m_OriginalSkin.m_aEyes); ++i)
		pSkin->m_OriginalSkin.m_aEyes[i] = Graphics()->LoadSpriteTexture(Info, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_EYE_NORMAL + i]);
	pSkin->m_Metrics = pJob->m_Data.m_Metrics;
	pSkin->m_BloodColor = pJob->m_Data.m_BloodColor;
	Entry.m_pPreviewSkin = std::move(pSkin);
}

void CAssetsMarket::UpdatePreviews()
{
	int NumRequests = 0;
	for(const CEntry &Entry : m_vEntries)
		NumRequests += Entry.m_pPreviewRequest != nullptr;

	for(CEntry &Entry : m_vEntries)
	{
		if(Entry.m_pPreviewRequest && Entry.m_pPreviewRequest->Done())
		{
			const std::shared_ptr<IHttpRequest> pRequest = std::move(Entry.m_pPreviewRequest);
			Entry.m_pPreviewRequest = nullptr;
			--NumRequests;
			if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200)
			{
				Entry.m_pPreviewJob = std::make_shared<CPreviewJob>(this, m_Asset, Entry.m_Item.m_aName, pRequest);
				Engine()->AddJob(Entry.m_pPreviewJob);
			}
			else
			{
				Entry.m_PreviewFailed = true;
			}
		}

		if(Entry.m_pPreviewJob && Entry.m_pPreviewJob->Done())
			FinishPreview(Entry);

		if(!Entry.m_PreviewRequested || Entry.m_PreviewStarted || NumRequests >= MAX_PREVIEW_REQUESTS)
			continue;

		Entry.m_PreviewStarted = true;
		char aUrl[CTeedataClient::MAX_URL_LENGTH];
		if(!CTeedataClient::BuildFileUrl(aUrl, sizeof(aUrl), g_Config.m_ScAssetsMarketUrl, m_Asset, Entry.m_Item.m_aName, true))
		{
			Entry.m_PreviewFailed = true;
			continue;
		}
		Entry.m_pPreviewRequest = HttpGet(aUrl);
		Entry.m_pPreviewRequest->Timeout(CTimeout{8000, 15000, 1024, 8});
		Entry.m_pPreviewRequest->MaxResponseSize(CTeedataClient::MAX_THUMBNAIL_SIZE);
		Entry.m_pPreviewRequest->LogProgress(HTTPLOG::FAILURE);
		Entry.m_pPreviewRequest->FailOnErrorStatus(false);
		Http()->Run(Entry.m_pPreviewRequest);
		++NumRequests;
	}
}

void CAssetsMarket::Install(int Index)
{
	if(!Enabled() || Index < 0 || Index >= (int)m_vEntries.size())
		return;
	CEntry &Entry = m_vEntries[Index];
	if(Entry.m_InstallState == EInstallState::INSTALLING || Entry.m_InstallState == EInstallState::INSTALLED)
		return;
	if(IsInstalledOnDisk(m_Asset, Entry.m_Item.m_aName))
	{
		Entry.m_InstallState = EInstallState::INSTALLED;
		return;
	}

	char aUrl[CTeedataClient::MAX_URL_LENGTH];
	if(!CTeedataClient::BuildFileUrl(aUrl, sizeof(aUrl), g_Config.m_ScAssetsMarketUrl, m_Asset, Entry.m_Item.m_aName, false))
	{
		Entry.m_InstallState = EInstallState::FAILED;
		str_copy(Entry.m_aError, "Invalid URL");
		return;
	}

	CInstall &NewInstall = m_vInstalls.emplace_back();
	NewInstall.m_Asset = m_Asset;
	NewInstall.m_Item = Entry.m_Item;
	NewInstall.m_pRequest = HttpGet(aUrl);
	NewInstall.m_pRequest->Timeout(CTimeout{10000, 0, 8192, 10});
	NewInstall.m_pRequest->MaxResponseSize(CTeedataClient::MAX_ASSET_SIZE);
	NewInstall.m_pRequest->LogProgress(HTTPLOG::FAILURE);
	NewInstall.m_pRequest->FailOnErrorStatus(false);
	Http()->Run(NewInstall.m_pRequest);
	Entry.m_InstallState = EInstallState::INSTALLING;
	Entry.m_aError[0] = '\0';
}

void CAssetsMarket::FinishInstall(const CInstall &Install, bool Success, const char *pError)
{
	for(CEntry &Entry : m_vEntries)
	{
		if(Install.m_Asset != m_Asset || str_comp(Entry.m_Item.m_aName, Install.m_Item.m_aName) != 0)
			continue;
		Entry.m_InstallState = Success ? EInstallState::INSTALLED : EInstallState::FAILED;
		str_copy(Entry.m_aError, pError);
	}

	if(!Success)
	{
		log_error("assets_market", "Failed to install %s '%s': %s", CTeedataClient::ApiName(Install.m_Asset), Install.m_Item.m_aName, pError);
		return;
	}
	log_info("assets_market", "Installed %s '%s'", CTeedataClient::ApiName(Install.m_Asset), Install.m_Item.m_aName);
	if(Install.m_Asset == ETeedataAsset::SKIN)
		m_SkinsNeedRefresh = true;
	else if(const char *pTab = str_startswith(CTeedataClient::InstallFolder(Install.m_Asset), "assets/"))
		GameClient()->m_Menus.RefreshCustomAssetList(pTab);
}

void CAssetsMarket::UpdateInstalls()
{
	for(auto It = m_vInstalls.begin(); It != m_vInstalls.end();)
	{
		CInstall &Install = *It;
		if(Install.m_pRequest)
		{
			if(!Install.m_pRequest->Done())
			{
				++It;
				continue;
			}
			if(Install.m_pRequest->State() != EHttpState::DONE || Install.m_pRequest->StatusCode() != 200)
			{
				char aError[64];
				if(Install.m_pRequest->State() == EHttpState::DONE)
					str_format(aError, sizeof(aError), "HTTP %d", Install.m_pRequest->StatusCode());
				else
					str_copy(aError, "Download failed");
				FinishInstall(Install, false, aError);
				It = m_vInstalls.erase(It);
				continue;
			}
			Install.m_pJob = std::make_shared<CInstallJob>(this, Install.m_Asset, Install.m_Item, Install.m_pRequest);
			Install.m_pRequest = nullptr;
			Engine()->AddJob(Install.m_pJob);
		}

		if(!Install.m_pJob->Done())
		{
			++It;
			continue;
		}
		FinishInstall(Install, Install.m_pJob->m_Success, Install.m_pJob->m_aError);
		It = m_vInstalls.erase(It);
	}

	if(m_SkinsNeedRefresh && !std::any_of(m_vInstalls.begin(), m_vInstalls.end(), [](const CInstall &Install) { return Install.m_Asset == ETeedataAsset::SKIN; }))
	{
		m_SkinsNeedRefresh = false;
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
	}
}

void CAssetsMarket::OnUpdate()
{
	if(Enabled())
	{
		UpdatePageRequest();
		UpdatePreviews();
	}
	UpdateInstalls();
}

void CAssetsMarket::OnShutdown()
{
	if(m_pPageRequest)
	{
		m_pPageRequest->Abort();
		m_pPageRequest = nullptr;
	}
	ClearEntries();
	for(CInstall &Install : m_vInstalls)
	{
		if(Install.m_pRequest)
			Install.m_pRequest->Abort();
		if(Install.m_pJob)
			Install.m_pJob->Abort();
	}
	m_vInstalls.clear();
	m_vCache.clear();
}
