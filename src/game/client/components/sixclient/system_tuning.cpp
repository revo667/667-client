/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-f7f2fab3 */

#include "system_tuning.h"

#include <base/log.h>
#include <base/mem.h>
#include <base/thread.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <Windows.h>
#include <TlHelp32.h>
#include <mmsystem.h>
#include <processthreadsapi.h>

static bool IsDiscordProcessName(const wchar_t *pProcessName)
{
	return _wcsicmp(pProcessName, L"Discord.exe") == 0 ||
	       _wcsicmp(pProcessName, L"DiscordCanary.exe") == 0 ||
	       _wcsicmp(pProcessName, L"DiscordPTB.exe") == 0 ||
	       _wcsicmp(pProcessName, L"DiscordSystemHelper.exe") == 0;
}

struct SPowerThrottlingState
{
	ULONG m_Version;
	ULONG m_ControlMask;
	ULONG m_StateMask;
};

static constexpr int PROCESS_INFO_POWER_THROTTLING = 4;
static constexpr ULONG POWER_THROTTLING_VERSION = 1;
static constexpr ULONG POWER_THROTTLING_EXECUTION_SPEED = 0x1;
static constexpr ULONG POWER_THROTTLING_IGNORE_TIMER_RESOLUTION = 0x4;

typedef BOOL(WINAPI *FSetProcessInformation)(HANDLE, int, LPVOID, DWORD);
typedef HANDLE(WINAPI *FAvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
typedef BOOL(WINAPI *FAvRevertMmThreadCharacteristics)(HANDLE);

static void SetPowerThrottlingOptOut(bool OptOut)
{
	static FSetProcessInformation s_pfnSetProcessInformation = (FSetProcessInformation)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
	if(!s_pfnSetProcessInformation)
		return;

	SPowerThrottlingState State;
	State.m_Version = POWER_THROTTLING_VERSION;
	State.m_ControlMask = OptOut ? (POWER_THROTTLING_EXECUTION_SPEED | POWER_THROTTLING_IGNORE_TIMER_RESOLUTION) : 0;
	State.m_StateMask = 0;
	if(!s_pfnSetProcessInformation(GetCurrentProcess(), PROCESS_INFO_POWER_THROTTLING, &State, sizeof(State)))
		log_info("667client", "Failed to change power throttling state");
}
#elif defined(CONF_PLATFORM_MACOS)
#include <macos/latency.h>
#endif

void CSystemTuning::SetDDNetProcessPriority(bool Set)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!SetPriorityClass(GetCurrentProcess(), Set ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS))
	{
		log_info("667client", Set ? "Failed to set process priority" : "Failed to reset process priority");
		return;
	}
	if(!SetThreadPriority(GetCurrentThread(), Set ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL))
	{
		log_info("667client", Set ? "Failed to set thread priority" : "Failed to reset thread priority");
		return;
	}
#else
	(void)Set;
#endif
}

void CSystemTuning::SetLowLatencyTimers(bool Enable)
{
	if(Enable == m_LowLatencyTimersApplied)
		return;
	m_LowLatencyTimersApplied = Enable;

#if defined(CONF_FAMILY_WINDOWS)
	SetPowerThrottlingOptOut(Enable);
	if(Enable)
		timeBeginPeriod(1);
	else
		timeEndPeriod(1);
#elif defined(CONF_PLATFORM_MACOS)
	MacSetLatencyCriticalActivity(Enable);
#endif
}

void CSystemTuning::SetMmcss(bool Enable)
{
	if(Enable == m_MmcssApplied)
		return;

#if defined(CONF_FAMILY_WINDOWS)
	static HMODULE s_hAvrt = LoadLibraryW(L"avrt.dll");
	if(!s_hAvrt)
		return;
	static FAvSetMmThreadCharacteristicsW s_pfnSet = (FAvSetMmThreadCharacteristicsW)(void *)GetProcAddress(s_hAvrt, "AvSetMmThreadCharacteristicsW");
	static FAvRevertMmThreadCharacteristics s_pfnRevert = (FAvRevertMmThreadCharacteristics)(void *)GetProcAddress(s_hAvrt, "AvRevertMmThreadCharacteristics");
	if(!s_pfnSet || !s_pfnRevert)
		return;

	if(Enable)
	{
		DWORD TaskIndex = 0;
		m_pMmcssHandle = s_pfnSet(L"Games", &TaskIndex);
		if(!m_pMmcssHandle)
		{
			log_info("667client", "Failed to register thread with MMCSS");
			return;
		}
	}
	else if(m_pMmcssHandle)
	{
		s_pfnRevert((HANDLE)m_pMmcssHandle);
		m_pMmcssHandle = nullptr;
	}
	m_MmcssApplied = Enable;
#endif
}

void CSystemTuning::UpdateLatencyState(bool WindowActive)
{
	SetLowLatencyTimers(g_Config.m_ScLowLatencyTimers && WindowActive);
	SetMmcss(g_Config.m_ScMmcss && WindowActive);
}

