/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-85763ad3 */

#include "beta_reporter.h"

#include "inputs.h"
#include <game/client/components/sixclient/social_card.h>
#include <game/client/components/sixclient/spotify_connect.h>
#include "version.h"

#include <base/dbg.h>
#include <base/detect.h>
#include <base/io.h>
#include <base/net.h>
#include <base/log.h>
#include <base/os.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/beta_probe.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/uuid_manager.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <typeinfo>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <csignal>
#endif

#if defined(CONF_PLATFORM_MACOS)
#include <mach/mach.h>
#endif

static constexpr const char *BETA_DIR = "667Client/beta";
static constexpr const char *SESSION_FILE = "667Client/beta/session.txt";
static constexpr const char *CRASH_FILE = "667Client/beta/crash.txt";
static constexpr const char *ASSERT_FILE = "667Client/beta/assert.txt";
static constexpr const char *HANG_FILE = "667Client/beta/hang.txt";
static constexpr const char *PENDING_FILE = "667Client/beta/pending.json";
static constexpr const char *ID_FILE = "667Client/beta/id.txt";

static constexpr int UPLOAD_INTERVAL_SECONDS = 120;
static constexpr int URGENT_UPLOAD_SECONDS = 10;
static constexpr int MAX_EVENTS = 40;
static constexpr int HANG_MS = 1000;
static constexpr int SAMPLE_STALL_MS = 120;
static constexpr int WATCHDOG_INTERVAL_MS = 25;
static constexpr int ARM_DELAY_SECONDS = 5;
static constexpr int MAX_STACK_FRAMES = 32;

static char gs_aCrashPath[IO_MAX_PATH_LENGTH];
static char gs_aAssertPath[IO_MAX_PATH_LENGTH];
static char gs_aHangPath[IO_MAX_PATH_LENGTH];

namespace
{
	void WriteWholeFile(const char *pPath, const char *pData, size_t Size)
	{
		IOHANDLE File = io_open(pPath, IOFLAG_WRITE);
		if(!File)
			return;
		io_write(File, pData, Size);
		io_close(File);
	}

	void AssertHook(const char *pMessage)
	{
		char aBuf[2048];
		str_format(aBuf, sizeof(aBuf), "message=%s\n", pMessage);
		WriteWholeFile(gs_aAssertPath, aBuf, str_length(aBuf));
	}

	std::string ComponentName(const CComponent *pComponent)
	{
		const char *pName = typeid(*pComponent).name();
		if(str_startswith(pName, "class "))
			pName += 6;
		else if(str_startswith(pName, "struct "))
			pName += 7;
		while(*pName >= '0' && *pName <= '9')
			pName++;
		return pName;
	}

#if defined(CONF_FAMILY_WINDOWS)
	LPTOP_LEVEL_EXCEPTION_FILTER gs_pfnPreviousFilter = nullptr;
	DWORD gs_MainThreadId = 0;
	HANDLE gs_MainThread = nullptr;
	ULONG_PTR gs_MainStackLow = 0;
	ULONG_PTR gs_MainStackHigh = 0;
	uintptr_t gs_ExeBase = 0;
	uintptr_t gs_ExeEnd = 0;
	wchar_t gs_awCrashPath[IO_MAX_PATH_LENGTH];
	wchar_t gs_awHangPath[IO_MAX_PATH_LENGTH];

	void FormatAddress(uintptr_t Address, char *pBuf, size_t BufSize)
	{
		HMODULE Module = nullptr;
		if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)Address, &Module) && Module)
		{
			wchar_t aPath[MAX_PATH];
			const DWORD Length = GetModuleFileNameW(Module, aPath, MAX_PATH);
			const wchar_t *pName = aPath;
			for(DWORD i = 0; i < Length; i++)
				if(aPath[i] == L'\\' || aPath[i] == L'/')
					pName = aPath + i + 1;
			char aName[MAX_PATH];
			if(Length == 0 || !WideCharToMultiByte(CP_UTF8, 0, pName, -1, aName, sizeof(aName), nullptr, nullptr))
				str_copy(aName, "?", sizeof(aName));
			snprintf(pBuf, BufSize, "%s+0x%llx", aName, (unsigned long long)(Address - (uintptr_t)Module));
		}
		else
			snprintf(pBuf, BufSize, "0x%llx", (unsigned long long)Address);
	}

	void WriteFileW(const wchar_t *pPath, const char *pData, size_t Size)
	{
		HANDLE File = CreateFileW(pPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(File == INVALID_HANDLE_VALUE)
			return;
		DWORD Written = 0;
		WriteFile(File, pData, (DWORD)Size, &Written, nullptr);
		CloseHandle(File);
	}

#if defined(_M_X64) || defined(__x86_64__)
	int WalkStack(CONTEXT Context, uintptr_t *pFrames, int MaxFrames)
	{
		int Num = 0;
		while(Num < MaxFrames && Context.Rip)
		{
			pFrames[Num++] = (uintptr_t)Context.Rip;
			DWORD64 ImageBase = 0;
			PRUNTIME_FUNCTION pFunction = RtlLookupFunctionEntry(Context.Rip, &ImageBase, nullptr);
			if(pFunction)
			{
				PVOID pHandlerData = nullptr;
				DWORD64 EstablisherFrame = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Context.Rip, pFunction, &Context, &pHandlerData, &EstablisherFrame, nullptr);
			}
			else if(Num == 1)
			{
				Context.Rip = *(DWORD64 *)Context.Rsp;
				Context.Rsp += 8;
			}
			else
				break;
		}
		return Num;
	}
