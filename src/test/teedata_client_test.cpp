#include <base/bytes.h>
#include <base/hash.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/shared/json.h>

#include <game/client/components/sixclient/teedata_client.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

static const char TEEDATA_SKIN_PAGE[] = R"json({"result":{"totalCount":1353,"items":[
{"id":812,"name":"pepeclown","folder_path":"/skins/pepeclown","file_path":"/skins/pepeclown/pepeclown.png","thumbnail_path":"/skins/pepeclown/pepeclown.thumbnail.png","extension":".png","author_id":1,"checksum":"c1c4248fea623306513680ff1e60735c","size":14039,"width":256,"height":128,"average_color":"#615d5d","status":"accepted","created_at":"2023-09-22T04:29:08.109Z","updated_at":"2024-07-27T14:55:26.498Z","author":{"id":1,"name":"System","twitch_username":null,"youtube_username":null,"github_username":null,"discord_username":null,"country_code":"DEFAULT","created_at":"2023-09-22T04:29:01.214Z","updated_at":"2026-06-22T19:26:13.894Z","avatar":{"id":1,"skin_id":843,"created_at":"2023-09-22T04:29:04.422Z","updated_at":"2024-03-29T13:32:44.536Z","user_id":1,"skin":{"id":843,"name":"teedata","folder_path":"/skins/teedata","file_path":"/skins/teedata/teedata.png","thumbnail_path":"/skins/teedata/teedata.thumbnail.png","extension":".png","author_id":1,"checksum":"8496ebc437516c89e44f2b8a435d7d78","size":13796,"width":256,"height":128,"average_color":"#79634c","status":"accepted","created_at":"2023-09-23T02:06:44.104Z","updated_at":"2024-07-27T14:55:26.983Z"}},"roles":[{"id":2,"name":"member","created_at":"2023-09-22T04:29:01.113Z","updated_at":"2023-09-22T04:29:01.113Z"}],"_count":{"skins":754,"mapres":337,"gameskins":46,"emoticons":28,"cursors":13,"particles":21,"entities":8,"fonts":13,"templates":5}},"_count":{"downloads":17482}},
{"id":620,"name":"cammo2.0","folder_path":"/skins/cammo2.0","file_path":"/skins/cammo2.0/cammo2.0.png","thumbnail_path":"/skins/cammo2.0/cammo2.0.thumbnail.png","extension":".png","author_id":1,"checksum":"f9031598031804cf83cbdee24cee6548","size":9962,"width":256,"height":128,"average_color":"#3a3e32","status":"accepted","created_at":"2023-09-22T04:29:07.434Z","updated_at":"2024-07-27T14:55:24.357Z","_count":{"downloads":79}},
{"id":9001,"name":"../../evil","checksum":"c1c4248fea623306513680ff1e60735c","size":100,"width":256,"height":128,"status":"accepted"},
{"id":9002,"name":"sub/dir","checksum":"c1c4248fea623306513680ff1e60735c","size":100,"width":256,"height":128,"status":"accepted"},
{"id":9003,"name":"back\\slash","checksum":"c1c4248fea623306513680ff1e60735c","size":100,"width":256,"height":128,"status":"accepted"},
{"id":9004,"name":"adidasjsleopardjeremyscotttailsandstorm","checksum":"c1c4248fea623306513680ff1e60735c","size":100,"width":256,"height":128,"status":"accepted"},
{"id":9005,"name":"pendingskin","checksum":"c1c4248fea623306513680ff1e60735c","size":100,"width":256,"height":128,"status":"pending"},
{"id":9006,"name":"hugeskin","checksum":"c1c4248fea623306513680ff1e60735c","size":104857600,"width":256,"height":128,"status":"accepted"},
{"id":9007,"name":"badchecksum","checksum":"not-a-checksum","size":100,"width":256,"height":128,"status":"accepted"},
{"id":9008,"checksum":"c1c4248fea623306513680ff1e60735c","size":100,"width":256,"height":128,"status":"accepted"},
"not an object"
]}})json";

