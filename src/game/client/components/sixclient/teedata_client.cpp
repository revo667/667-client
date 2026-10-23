#include "teedata_client.h"

#include <base/bytes.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/http.h>
#include <engine/shared/json.h>
#include <engine/shared/protocol.h>

#include <algorithm>

namespace
{
	const unsigned char PNG_SIGNATURE[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	constexpr size_t PNG_HEADER_SIZE = 24;

	const char *JsonString(const json_value *pValue)
	{
		if(pValue->type != json_string || str_length(pValue->u.string.ptr) != (int)pValue->u.string.length)
			return nullptr;
		return pValue->u.string.ptr;
	}

	int JsonIntClamped(const json_value *pValue, int Min, int Max)
	{
		if(pValue->type != json_integer)
			return Min;
		return (int)std::clamp<json_int_t>(pValue->u.integer, Min, Max);
	}

	bool CopyBaseUrl(char *pBuf, int BufSize, const char *pBaseUrl)
	{
		if(!CTeedataClient::IsValidBaseUrl(pBaseUrl))
			return false;
		str_copy(pBuf, pBaseUrl, BufSize);
		int Length = str_length(pBuf);
		while(Length > 0 && pBuf[Length - 1] == '/')
			pBuf[--Length] = '\0';
		return true;
	}

	bool CopyResult(char *pBuf, size_t BufSize, const char *pResult)
	{
		if(str_length(pResult) >= (int)BufSize)
			return false;
		str_copy(pBuf, pResult, BufSize);
		return true;
	}

	bool ParseItem(const json_value *pItem, ETeedataAsset Asset, CTeedataItem &Item)
	{
		const char *pName = JsonString(json_object_get(pItem, "name"));
		const char *pStatus = JsonString(json_object_get(pItem, "status"));
		const char *pChecksum = JsonString(json_object_get(pItem, "checksum"));
		const json_value *pSize = json_object_get(pItem, "size");
		if(pName == nullptr || pStatus == nullptr || pChecksum == nullptr || pSize->type != json_integer)
			return false;
		if(str_comp(pStatus, "accepted") != 0 || !CTeedataClient::IsValidName(Asset, pName))
			return false;
		if(pSize->u.integer <= 0 || pSize->u.integer > (json_int_t)CTeedataClient::MAX_ASSET_SIZE)
			return false;
		if(str_length(pChecksum) != (int)MD5_MAXSTRSIZE - 1 || md5_from_str(&Item.m_Checksum, pChecksum) != 0)
			return false;

		str_copy(Item.m_aName, pName);
		Item.m_Id = JsonIntClamped(json_object_get(pItem, "id"), 0, 0x7fffffff);
		Item.m_Size = (int)pSize->u.integer;
		Item.m_Width = JsonIntClamped(json_object_get(pItem, "width"), 0, 16384);
		Item.m_Height = JsonIntClamped(json_object_get(pItem, "height"), 0, 16384);

		const json_value *pAuthor = json_object_get(pItem, "author");
		if(pAuthor->type == json_object)
		{
			const char *pAuthorName = JsonString(json_object_get(pAuthor, "name"));
			if(pAuthorName != nullptr)
				str_copy(Item.m_aAuthor, pAuthorName);
		}

		const json_value *pCount = json_object_get(pItem, "_count");
		if(pCount->type == json_object)
			Item.m_Downloads = JsonIntClamped(json_object_get(pCount, "downloads"), 0, 0x7fffffff);
		return true;
	}
}

const char *CTeedataClient::ApiName(ETeedataAsset Asset)
{
	switch(Asset)
	{
	case ETeedataAsset::SKIN: return "skin";
	case ETeedataAsset::GAMESKIN: return "gameskin";
	case ETeedataAsset::ENTITY: return "entity";
	case ETeedataAsset::EMOTICON: return "emoticon";
	case ETeedataAsset::PARTICLE: return "particle";
	case ETeedataAsset::CURSOR: return "cursor";
	case ETeedataAsset::FONT: return "font";
	default: return "";
	}
}

const char *CTeedataClient::DatabaseFolder(ETeedataAsset Asset)
{
	switch(Asset)
	{
	case ETeedataAsset::SKIN: return "skins";
	case ETeedataAsset::GAMESKIN: return "gameskins";
	case ETeedataAsset::ENTITY: return "entities";
	case ETeedataAsset::EMOTICON: return "emoticons";
	case ETeedataAsset::PARTICLE: return "particles";
	case ETeedataAsset::CURSOR: return "cursors";
	case ETeedataAsset::FONT: return "fonts";
	default: return "";
	}
}

const char *CTeedataClient::InstallFolder(ETeedataAsset Asset)
{
	switch(Asset)
	{
	case ETeedataAsset::SKIN: return "skins";
	case ETeedataAsset::GAMESKIN: return "assets/game";
	case ETeedataAsset::ENTITY: return "assets/entities";
	case ETeedataAsset::EMOTICON: return "assets/emoticons";
	case ETeedataAsset::PARTICLE: return "assets/particles";
	case ETeedataAsset::CURSOR: return "assets/cursor";
	case ETeedataAsset::FONT: return "mapres";
	default: return "";
	}
}

int CTeedataClient::MaxNameLength(ETeedataAsset Asset)
{
	return Asset == ETeedataAsset::SKIN ? (int)MAX_SKIN_LENGTH : (int)CTeedataItem::MAX_NAME_LENGTH;
}

const char *CTeedataClient::SortName(ETeedataSort Sort)
{
	switch(Sort)
	{
	case ETeedataSort::DOWNLOAD: return "download";
	case ETeedataSort::TRENDING: return "trending";
	case ETeedataSort::DATE: return "date";
	case ETeedataSort::NAME: return "name";
	case ETeedataSort::HD: return "hd";
	default: return "download";
	}
}

bool CTeedataClient::IsValidName(ETeedataAsset Asset, const char *pName)
{
	if(InstallFolder(Asset)[0] == '\0' || str_length(pName) >= MaxNameLength(Asset))
		return false;
	if(Asset != ETeedataAsset::SKIN && str_comp_nocase(pName, "default") == 0)
		return false;
	return str_valid_filename(pName);
}

bool CTeedataClient::IsValidBaseUrl(const char *pBaseUrl)
{
	const char *pHost = str_startswith(pBaseUrl, "https://");
	if(pHost == nullptr || pHost[0] == '\0' || pHost[0] == '/' || str_length(pBaseUrl) >= MAX_BASE_URL_LENGTH)
		return false;
	for(const char *pChar = pBaseUrl; *pChar; ++pChar)
	{
		const unsigned char Char = *pChar;
		if(Char <= ' ' || Char >= 0x7f || Char == '?' || Char == '#' || Char == '@' || Char == '\\')
			return false;
	}
	return true;
}

bool CTeedataClient::BuildPageUrl(char *pBuf, size_t BufSize, const char *pBaseUrl, ETeedataAsset Asset, const char *pSearch, ETeedataSort Sort, int Page)
{
	char aBase[MAX_BASE_URL_LENGTH];
	if(Page < 1 || str_length(pSearch) > MAX_SEARCH_LENGTH || ApiName(Asset)[0] == '\0' || !CopyBaseUrl(aBase, sizeof(aBase), pBaseUrl))
		return false;

	char aUrl[MAX_URL_LENGTH * 2];
	if(pSearch[0] == '\0')
	{
		str_format(aUrl, sizeof(aUrl), "%s/api/%s/read?page=%d&limit=%d&sort=%s&status=accepted", aBase, ApiName(Asset), Page, ITEMS_PER_PAGE, SortName(Sort));
	}
	else
	{
		char aSearch[MAX_SEARCH_LENGTH * 3 + 1];
		EscapeUrl(aSearch, pSearch);
		str_format(aUrl, sizeof(aUrl), "%s/api/%s/read?name=%s&page=%d&limit=%d&sort=%s&status=accepted", aBase, ApiName(Asset), aSearch, Page, ITEMS_PER_PAGE, SortName(Sort));
	}
	return CopyResult(pBuf, BufSize, aUrl);
}

bool CTeedataClient::BuildFileUrl(char *pBuf, size_t BufSize, const char *pBaseUrl, ETeedataAsset Asset, const char *pName, bool Thumbnail)
{
	char aBase[MAX_BASE_URL_LENGTH];
	if(!IsValidName(Asset, pName) || !CopyBaseUrl(aBase, sizeof(aBase), pBaseUrl))
		return false;

	char aName[CTeedataItem::MAX_NAME_LENGTH * 3 + 1];
	EscapeUrl(aName, pName);
	char aUrl[MAX_URL_LENGTH * 2];
	str_format(aUrl, sizeof(aUrl), "%s/databasev2/%s/%s/%s%s", aBase, DatabaseFolder(Asset), aName, aName, Thumbnail ? ".thumbnail.png" : ".png");
	return CopyResult(pBuf, BufSize, aUrl);
}

bool CTeedataClient::BuildInstallPath(char *pBuf, size_t BufSize, ETeedataAsset Asset, const char *pName)
{
	if(!IsValidName(Asset, pName))
		return false;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.png", InstallFolder(Asset), pName);
	return CopyResult(pBuf, BufSize, aPath);
}

bool CTeedataClient::ParsePage(const json_value *pRoot, ETeedataAsset Asset, CTeedataPage &Page)
{
	Page.m_TotalCount = 0;
	Page.m_vItems.clear();
	if(pRoot == nullptr || pRoot->type != json_object)
		return false;

	const json_value *pResult = json_object_get(pRoot, "result");
	if(pResult->type != json_object)
		return false;
	const json_value *pTotalCount = json_object_get(pResult, "totalCount");
	const json_value *pItems = json_object_get(pResult, "items");
	if(pTotalCount->type != json_integer || pItems->type != json_array)
		return false;

	Page.m_TotalCount = JsonIntClamped(pTotalCount, 0, 1000000);
	const int NumItems = std::min(json_array_length(pItems), (int)MAX_PAGE_ITEMS);
	for(int i = 0; i < NumItems; ++i)
	{
		const json_value *pItem = json_array_get(pItems, i);
		if(pItem->type != json_object)
			continue;
		CTeedataItem Item;
		if(ParseItem(pItem, Asset, Item))
			Page.m_vItems.push_back(Item);
	}
	return true;
}

bool CTeedataClient::IsPng(const unsigned char *pData, size_t Size)
{
	if(pData == nullptr || Size < PNG_HEADER_SIZE || mem_comp(pData, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) != 0 || mem_comp(pData + 12, "IHDR", 4) != 0)
		return false;
	const unsigned Width = bytes_be_to_uint(pData + 16);
	const unsigned Height = bytes_be_to_uint(pData + 20);
	return Width > 0 && Height > 0 && Width <= MAX_IMAGE_DIMENSION && Height <= MAX_IMAGE_DIMENSION;
}

bool CTeedataClient::ValidateFile(const unsigned char *pData, size_t Size, const CTeedataItem &Item)
{
	if(Size > MAX_ASSET_SIZE || !IsPng(pData, Size))
		return false;
	return md5(pData, Size) == Item.m_Checksum;
}