void CSystemTuning::ApplyLowLatencyPreset()
{
	if(g_Config.m_GfxVsync)
		Graphics()->SetVSync(false);
	g_Config.m_GfxAsyncRenderOld = 1;
	g_Config.m_ClRefreshRate = 0;
	g_Config.m_ScLowLatencyTimers = 1;
#if defined(CONF_FAMILY_WINDOWS)
	g_Config.m_ScMmcss = 1;
	g_Config.m_ScHighProcessPriority = 1;
	SetDDNetProcessPriority(true);
#endif
	UpdateLatencyState(static_cast<IEngineGraphics *>(Graphics())->WindowActive() != 0);
}

void CSystemTuning::DiscordPriorityThread(void *pUserData)
{
	CSystemTuning *pSelf = (CSystemTuning *)pUserData;
	pSelf->SetDiscordProcessesNormalPriority();
	pSelf->m_DiscordPriorityDelay.store(time_get() + time_freq() * 30);
	pSelf->m_DiscordPriorityThreadRunning.store(false);
}

void CSystemTuning::StartDiscordPriorityThread()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(m_DiscordPriorityThreadRunning.load())
		return;

	if(m_pDiscordPriorityThread)
	{
		thread_wait(m_pDiscordPriorityThread);
		m_pDiscordPriorityThread = nullptr;
	}

	m_DiscordPriorityDelay.store(0);
	m_DiscordPriorityThreadRunning.store(true);
	m_pDiscordPriorityThread = thread_init(DiscordPriorityThread, this, "discord-priority");
#endif
}

void CSystemTuning::SetDiscordProcessesNormalPriority()
{
#if defined(CONF_FAMILY_WINDOWS)
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(hSnapshot == INVALID_HANDLE_VALUE)
	{
		log_info("667client", "Failed to create process snapshot");
		return;
	}

	PROCESSENTRY32 Entry;
	mem_zero(&Entry, sizeof(Entry));
	Entry.dwSize = sizeof(Entry);

	int Changed = 0;
	int Failed = 0;

	if(Process32First(hSnapshot, &Entry))
	{
		do
		{
			if(!IsDiscordProcessName(Entry.szExeFile))
				continue;

			HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID);
			if(!hProcess)
			{
				Failed++;
				continue;
			}

			if(SetPriorityClass(hProcess, NORMAL_PRIORITY_CLASS))
				Changed++;
			else
				Failed++;

			CloseHandle(hProcess);
		} while(Process32Next(hSnapshot, &Entry));
	}

	CloseHandle(hSnapshot);
	log_info("667client", "Discord process priority: changed=%d failed=%d", Changed, Failed);
#endif
}

void CSystemTuning::OnInit()
{
	SetDDNetProcessPriority(g_Config.m_ScHighProcessPriority);
	if(g_Config.m_ScDiscordNormalProcessPriority)
		StartDiscordPriorityThread();

	m_LastWindowActive = static_cast<IEngineGraphics *>(Graphics())->WindowActive() != 0;
	m_HasWindowActive = true;
	UpdateLatencyState(m_LastWindowActive);
}

void CSystemTuning::OnRender()
{
#if defined(CONF_FAMILY_WINDOWS)
	const int64_t DiscordPriorityDelay = m_DiscordPriorityDelay.load();
	if(g_Config.m_ScDiscordNormalProcessPriority && !m_DiscordPriorityThreadRunning.load() && DiscordPriorityDelay < time_get())
		StartDiscordPriorityThread();
#endif

	const bool WindowActive = static_cast<IEngineGraphics *>(Graphics())->WindowActive() != 0;
	if(!m_HasWindowActive || WindowActive != m_LastWindowActive)
	{
		m_HasWindowActive = true;
		m_LastWindowActive = WindowActive;
		SetDDNetProcessPriority(g_Config.m_ScHighProcessPriority);
	}
	UpdateLatencyState(WindowActive);
}

void CSystemTuning::OnShutdown()
{
	SetLowLatencyTimers(false);
	SetMmcss(false);
	if(m_pDiscordPriorityThread)
	{
		thread_wait(m_pDiscordPriorityThread);
		m_pDiscordPriorityThread = nullptr;
	}
	m_DiscordPriorityThreadRunning.store(false);
}

void CSystemTuning::OnConsoleInit()
{
	Console()->Chain("sc_high_process_priority", ConchainDDNetProcessPriority, this);
	Console()->Chain("sc_discord_normal_process_priority", ConchainDiscordProcessPriority, this);
}

void CSystemTuning::ConchainDDNetProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CSystemTuning *pSelf = (CSystemTuning *)pUserData;
	if(pResult->NumArguments())
	{
		const bool Value = pResult->GetInteger(0) != 0;
		pSelf->SetDDNetProcessPriority(Value);
	}
}

void CSystemTuning::ConchainDiscordProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CSystemTuning *pSelf = (CSystemTuning *)pUserData;
	if(pResult->NumArguments())
	{
		if(pResult->GetInteger(0) != 0)
			pSelf->StartDiscordPriorityThread();
	}
}
