/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-b6130947 */

#include "image_picker.h"

#include <base/detect.h>

#include <cstdio>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
// commdlg.h needs the GDI types the project leaves out of windows.h; nothing here clashes with them.
#undef NOGDI
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
// commdlg.h has to come after windows.h.
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace
{
#if defined(CONF_FAMILY_WINDOWS)
	std::string PickImage(const std::string &Title)
	{
		std::wstring WideTitle(Title.size() + 1, L'\0');
		WideTitle.resize(MultiByteToWideChar(CP_UTF8, 0, Title.c_str(), -1, WideTitle.data(), (int)WideTitle.size()));
		wchar_t aFile[MAX_PATH * 4] = L"";
		OPENFILENAMEW Dialog = {};
		Dialog.lStructSize = sizeof(Dialog);
		Dialog.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp\0All files\0*.*\0";
		Dialog.lpstrFile = aFile;
		Dialog.nMaxFile = sizeof(aFile) / sizeof(aFile[0]);
		Dialog.lpstrTitle = WideTitle.c_str();
		Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
		if(!GetOpenFileNameW(&Dialog))
			return "";
		char aUtf8[MAX_PATH * 4 * 3];
		if(!WideCharToMultiByte(CP_UTF8, 0, aFile, -1, aUtf8, sizeof(aUtf8), nullptr, nullptr))
			return "";
		return aUtf8;
	}
#else
	std::string RunAndReadLine(const char *pCommand)
	{
		FILE *pPipe = popen(pCommand, "r");
		if(!pPipe)
			return "";
		std::string Output;
		char aBuf[1024];
		while(fgets(aBuf, sizeof(aBuf), pPipe))
			Output += aBuf;
		pclose(pPipe);
		while(!Output.empty() && (Output.back() == '\n' || Output.back() == '\r'))
			Output.pop_back();
		return Output;
	}

	std::string PickImage(const std::string &Title)
	{
		std::string Quoted;
		for(char c : Title)
			if(c != '"' && c != '\'' && c != '\\')
				Quoted += c;
#if defined(CONF_PLATFORM_MACOS)
		const std::string Command = "/usr/bin/osascript -e 'tell me to activate' -e 'POSIX path of (choose file with prompt \"" + Quoted + "\" of type {\"public.image\"})' 2>/dev/null";
#else
		const std::string Command = "zenity --file-selection --title='" + Quoted + "' --file-filter='Images | *.png *.jpg *.jpeg *.gif *.webp *.bmp' 2>/dev/null";
#endif
		return RunAndReadLine(Command.c_str());
	}
#endif
}

CImagePicker::~CImagePicker()
{
	// The dialog can't be closed from here; let the thread finish on its own.
	if(m_Thread.joinable())
		m_Thread.detach();
}

void CImagePicker::Start(const char *pTitle)
{
	{
		const CLockScope LockScope(m_pState->m_Lock);
		if(m_pState->m_Running)
			return;
		m_pState->m_Running = true;
		m_pState->m_HasResult = false;
		m_pState->m_Result.clear();
	}
	if(m_Thread.joinable())
		m_Thread.join();
	m_Thread = std::thread([pState = m_pState, Title = std::string(pTitle)]() {
		std::string Path = PickImage(Title);
		const CLockScope LockScope(pState->m_Lock);
		pState->m_Result = std::move(Path);
		pState->m_HasResult = !pState->m_Result.empty();
		pState->m_Running = false;
	});
}

bool CImagePicker::Busy()
{
	const CLockScope LockScope(m_pState->m_Lock);
	return m_pState->m_Running;
}

bool CImagePicker::TakeResult(std::string &Path)
{
	const CLockScope LockScope(m_pState->m_Lock);
	if(m_pState->m_Running || !m_pState->m_HasResult)
		return false;
	m_pState->m_HasResult = false;
	Path = std::move(m_pState->m_Result);
	return true;
}