static const char TEEDATA_GAMESKIN_PAGE[] = R"json({"result":{"totalCount":584,"items":[
{"id":865,"name":"irisikablack","folder_path":"/gameskins/irisikablack","file_path":"/gameskins/irisikablack/irisikablack.png","thumbnail_path":"/gameskins/irisikablack/irisikablack.thumbnail.png","extension":".png","author_id":8243,"checksum":"26bdad914f865f365fe0aa46c4f01286","size":185127,"width":1024,"height":512,"average_color":"#5f5e60","status":"accepted","created_at":"2026-03-28T16:07:48.979Z","updated_at":"2026-04-07T17:35:19.712Z","_count":{"downloads":5811}},
{"id":9010,"name":"default","checksum":"26bdad914f865f365fe0aa46c4f01286","size":100,"width":1024,"height":512,"status":"accepted"}
]}})json";

static const unsigned char PNG_1X1[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
	0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
	0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
	0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

static bool ParseFixture(const char *pJson, ETeedataAsset Asset, CTeedataPage &Page)
{
	json_value *pRoot = JsonParse(pJson, str_length(pJson));
	const bool Result = CTeedataClient::ParsePage(pRoot, Asset, Page);
	json_value_free(pRoot);
	return Result;
}

TEST(Teedata, ParseSkinPage)
{
	CTeedataPage Page;
	ASSERT_TRUE(ParseFixture(TEEDATA_SKIN_PAGE, ETeedataAsset::SKIN, Page));
	EXPECT_EQ(Page.m_TotalCount, 1353);
	ASSERT_EQ(Page.m_vItems.size(), 2u);

	const CTeedataItem &First = Page.m_vItems[0];
	EXPECT_EQ(First.m_Id, 812);
	EXPECT_STREQ(First.m_aName, "pepeclown");
	EXPECT_STREQ(First.m_aAuthor, "System");
	EXPECT_EQ(First.m_Size, 14039);
	EXPECT_EQ(First.m_Width, 256);
	EXPECT_EQ(First.m_Height, 128);
	EXPECT_EQ(First.m_Downloads, 17482);
	char aChecksum[MD5_MAXSTRSIZE];
	md5_str(First.m_Checksum, aChecksum, sizeof(aChecksum));
	EXPECT_STREQ(aChecksum, "c1c4248fea623306513680ff1e60735c");

	const CTeedataItem &Second = Page.m_vItems[1];
	EXPECT_STREQ(Second.m_aName, "cammo2.0");
	EXPECT_STREQ(Second.m_aAuthor, "");
	EXPECT_EQ(Second.m_Downloads, 79);
}

TEST(Teedata, ParseGameskinPage)
{
	CTeedataPage Page;
	ASSERT_TRUE(ParseFixture(TEEDATA_GAMESKIN_PAGE, ETeedataAsset::GAMESKIN, Page));
	EXPECT_EQ(Page.m_TotalCount, 584);
	ASSERT_EQ(Page.m_vItems.size(), 1u);
	EXPECT_STREQ(Page.m_vItems[0].m_aName, "irisikablack");
	EXPECT_EQ(Page.m_vItems[0].m_Width, 1024);
}

TEST(Teedata, ParseMalformed)
{
	CTeedataPage Page;
	EXPECT_FALSE(CTeedataClient::ParsePage(nullptr, ETeedataAsset::SKIN, Page));
	EXPECT_FALSE(ParseFixture("[]", ETeedataAsset::SKIN, Page));
	EXPECT_FALSE(ParseFixture("{}", ETeedataAsset::SKIN, Page));
	EXPECT_FALSE(ParseFixture(R"({"errors":["sort must be one of the following values"]})", ETeedataAsset::SKIN, Page));
	EXPECT_FALSE(ParseFixture(R"({"result":[]})", ETeedataAsset::SKIN, Page));
	EXPECT_FALSE(ParseFixture(R"({"result":{"totalCount":"1","items":[]}})", ETeedataAsset::SKIN, Page));
	EXPECT_FALSE(ParseFixture(R"({"result":{"totalCount":1,"items":{}}})", ETeedataAsset::SKIN, Page));
	EXPECT_TRUE(Page.m_vItems.empty());

	EXPECT_TRUE(ParseFixture(R"({"result":{"totalCount":-5,"items":[]}})", ETeedataAsset::SKIN, Page));
	EXPECT_EQ(Page.m_TotalCount, 0);
}

TEST(Teedata, ParseTruncated)
{
	const int Length = str_length(TEEDATA_SKIN_PAGE);
	for(int Cut : {1, 20, Length / 3, Length / 2, Length - 1})
	{
		const std::string Truncated(TEEDATA_SKIN_PAGE, Cut);
		CTeedataPage Page;
		EXPECT_FALSE(ParseFixture(Truncated.c_str(), ETeedataAsset::SKIN, Page)) << "cut at " << Cut;
		EXPECT_TRUE(Page.m_vItems.empty());
	}
}

TEST(Teedata, ParseLimitsItems)
{
	std::string Json = R"({"result":{"totalCount":500,"items":[)";
	for(int i = 0; i < CTeedataClient::MAX_PAGE_ITEMS + 20; ++i)
	{
		if(i > 0)
			Json += ",";
		Json += R"({"id":1,"name":"skin)" + std::to_string(i) + R"(","checksum":"c1c4248fea623306513680ff1e60735c","size":100,"status":"accepted"})";
	}
	Json += "]}}";
	CTeedataPage Page;
	ASSERT_TRUE(ParseFixture(Json.c_str(), ETeedataAsset::SKIN, Page));
	EXPECT_EQ((int)Page.m_vItems.size(), CTeedataClient::MAX_PAGE_ITEMS);
}

TEST(Teedata, ParseTruncatesAuthor)
{
	CTeedataPage Page;
	ASSERT_TRUE(ParseFixture(R"({"result":{"totalCount":1,"items":[{"id":1,"name":"a","checksum":"c1c4248fea623306513680ff1e60735c","size":100,"status":"accepted","author":{"name":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}]}})", ETeedataAsset::SKIN, Page));
	ASSERT_EQ(Page.m_vItems.size(), 1u);
	EXPECT_EQ(str_length(Page.m_vItems[0].m_aAuthor), CTeedataItem::MAX_AUTHOR_LENGTH - 1);
}

TEST(Teedata, ValidName)
{
	EXPECT_TRUE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "pepeclown"));
	EXPECT_TRUE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "cammo2.0"));
	EXPECT_TRUE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "default"));
	EXPECT_TRUE(CTeedataClient::IsValidName(ETeedataAsset::EMOTICON, "attempt_to_simulate_black"));

	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, ""));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, ".."));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "../evil"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "..\\evil"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "a/b"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "a\\b"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "/etc"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "C:"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "russiaskin."));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "con"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "line\nbreak"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::SKIN, "cf_glowcontrastcoalabopp"));
	EXPECT_TRUE(CTeedataClient::IsValidName(ETeedataAsset::GAMESKIN, "cf_glowcontrastcoalabopp"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::GAMESKIN, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::GAMESKIN, "default"));
	EXPECT_FALSE(CTeedataClient::IsValidName(ETeedataAsset::CURSOR, "Default"));
}