#endif

	LONG WINAPI BetaExceptionFilter(EXCEPTION_POINTERS *pInfo)
	{
		static LONG s_Entered = 0;
		if(InterlockedExchange(&s_Entered, 1) == 0 && pInfo && pInfo->ExceptionRecord)
		{
			static char s_aBuf[8192];
			int Length = 0;
			auto Append = [&](const char *pLine) {
				const int LineLength = (int)strlen(pLine);
				if(Length + LineLength + 1 < (int)sizeof(s_aBuf))
				{
					memcpy(s_aBuf + Length, pLine, LineLength);
					Length += LineLength;
					s_aBuf[Length++] = '\n';
				}
			};
			char aLine[512], aAddress[300];
			const EXCEPTION_RECORD *pRecord = pInfo->ExceptionRecord;
			snprintf(aLine, sizeof(aLine), "code=0x%08lX", (unsigned long)pRecord->ExceptionCode);
			Append(aLine);
			FormatAddress((uintptr_t)pRecord->ExceptionAddress, aAddress, sizeof(aAddress));
			snprintf(aLine, sizeof(aLine), "address=%s", aAddress);
			Append(aLine);
			snprintf(aLine, sizeof(aLine), "thread=%s", GetCurrentThreadId() == gs_MainThreadId ? "main" : "other");
			Append(aLine);
			if(pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRecord->NumberParameters >= 2)
			{
				const ULONG_PTR Kind = pRecord->ExceptionInformation[0];
				snprintf(aLine, sizeof(aLine), "access=%s 0x%llx", Kind == 0 ? "read" : (Kind == 1 ? "write" : "execute"), (unsigned long long)pRecord->ExceptionInformation[1]);
				Append(aLine);
			}
#if defined(_M_X64) || defined(__x86_64__)
			if(pInfo->ContextRecord)
			{
				uintptr_t aFrames[MAX_STACK_FRAMES];
				const int NumFrames = WalkStack(*pInfo->ContextRecord, aFrames, MAX_STACK_FRAMES);
				for(int i = 0; i < NumFrames; i++)
				{
					FormatAddress(aFrames[i], aAddress, sizeof(aAddress));
					snprintf(aLine, sizeof(aLine), "frame=%s", aAddress);
					Append(aLine);
				}
			}
#endif
			struct SWriter
			{
				char *m_pBuf;
				int *m_pLength;
				int m_Size;
			} Writer = {s_aBuf, &Length, (int)sizeof(s_aBuf)};
			BetaProbe::WriteBreadcrumbsUnlocked([](const char *pCrumb, void *pUser) {
				SWriter *pWriter = static_cast<SWriter *>(pUser);
				const int CrumbLength = (int)strlen(pCrumb);
				if(*pWriter->m_pLength + 11 + CrumbLength + 1 >= pWriter->m_Size)
					return;
				memcpy(pWriter->m_pBuf + *pWriter->m_pLength, "breadcrumb=", 11);
				memcpy(pWriter->m_pBuf + *pWriter->m_pLength + 11, pCrumb, CrumbLength);
				*pWriter->m_pLength += 11 + CrumbLength;
				pWriter->m_pBuf[(*pWriter->m_pLength)++] = '\n';
			},
				&Writer);
			WriteFileW(gs_awCrashPath, s_aBuf, Length);
		}
		return gs_pfnPreviousFilter ? gs_pfnPreviousFilter(pInfo) : EXCEPTION_CONTINUE_SEARCH;
	}

	void InstallCrashHandler()
	{
		MultiByteToWideChar(CP_UTF8, 0, gs_aCrashPath, -1, gs_awCrashPath, IO_MAX_PATH_LENGTH);
		MultiByteToWideChar(CP_UTF8, 0, gs_aHangPath, -1, gs_awHangPath, IO_MAX_PATH_LENGTH);
		gs_MainThreadId = GetCurrentThreadId();
		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &gs_MainThread, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
		const NT_TIB *pTib = (const NT_TIB *)NtCurrentTeb();
		gs_MainStackLow = (ULONG_PTR)pTib->StackLimit;
		gs_MainStackHigh = (ULONG_PTR)pTib->StackBase;
		const uintptr_t Base = (uintptr_t)GetModuleHandleW(nullptr);
		const IMAGE_DOS_HEADER *pDos = (const IMAGE_DOS_HEADER *)Base;
		const IMAGE_NT_HEADERS *pNt = (const IMAGE_NT_HEADERS *)(Base + pDos->e_lfanew);
		gs_ExeBase = Base;
		gs_ExeEnd = Base + pNt->OptionalHeader.SizeOfImage;
		gs_pfnPreviousFilter = SetUnhandledExceptionFilter(BetaExceptionFilter);
	}

	bool FollowsCall(uintptr_t Address)
	{
		if(Address < gs_ExeBase + 8 || Address >= gs_ExeEnd)
			return false;
		const unsigned char *p = (const unsigned char *)Address;
		if(p[-5] == 0xE8)
			return true;
		for(int Back : {2, 3, 6, 7})
			if(p[-Back] == 0xFF && (p[-Back + 1] & 0x38) == 0x10)
				return true;
		return false;
	}

	bool SampleMainThread(std::vector<std::string> &vFrames)
	{
#if defined(_M_X64) || defined(__x86_64__)
		static unsigned char s_aStack[64 * 1024];
		if(!gs_MainThread)
			return false;
		if(SuspendThread(gs_MainThread) == (DWORD)-1)
			return false;
		CONTEXT Context = {};
		Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		size_t Copied = 0;
		uintptr_t StackStart = 0;
		if(GetThreadContext(gs_MainThread, &Context) && Context.Rsp >= gs_MainStackLow && Context.Rsp < gs_MainStackHigh)
		{
			StackStart = (uintptr_t)Context.Rsp;
			Copied = std::min<size_t>(sizeof(s_aStack), gs_MainStackHigh - StackStart);
			memcpy(s_aStack, (const void *)StackStart, Copied);
		}
		ResumeThread(gs_MainThread);

		char aAddress[300];
		FormatAddress((uintptr_t)Context.Rip, aAddress, sizeof(aAddress));
		vFrames.emplace_back(aAddress);
		for(size_t Offset = 0; Offset + sizeof(uintptr_t) <= Copied && (int)vFrames.size() < MAX_STACK_FRAMES; Offset += sizeof(uintptr_t))
		{
			uintptr_t Value;
			memcpy(&Value, s_aStack + Offset, sizeof(Value));
			if(FollowsCall(Value))
			{
				FormatAddress(Value, aAddress, sizeof(aAddress));
				vFrames.emplace_back(aAddress);
			}
		}
		return true;
#else
		(void)vFrames;
		return false;
#endif
	}
#else
	void WriteSignalReport(int Signal, void *pAddress)
	{
		const int Fd = open(gs_aCrashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if(Fd < 0)
			return;
		char aBuf[160];
		const int Length = snprintf(aBuf, sizeof(aBuf), "signal=%d\naddress=%p\n", Signal, pAddress);
		if(Length > 0)
			(void)!write(Fd, aBuf, Length);
		BetaProbe::WriteBreadcrumbsUnlocked([](const char *pCrumb, void *pUser) {
			const int CrumbFd = *static_cast<int *>(pUser);
			(void)!write(CrumbFd, "breadcrumb=", 11);
			(void)!write(CrumbFd, pCrumb, strlen(pCrumb));
			(void)!write(CrumbFd, "\n", 1);
		},
			(void *)&Fd);
		(void)!write(Fd, "stack:\n", 7);
		void *apFrames[MAX_STACK_FRAMES];
		const int NumFrames = backtrace(apFrames, MAX_STACK_FRAMES);
		backtrace_symbols_fd(apFrames, NumFrames, Fd);
		close(Fd);
	}

	void BetaSignalHandler(int Signal, siginfo_t *pInfo, void *pContext)
	{
		(void)pContext;
		static volatile sig_atomic_t s_Entered = 0;
		if(!s_Entered)
		{
			s_Entered = 1;
			WriteSignalReport(Signal, pInfo ? pInfo->si_addr : nullptr);
		}
		signal(Signal, SIG_DFL);
		raise(Signal);
	}

	void InstallCrashHandler()
	{
		struct sigaction Action = {};
		Action.sa_sigaction = BetaSignalHandler;
		Action.sa_flags = SA_SIGINFO | SA_RESETHAND;
		sigemptyset(&Action.sa_mask);
		for(int Signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP})
			sigaction(Signal, &Action, nullptr);
	}

#if defined(CONF_PLATFORM_MACOS)
	mach_port_t gs_MainMachThread = MACH_PORT_NULL;
	uintptr_t gs_MainStackLow = 0;
	uintptr_t gs_MainStackHigh = 0;
