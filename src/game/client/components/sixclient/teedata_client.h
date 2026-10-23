#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_TEEDATA_CLIENT_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_TEEDATA_CLIENT_H

#include <base/hash.h>

#include <cstddef>
#include <vector>

typedef struct _json_value json_value;

enum class ETeedataAsset
{
	SKIN,
	GAMESKIN,
	ENTITY,
	EMOTICON,
	PARTICLE,
	CURSOR,
	FONT,
	NUM,
};

enum class ETeedataSort
{
	DOWNLOAD,
	TRENDING,
	DATE,
	NAME,
	HD,
	NUM,
};

class CTeedataItem
{
public:
	enum
	{
		MAX_NAME_LENGTH = 50,
		MAX_AUTHOR_LENGTH = 32,
	};

	int m_Id = 0;
	char m_aName[MAX_NAME_LENGTH] = "";
	char m_aAuthor[MAX_AUTHOR_LENGTH] = "";
	MD5_DIGEST m_Checksum = {};
	int m_Size = 0;
	int m_Width = 0;
	int m_Height = 0;
	int m_Downloads = 0;
};

class CTeedataPage
{
public:
	int m_TotalCount = 0;
	std::vector<CTeedataItem> m_vItems;
};

class CTeedataClient
{
public:
	static constexpr int ITEMS_PER_PAGE = 24;
	static constexpr int MAX_PAGE_ITEMS = 100;
	static constexpr int MAX_SEARCH_LENGTH = 32;
	static constexpr int MAX_URL_LENGTH = 256;
	static constexpr int MAX_BASE_URL_LENGTH = 128;
	static constexpr size_t MAX_PAGE_RESPONSE_SIZE = 1024 * 1024;
	static constexpr size_t MAX_ASSET_SIZE = 10 * 1024 * 1024;
	static constexpr size_t MAX_THUMBNAIL_SIZE = 512 * 1024;
	static constexpr unsigned MAX_IMAGE_DIMENSION = 4096;

	static const char *ApiName(ETeedataAsset Asset);
	static const char *DatabaseFolder(ETeedataAsset Asset);
	static const char *InstallFolder(ETeedataAsset Asset);
	static int MaxNameLength(ETeedataAsset Asset);
	static const char *SortName(ETeedataSort Sort);

	static bool IsValidName(ETeedataAsset Asset, const char *pName);
	static bool IsValidBaseUrl(const char *pBaseUrl);
	static bool BuildPageUrl(char *pBuf, size_t BufSize, const char *pBaseUrl, ETeedataAsset Asset, const char *pSearch, ETeedataSort Sort, int Page);
	static bool BuildFileUrl(char *pBuf, size_t BufSize, const char *pBaseUrl, ETeedataAsset Asset, const char *pName, bool Thumbnail);
	static bool BuildInstallPath(char *pBuf, size_t BufSize, ETeedataAsset Asset, const char *pName);
	static bool ParsePage(const json_value *pRoot, ETeedataAsset Asset, CTeedataPage &Page);
	static bool IsPng(const unsigned char *pData, size_t Size);
	static bool ValidateFile(const unsigned char *pData, size_t Size, const CTeedataItem &Item);
};

#endif
