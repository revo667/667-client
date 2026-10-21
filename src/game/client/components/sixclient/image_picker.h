/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-60429b52 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_IMAGE_PICKER_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_IMAGE_PICKER_H

#include <base/lock.h>

#include <memory>
#include <string>
#include <thread>

// 667client: the operating system's "open file" dialog for one image, shown from a worker
// thread so the game keeps rendering. Windows uses GetOpenFileName, macOS osascript's
// "choose file", Linux zenity.
class CImagePicker
{
public:
	~CImagePicker();

	// Does nothing while a dialog is already open.
	void Start(const char *pTitle);
	bool Busy();
	// The chosen absolute path (UTF-8) once the dialog closed; false while open or when cancelled.
	bool TakeResult(std::string &Path);

private:
	// Shared with the dialog thread, which can outlive the picker: an open dialog can't be closed
	// from here, so the thread is detached on destruction and must not touch the picker itself.
	struct SState
	{
		CLock m_Lock;
		bool m_Running GUARDED_BY(m_Lock) = false;
		bool m_HasResult GUARDED_BY(m_Lock) = false;
		std::string m_Result GUARDED_BY(m_Lock);
	};
	std::shared_ptr<SState> m_pState = std::make_shared<SState>();
	std::thread m_Thread;
};

#endif
