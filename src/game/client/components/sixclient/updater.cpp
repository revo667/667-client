#include "updater.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/process.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/client/updater.h>
#include <engine/config.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/ed25519.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/sixclient/version.h>
#include <game/version.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

static bool StrEndsWithNoCase(const char *pStr, const char *pSuffix)
{
	if(!pStr || !pSuffix)
		return false;
	int StrLen = str_length(pStr);
	int SuffixLen = str_length(pSuffix);
	if(SuffixLen > StrLen)
		return false;
	return str_comp_nocase(pStr + StrLen - SuffixLen, pSuffix) == 0;
}

static constexpr const char *UPDATE_MANIFEST_NAME = "CecenClient-update.txt";
static constexpr const char *UPDATE_SIGNATURE_NAME = "CecenClient-update.txt.sig";
static constexpr unsigned char UPDATE_PUBLIC_KEY[ED25519_PUBLIC_KEY_LENGTH] = {
	0x1a, 0xd5, 0x90, 0x0d, 0x23, 0xe1, 0x05, 0x66, 0x5f, 0x05, 0x80, 0x3c,
	0x07, 0x26, 0x1c, 0x78, 0xd3, 0xfb, 0x92, 0x34, 0xe0, 0xd7, 0x23, 0xbb,
	0x8e, 0xe3, 0x92, 0x19, 0xd5, 0x85, 0x19, 0xb3};

static constexpr const char *GITHUB_RELEASES_URL = "https://api.github.com/repos/revo667/cecen-client-releases/releases?per_page=10";
#if defined(CONF_PLATFORM_LINUX)
static constexpr const char *UPDATE_ARCHIVE_PATH = "update/cecenclient-release.tar.xz";
#elif defined(CONF_PLATFORM_MACOS)
static constexpr const char *UPDATE_ARCHIVE_PATH = "667Client/update/cecenclient-update.dmg";
#else
static constexpr const char *UPDATE_ARCHIVE_PATH = "update/cecenclient-release.zip";
#endif

static void BuildGitHubReleasesUrl(char *pBuf, int BufSize)
{
	str_format(pBuf, BufSize, "%s&t=%lld", GITHUB_RELEASES_URL, (long long)time_timestamp());
}

static std::string ToLowerAscii(const char *pStr)
{
	std::string Lower;
	if(!pStr)
		return Lower;

	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pStr); *p != '\0'; ++p)
		Lower.push_back(static_cast<char>(std::tolower(*p)));
	return Lower;
}

static void NormalizeVersionString(const char *pVersion, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;

	if(!pVersion)
	{
		pBuf[0] = '\0';
		return;
	}

	while(*pVersion != '\0' && std::isspace(static_cast<unsigned char>(*pVersion)))
		++pVersion;

	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit(static_cast<unsigned char>(pVersion[1])))
		++pVersion;

	str_copy(pBuf, pVersion, BufSize);
}

static std::vector<int> ExtractVersionNumbers(const char *pVersion)
{
	std::vector<int> vNumbers;
	if(!pVersion)
		return vNumbers;

	int Current = -1;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pVersion); *p != '\0'; ++p)
	{
		if(std::isdigit(*p))
		{
			if(Current < 0)
				Current = 0;
			Current = Current * 10 + (*p - '0');
		}
		else if(Current >= 0)
		{
			vNumbers.push_back(Current);
			Current = -1;
		}
	}

	if(Current >= 0)
		vNumbers.push_back(Current);

	return vNumbers;
}

static int CompareVersionStrings(const char *pLeft, const char *pRight)
{
	char aLeftNormalized[64];
	char aRightNormalized[64];
	NormalizeVersionString(pLeft, aLeftNormalized, sizeof(aLeftNormalized));
	NormalizeVersionString(pRight, aRightNormalized, sizeof(aRightNormalized));

	const std::vector<int> vLeft = ExtractVersionNumbers(aLeftNormalized);
	const std::vector<int> vRight = ExtractVersionNumbers(aRightNormalized);
	const size_t Num = std::max(vLeft.size(), vRight.size());
	for(size_t i = 0; i < Num; ++i)
	{
		const int Left = i < vLeft.size() ? vLeft[i] : 0;
		const int Right = i < vRight.size() ? vRight[i] : 0;
		if(Left < Right)
			return -1;
		if(Left > Right)
			return 1;
	}

	return str_comp_nocase(aLeftNormalized, aRightNormalized);
}