TEST(Teedata, ValidBaseUrl)
{
	EXPECT_TRUE(CTeedataClient::IsValidBaseUrl("https://teedata.net"));
	EXPECT_TRUE(CTeedataClient::IsValidBaseUrl("https://teedata.net/"));
	EXPECT_TRUE(CTeedataClient::IsValidBaseUrl("https://localhost:8443/mirror"));

	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl(""));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("http://teedata.net"));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("ftp://teedata.net"));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("https://"));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("https:///api"));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("https://teedata.net?x=1"));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("https://teedata.net#x"));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("https://user@teedata.net"));
	EXPECT_FALSE(CTeedataClient::IsValidBaseUrl("https://tee data.net"));
}

TEST(Teedata, PageUrl)
{
	char aUrl[CTeedataClient::MAX_URL_LENGTH];
	ASSERT_TRUE(CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::SKIN, "", ETeedataSort::DOWNLOAD, 1));
	EXPECT_STREQ(aUrl, "https://teedata.net/api/skin/read?page=1&limit=24&sort=download&status=accepted");

	ASSERT_TRUE(CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), "https://teedata.net/", ETeedataAsset::GAMESKIN, "a b&c", ETeedataSort::DATE, 3));
	EXPECT_STREQ(aUrl, "https://teedata.net/api/gameskin/read?name=a%20b%26c&page=3&limit=24&sort=date&status=accepted");

	ASSERT_TRUE(CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::ENTITY, "", ETeedataSort::TRENDING, 2));
	EXPECT_STREQ(aUrl, "https://teedata.net/api/entity/read?page=2&limit=24&sort=trending&status=accepted");

	ASSERT_TRUE(CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::FONT, "", ETeedataSort::HD, 1));
	EXPECT_STREQ(aUrl, "https://teedata.net/api/font/read?page=1&limit=24&sort=hd&status=accepted");

	EXPECT_FALSE(CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), "http://teedata.net", ETeedataAsset::SKIN, "", ETeedataSort::DOWNLOAD, 1));
	EXPECT_FALSE(CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::SKIN, "", ETeedataSort::DOWNLOAD, 0));
	EXPECT_FALSE(CTeedataClient::BuildPageUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::SKIN, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", ETeedataSort::DOWNLOAD, 1));
	EXPECT_FALSE(CTeedataClient::BuildPageUrl(aUrl, 32, "https://teedata.net", ETeedataAsset::SKIN, "", ETeedataSort::DOWNLOAD, 1));
}

