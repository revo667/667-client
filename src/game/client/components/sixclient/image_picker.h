/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-60429b52 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_IMAGE_PICKER_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_IMAGE_PICKER_H

#include <base/lock.h>

#include <memory>
#include <string>
#include <thread>

class CImagePicker
{
public:
	~CImagePicker();

	void Start(const char *pTitle);
	bool Busy();
	bool TakeResult(std::string &Path);

private:
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
