/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-5c2d0625 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_FOREGROUND_GUARD_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_FOREGROUND_GUARD_H

#include <base/detect.h>

#include <atomic>

class CForegroundGuard
{
#if defined(CONF_FAMILY_WINDOWS)
	void *m_pWindow = nullptr;
	bool m_Locked = false;
#endif

public:
	CForegroundGuard();
	~CForegroundGuard();
	CForegroundGuard(const CForegroundGuard &) = delete;
	CForegroundGuard &operator=(const CForegroundGuard &) = delete;

	static bool OpenLinkInBackground(const char *pLink);
	void Hold(int Milliseconds);
	void HoldUntilSpotifyWindow(int MaxMilliseconds);
	void SetAbortFlag(const std::atomic<bool> *pAbort) { m_pAbort = pAbort; }

private:
	bool Aborted() const { return m_pAbort && m_pAbort->load(); }
	const std::atomic<bool> *m_pAbort = nullptr;
};

#endif