static int ScoreArchiveAsset(const char *pAssetName)
{
	if(!pAssetName)
		return -1;

	const std::string Lower = ToLowerAscii(pAssetName);
	const bool NewName = Lower.find("cecenclient") != std::string::npos;
	if(!NewName && Lower.find("667client") == std::string::npos)
		return -1;

#if defined(CONF_FAMILY_WINDOWS)
	if(!StrEndsWithNoCase(pAssetName, ".zip"))
		return -1;
	if(Lower.find("windows") == std::string::npos && Lower.find("win") == std::string::npos)
		return -1;
#elif defined(CONF_PLATFORM_LINUX)
	if(!StrEndsWithNoCase(pAssetName, ".tar.xz"))
		return -1;
	if(Lower.find("linux") == std::string::npos)
		return -1;
#elif defined(CONF_PLATFORM_MACOS)
	if(!StrEndsWithNoCase(pAssetName, ".dmg"))
		return -1;
	if(Lower.find("mac") == std::string::npos)
		return -1;
#else
	return -1;
#endif

	if(Lower.find("debug") != std::string::npos || Lower.find("symbols") != std::string::npos || Lower.find("source") != std::string::npos)
		return -1;

	int Score = 100;

#if defined(CONF_FAMILY_WINDOWS)
	if(Lower == "cecenclient-windows.zip")
		Score += 300;
	else if(Lower == "667client-windows.zip")
		Score += 200;
	if(Lower.find("x64") != std::string::npos || Lower.find("64") != std::string::npos || Lower.find("amd64") != std::string::npos)
		Score += 20;
#elif defined(CONF_PLATFORM_LINUX)
	if(Lower == "cecenclient-linux.tar.xz")
		Score += 300;
	else if(Lower == "667client-linux.tar.xz")
		Score += 200;
#elif defined(CONF_PLATFORM_MACOS)
	if(Lower == "cecenclient-macos.dmg")
		Score += 300;
	else if(Lower == "667client-macos.dmg")
		Score += 200;
#endif

#if defined(CONF_ARCH_AMD64)
	if(Lower.find("x64") != std::string::npos || Lower.find("64") != std::string::npos || Lower.find("amd64") != std::string::npos)
		Score += 10;
#elif defined(CONF_ARCH_IA32)
	if(Lower.find("x86") != std::string::npos || Lower.find("32") != std::string::npos)
		Score += 10;
#endif

	return Score;
}

static bool ReleaseIsDraft(const json_value *pJson)
{
	if(!pJson || pJson->type != json_object)
		return false;
	const json_value *pDraft = json_object_get(pJson, "draft");
	return pDraft && pDraft->type == json_boolean && pDraft->u.boolean;
}

struct SReleaseInfo
{
	char m_aVersion[64] = "";
	char m_aArchiveName[128] = "";
	char m_aArchiveUrl[2048] = "";
	char m_aManifestUrl[2048] = "";
	char m_aSignatureUrl[2048] = "";
};

static bool ParseReleaseObject(const json_value *pJson, SReleaseInfo *pInfo)
{
	if(!pJson || pJson->type != json_object)
		return false;
	if(ReleaseIsDraft(pJson))
		return false;

	const char *pReleaseVersion = json_string_get(json_object_get(pJson, "tag_name"));
	if(!pReleaseVersion)
		pReleaseVersion = json_string_get(json_object_get(pJson, "name"));
	if(!pReleaseVersion)
		return false;

	const json_value *pAssets = json_object_get(pJson, "assets");
	if(!pAssets || pAssets->type != json_array)
		return false;

	SReleaseInfo Info;
	int BestScore = -1;

	for(int i = 0; i < json_array_length(pAssets); ++i)
	{
		const json_value *pAsset = json_array_get(pAssets, i);
		if(!pAsset || pAsset->type != json_object)
			continue;

		const char *pName = json_string_get(json_object_get(pAsset, "name"));
		const char *pUrl = json_string_get(json_object_get(pAsset, "browser_download_url"));
		if(!pName || !pUrl)
			continue;
		if(str_comp(pName, UPDATE_MANIFEST_NAME) == 0)
		{
			str_copy(Info.m_aManifestUrl, pUrl, sizeof(Info.m_aManifestUrl));
			continue;
		}
		if(str_comp(pName, UPDATE_SIGNATURE_NAME) == 0)
		{
			str_copy(Info.m_aSignatureUrl, pUrl, sizeof(Info.m_aSignatureUrl));
			continue;
		}

		const int Score = ScoreArchiveAsset(pName);
		if(Score < BestScore)
			continue;

		BestScore = Score;
		str_copy(Info.m_aArchiveName, pName, sizeof(Info.m_aArchiveName));
		str_copy(Info.m_aArchiveUrl, pUrl, sizeof(Info.m_aArchiveUrl));
	}

	if(BestScore < 0 || Info.m_aManifestUrl[0] == '\0' || Info.m_aSignatureUrl[0] == '\0')
		return false;

	str_copy(Info.m_aVersion, pReleaseVersion, sizeof(Info.m_aVersion));
	*pInfo = Info;
	return true;
}

static bool ParseLatestRelease(json_value *pJson, SReleaseInfo *pInfo)
{
	if(!pJson)
		return false;

	if(pJson->type == json_object)
		return ParseReleaseObject(pJson, pInfo);

	if(pJson->type == json_array)
	{
		SReleaseInfo Best;
		bool Found = false;
		for(int i = 0; i < json_array_length(pJson); ++i)
		{
			SReleaseInfo Info;
			if(!ParseReleaseObject(json_array_get(pJson, i), &Info))
				continue;
			if(Found && CompareVersionStrings(Info.m_aVersion, Best.m_aVersion) <= 0)
				continue;
			Found = true;
			Best = Info;
		}
		if(!Found)
			return false;
		*pInfo = Best;
		return true;
	}

	return false;
}