TEST(Teedata, FileUrl)
{
	char aUrl[CTeedataClient::MAX_URL_LENGTH];
	ASSERT_TRUE(CTeedataClient::BuildFileUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::SKIN, "pepeclown", false));
	EXPECT_STREQ(aUrl, "https://teedata.net/databasev2/skins/pepeclown/pepeclown.png");

	ASSERT_TRUE(CTeedataClient::BuildFileUrl(aUrl, sizeof(aUrl), "https://teedata.net/", ETeedataAsset::ENTITY, "my entities", true));
	EXPECT_STREQ(aUrl, "https://teedata.net/databasev2/entities/my%20entities/my%20entities.thumbnail.png");

	ASSERT_TRUE(CTeedataClient::BuildFileUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::CURSOR, "gui_cursor", false));
	EXPECT_STREQ(aUrl, "https://teedata.net/databasev2/cursors/gui_cursor/gui_cursor.png");

	EXPECT_FALSE(CTeedataClient::BuildFileUrl(aUrl, sizeof(aUrl), "https://teedata.net", ETeedataAsset::SKIN, "../evil", false));
	EXPECT_FALSE(CTeedataClient::BuildFileUrl(aUrl, sizeof(aUrl), "http://teedata.net", ETeedataAsset::SKIN, "pepeclown", false));
}

TEST(Teedata, InstallPath)
{
	char aPath[128];
	ASSERT_TRUE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::SKIN, "pepeclown"));
	EXPECT_STREQ(aPath, "skins/pepeclown.png");
	ASSERT_TRUE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::GAMESKIN, "irisikablack"));
	EXPECT_STREQ(aPath, "assets/game/irisikablack.png");
	ASSERT_TRUE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::ENTITY, "beg"));
	EXPECT_STREQ(aPath, "assets/entities/beg.png");
	ASSERT_TRUE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::EMOTICON, "emoticons"));
	EXPECT_STREQ(aPath, "assets/emoticons/emoticons.png");
	ASSERT_TRUE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::PARTICLE, "particles"));
	EXPECT_STREQ(aPath, "assets/particles/particles.png");
	ASSERT_TRUE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::CURSOR, "gui_cursor"));
	EXPECT_STREQ(aPath, "assets/cursor/gui_cursor.png");
	ASSERT_TRUE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::FONT, "minimalist"));
	EXPECT_STREQ(aPath, "mapres/minimalist.png");

	EXPECT_FALSE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::SKIN, "../../config"));
	EXPECT_FALSE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::GAMESKIN, "a/b"));
	EXPECT_FALSE(CTeedataClient::BuildInstallPath(aPath, sizeof(aPath), ETeedataAsset::GAMESKIN, "a\\b"));
	EXPECT_FALSE(CTeedataClient::BuildInstallPath(aPath, 8, ETeedataAsset::SKIN, "pepeclown"));
}