#endif

	void RememberMainThread()
	{
#if defined(CONF_PLATFORM_MACOS)
		gs_MainMachThread = mach_thread_self();
		gs_MainStackHigh = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
		gs_MainStackLow = gs_MainStackHigh - pthread_get_stacksize_np(pthread_self());
#endif
	}

	void FormatAddress(uintptr_t Address, char *pBuf, size_t BufSize)
	{
		Dl_info Info;
		if(dladdr((void *)Address, &Info) && Info.dli_fname)
		{
			const char *pName = strrchr(Info.dli_fname, '/');
			pName = pName ? pName + 1 : Info.dli_fname;
			if(Info.dli_sname)
				snprintf(pBuf, BufSize, "%s+0x%llx (%s)", pName, (unsigned long long)(Address - (uintptr_t)Info.dli_fbase), Info.dli_sname);
			else
				snprintf(pBuf, BufSize, "%s+0x%llx", pName, (unsigned long long)(Address - (uintptr_t)Info.dli_fbase));
		}
		else
			snprintf(pBuf, BufSize, "0x%llx", (unsigned long long)Address);
	}

	bool SampleMainThread(std::vector<std::string> &vFrames)
	{
#if defined(CONF_PLATFORM_MACOS) && (defined(__arm64__) || defined(__x86_64__))
		if(gs_MainMachThread == MACH_PORT_NULL || thread_suspend(gs_MainMachThread) != KERN_SUCCESS)
			return false;
		uintptr_t aFrames[MAX_STACK_FRAMES];
		int NumFrames = 0;
		uintptr_t FramePointer = 0;
#if defined(__arm64__)
		arm_thread_state64_t State;
		mach_msg_type_number_t Count = ARM_THREAD_STATE64_COUNT;
		if(thread_get_state(gs_MainMachThread, ARM_THREAD_STATE64, (thread_state_t)&State, &Count) == KERN_SUCCESS)
		{
			aFrames[NumFrames++] = (uintptr_t)arm_thread_state64_get_pc(State);
			aFrames[NumFrames++] = (uintptr_t)arm_thread_state64_get_lr(State);
			FramePointer = (uintptr_t)arm_thread_state64_get_fp(State);
		}
#else
		x86_thread_state64_t State;
		mach_msg_type_number_t Count = x86_THREAD_STATE64_COUNT;
		if(thread_get_state(gs_MainMachThread, x86_THREAD_STATE64, (thread_state_t)&State, &Count) == KERN_SUCCESS)
		{
			aFrames[NumFrames++] = (uintptr_t)State.__rip;
			FramePointer = (uintptr_t)State.__rbp;
		}
#endif
		while(NumFrames < MAX_STACK_FRAMES && FramePointer >= gs_MainStackLow && FramePointer + 16 <= gs_MainStackHigh && FramePointer % sizeof(uintptr_t) == 0)
		{
			const uintptr_t *pFrame = (const uintptr_t *)FramePointer;
			const uintptr_t Next = pFrame[0];
			const uintptr_t Return = pFrame[1] & 0x0000FFFFFFFFFFFFull;
			if(Return == 0)
				break;
			aFrames[NumFrames++] = Return;
			if(Next <= FramePointer)
				break;
			FramePointer = Next;
		}
		thread_resume(gs_MainMachThread);

		char aAddress[400];
		for(int i = 0; i < NumFrames; i++)
		{
			FormatAddress(aFrames[i], aAddress, sizeof(aAddress));
			vFrames.emplace_back(aAddress);
		}
		return NumFrames > 0;
#else
		(void)vFrames;
		return false;
#endif
	}
#endif

	void TerminateHandler()
	{
		static const char s_aMessage[] = "message=std::terminate (uncaught exception or pure virtual call)\n";
		WriteWholeFile(gs_aAssertPath, s_aMessage, sizeof(s_aMessage) - 1);
		std::abort();
	}

	void ParseReport(const char *pText, CBetaReporter::SEvent &Event)
	{
		bool InStack = false;
		const char *pLine = pText;
		while(pLine && *pLine)
		{
			const char *pEnd = strchr(pLine, '\n');
			std::string Line = pEnd ? std::string(pLine, pEnd - pLine) : std::string(pLine);
			pLine = pEnd ? pEnd + 1 : nullptr;
			while(!Line.empty() && (Line.back() == '\r' || Line.back() == ' '))
				Line.pop_back();
			if(Line.empty())
				continue;
			if(str_startswith(Line.c_str(), "breadcrumb="))
			{
				if(Event.m_vBreadcrumbs.size() < 64)
					Event.m_vBreadcrumbs.push_back(Line.substr(11));
				continue;
			}
			if(Line == "stack:")
			{
				InStack = true;
				continue;
			}
			if(InStack || str_startswith(Line.c_str(), "frame="))
			{
				if(Event.m_vStack.size() < 48)
					Event.m_vStack.push_back(str_startswith(Line.c_str(), "frame=") ? Line.substr(6) : Line);
				continue;
			}
			const size_t Equals = Line.find('=');
			if(Equals != std::string::npos && Event.m_vStrings.size() < 16)
				Event.m_vStrings.emplace_back(Line.substr(0, Equals), Line.substr(Equals + 1, 1500));
		}
	}
}

CBetaReporter::CBetaReporter() = default;

CBetaReporter::~CBetaReporter()
{
	StopWatchdog();
}

bool CBetaReporter::Enabled() const
{
	return g_Config.m_ScBetaReports != 0 && !m_ReportUrl.empty();
}

void CBetaReporter::OnConsoleInit()
{
	Console()->Register("beta_test", "s[hitch|hang|crash|assert|send]", CFGFLAG_CLIENT, ConBetaTest, this, "Tests the beta reporter");
}