static bool ParseUpdateManifest(const std::string &Manifest, const char *pVersion, const char *pArchiveName, SHA256_DIGEST *pSha256)
{
	char aWantVersion[64];
	NormalizeVersionString(pVersion, aWantVersion, sizeof(aWantVersion));

	std::istringstream Stream(Manifest);
	std::string Line;
	bool HeaderSeen = false;
	bool VersionMatches = false;
	bool HashFound = false;
	while(std::getline(Stream, Line))
	{
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		std::istringstream Fields(Line);
		std::string Key, Value, Name, Extra;
		Fields >> Key >> Value;
		if(!HeaderSeen)
		{
			if(Key != "cecen-update" || Value != "1")
				return false;
			HeaderSeen = true;
			continue;
		}

		if(Key == "version")
		{
			char aVersion[64];
			NormalizeVersionString(Value.c_str(), aVersion, sizeof(aVersion));
			VersionMatches = str_comp(aVersion, aWantVersion) == 0;
		}
		else if(Key == "sha256")
		{
			Fields >> Name;
			if(Fields >> Extra || Name != pArchiveName)
				continue;
			if(Value.size() != 2 * SHA256_DIGEST_LENGTH || sha256_from_str(pSha256, Value.c_str()) != 0)
				return false;
			HashFound = true;
		}
	}
	return HeaderSeen && VersionMatches && HashFound;
}

static void StripFilename(char *pPath)
{
	if(!pPath)
		return;

	for(int i = str_length(pPath) - 1; i >= 0; --i)
	{
		if(pPath[i] == '/' || pPath[i] == '\\')
		{
			pPath[i] = '\0';
			return;
		}
	}
	pPath[0] = '\0';
}

#if defined(CONF_PLATFORM_MACOS)
static bool MacPathIsAppBundle(const char *pPath)
{
	return pPath && pPath[0] != '\0' && str_endswith(pPath, ".app") && fs_is_dir(pPath);
}

static bool MacIsDevelopmentBuild(IStorage *pStorage)
{
	char aExeDir[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExeDir, sizeof(aExeDir));
	StripFilename(aExeDir);
	char aCache[IO_MAX_PATH_LENGTH];
	str_format(aCache, sizeof(aCache), "%s/CMakeCache.txt", aExeDir);
	return aExeDir[0] != '\0' && fs_is_file(aCache);
}

static bool MacAppBundlePath(IStorage *pStorage, char *pBuf, int BufSize)
{
	char aExePath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExePath, sizeof(aExePath));
	for(int i = 0; i < 3; i++)
		StripFilename(aExePath);
	if(MacPathIsAppBundle(aExePath))
	{
		str_copy(pBuf, aExePath, BufSize);
		return true;
	}

	static const char *s_apCandidates[] = {
		"/Applications/Cecen Client.app",
		"/Applications/CecenClient.app",
		"/Applications/667 Client.app",
		"/Applications/667Client.app",
	};
	for(const char *pCandidate : s_apCandidates)
	{
		if(MacPathIsAppBundle(pCandidate))
		{
			str_copy(pBuf, pCandidate, BufSize);
			return true;
		}
	}

	const char *pHome = getenv("HOME");
	if(pHome && pHome[0] != '\0')
	{
		char aHomeApp[IO_MAX_PATH_LENGTH];
		str_format(aHomeApp, sizeof(aHomeApp), "%s/Applications/Cecen Client.app", pHome);
		if(MacPathIsAppBundle(aHomeApp))
		{
			str_copy(pBuf, aHomeApp, BufSize);
			return true;
		}
		str_format(aHomeApp, sizeof(aHomeApp), "%s/Applications/CecenClient.app", pHome);
		if(MacPathIsAppBundle(aHomeApp))
		{
			str_copy(pBuf, aHomeApp, BufSize);
			return true;
		}
	}

	return false;
}

