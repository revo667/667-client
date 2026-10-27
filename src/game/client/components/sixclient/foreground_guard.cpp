/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-78614cfb */

#include "foreground_guard.h"

#include <base/detect.h>
#include <base/os.h>

#include <algorithm>
#include <chrono>
#include <thread>

static constexpr int HOLD_STEP_MS = 40;

#if defined(CONF_FAMILY_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>
static constexpr int SPOTIFY_WINDOW_SETTLE_MS = 1500;

static std::vector<DWORD> SpotifyProcessIds()
{
	std::vector<DWORD> vPids;
	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(Snapshot == INVALID_HANDLE_VALUE)
		return vPids;
	PROCESSENTRY32W Entry = {};
	Entry.dwSize = sizeof(Entry);
	if(Process32FirstW(Snapshot, &Entry))
	{
		do
		{
			if(_wcsicmp(Entry.szExeFile, L"Spotify.exe") == 0)
				vPids.push_back(Entry.th32ProcessID);
		} while(Process32NextW(Snapshot, &Entry));
	}
	CloseHandle(Snapshot);
	return vPids;
}

static bool IsSpotifyWindow(HWND Window)
{
	if(!Window)
		return false;
	DWORD Pid = 0;
	GetWindowThreadProcessId(Window, &Pid);
	if(Pid == 0)
		return false;
	HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
	if(!Process)
		return false;
	wchar_t aPath[MAX_PATH];
	DWORD Size = MAX_PATH;
	const BOOL Ok = QueryFullProcessImageNameW(Process, 0, aPath, &Size);
	CloseHandle(Process);
	if(!Ok)
		return false;
	const wchar_t *pName = wcsrchr(aPath, L'\\');
	return _wcsicmp(pName ? pName + 1 : aPath, L"Spotify.exe") == 0;
}

