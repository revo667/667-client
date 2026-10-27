/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-f0ec89f2 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_MEDIA_DECODER_MACOS_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_MEDIA_DECODER_MACOS_H

#include <cstddef>
#include <cstdint>
#include <vector>

struct SMacDecodedImageFrame
{
	std::vector<uint8_t> m_vRgba;
	int m_Width = 0;
	int m_Height = 0;
	int m_DurationMs = 0;
};

bool MacDecodeImageFrames(const unsigned char *pData, size_t DataSize, int MaxDimension, int MaxFrames, size_t MaxTotalBytes, bool AllFrames, std::vector<SMacDecodedImageFrame> &vFrames);

#endif