void CBetaReporter::ConBetaTest(IConsole::IResult *pResult, void *pUserData)
{
	CBetaReporter *pSelf = static_cast<CBetaReporter *>(pUserData);
	const char *pWhat = pResult->GetString(0);
	if(str_comp(pWhat, "hitch") == 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	else if(str_comp(pWhat, "hang") == 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(2500));
	else if(str_comp(pWhat, "crash") == 0)
	{
		volatile int *pNull = nullptr;
		*pNull = 1;
	}
	else if(str_comp(pWhat, "assert") == 0)
		dbg_assert(false, "beta_test assert");
	else if(str_comp(pWhat, "send") == 0)
		pSelf->Upload(true);
}

void CBetaReporter::OnInit()
{
	char aBase[256];
	CecenSocial::ServiceBase(aBase, sizeof(aBase));
	if(aBase[0] != '\0')
		m_ReportUrl = std::string(aBase) + "/report";

	Storage()->CreateFolder("667Client", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(BETA_DIR, IStorage::TYPE_SAVE);
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, CRASH_FILE, gs_aCrashPath, sizeof(gs_aCrashPath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, ASSERT_FILE, gs_aAssertPath, sizeof(gs_aAssertPath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, HANG_FILE, gs_aHangPath, sizeof(gs_aHangPath));

	if(char *pId = Storage()->ReadFileStr(ID_FILE, IStorage::TYPE_SAVE))
	{
		m_InstallId = pId;
		free(pId);
		while(!m_InstallId.empty() && (m_InstallId.back() == '\n' || m_InstallId.back() == '\r' || m_InstallId.back() == ' '))
			m_InstallId.pop_back();
	}
	if(m_InstallId.size() < 8 || m_InstallId.size() > 64)
	{
		char aUuid[UUID_MAXSTRSIZE];
		FormatUuid(RandomUuid(), aUuid, sizeof(aUuid));
		m_InstallId = aUuid;
		char aPath[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, ID_FILE, aPath, sizeof(aPath));
		WriteWholeFile(aPath, m_InstallId.c_str(), m_InstallId.size());
	}

	static const char *const s_apFeatureComponents[] = {"CClientIndicator", "CMusicPlayer", "CMusicDock", "CScoreboard", "CNamePlates", "CMenus", "CFgfWeather", "CTeamScoreboard", "CTranslate", "CHud", "CParticles", "CBetaReporter"};
	const auto &vpComponents = GameClient()->AllComponents();
	GameClient()->m_vTimedComponents.assign(vpComponents.size(), 0);
	for(size_t i = 0; i < vpComponents.size(); i++)
	{
		const std::string Name = ComponentName(vpComponents[i]);
		for(const char *pFeature : s_apFeatureComponents)
		{
			if(Name == pFeature)
			{
				GameClient()->m_vTimedComponents[i] = 1;
				SFeature Feature;
				Feature.m_Name = Name;
				Feature.m_Index = i;
				m_vFeatures.push_back(Feature);
			}
		}
	}

	for(int Part = 0; Part < BetaProbe::NUM_PARTS; Part++)
	{
		SFeature Feature;
		Feature.m_Name = BetaProbe::PartName(Part);
		Feature.m_Part = Part;
		m_vFeatures.push_back(Feature);
	}

	m_SessionStart = time_timestamp();
	ReadPreviousSession();
	WriteSessionMarker();

#if defined(CONF_FAMILY_WINDOWS)
	InstallCrashHandler();
#else
	RememberMainThread();
	InstallCrashHandler();
#endif
	BetaProbe::SetAssertHook(AssertHook);
	std::set_terminate(TerminateHandler);
	StartWatchdog();
}

void CBetaReporter::WriteSessionMarker()
{
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start=%lld\nversion=%s\ncommit=%s\n", (long long)m_SessionStart, SIXCLIENT_VERSION, GIT_SHORTREV_HASH ? GIT_SHORTREV_HASH : "");
	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, SESSION_FILE, aPath, sizeof(aPath));
	WriteWholeFile(aPath, aBuf, str_length(aBuf));
}

void CBetaReporter::ReadPreviousSession()
{
	auto &&ReadAndRemove = [&](const char *pFile) -> std::string {
		std::string Text;
		if(char *pText = Storage()->ReadFileStr(pFile, IStorage::TYPE_SAVE))
		{
			Text = pText;
			free(pText);
			Storage()->RemoveFile(pFile, IStorage::TYPE_SAVE);
		}
		if(Text.size() > 32 * 1024)
			Text.resize(32 * 1024);
		return Text;
	};
	const std::string Session = ReadAndRemove(SESSION_FILE);
	const std::string Crash = ReadAndRemove(CRASH_FILE);
	const std::string Assert = ReadAndRemove(ASSERT_FILE);
	const std::string Hang = ReadAndRemove(HANG_FILE);
	if(Session.empty() && Crash.empty() && Assert.empty())
	{
		SendSavedPending();
		return;
	}

	SEvent Event;
	Event.m_Time = time_timestamp();
	if(!Assert.empty())
	{
		Event.m_Type = "assert";
		ParseReport(Assert.c_str(), Event);
		if(!Crash.empty())
		{
			SEvent CrashEvent;
			ParseReport(Crash.c_str(), CrashEvent);
			Event.m_vStack = std::move(CrashEvent.m_vStack);
		}
	}
	else if(!Crash.empty())
	{
		Event.m_Type = "crash";
		ParseReport(Crash.c_str(), Event);
	}
	else
	{
		Event.m_Type = "unclean_exit";
		if(!Hang.empty())
			ParseReport(Hang.c_str(), Event);
	}
	const std::string SpotifyLog = SpotifyHelperLogTail();
	if(!SpotifyLog.empty())
		Event.m_vStrings.emplace_back("spotify_log", SpotifyLog);
	SEvent SessionInfo;
	ParseReport(Session.c_str(), SessionInfo);
	for(auto &[Key, Value] : SessionInfo.m_vStrings)
		Event.m_vStrings.emplace_back("session_" + Key, Value);

	if(Event.m_Type != "unclean_exit")
	{
		struct SNewest
		{
			std::string m_Name;
			time_t m_Time = 0;
		} Newest;
		Storage()->ListDirectoryInfo(IStorage::TYPE_SAVE, "dumps", [](const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser) {
			SNewest *pNewest = static_cast<SNewest *>(pUser);
			if(!IsDir && str_find(pInfo->m_pName, "assert_log") && pInfo->m_TimeModified >= pNewest->m_Time)
			{
				pNewest->m_Name = pInfo->m_pName;
				pNewest->m_Time = pInfo->m_TimeModified;
			}
			return 0;
		},
			&Newest);
		if(!Newest.m_Name.empty() && Newest.m_Time + 3600 * 24 * 7 >= time_timestamp())
		{
			const std::string Path = "dumps/" + Newest.m_Name;
			if(char *pLog = Storage()->ReadFileStr(Path.c_str(), IStorage::TYPE_SAVE))
			{
				std::string Log = pLog;
				free(pLog);
				if(Log.size() > 6000)
					Log = Log.substr(Log.size() - 6000);
				Event.m_vStrings.emplace_back("log_tail", Log);
			}
		}
	}

	std::vector<SEvent> vEvents;
	vEvents.push_back(std::move(Event));
	const std::string Payload = BuildPayload(vEvents);
	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, PENDING_FILE, aPath, sizeof(aPath));
	WriteWholeFile(aPath, Payload.c_str(), Payload.size());
	log_info("beta", "the last session ended without a clean exit (%s), report queued", vEvents.front().m_Type.c_str());
	SendSavedPending();
}

std::string CBetaReporter::SpotifyHelperLogTail()
{
	std::string Log;
	if(char *pLog = Storage()->ReadFileStr("667Client/spotify/spotify-helper.log", IStorage::TYPE_SAVE))
	{
		Log = pLog;
		free(pLog);
	}
	if(Log.size() > 2500)
		Log = Log.substr(Log.size() - 2500);
	return Log;
}

void CBetaReporter::SendSavedPending()
{
	if(!Enabled() || m_pUpload)
		return;
	char *pPayload = Storage()->ReadFileStr(PENDING_FILE, IStorage::TYPE_SAVE);
	if(!pPayload)
		return;
	std::shared_ptr<IHttpRequest> pRequest = HttpPostJson(m_ReportUrl.c_str(), pPayload);
	free(pPayload);
	pRequest->LogProgress(HTTPLOG::NONE);
	pRequest->FailOnErrorStatus(false);
	pRequest->Timeout(CTimeout{8000, 20000, 500, 5});
	m_pUpload = pRequest;
	m_UploadedFromFile = PENDING_FILE;
	Http()->Run(pRequest);
}

void CBetaReporter::OnShutdown()
{
	StopWatchdog();
	if(!m_vEvents.empty() && !Storage()->FileExists(PENDING_FILE, IStorage::TYPE_SAVE))
		SavePending();
	Storage()->RemoveFile(SESSION_FILE, IStorage::TYPE_SAVE);
}

void CBetaReporter::SavePending()
{
	const std::string Payload = BuildPayload(m_vEvents);
	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, PENDING_FILE, aPath, sizeof(aPath));
	WriteWholeFile(aPath, Payload.c_str(), Payload.size());
	m_vEvents.clear();
}