static constexpr const char *MAC_APPLY_SCRIPT =
	"#!/bin/sh\n"
	"trap '' HUP\n"
	"PID=\"$1\"; DMG=\"$2\"; APP=\"$3\"; RELAUNCH=\"$4\"\n"
	"LOG=\"$(dirname \"$DMG\")/cecenclient-update.log\"\n"
	"exec >>\"$LOG\" 2>&1\n"
	"set -x\n"
	"echo \"apply start pid=$PID dmg=$DMG app=$APP relaunch=$RELAUNCH\"\n"
	"while kill -0 \"$PID\" 2>/dev/null; do sleep 0.2; done\n"
	"MNT=$(mktemp -d /tmp/cecenclient-update.XXXXXX) || exit 1\n"
	"BACKUP=$(mktemp -d /tmp/cecenclient-userfiles.XXXXXX) || exit 1\n"
	"xattr -dr com.apple.quarantine \"$DMG\" 2>/dev/null\n"
	"hdiutil attach -nobrowse -readonly -noautoopen -quiet -mountpoint \"$MNT\" \"$DMG\" || exit 1\n"
	"pick_app() {\n"
	"  find \"$1\" -maxdepth \"$2\" -name '*.app' 2>/dev/null | grep -viE '(^|/)[^/]*[Ss]erver\\.app$' | grep -iE '(Cecen|667|DDNet)' | head -n 1\n"
	"}\n"
	"pick_app_any() {\n"
	"  find \"$1\" -maxdepth \"$2\" -name '*.app' 2>/dev/null | grep -viE '(^|/)[^/]*[Ss]erver\\.app$' | head -n 1\n"
	"}\n"
	"SRC=$(pick_app \"$MNT\" 1)\n"
	"[ -n \"$SRC\" ] || SRC=$(pick_app \"$MNT\" 2)\n"
	"[ -n \"$SRC\" ] || SRC=$(pick_app_any \"$MNT\" 1)\n"
	"[ -n \"$SRC\" ] || SRC=$(pick_app_any \"$MNT\" 2)\n"
	"echo \"src=$SRC\"\n"
	"[ -n \"$SRC\" ] || { hdiutil detach \"$MNT\" -quiet; rmdir \"$MNT\" 2>/dev/null; exit 1; }\n"
	"preserve_user_dir() {\n"
	"  REL=\"$1\"\n"
	"  if [ -e \"$APP/$REL\" ]; then\n"
	"    mkdir -p \"$BACKUP/$(dirname \"$REL\")\"\n"
	"    cp -a \"$APP/$REL\" \"$BACKUP/$REL\"\n"
	"  fi\n"
	"}\n"
	"restore_user_dir() {\n"
	"  REL=\"$1\"\n"
	"  if [ -e \"$BACKUP/$REL\" ]; then\n"
	"    mkdir -p \"$APP/$(dirname \"$REL\")\"\n"
	"    rm -rf \"$APP/$REL\"\n"
	"    cp -a \"$BACKUP/$REL\" \"$APP/$REL\"\n"
	"  fi\n"
	"}\n"
	"merge_user_dir() {\n"
	"  REL=\"$1\"\n"
	"  if [ -d \"$BACKUP/$REL\" ]; then\n"
	"    mkdir -p \"$APP/$REL\"\n"
	"    if command -v rsync >/dev/null 2>&1; then\n"
	"      rsync -a --ignore-existing \"$BACKUP/$REL/\" \"$APP/$REL/\"\n"
	"    else\n"
	"      cp -an \"$BACKUP/$REL/.\" \"$APP/$REL/\" 2>/dev/null || true\n"
	"    fi\n"
	"  fi\n"
	"}\n"
	"USER_FULL_DIRS=\"Contents/Resources/skins Contents/Resources/downloadedskins Contents/Resources/maps Contents/Resources/mapres Contents/Resources/demos Contents/Resources/themes Contents/Resources/screenshots Contents/Resources/667Client Contents/Resources/config Contents/MacOS/skins Contents/MacOS/downloadedskins Contents/MacOS/maps Contents/MacOS/demos Contents/MacOS/themes Contents/MacOS/667Client\"\n"
	"USER_MERGE_DIRS=\"Contents/Resources/data/assets Contents/Resources/data/audio Contents/MacOS/data/assets\"\n"
	"for REL in $USER_FULL_DIRS $USER_MERGE_DIRS; do preserve_user_dir \"$REL\"; done\n"
	"if ditto \"$SRC\" \"$APP.new\"; then\n"
	"  rm -rf \"$APP.old\"\n"
	"  if mv \"$APP\" \"$APP.old\" && mv \"$APP.new\" \"$APP\"; then\n"
	"    for REL in $USER_FULL_DIRS; do restore_user_dir \"$REL\"; done\n"
	"    for REL in $USER_MERGE_DIRS; do merge_user_dir \"$REL\"; done\n"
	"    rm -rf \"$APP.old\"\n"
	"  else\n"
	"    [ -d \"$APP\" ] || mv \"$APP.old\" \"$APP\"\n"
	"    rm -rf \"$APP.new\"\n"
	"    hdiutil detach \"$MNT\" -quiet\n"
	"    rm -rf \"$BACKUP\"\n"
	"    rmdir \"$MNT\" 2>/dev/null\n"
	"    exit 1\n"
	"  fi\n"
	"else\n"
	"  hdiutil detach \"$MNT\" -quiet\n"
	"  rm -rf \"$BACKUP\"\n"
	"  rmdir \"$MNT\" 2>/dev/null\n"
	"  exit 1\n"
	"fi\n"
	"hdiutil detach \"$MNT\" -quiet\n"
	"rm -rf \"$BACKUP\"\n"
	"rmdir \"$MNT\" 2>/dev/null\n"
	"rm -f \"$DMG\" \"$0\"\n"
	"xattr -dr com.apple.quarantine \"$APP\" 2>/dev/null\n"
	"[ \"$RELAUNCH\" = \"1\" ] && open \"$APP\"\n"
	"echo apply ok\n"
	"exit 0\n";

