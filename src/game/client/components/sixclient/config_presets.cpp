/* Copyright © 2026 BestProject Team */
#include "config_presets.h"

#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <algorithm>
#include <utility>

static constexpr const char *CONFIGS_DIR = "667Client/configs";

bool CScConfigPresets::NameOk(const char *pName)
{
	if(!pName || !str_utf8_check(pName) || str_length(pName) >= MAX_NAME_LENGTH)
		return false;
	return str_valid_filename(pName);
}

static bool MakePath(char *pPath, int PathSize, const char *pName)
{
	if(!CScConfigPresets::NameOk(pName))
		return false;
	str_format(pPath, PathSize, "%s/%s.cfg", CONFIGS_DIR, pName);
	return true;
}

static int ListCallback(const CFsFileInfo *pInfo, int IsDir, int, void *pUser)
{
	if(IsDir)
		return 0;
	const char *pExt = str_endswith(pInfo->m_pName, ".cfg");
	if(!pExt)
		return 0;
	const int NameLen = (int)(pExt - pInfo->m_pName);
	if(NameLen <= 0 || NameLen >= CScConfigPresets::MAX_NAME_LENGTH)
		return 0;

	CScConfigPreset Preset;
	str_copy(Preset.m_aName, pInfo->m_pName, NameLen + 1);
	if(!CScConfigPresets::NameOk(Preset.m_aName))
		return 0;
	Preset.m_Time = pInfo->m_TimeModified;
	Preset.m_Changes = 0;
	auto *pList = static_cast<std::pair<CScConfigPresets *, IStorage *> *>(pUser);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", CONFIGS_DIR, pInfo->m_pName);
	IOHANDLE File = pList->second->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File)
	{
		CLineReader Reader;
		if(Reader.OpenFile(File))
		{
			const char *pLine = Reader.Get();
			int Value = 0;
			if(pLine && sscanf(pLine, "# changes %d", &Value) == 1)
				Preset.m_Changes = Value;
		}
	}
	pList->first->m_vPresets.push_back(Preset);
	return 0;
}

static void CountChangedVariable(SConfigVariable *pVariable, void *pUserData)
{
	if(pVariable->m_ConfigDomain == ConfigDomain::SIXCLIENT && (pVariable->m_Flags & CFGFLAG_SAVE) != 0 && !pVariable->IsDefault())
		++*static_cast<int *>(pUserData);
}

static void ResetSixClientVariable(SConfigVariable *pVariable, void *)
{
	if(pVariable->m_ConfigDomain == ConfigDomain::SIXCLIENT && (pVariable->m_Flags & CFGFLAG_SAVE) != 0)
		pVariable->ResetToDefault();
}

static void WritePresetLine(IOHANDLE File, const char *pLine)
{
	io_write(File, pLine, str_length(pLine));
	io_write_newline(File);
}

static void WritePresetVariable(SConfigVariable *pVariable, void *pUserData)
{
	if(pVariable->m_ConfigDomain != ConfigDomain::SIXCLIENT)
		return;
	if((pVariable->m_Flags & CFGFLAG_SAVE) == 0 || pVariable->IsDefault())
		return;
	char aBuf[2048];
	pVariable->Serialize(aBuf, sizeof(aBuf));
	WritePresetLine(static_cast<IOHANDLE>(pUserData), aBuf);
}

bool CScConfigPresets::WriteFile(const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!MakePath(aPath, sizeof(aPath), pName))
		return false;
	Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(CONFIGS_DIR, IStorage::TYPE_SAVE);

	char aTmpPath[IO_MAX_PATH_LENGTH];
	IOHANDLE File = Storage()->OpenFile(IStorage::FormatTmpPath(aTmpPath, sizeof(aTmpPath), aPath), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	char aCount[64];
	str_format(aCount, sizeof(aCount), "# changes %d", CountChanged());
	WritePresetLine(File, aCount);
	ConfigManager()->VisitVariables(WritePresetVariable, File);
	if(io_sync(File) != 0 || io_close(File) != 0)
	{
		Storage()->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return false;
	}
	if(!Storage()->RenameFile(aTmpPath, aPath, IStorage::TYPE_SAVE))
	{
		Storage()->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return false;
	}
	return true;
}

void CScConfigPresets::OnConsoleInit()
{
	Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(CONFIGS_DIR, IStorage::TYPE_SAVE);
	Refresh();
}

void CScConfigPresets::Refresh()
{
	m_vPresets.clear();
	std::pair<CScConfigPresets *, IStorage *> List{this, Storage()};
	Storage()->ListDirectoryInfo(IStorage::TYPE_SAVE, CONFIGS_DIR, ListCallback, &List);
	std::sort(m_vPresets.begin(), m_vPresets.end(), [](const CScConfigPreset &A, const CScConfigPreset &B) {
		return str_comp_nocase(A.m_aName, B.m_aName) < 0;
	});
}

bool CScConfigPresets::Exists(const char *pName) const
{
	for(const CScConfigPreset &Preset : m_vPresets)
	{
		if(str_comp(Preset.m_aName, pName) == 0)
			return true;
	}
	return false;
}

bool CScConfigPresets::SaveNew(const char *pName)
{
	if(!NameOk(pName) || Exists(pName))
		return false;
	if(!WriteFile(pName))
		return false;
	Refresh();
	return Exists(pName);
}

bool CScConfigPresets::Override(const char *pName)
{
	if(!NameOk(pName) || !Exists(pName))
		return false;
	if(!WriteFile(pName))
		return false;
	Refresh();
	return true;
}

bool CScConfigPresets::Load(const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!MakePath(aPath, sizeof(aPath), pName))
		return false;
	if(!Storage()->FileExists(aPath, IStorage::TYPE_SAVE))
	{
		Refresh();
		return false;
	}

	ConfigManager()->VisitVariables(ResetSixClientVariable, nullptr);
	if(!Console()->ExecuteFile(aPath, IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE))
		return false;
	ConfigManager()->Save();
	return true;
}

int CScConfigPresets::CountChanged() const
{
	int Count = 0;
	const_cast<CScConfigPresets *>(this)->ConfigManager()->VisitVariables(CountChangedVariable, &Count);
	return Count;
}

void CScConfigPresets::OpenFolder() const
{
	CScConfigPresets *pSelf = const_cast<CScConfigPresets *>(this);
	pSelf->Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
	pSelf->Storage()->CreateFolder(CONFIGS_DIR, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	pSelf->Storage()->GetCompletePath(IStorage::TYPE_SAVE, CONFIGS_DIR, aPath, sizeof(aPath));
	pSelf->Client()->ViewFile(aPath);
}

bool CScConfigPresets::Delete(const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!MakePath(aPath, sizeof(aPath), pName))
		return false;
	if(!Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE))
		return false;
	Refresh();
	return true;
}