void CBetaReporter::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	m_Armed = false;
	m_ArmedSince = time_get_impl();
	m_WatchArmed = false;
	(void)NewState;
}

void CBetaReporter::OnRelease()
{
	m_Armed = false;
	m_ArmedSince = time_get_impl();
	m_WatchArmed = false;
	m_LastRenderTime = 0;
}

void CBetaReporter::AddCommon(SEvent &Event)
{
	Event.m_Time = time_timestamp();
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		char aAddress[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddress, sizeof(aAddress), true);
		Event.m_vStrings.emplace_back("server", aAddress);
		Event.m_vStrings.emplace_back("map", GameClient()->Map()->BaseName());
		if(GameClient()->m_Snap.m_pLocalInfo)
			Event.m_vNumbers.emplace_back("ping", GameClient()->m_Snap.m_pLocalInfo->m_Latency);
	}
	Event.m_vNumbers.emplace_back("fps", m_FrameTicks > 0 ? (int64_t)(m_Frames * time_freq() / m_FrameTicks) : 0);
	Event.m_vNumbers.emplace_back("inputs", g_Config.m_ScInputs);
	Event.m_vNumbers.emplace_back("margin", g_Config.m_ClPredictionMargin);
	Event.m_vNumbers.emplace_back("menu", GameClient()->m_Menus.IsActive() ? 1 : 0);
}

void CBetaReporter::AddEvent(SEvent &&Event)
{
	if((int)m_vEvents.size() < MAX_EVENTS)
	{
		m_vEvents.push_back(std::move(Event));
		return;
	}
	auto Ms = [](const SEvent &E) {
		for(const auto &[Key, Value] : E.m_vNumbers)
			if(Key == "ms")
				return Value;
		return (int64_t)0;
	};
	auto Smallest = std::min_element(m_vEvents.begin(), m_vEvents.end(), [&](const SEvent &A, const SEvent &B) {
		const bool AHitch = A.m_Type == "hitch", BHitch = B.m_Type == "hitch";
		if(AHitch != BHitch)
			return AHitch;
		return Ms(A) < Ms(B);
	});
	if(Smallest->m_Type == "hitch" && (Event.m_Type != "hitch" || Ms(Event) > Ms(*Smallest)))
		*Smallest = std::move(Event);
}

void CBetaReporter::RecordHitch(int ActiveMs, int64_t Now)
{
	(void)Now;
	SEvent Event;
	Event.m_Type = ActiveMs >= HANG_MS ? "hang" : "hitch";
	Event.m_vNumbers.emplace_back("ms", ActiveMs);
	const int64_t Measured = BetaProbe::g_aPhaseTicks[BetaProbe::PHASE_UPDATE] + BetaProbe::g_aPhaseTicks[BetaProbe::PHASE_RENDER] + BetaProbe::g_aPhaseTicks[BetaProbe::PHASE_SWAP];
	const int OtherMs = ActiveMs - (int)(Measured * 1000 / time_freq());
	if(OtherMs >= 1)
		Event.m_vNumbers.emplace_back("other", OtherMs);
	for(int Phase = 0; Phase < BetaProbe::NUM_PHASES; Phase++)
	{
		if(Phase == BetaProbe::PHASE_SLEEP)
			continue;
		const int PhaseMs = (int)(BetaProbe::g_aPhaseTicks[Phase] * 1000 / time_freq());
		if(PhaseMs >= 1)
			Event.m_vNumbers.emplace_back(BetaProbe::PhaseName(Phase), PhaseMs);
	}

	const auto &vpComponents = GameClient()->AllComponents();
	const auto &vTimes = GameClient()->m_vComponentTimes;
	std::vector<std::pair<std::string, int>> vTop;
	for(size_t i = 0; i < vTimes.size() && i < vpComponents.size(); i++)
	{
		const std::string Name = ComponentName(vpComponents[i]);
		const int UpdateMs = (int)(vTimes[i].m_Update * 1000 / time_freq());
		const int RenderMs = (int)(vTimes[i].m_Render * 1000 / time_freq());
		const int SnapshotMs = (int)(vTimes[i].m_Snapshot * 1000 / time_freq());
		if(UpdateMs >= 2)
			vTop.emplace_back(Name + ".update", UpdateMs);
		if(RenderMs >= 2)
			vTop.emplace_back(Name + ".render", RenderMs);
		if(SnapshotMs >= 2)
			vTop.emplace_back(Name + ".snapshot", SnapshotMs);
	}
	for(int Part = 0; Part < BetaProbe::NUM_PARTS; Part++)
	{
		const int PartMs = (int)(BetaProbe::g_aPartTicks[Part] * 1000 / time_freq());
		if(PartMs >= 2)
			vTop.emplace_back(std::string("part.") + BetaProbe::PartName(Part), PartMs);
	}
	std::sort(vTop.begin(), vTop.end(), [](const auto &A, const auto &B) { return A.second > B.second; });
	if(vTop.size() > 6)
		vTop.resize(6);
	Event.m_vTop = std::move(vTop);

	{
		const std::lock_guard<std::mutex> Lock(m_WatchdogMutex);
		if(!m_vHangSamples.empty())
		{
			Event.m_vStack = m_vHangSamples.back().m_vFrames;
			Event.m_vNumbers.emplace_back("samples", (int64_t)m_vHangSamples.size());
		}
	}
	AddCommon(Event);

	m_NumHitches++;
	if(ActiveMs >= HANG_MS)
		m_NumHangs++;
	m_WorstHitchMs = std::max(m_WorstHitchMs, ActiveMs);
	log_info("beta", "%s of %d ms recorded", Event.m_Type.c_str(), ActiveMs);
	AddEvent(std::move(Event));
}