static constexpr const char *MAC_PORTABLE_APPLY_SCRIPT =
	"#!/bin/sh\n"
	"trap '' HUP\n"
	"PID=\"$1\"; DMG=\"$2\"; EXE_DIR=\"$3\"; EXE_PATH=\"$4\"; RELAUNCH=\"$5\"\n"
	"LOG=\"$(dirname \"$DMG\")/cecenclient-update.log\"\n"
	"exec >>\"$LOG\" 2>&1\n"
	"set -x\n"
	"echo \"portable start pid=$PID dmg=$DMG dir=$EXE_DIR exe=$EXE_PATH relaunch=$RELAUNCH\"\n"
	"while kill -0 \"$PID\" 2>/dev/null; do sleep 0.2; done\n"
	"MNT=$(mktemp -d /tmp/cecenclient-update.XXXXXX) || exit 1\n"
	"xattr -dr com.apple.quarantine \"$DMG\" 2>/dev/null\n"
	"hdiutil attach -nobrowse -readonly -noautoopen -quiet -mountpoint \"$MNT\" \"$DMG\" || exit 1\n"
	"pick_app() {\n"
	"  find \"$1\" -maxdepth \"$2\" -name '*.app' 2>/dev/null | grep -viE '(^|/)[^/]*[Ss]erver\\.app$' | grep -iE '(Cecen|667|DDNet)' | head -n 1\n"
	"}\n"
	"pick_app_any() {\n"
	"  find \"$1\" -maxdepth \"$2\" -name '*.app' 2>/dev/null | grep -viE '(^|/)[^/]*[Ss]erver\\.app$' | head -n 1\n"
	"}\n"
	"SRC=$(pick_app \"$MNT\" 1)\n"
	"[ -n \"$SRC\" ] || SRC=$(pick_app \"$MNT\" 2)\n"
	"[ -n \"$SRC\" ] || SRC=$(pick_app_any \"$MNT\" 1)\n"
	"[ -n \"$SRC\" ] || SRC=$(pick_app_any \"$MNT\" 2)\n"
	"echo \"src=$SRC\"\n"
	"if [ -z \"$SRC\" ]; then\n"
	"  hdiutil detach \"$MNT\" -quiet\n"
	"  rmdir \"$MNT\" 2>/dev/null\n"
	"  exit 1\n"
	"fi\n"
	"MACOS_SRC=\"$SRC/Contents/MacOS\"\n"
	"FRAMEWORKS_SRC=\"$SRC/Contents/Frameworks\"\n"
	"if [ -d \"$MACOS_SRC\" ]; then\n"
	"  for f in \"$MACOS_SRC\"/*; do\n"
	"    [ -e \"$f\" ] || continue\n"
	"    base=$(basename \"$f\")\n"
	"    case \"$base\" in data|skins|downloadedskins|maps|demos|themes|screenshots|config|667Client|update) continue ;; esac\n"
	"    rm -rf \"$EXE_DIR/$base\"\n"
	"    cp -a \"$f\" \"$EXE_DIR/$base\"\n"
	"  done\n"
	"fi\n"
	"if [ -d \"$FRAMEWORKS_SRC\" ]; then\n"
	"  mkdir -p \"$EXE_DIR\"\n"
	"  for f in \"$FRAMEWORKS_SRC\"/*; do\n"
	"    [ -e \"$f\" ] || continue\n"
	"    base=$(basename \"$f\")\n"
	"    rm -rf \"$EXE_DIR/$base\"\n"
	"    cp -a \"$f\" \"$EXE_DIR/$base\"\n"
	"  done\n"
	"fi\n"
	"RESOURCES_SRC=\"$SRC/Contents/Resources\"\n"
	"if [ -d \"$RESOURCES_SRC/data\" ]; then\n"
	"  mkdir -p \"$EXE_DIR/data\"\n"
	"  cp -a \"$RESOURCES_SRC/data/.\" \"$EXE_DIR/data/\"\n"
	"fi\n"
	"hdiutil detach \"$MNT\" -quiet\n"
	"rmdir \"$MNT\" 2>/dev/null\n"
	"rm -f \"$DMG\" \"$0\"\n"
	"xattr -dr com.apple.quarantine \"$EXE_DIR\" 2>/dev/null\n"
	"if [ \"$RELAUNCH\" = \"1\" ] && [ -x \"$EXE_PATH\" ]; then\n"
	"  (cd \"$EXE_DIR\" && open \"$EXE_PATH\")\n"
	"fi\n"
	"echo portable ok\n"
	"exit 0\n";
#endif

CUpdater::CUpdater()
{
	m_pClient = nullptr;
	m_pStorage = nullptr;
	m_pHttp = nullptr;

	m_State = CLEAN;
	m_aStatus[0] = '\0';
	m_Percent = 0;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	m_aManifestUrl[0] = '\0';
	m_aSignatureUrl[0] = '\0';
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
}

