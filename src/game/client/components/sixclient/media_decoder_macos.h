/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-f0ec89f2 */

#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_MEDIA_DECODER_MACOS_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_MEDIA_DECODER_MACOS_H

#include <cstddef>
#include <cstdint>
#include <vector>

// The ffmpeg shipped for macOS is built without any decoders, so JPEG/GIF/WebP/HEIC
// (album covers, chat media) are decoded with ImageIO instead.
struct SMacDecodedImageFrame
{
	std::vector<uint8_t> m_vRgba; // straight (non-premultiplied) RGBA8, top row first
	int m_Width = 0;
	int m_Height = 0;
	int m_DurationMs = 0;
};

// Frames are fully composited. MaxDimension <= 0 keeps the source size.
bool MacDecodeImageFrames(const unsigned char *pData, size_t DataSize, int MaxDimension, int MaxFrames, size_t MaxTotalBytes, bool AllFrames, std::vector<SMacDecodedImageFrame> &vFrames);

#endif