void CBetaReporter::CollectWindow(int64_t Now)
{
	const int64_t Freq = time_freq();
	const int64_t Delta = m_LastRenderTime ? Now - m_LastRenderTime : 0;
	const int64_t ActiveTicks = std::max<int64_t>(0, Delta - BetaProbe::g_aPhaseTicks[BetaProbe::PHASE_SLEEP]);
	const int ActiveMs = (int)(ActiveTicks * 1000 / Freq);

	const bool WindowActive = static_cast<IEngineGraphics *>(Graphics())->WindowActive() != 0;
	const bool ShouldArm = Enabled() && Client()->State() == IClient::STATE_ONLINE && WindowActive;
	if(!ShouldArm)
	{
		m_Armed = false;
		m_ArmedSince = Now;
	}
	else if(!m_Armed && Now - m_ArmedSince > Freq * ARM_DELAY_SECONDS)
		m_Armed = true;

	if(m_Armed && Delta > 0)
	{
		const int AverageMs = m_Frames > 0 ? (int)(m_FrameTicks * 1000 / Freq / m_Frames) : 0;
		const int ThresholdMs = std::max(g_Config.m_ScBetaHitchMs, 4 * AverageMs);
		m_Frames++;
		m_FrameTicks += ActiveTicks;
		if(ActiveMs >= ThresholdMs)
			RecordHitch(ActiveMs, Now);
	}

	int Type, Value;
	int64_t Time;
	while(BetaProbe::PopEvent(Type, Value, Time))
	{
		if(!m_Armed)
			continue;
		if(Type == BetaProbe::EVENT_SNAPSHOT_GAP)
		{
			if(ActiveMs + 60 >= Value)
				continue;
			m_NumSnapshotGaps++;
			m_WorstSnapshotGapMs = std::max(m_WorstSnapshotGapMs, Value);
			if(Value >= 400)
			{
				SEvent Event;
				Event.m_Type = "snapshot_gap";
				Event.m_vNumbers.emplace_back("ms", Value);
				AddCommon(Event);
				AddEvent(std::move(Event));
			}
		}
		else if(Type == BetaProbe::EVENT_PREDICTION_RESET)
		{
			m_NumPredictionResets++;
			SEvent Event;
			Event.m_Type = "prediction_reset";
			AddCommon(Event);
			AddEvent(std::move(Event));
		}
	}
	CGameClient &Game = *GameClient();
	if(m_Armed)
	{
		const int64_t JitterTicks = Freq * 4 / 1000;
		for(SFeature &Feature : m_vFeatures)
		{
			int64_t Ticks;
			if(Feature.m_Part >= 0)
				Ticks = BetaProbe::g_aPartTicks[Feature.m_Part];
			else if(Feature.m_Index < Game.m_vComponentTimes.size())
			{
				const CGameClient::SComponentTime &ComponentTime = Game.m_vComponentTimes[Feature.m_Index];
				Ticks = ComponentTime.m_Update + ComponentTime.m_Render + ComponentTime.m_Snapshot;
			}
			else
				continue;
			Feature.m_TotalTicks += Ticks;
			Feature.m_MaxTicks = std::max(Feature.m_MaxTicks, Ticks);
			if(Ticks >= JitterTicks)
				Feature.m_JitterFrames++;
		}

		m_NumCorrections += Game.m_BetaCorrections;
		m_MaxCorrection = std::max(m_MaxCorrection, (int)Game.m_BetaMaxCorrection);
		m_NumFastSnapshotJumps += Game.m_BetaFastInputSnapshotJumps;
		m_NumFastInputJumps += Game.m_BetaFastInputInputJumps;
		m_MaxFastJump = std::max(m_MaxFastJump, (int)Game.m_BetaMaxFastInputJump);
		m_NumOthersCorrections += Game.m_BetaOthersCorrections;
		m_MaxOthersCorrection = std::max(m_MaxOthersCorrection, (int)Game.m_BetaMaxOthersCorrection);

		if(m_BurstStart == 0)
			m_BurstStart = Now;
		m_BurstCorrections += Game.m_BetaCorrections;
		m_BurstFastJumps += Game.m_BetaFastInputSnapshotJumps;
		m_BurstOthers += Game.m_BetaOthersCorrections;
		m_BurstMaxCorrection = std::max({m_BurstMaxCorrection, (int)Game.m_BetaMaxCorrection, (int)Game.m_BetaMaxFastInputJump});
		if(Now - m_BurstStart >= Freq * 3)
		{
			if(m_BurstCorrections + m_BurstFastJumps >= 4 || m_BurstOthers >= 12)
			{
				SEvent Event;
				Event.m_Type = "rubberband";
				Event.m_vNumbers.emplace_back("corrections", m_BurstCorrections);
				Event.m_vNumbers.emplace_back("fast_input_jumps", m_BurstFastJumps);
				Event.m_vNumbers.emplace_back("others_corrections", m_BurstOthers);
				Event.m_vNumbers.emplace_back("max_distance", m_BurstMaxCorrection);
				AddCommon(Event);
				AddEvent(std::move(Event));
			}
			m_BurstStart = Now;
			m_BurstCorrections = m_BurstFastJumps = m_BurstOthers = m_BurstMaxCorrection = 0;
		}
	}
	else
		m_BurstStart = 0;
	Game.m_BetaCorrections = 0;
	Game.m_BetaMaxCorrection = 0.0f;
	Game.m_BetaFastInputSnapshotJumps = 0;
	Game.m_BetaFastInputInputJumps = 0;
	Game.m_BetaMaxFastInputJump = 0.0f;
	Game.m_BetaOthersCorrections = 0;
	Game.m_BetaMaxOthersCorrection = 0.0f;

	std::fill(std::begin(BetaProbe::g_aPhaseTicks), std::end(BetaProbe::g_aPhaseTicks), 0);
	std::fill(std::begin(BetaProbe::g_aPartTicks), std::end(BetaProbe::g_aPartTicks), 0);
	for(auto &ComponentTime : GameClient()->m_vComponentTimes)
		ComponentTime = CGameClient::SComponentTime();
	{
		const std::lock_guard<std::mutex> Lock(m_WatchdogMutex);
		m_vHangSamples.clear();
	}
	if(m_HangFileWritten.exchange(false))
		Storage()->RemoveFile(HANG_FILE, IStorage::TYPE_SAVE);
	m_WatchArmed = m_Armed;
}

void CBetaReporter::OnRender()
{
	const int64_t Now = time_get_impl();
	CollectWindow(Now);
	m_LastRenderTime = Now;
}

void CBetaReporter::OnUpdate()
{
	if(m_pUpload && m_pUpload->Done())
	{
		const int Status = m_pUpload->State() == EHttpState::DONE ? m_pUpload->StatusCode() : 0;
		const bool Ok = Status >= 200 && Status < 300;
		if(Ok && !m_UploadedFromFile.empty())
			Storage()->RemoveFile(m_UploadedFromFile.c_str(), IStorage::TYPE_SAVE);
		if(!Ok)
			log_warn("beta", "sending the report failed (state %d, HTTP %d)", (int)m_pUpload->State(), Status);
		m_pUpload = nullptr;
		m_UploadedFromFile.clear();
	}
	if(!Enabled())
	{
		m_vEvents.clear();
		return;
	}
	Upload(false);
}