void CUpdater::Init()
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = Kernel()->RequestInterface<IHttp>();

#if !defined(CONF_HEADLESS_CLIENT) && (defined(CONF_FAMILY_WINDOWS) || defined(CONF_PLATFORM_LINUX))
	m_AutoCheckPending = true;
#elif !defined(CONF_HEADLESS_CLIENT) && defined(CONF_PLATFORM_MACOS)
	m_AutoCheckPending = !MacIsDevelopmentBuild(m_pStorage);
#endif
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope LockScope(m_Lock);
	m_State = NewState;
}

void CUpdater::SetStatus(const char *pStatus)
{
	if(pStatus && pStatus[0])
		log_info("updater", "%s", pStatus);
	const CLockScope LockScope(m_Lock);
	str_copy(m_aStatus, pStatus ? pStatus : "", sizeof(m_aStatus));
}

void CUpdater::SetPercent(int Percent)
{
	const CLockScope LockScope(m_Lock);
	m_Percent = std::clamp(Percent, 0, 100);
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope LockScope(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope LockScope(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope LockScope(m_Lock);
	return m_Percent;
}

const char *CUpdater::GetLatestVersionString()
{
	return m_aLatestVersion;
}

void CUpdater::ResetTask()
{
	if(m_pCurrentTask)
	{
		m_pCurrentTask->Abort();
		m_pCurrentTask = nullptr;
	}
	m_TaskKind = ETaskKind::NONE;
}

void CUpdater::StartReleaseFetch()
{
	ResetTask();
	SetStatus("Checking latest release");
	SetPercent(0);
	SetCurrentState(IUpdater::GETTING_MANIFEST);

	char aUrl[256];
	BuildGitHubReleasesUrl(aUrl, sizeof(aUrl));
	m_TaskKind = ETaskKind::FETCH_RELEASE;
	m_pCurrentTask = HttpGet(aUrl);
	m_pCurrentTask->HeaderString("Accept", "application/vnd.github+json");
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pCurrentTask->HeaderString("Cache-Control", "no-cache");
	m_pCurrentTask->HeaderString("Pragma", "no-cache");
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

void CUpdater::ParseReleaseTask()
{
	json_value *pJson = m_pCurrentTask ? m_pCurrentTask->ResultJson() : nullptr;
	if(!pJson)
	{
		SetStatus("Failed to parse release info");
		SetCurrentState(IUpdater::FAIL);
		return;
	}

	SReleaseInfo Info;
	const bool Parsed = ParseLatestRelease(pJson, &Info);
	json_value_free(pJson);

	if(!Parsed || CompareVersionStrings(Info.m_aVersion, SIXCLIENT_VERSION) <= 0)
	{
		m_aLatestVersion[0] = '\0';
		m_aArchiveName[0] = '\0';
		m_aArchiveUrl[0] = '\0';
		m_aManifestUrl[0] = '\0';
		m_aSignatureUrl[0] = '\0';
		SetStatus("No update available");
		SetCurrentState(IUpdater::CLEAN);
		return;
	}

	log_info("updater", "found %s (running %s), asset %s", Info.m_aVersion, SIXCLIENT_VERSION, Info.m_aArchiveName);
	str_copy(m_aLatestVersion, Info.m_aVersion, sizeof(m_aLatestVersion));
	str_copy(m_aArchiveName, Info.m_aArchiveName, sizeof(m_aArchiveName));
	str_copy(m_aArchiveUrl, Info.m_aArchiveUrl, sizeof(m_aArchiveUrl));
	str_copy(m_aManifestUrl, Info.m_aManifestUrl, sizeof(m_aManifestUrl));
	str_copy(m_aSignatureUrl, Info.m_aSignatureUrl, sizeof(m_aSignatureUrl));
	SetStatus("Update available");
	SetCurrentState(IUpdater::VERSION_AVAILABLE);
}

void CUpdater::StartManifestFetch(bool Signature)
{
	ResetTask();
	SetStatus("Checking update signature");
	SetPercent(0);
	SetCurrentState(IUpdater::DOWNLOADING);

	m_TaskKind = Signature ? ETaskKind::FETCH_SIGNATURE : ETaskKind::FETCH_MANIFEST;
	m_pCurrentTask = HttpGet(Signature ? m_aSignatureUrl : m_aManifestUrl);
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->MaxResponseSize(64 * 1024);
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::VerifyManifest()
{
	unsigned char *pSignature = nullptr;
	size_t SignatureLength = 0;
	m_pCurrentTask->Result(&pSignature, &SignatureLength);
	if(!pSignature || SignatureLength != ED25519_SIGNATURE_LENGTH)
		return false;
	if(!ed25519_verify(UPDATE_PUBLIC_KEY, reinterpret_cast<const unsigned char *>(m_Manifest.data()), m_Manifest.size(), pSignature))
		return false;

	SHA256_DIGEST Sha256;
	if(!ParseUpdateManifest(m_Manifest, m_aLatestVersion, m_aArchiveName, &Sha256))
		return false;
	m_ArchiveSha256 = Sha256;
	return true;
}

void CUpdater::StartArchiveDownload()
{
	ResetTask();
#if defined(CONF_PLATFORM_MACOS)
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
	m_pStorage->CreateFolder("667Client", IStorage::TYPE_SAVE);
	m_pStorage->CreateFolder("667Client/update", IStorage::TYPE_SAVE);
	m_pStorage->RemoveFile(m_aArchivePath, IStorage::TYPE_SAVE);
#else
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
	m_pStorage->RemoveBinaryFile(m_aArchivePath);
#endif

	SetStatus(m_aArchiveName);
	SetPercent(0);
	SetCurrentState(IUpdater::DOWNLOADING);

	m_TaskKind = ETaskKind::DOWNLOAD_ARCHIVE;
#if defined(CONF_PLATFORM_MACOS)
	m_pCurrentTask = HttpGetFile(m_aArchiveUrl, m_pStorage, m_aArchivePath, IStorage::TYPE_SAVE);
#else
	m_pCurrentTask = HttpGetFile(m_aArchiveUrl, m_pStorage, m_aArchivePath, IStorage::TYPE_ABSOLUTE);
#endif
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->ExpectSha256(m_ArchiveSha256);
	m_pCurrentTask->Timeout(CTimeout{30000, 0, 500, 20});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::LaunchApplyScriptAndQuit()
{
	if(!LaunchApply(true))
		return false;
	m_pClient->Quit();
	return true;
}

bool CUpdater::LaunchApply(bool Relaunch)
{
#if defined(CONF_PLATFORM_MACOS)
	char aArchivePath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!fs_is_file(aArchivePath))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

	if(MacIsDevelopmentBuild(m_pStorage))
	{
		SetStatus("Auto update is disabled for development builds");
		return false;
	}

	char aAppPath[IO_MAX_PATH_LENGTH];
	const bool HasAppBundle = MacAppBundlePath(m_pStorage, aAppPath, sizeof(aAppPath));
	char aScriptPath[IO_MAX_PATH_LENGTH];
	str_copy(aScriptPath, aArchivePath, sizeof(aScriptPath));
	StripFilename(aScriptPath);
	str_append(aScriptPath, "/cecenclient-update.sh", sizeof(aScriptPath));
	IOHANDLE Script = io_open(aScriptPath, IOFLAG_WRITE);
	if(!Script)
	{
		SetStatus("Failed to write the update script");
		return false;
	}

	char aPid[32];
	str_format(aPid, sizeof(aPid), "%d", process_id());
	if(HasAppBundle)
	{
		io_write(Script, MAC_APPLY_SCRIPT, str_length(MAC_APPLY_SCRIPT));
		io_close(Script);
		log_info("updater", "installing %s into %s", aArchivePath, aAppPath);
		const char *apArguments[] = {aScriptPath, aPid, aArchivePath, aAppPath, Relaunch ? "1" : "0"};
		if(process_execute("/bin/sh", EShellExecuteWindowState::BACKGROUND, apArguments, std::size(apArguments)) == INVALID_PROCESS)
		{
			SetStatus("Failed to launch updater");
			return false;
		}
		return true;
	}

	char aExePath[IO_MAX_PATH_LENGTH];
	char aExeDir[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExePath, sizeof(aExePath));
	str_copy(aExeDir, aExePath, sizeof(aExeDir));
	StripFilename(aExeDir);
	if(aExeDir[0] == '\0')
	{
		io_close(Script);
		SetStatus("Failed to resolve install directory");
		return false;
	}

	io_write(Script, MAC_PORTABLE_APPLY_SCRIPT, str_length(MAC_PORTABLE_APPLY_SCRIPT));
	io_close(Script);
	log_info("updater", "installing portable update %s into %s", aArchivePath, aExeDir);
	const char *apArguments[] = {aScriptPath, aPid, aArchivePath, aExeDir, aExePath, Relaunch ? "1" : "0"};
	if(process_execute("/bin/sh", EShellExecuteWindowState::BACKGROUND, apArguments, std::size(apArguments)) == INVALID_PROCESS)
	{
		SetStatus("Failed to launch updater");
		return false;
	}
	return true;
#elif defined(CONF_FAMILY_WINDOWS) || defined(CONF_PLATFORM_LINUX)
	char aArchivePath[IO_MAX_PATH_LENGTH];
	char aUpdaterPath[IO_MAX_PATH_LENGTH];
	char aInstallDir[IO_MAX_PATH_LENGTH];
	char aExePath[IO_MAX_PATH_LENGTH];
	char aPid[32];

	m_pStorage->GetBinaryPath(m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!m_pStorage->FileExists(aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

#if defined(CONF_FAMILY_WINDOWS)
	m_pStorage->GetBinaryPathAbsolute("CecenClient-updater.exe", aUpdaterPath, sizeof(aUpdaterPath));
#else
	m_pStorage->GetBinaryPathAbsolute("CecenClient-updater", aUpdaterPath, sizeof(aUpdaterPath));
#endif
	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExePath, sizeof(aExePath));
	str_copy(aInstallDir, aExePath, sizeof(aInstallDir));
	StripFilename(aInstallDir);

	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArguments[] = {aPid, aArchivePath, aInstallDir, aExePath};

	if(process_execute(aUpdaterPath, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments)) == INVALID_PROCESS)
	{
		SetStatus("Failed to launch updater");
		return false;
	}
	return true;
#else
	SetStatus("Archive updater is only available on Windows and Linux");
	return false;
#endif
}

void CUpdater::CheckForUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING)
		return;

#if !defined(CONF_FAMILY_WINDOWS) && !defined(CONF_PLATFORM_LINUX) && !defined(CONF_PLATFORM_MACOS)
	SetStatus("In-place updater is not available on this platform");
	SetCurrentState(IUpdater::FAIL);
	return;
#endif

	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	m_aManifestUrl[0] = '\0';
	m_aSignatureUrl[0] = '\0';
	StartReleaseFetch();
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING)
		return;

	if((State == IUpdater::VERSION_AVAILABLE || State == IUpdater::FAIL) && m_aArchiveUrl[0] != '\0')
	{
		m_DownloadRetries = 0;
		StartManifestFetch(false);
		return;
	}

	CheckForUpdate();
}

void CUpdater::ApplyUpdateAndRestart()
{
	if(GetCurrentState() != IUpdater::NEED_RESTART || m_ApplyStarted)
		return;

	m_ApplyStarted = true; // 667client
	if(!LaunchApplyScriptAndQuit())
	{
		m_ApplyStarted = false;
		SetCurrentState(IUpdater::FAIL);
	}
}

void CUpdater::Update()
{
	if(g_Config.m_ScAutoUpdate != 0)
	{
		const EUpdaterState State = GetCurrentState();
		if(State == IUpdater::VERSION_AVAILABLE)
			InitiateUpdate();
		else if(State == IUpdater::NEED_RESTART && !m_ApplyStarted && m_pClient->State() == IClient::STATE_OFFLINE)
			ApplyUpdateAndRestart();
	}

	if(m_AutoCheckPending && m_pHttp && GetCurrentState() == CLEAN)
	{
		m_AutoCheckPending = false;
		CheckForUpdate();
	}

	if(!m_pCurrentTask)
		return;

	if(!m_pCurrentTask->Done())
	{
		if(GetCurrentState() == IUpdater::DOWNLOADING)
			SetPercent(m_pCurrentTask->Progress());
		return;
	}

	const bool RequestDone = m_pCurrentTask->State() == EHttpState::DONE;
	if(!RequestDone || m_pCurrentTask->StatusCode() >= 400)
	{
		log_error("updater", "request failed (state %d, http %d)", (int)m_pCurrentTask->State(), RequestDone ? m_pCurrentTask->StatusCode() : 0);
		if(m_TaskKind == ETaskKind::DOWNLOAD_ARCHIVE && m_DownloadRetries < 3)
		{
			m_DownloadRetries++;
			SetStatus("Download failed, retrying");
			StartArchiveDownload();
			return;
		}
		ResetTask();
		SetStatus("Update check failed");
		SetCurrentState(IUpdater::FAIL);
		return;
	}

	if(m_TaskKind == ETaskKind::FETCH_RELEASE)
	{
		ParseReleaseTask();
		ResetTask();
		return;
	}

	if(m_TaskKind == ETaskKind::FETCH_MANIFEST)
	{
		unsigned char *pManifest = nullptr;
		size_t ManifestLength = 0;
		m_pCurrentTask->Result(&pManifest, &ManifestLength);
		m_Manifest.assign(reinterpret_cast<const char *>(pManifest), pManifest ? ManifestLength : 0);
		StartManifestFetch(true);
		return;
	}

	if(m_TaskKind == ETaskKind::FETCH_SIGNATURE)
	{
		const bool Verified = VerifyManifest();
		m_Manifest.clear();
		if(!Verified)
		{
			log_error("updater", "update %s failed the signature check", m_aLatestVersion);
			ResetTask();
			SetStatus("Update signature check failed");
			SetCurrentState(IUpdater::FAIL);
			return;
		}
		StartArchiveDownload();
		return;
	}

	if(m_TaskKind == ETaskKind::DOWNLOAD_ARCHIVE)
	{
		ResetTask();
		SetPercent(100);
		SetStatus(m_aArchiveName[0] != '\0' ? m_aArchiveName : "update");
		SetCurrentState(IUpdater::NEED_RESTART);
	}
}

void CUpdater::OnQuit()
{
	if(m_ApplyStarted || GetCurrentState() != IUpdater::NEED_RESTART)
		return;
	m_ApplyStarted = true;
	LaunchApply(false);
}
