#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_SYSTEM_TUNING_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_SYSTEM_TUNING_H

#include <engine/console.h>

#include <game/client/component.h>

#include <atomic>

class CSystemTuning : public CComponent
{
	int Sizeof() const override { return sizeof(*this); }

	static void ConchainDDNetProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainDiscordProcessPriority(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void DiscordPriorityThread(void *pUserData);

	std::atomic<int64_t> m_DiscordPriorityDelay{0};
	std::atomic_bool m_DiscordPriorityThreadRunning{false};
	void *m_pDiscordPriorityThread = nullptr;
	bool m_HasWindowActive = false;
	bool m_LastWindowActive = true;

	bool m_LowLatencyTimersApplied = false;
	bool m_MmcssApplied = false;
	void *m_pMmcssHandle = nullptr;

	void SetDiscordProcessesNormalPriority();
	void SetLowLatencyTimers(bool Enable);
	void SetMmcss(bool Enable);
	void UpdateLatencyState(bool WindowActive);

	void OnInit() override;
	void OnRender() override;
	void OnShutdown() override;
	void OnConsoleInit() override;

public:
	void SetDDNetProcessPriority(bool Set);
	void StartDiscordPriorityThread();
	void ApplyLowLatencyPreset();

	bool LowLatencyTimersActive() const { return m_LowLatencyTimersApplied; }
	bool MmcssActive() const { return m_MmcssApplied; }
};

#endif