void CBetaReporter::Upload(bool Force)
{
	if(m_pUpload || m_ReportUrl.empty())
		return;
	const int64_t Now = time_get_impl();
	if(!Force)
	{
		if(m_vEvents.empty())
			return;
		const bool Urgent = std::any_of(m_vEvents.begin(), m_vEvents.end(), [](const SEvent &Event) { return Event.m_Type == "hang"; });
		const int Interval = Urgent ? URGENT_UPLOAD_SECONDS : UPLOAD_INTERVAL_SECONDS;
		if(m_LastUpload != 0 && Now - m_LastUpload < time_freq() * Interval)
			return;
		if(m_LastUpload == 0 && !Urgent && Now - m_ArmedSince < time_freq() * 30)
			return;
	}
	const std::string Payload = BuildPayload(m_vEvents);
	m_vEvents.clear();
	m_NumHitches = m_NumHangs = m_WorstHitchMs = 0;
	m_NumSnapshotGaps = m_WorstSnapshotGapMs = m_NumPredictionResets = 0;
	m_NumCorrections = m_MaxCorrection = 0;
	m_NumFastSnapshotJumps = m_NumFastInputJumps = m_MaxFastJump = 0;
	m_NumOthersCorrections = m_MaxOthersCorrection = 0;
	for(SFeature &Feature : m_vFeatures)
	{
		Feature.m_TotalTicks = Feature.m_MaxTicks = 0;
		Feature.m_JitterFrames = 0;
	}
	m_Frames = m_FrameTicks = 0;
	m_LastUpload = Now;

	std::shared_ptr<IHttpRequest> pRequest = HttpPostJson(m_ReportUrl.c_str(), Payload.c_str());
	pRequest->LogProgress(HTTPLOG::NONE);
	pRequest->FailOnErrorStatus(false);
	pRequest->Timeout(CTimeout{8000, 20000, 500, 5});
	m_pUpload = pRequest;
	Http()->Run(pRequest);
}

std::string CBetaReporter::BuildPayload(const std::vector<SEvent> &vEvents)
{
	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("v");
	Json.WriteIntValue(1);
	Json.WriteAttribute("install");
	Json.WriteStrValue(m_InstallId.c_str());
	Json.WriteAttribute("name");
	Json.WriteStrValue(g_Config.m_PlayerName);
	Json.WriteAttribute("version");
	Json.WriteStrValue(SIXCLIENT_VERSION);
	Json.WriteAttribute("commit");
	Json.WriteStrValue(GIT_SHORTREV_HASH ? GIT_SHORTREV_HASH : "");
	Json.WriteAttribute("platform");
	Json.WriteStrValue(CONF_PLATFORM_STRING);
	Json.WriteAttribute("arch");
	Json.WriteStrValue(CONF_ARCH_STRING);
	char aOs[128];
	if(!os_version_str(aOs, sizeof(aOs)))
		str_copy(aOs, "?");
	Json.WriteAttribute("os");
	Json.WriteStrValue(aOs);
	Json.WriteAttribute("cpu_threads");
	Json.WriteIntValue((int)std::thread::hardware_concurrency());
	if(Graphics())
	{
		char aGpu[512];
		str_format(aGpu, sizeof(aGpu), "%s | %s | %s", Graphics()->GetVendorString(), Graphics()->GetRendererString(), Graphics()->GetVersionString());
		Json.WriteAttribute("gpu");
		Json.WriteStrValue(aGpu);
	}
	Json.WriteAttribute("backend");
	Json.WriteStrValue(g_Config.m_GfxBackend);

	Json.WriteAttribute("settings");
	Json.BeginObject();
	auto &&Setting = [&](const char *pName, int Value) {
		Json.WriteAttribute(pName);
		Json.WriteIntValue(Value);
	};
	Setting("sc_inputs", g_Config.m_ScInputs);
	Setting("cl_predict", g_Config.m_ClPredict);
	Setting("cl_antiping", g_Config.m_ClAntiPing);
	Setting("cl_prediction_margin", g_Config.m_ClPredictionMargin);
	Setting("sc_auto_margin", g_Config.m_ScAutoMargin);
	Setting("tc_remove_anti", g_Config.m_TcRemoveAnti);
	Setting("cl_refresh_rate", g_Config.m_ClRefreshRate);
	Setting("gfx_refresh_rate", g_Config.m_GfxRefreshRate);
	Setting("gfx_vsync", g_Config.m_GfxVsync);
	Setting("gfx_fullscreen", g_Config.m_GfxFullscreen);
	Setting("sc_music_player", g_Config.m_ScMusicPlayer);
	Setting("sc_spotify_connect", g_Config.m_ScSpotifyConnect);
	Setting("sc_discord_rpc", g_Config.m_ScDiscordRPC);
	Setting("sc_finish_prediction", g_Config.m_ScFinishPrediction);
	Setting("tc_warlist", g_Config.m_TcWarList);
	Json.EndObject();

	Json.WriteAttribute("summary");
	Json.BeginObject();
	Setting("frames", (int)std::min<int64_t>(m_Frames, 2000000000));
	Setting("avg_frame_us", m_Frames > 0 ? (int)(m_FrameTicks * 1000000 / time_freq() / m_Frames) : 0);
	Setting("hitches", m_NumHitches);
	Setting("hangs", m_NumHangs);
	Setting("worst_hitch_ms", m_WorstHitchMs);
	Setting("snapshot_gaps", m_NumSnapshotGaps);
	Setting("worst_snapshot_gap_ms", m_WorstSnapshotGapMs);
	Setting("prediction_resets", m_NumPredictionResets);
	Setting("corrections", m_NumCorrections);
	Setting("max_correction", m_MaxCorrection);
	Setting("session_minutes", (int)((time_timestamp() - m_SessionStart) / 60));
	Json.EndObject();

	Json.WriteAttribute("input");
	Json.BeginObject();
	Setting("mode", g_Config.m_ScInputs);
	Setting("offset_us", (int)(ScInputs::EffectiveOffsetTicks() * 1000000 / std::max(1, Client()->GameTickSpeed())));
	Setting("fast_input_snapshot_jumps", m_NumFastSnapshotJumps);
	Setting("fast_input_input_jumps", m_NumFastInputJumps);
	Setting("max_fast_input_jump", m_MaxFastJump);
	Setting("corrections", m_NumCorrections);
	Setting("max_correction", m_MaxCorrection);
	Setting("others_corrections", m_NumOthersCorrections);
	Setting("max_others_correction", m_MaxOthersCorrection);
	Setting("prediction_resets", m_NumPredictionResets);
	Setting("cl_antiping_players", g_Config.m_ClAntiPingPlayers);
	Setting("tc_remove_anti", g_Config.m_TcRemoveAnti);
	Json.EndObject();

	Json.WriteAttribute("features");
	Json.BeginObject();
	for(const SFeature &Feature : m_vFeatures)
	{
		const int64_t AvgUs = m_Frames > 0 ? Feature.m_TotalTicks * 1000000 / time_freq() / m_Frames : 0;
		if(AvgUs < 5 && Feature.m_MaxTicks * 1000 / time_freq() < 1 && Feature.m_JitterFrames == 0)
			continue;
		Json.WriteAttribute(Feature.m_Name.c_str());
		Json.BeginObject();
		Setting("avg_us", m_Frames > 0 ? (int)(Feature.m_TotalTicks * 1000000 / time_freq() / m_Frames) : 0);
		Setting("max_ms", (int)(Feature.m_MaxTicks * 1000 / time_freq()));
		Setting("jitter_frames", Feature.m_JitterFrames);
		Json.EndObject();
	}
	Json.EndObject();

	{
		const CSpotifyConnect &Connect = GameClient()->m_MusicPlayer.SpotifyConnect();
		const CSpotifyConnect::SStats Stats = Connect.Stats();
		static const char *const s_apStatus[] = {"unavailable", "starting", "logged_out", "pairing", "connecting", "ready"};
		const int Status = (int)Connect.Info().m_Status;
		Json.WriteAttribute("spotify");
		Json.BeginObject();
		Json.WriteAttribute("status");
		Json.WriteStrValue(Status >= 0 && Status < (int)std::size(s_apStatus) ? s_apStatus[Status] : "?");
		Setting("enabled", g_Config.m_ScSpotifyConnect);
		Setting("listening_along", GameClient()->m_ClientIndicator.IsListening() ? 1 : 0);
		Setting("helper_starts", Stats.m_Spawns);
		Setting("helper_start_failures", Stats.m_SpawnFailures);
		Setting("helper_exits", Stats.m_Exits);
		Setting("last_exit_code", Stats.m_LastExitCode);
		Setting("last_start_ms", Stats.m_LastSpawnMs);
		Setting("max_start_ms", Stats.m_MaxSpawnMs);
		Setting("commands", Stats.m_Commands);
		Setting("failed_commands", Stats.m_FailedCommands);
		Setting("dropped_commands", Stats.m_DroppedCommands);
		Json.EndObject();

		const int Problems = Stats.m_SpawnFailures + Stats.m_Exits + Stats.m_FailedCommands;
		if(Problems > m_ReportedSpotifyProblems)
		{
			m_ReportedSpotifyProblems = Problems;
			const std::string Log = SpotifyHelperLogTail();
			if(!Log.empty())
			{
				Json.WriteAttribute("spotify_log");
				Json.WriteStrValue(Log.c_str());
			}
		}
	}

	std::vector<std::string> vBreadcrumbs;
	BetaProbe::CopyBreadcrumbs(vBreadcrumbs, m_BreadcrumbSequence);
	if(!vBreadcrumbs.empty())
	{
		Json.WriteAttribute("breadcrumbs");
		Json.BeginArray();
		for(const std::string &Line : vBreadcrumbs)
			Json.WriteStrValue(Line.c_str());
		Json.EndArray();
	}

	Json.WriteAttribute("events");
	Json.BeginArray();
	for(const SEvent &Event : vEvents)
	{
		Json.BeginObject();
		Json.WriteAttribute("type");
		Json.WriteStrValue(Event.m_Type.c_str());
		Json.WriteAttribute("t");
		Json.WriteIntValue((int)Event.m_Time);
		for(const auto &[Key, Value] : Event.m_vNumbers)
		{
			Json.WriteAttribute(Key.c_str());
			Json.WriteIntValue((int)std::clamp<int64_t>(Value, -2000000000, 2000000000));
		}
		for(const auto &[Key, Value] : Event.m_vStrings)
		{
			Json.WriteAttribute(Key.c_str());
			Json.WriteStrValue(Value.c_str());
		}
		if(!Event.m_vTop.empty())
		{
			Json.WriteAttribute("top");
			Json.BeginArray();
			for(const auto &[Name, Ms] : Event.m_vTop)
			{
				Json.BeginArray();
				Json.WriteStrValue(Name.c_str());
				Json.WriteIntValue(Ms);
				Json.EndArray();
			}
			Json.EndArray();
		}
		if(!Event.m_vStack.empty())
		{
			Json.WriteAttribute("stack");
			Json.BeginArray();
			for(const std::string &Frame : Event.m_vStack)
				Json.WriteStrValue(Frame.c_str());
			Json.EndArray();
		}
		if(!Event.m_vBreadcrumbs.empty())
		{
			Json.WriteAttribute("breadcrumbs");
			Json.BeginArray();
			for(const std::string &Line : Event.m_vBreadcrumbs)
				Json.WriteStrValue(Line.c_str());
			Json.EndArray();
		}
		Json.EndObject();
	}
	Json.EndArray();
	Json.EndObject();
	return Json.GetOutputString();
}