static bool SpotifyWindowVisible()
{
	std::vector<DWORD> vPids = SpotifyProcessIds();
	if(vPids.empty())
		return false;

	struct SEnumData
	{
		const std::vector<DWORD> *m_pPids;
		bool m_Found;
	};
	SEnumData Data = {&vPids, false};
	EnumWindows([](HWND Hwnd, LPARAM LParam) -> BOOL {
		SEnumData *pData = reinterpret_cast<SEnumData *>(LParam);
		if(!IsWindowVisible(Hwnd) || GetWindow(Hwnd, GW_OWNER) != nullptr)
			return TRUE;
		DWORD Pid = 0;
		GetWindowThreadProcessId(Hwnd, &Pid);
		for(const DWORD SpotifyPid : *pData->m_pPids)
		{
			if(Pid == SpotifyPid)
			{
				pData->m_Found = true;
				return FALSE;
			}
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&Data));
	return Data.m_Found;
}

static void ReclaimFromSpotify(HWND Window)
{
	if(!Window || !IsWindow(Window))
		return;
	HWND Foreground = GetForegroundWindow();
	if(Foreground == Window || !IsSpotifyWindow(Foreground))
		return;

	const DWORD ForegroundThread = GetWindowThreadProcessId(Foreground, nullptr);
	const DWORD CurrentThread = GetCurrentThreadId();
	const bool Attached = ForegroundThread != 0 && ForegroundThread != CurrentThread &&
			      AttachThreadInput(CurrentThread, ForegroundThread, TRUE);
	if(IsIconic(Window))
		ShowWindowAsync(Window, SW_RESTORE);
	SetForegroundWindow(Window);
	if(Attached)
		AttachThreadInput(CurrentThread, ForegroundThread, FALSE);

	ShowWindowAsync(Foreground, SW_SHOWMINNOACTIVE);
}

static bool QuerySpotifyExe(wchar_t *pOut, size_t OutChars, bool &Running)
{
	Running = false;
	if(!pOut || OutChars == 0)
		return false;
	pOut[0] = L'\0';

	for(const DWORD Pid : SpotifyProcessIds())
	{
		Running = true;
		HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
		if(!Process)
			continue;
		DWORD Size = (DWORD)OutChars;
		const BOOL Ok = QueryFullProcessImageNameW(Process, 0, pOut, &Size);
		CloseHandle(Process);
		if(Ok && pOut[0])
			return true;
	}

	wchar_t aBase[MAX_PATH];
	if(ExpandEnvironmentStringsW(L"%APPDATA%\\Spotify\\Spotify.exe", aBase, MAX_PATH) > 0 && GetFileAttributesW(aBase) != INVALID_FILE_ATTRIBUTES)
	{
		wcsncpy(pOut, aBase, OutChars - 1);
		pOut[OutChars - 1] = L'\0';
		return true;
	}
	if(ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\Spotify.exe", aBase, MAX_PATH) > 0 && GetFileAttributesW(aBase) != INVALID_FILE_ATTRIBUTES)
	{
		wcsncpy(pOut, aBase, OutChars - 1);
		pOut[OutChars - 1] = L'\0';
		return true;
	}
	return false;
}

CForegroundGuard::CForegroundGuard()
{
	HWND Window = GetForegroundWindow();
	DWORD ProcessId = 0;
	GetWindowThreadProcessId(Window, &ProcessId);
	if(Window != nullptr && ProcessId == GetCurrentProcessId())
	{
		m_pWindow = Window;
		m_Locked = LockSetForegroundWindow(LSFW_LOCK) != 0;
	}
}

CForegroundGuard::~CForegroundGuard()
{
	if(m_pWindow)
		ReclaimFromSpotify(static_cast<HWND>(m_pWindow));
	if(m_Locked)
		LockSetForegroundWindow(LSFW_UNLOCK);
}

bool CForegroundGuard::OpenLinkInBackground(const char *pLink)
{
	if(!pLink || !pLink[0])
		return true;
	const int Length = MultiByteToWideChar(CP_UTF8, 0, pLink, -1, nullptr, 0);
	if(Length <= 0)
		return true;
	std::wstring WideLink(Length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, pLink, -1, WideLink.data(), Length);

	wchar_t aExe[MAX_PATH];
	bool Running = false;
	if(QuerySpotifyExe(aExe, MAX_PATH, Running))
	{
		std::wstring CmdLine = L"\"";
		CmdLine += aExe;
		CmdLine += L"\" --uri=";
		CmdLine += WideLink;
		STARTUPINFOW Si = {};
		Si.cb = sizeof(Si);
		Si.dwFlags = STARTF_USESHOWWINDOW;
		Si.wShowWindow = SW_SHOWMINNOACTIVE;
		PROCESS_INFORMATION Pi = {};
		std::wstring Mutable = CmdLine;
		if(CreateProcessW(aExe, Mutable.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Si, &Pi))
		{
			CloseHandle(Pi.hThread);
			CloseHandle(Pi.hProcess);
			return Running;
		}
	}

	SHELLEXECUTEINFOW Info = {};
	Info.cbSize = sizeof(Info);
	Info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	Info.lpVerb = L"open";
	Info.lpFile = WideLink.c_str();
	Info.nShow = SW_SHOWMINNOACTIVE;
	ShellExecuteExW(&Info);
	return Running;
}

void CForegroundGuard::Hold(int Milliseconds)
{
	HWND Window = static_cast<HWND>(m_pWindow);
	for(int Waited = 0; Waited < Milliseconds && !Aborted(); Waited += HOLD_STEP_MS)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(HOLD_STEP_MS));
		if(Window)
			ReclaimFromSpotify(Window);
	}
}

void CForegroundGuard::HoldUntilSpotifyWindow(int MaxMilliseconds)
{
	HWND Window = static_cast<HWND>(m_pWindow);
	for(int Waited = 0; Waited < MaxMilliseconds && !Aborted(); Waited += HOLD_STEP_MS)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(HOLD_STEP_MS));
		if(Window)
			ReclaimFromSpotify(Window);
		if(SpotifyWindowVisible())
			break;
	}
	Hold(SPOTIFY_WINDOW_SETTLE_MS);
}
#else
CForegroundGuard::CForegroundGuard() = default;
CForegroundGuard::~CForegroundGuard() = default;

bool CForegroundGuard::OpenLinkInBackground(const char *pLink)
{
	(void)pLink;
	return true;
}

void CForegroundGuard::Hold(int Milliseconds)
{
	for(int Waited = 0; Waited < Milliseconds && !Aborted(); Waited += HOLD_STEP_MS)
		std::this_thread::sleep_for(std::chrono::milliseconds(std::min(HOLD_STEP_MS, Milliseconds - Waited)));
}

void CForegroundGuard::HoldUntilSpotifyWindow(int MaxMilliseconds)
{
	(void)MaxMilliseconds;
}
#endif