TEST(Teedata, ValidateFile)
{
	CTeedataItem Item;
	Item.m_Size = sizeof(PNG_1X1);
	Item.m_Checksum = md5(PNG_1X1, sizeof(PNG_1X1));
	EXPECT_TRUE(CTeedataClient::IsPng(PNG_1X1, sizeof(PNG_1X1)));
	EXPECT_TRUE(CTeedataClient::ValidateFile(PNG_1X1, sizeof(PNG_1X1), Item));

	EXPECT_FALSE(CTeedataClient::ValidateFile(nullptr, 0, Item));
	EXPECT_FALSE(CTeedataClient::ValidateFile(PNG_1X1, 7, Item));
	EXPECT_FALSE(CTeedataClient::ValidateFile(PNG_1X1, sizeof(PNG_1X1) - 1, Item));

	CTeedataItem WrongChecksum = Item;
	WrongChecksum.m_Checksum.data[0] ^= 0xff;
	EXPECT_FALSE(CTeedataClient::ValidateFile(PNG_1X1, sizeof(PNG_1X1), WrongChecksum));

	const char aHtml[] = "<!DOCTYPE html><html></html>";
	CTeedataItem Html;
	Html.m_Checksum = md5(aHtml, sizeof(aHtml));
	EXPECT_FALSE(CTeedataClient::IsPng((const unsigned char *)aHtml, sizeof(aHtml)));
	EXPECT_FALSE(CTeedataClient::ValidateFile((const unsigned char *)aHtml, sizeof(aHtml), Html));

	std::vector<unsigned char> vNoHeader(PNG_1X1, PNG_1X1 + sizeof(PNG_1X1));
	mem_copy(vNoHeader.data() + 12, "IEND", 4);
	EXPECT_FALSE(CTeedataClient::IsPng(vNoHeader.data(), vNoHeader.size()));

	std::vector<unsigned char> vHuge(CTeedataClient::MAX_ASSET_SIZE + 1, 0);
	mem_copy(vHuge.data(), PNG_1X1, sizeof(PNG_1X1));
	CTeedataItem Huge;
	Huge.m_Checksum = md5(vHuge.data(), vHuge.size());
	EXPECT_TRUE(CTeedataClient::IsPng(vHuge.data(), vHuge.size()));
	EXPECT_FALSE(CTeedataClient::ValidateFile(vHuge.data(), vHuge.size(), Huge));
}

static bool IsPngWithSize(unsigned Width, unsigned Height)
{
	std::vector<unsigned char> vPng(PNG_1X1, PNG_1X1 + sizeof(PNG_1X1));
	uint_to_bytes_be(vPng.data() + 16, Width);
	uint_to_bytes_be(vPng.data() + 20, Height);
	CTeedataItem Item;
	Item.m_Checksum = md5(vPng.data(), vPng.size());
	const bool IsPng = CTeedataClient::IsPng(vPng.data(), vPng.size());
	EXPECT_EQ(IsPng, CTeedataClient::ValidateFile(vPng.data(), vPng.size(), Item));
	return IsPng;
}

TEST(Teedata, ImageDimensions)
{
	EXPECT_TRUE(IsPngWithSize(1, 1));
	EXPECT_TRUE(IsPngWithSize(256, 128));
	EXPECT_TRUE(IsPngWithSize(CTeedataClient::MAX_IMAGE_DIMENSION, CTeedataClient::MAX_IMAGE_DIMENSION / 2));
	EXPECT_FALSE(IsPngWithSize(0, 128));
	EXPECT_FALSE(IsPngWithSize(256, 0));
	EXPECT_FALSE(IsPngWithSize(CTeedataClient::MAX_IMAGE_DIMENSION + 1, 128));
	EXPECT_FALSE(IsPngWithSize(256, CTeedataClient::MAX_IMAGE_DIMENSION + 1));
	EXPECT_FALSE(IsPngWithSize(100000, 100000));
	EXPECT_FALSE(IsPngWithSize(0xffffffff, 0xffffffff));
	EXPECT_FALSE(CTeedataClient::IsPng(PNG_1X1, 23));
}