void CBetaReporter::StartWatchdog()
{
	if(m_Watchdog.joinable())
		return;
	m_StopWatchdog = false;
	m_Watchdog = std::thread([this]() { WatchdogMain(); });
}

void CBetaReporter::StopWatchdog()
{
	{
		const std::lock_guard<std::mutex> Lock(m_WatchdogMutex);
		m_StopWatchdog = true;
	}
	m_WatchdogCv.notify_one();
	if(m_Watchdog.joinable())
		m_Watchdog.join();
}

void CBetaReporter::WatchdogMain()
{
	int64_t SampledHeartbeat = 0;
	int64_t LastSample = 0;
	int NumSamples = 0;
	while(true)
	{
		{
			std::unique_lock<std::mutex> Lock(m_WatchdogMutex);
			m_WatchdogCv.wait_for(Lock, std::chrono::milliseconds(WATCHDOG_INTERVAL_MS), [this]() { return m_StopWatchdog; });
			if(m_StopWatchdog)
				return;
		}
		const int64_t Heartbeat = BetaProbe::g_MainHeartbeat.load(std::memory_order_relaxed);
		const int64_t Now = time_get_impl();
		if(!m_WatchArmed || Heartbeat == 0)
			continue;
		const int StallMs = (int)((Now - Heartbeat) * 1000 / time_freq());
		if(StallMs < SAMPLE_STALL_MS)
			continue;
		if(Heartbeat != SampledHeartbeat)
		{
			SampledHeartbeat = Heartbeat;
			NumSamples = 0;
			LastSample = 0;
		}
		if(NumSamples >= 4 || (LastSample != 0 && Now - LastSample < time_freq() * 7 / 10))
			continue;
		LastSample = Now;
		NumSamples++;

		SHangSample Sample;
		Sample.m_Heartbeat = Heartbeat;
		Sample.m_StallMs = StallMs;
		if(!SampleMainThread(Sample.m_vFrames))
			Sample.m_vFrames.clear();

		if(StallMs >= HANG_MS)
		{
			std::string Text = "stall_ms=" + std::to_string(StallMs) + "\n";
			std::vector<std::string> vBreadcrumbs;
			uint64_t BreadcrumbSequence = 0;
			BetaProbe::CopyBreadcrumbs(vBreadcrumbs, BreadcrumbSequence);
			for(const std::string &Line : vBreadcrumbs)
				Text += "breadcrumb=" + Line + "\n";
			Text += "stack:\n";
			for(const std::string &Frame : Sample.m_vFrames)
				Text += Frame + "\n";
			WriteWholeFile(gs_aHangPath, Text.c_str(), Text.size());
			m_HangFileWritten = true;
		}

		const std::lock_guard<std::mutex> Lock(m_WatchdogMutex);
		if(m_vHangSamples.size() < 8)
			m_vHangSamples.push_back(std::move(Sample));
	}
}
