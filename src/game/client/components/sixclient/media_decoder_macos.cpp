/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-ee4b6850 */

#include "media_decoder_macos.h"

#include <base/detect.h>

#if defined(CONF_PLATFORM_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>

namespace
{
	double DictionaryDouble(CFDictionaryRef Dictionary, CFStringRef Key)
	{
		if(!Dictionary)
			return 0.0;
		const CFNumberRef Number = (CFNumberRef)CFDictionaryGetValue(Dictionary, Key);
		double Value = 0.0;
		if(!Number || CFGetTypeID(Number) != CFNumberGetTypeID() || !CFNumberGetValue(Number, kCFNumberDoubleType, &Value))
			return 0.0;
		return Value;
	}

	double FrameDelaySeconds(CFDictionaryRef Properties, CFStringRef ContainerKey, CFStringRef UnclampedKey, CFStringRef ClampedKey)
	{
		const CFDictionaryRef Container = Properties ? (CFDictionaryRef)CFDictionaryGetValue(Properties, ContainerKey) : nullptr;
		if(!Container || CFGetTypeID(Container) != CFDictionaryGetTypeID())
			return 0.0;
		const double Unclamped = DictionaryDouble(Container, UnclampedKey);
		return Unclamped > 0.0 ? Unclamped : DictionaryDouble(Container, ClampedKey);
	}

	int FrameDurationMs(CGImageSourceRef Source, size_t Index)
	{
		const CFDictionaryRef Properties = CGImageSourceCopyPropertiesAtIndex(Source, Index, nullptr);
		if(!Properties)
			return 0;
		double Seconds = FrameDelaySeconds(Properties, kCGImagePropertyGIFDictionary, kCGImagePropertyGIFUnclampedDelayTime, kCGImagePropertyGIFDelayTime);
		if(Seconds <= 0.0)
			Seconds = FrameDelaySeconds(Properties, kCGImagePropertyPNGDictionary, kCGImagePropertyAPNGUnclampedDelayTime, kCGImagePropertyAPNGDelayTime);
		if(Seconds <= 0.0)
		{
			if(__builtin_available(macOS 11.0, *))
				Seconds = FrameDelaySeconds(Properties, kCGImagePropertyWebPDictionary, kCGImagePropertyWebPUnclampedDelayTime, kCGImagePropertyWebPDelayTime);
		}
		CFRelease(Properties);
		return Seconds > 0.0 ? (int)std::lround(Seconds * 1000.0) : 0;
	}

	bool RenderFrame(CGImageRef Image, int MaxDimension, SMacDecodedImageFrame &Out)
	{
		const size_t SourceWidth = CGImageGetWidth(Image);
		const size_t SourceHeight = CGImageGetHeight(Image);
		if(SourceWidth == 0 || SourceHeight == 0 || SourceWidth > 4096 || SourceHeight > 4096)
			return false;

		double Scale = 1.0;
		if(MaxDimension > 0 && (SourceWidth > (size_t)MaxDimension || SourceHeight > (size_t)MaxDimension))
			Scale = std::min((double)MaxDimension / SourceWidth, (double)MaxDimension / SourceHeight);
		const int Width = std::max(1, (int)std::lround(SourceWidth * Scale));
		const int Height = std::max(1, (int)std::lround(SourceHeight * Scale));

		Out.m_Width = Width;
		Out.m_Height = Height;
		Out.m_vRgba.assign((size_t)Width * Height * 4, 0);

		const CGColorSpaceRef ColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		if(!ColorSpace)
			return false;
		const CGContextRef Context = CGBitmapContextCreate(Out.m_vRgba.data(), Width, Height, 8, (size_t)Width * 4, ColorSpace, (CGBitmapInfo)kCGImageAlphaPremultipliedLast | (CGBitmapInfo)kCGBitmapByteOrder32Big);
		CGColorSpaceRelease(ColorSpace);
		if(!Context)
			return false;
		CGContextSetInterpolationQuality(Context, kCGInterpolationHigh);
		CGContextSetBlendMode(Context, kCGBlendModeCopy);
		CGContextDrawImage(Context, CGRectMake(0, 0, Width, Height), Image);
		CGContextRelease(Context);

		for(size_t i = 0; i < Out.m_vRgba.size(); i += 4)
		{
			const unsigned Alpha = Out.m_vRgba[i + 3];
			if(Alpha == 0 || Alpha == 255)
				continue;
			for(int c = 0; c < 3; c++)
				Out.m_vRgba[i + c] = (uint8_t)std::min(255u, (Out.m_vRgba[i + c] * 255u + Alpha / 2) / Alpha);
		}
		return true;
	}
}

bool MacDecodeImageFrames(const unsigned char *pData, size_t DataSize, int MaxDimension, int MaxFrames, size_t MaxTotalBytes, bool AllFrames, std::vector<SMacDecodedImageFrame> &vFrames)
{
	vFrames.clear();
	if(!pData || DataSize == 0 || DataSize > (size_t)INT32_MAX)
		return false;

	const CFDataRef Data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, pData, (CFIndex)DataSize, kCFAllocatorNull);
	if(!Data)
		return false;
	const CGImageSourceRef Source = CGImageSourceCreateWithData(Data, nullptr);
	CFRelease(Data);
	if(!Source)
		return false;

	const size_t Count = CGImageSourceGetCount(Source);
	const size_t Wanted = AllFrames ? std::min(Count, (size_t)std::max(1, MaxFrames)) : std::min<size_t>(Count, 1);
	size_t TotalBytes = 0;
	for(size_t i = 0; i < Wanted; i++)
	{
		const CGImageRef Image = CGImageSourceCreateImageAtIndex(Source, i, nullptr);
		if(!Image)
			break;
		SMacDecodedImageFrame Frame;
		const bool Rendered = RenderFrame(Image, MaxDimension, Frame);
		CGImageRelease(Image);
		if(!Rendered)
			break;
		TotalBytes += Frame.m_vRgba.size();
		if(MaxTotalBytes > 0 && TotalBytes > MaxTotalBytes && !vFrames.empty())
			break;
		Frame.m_DurationMs = FrameDurationMs(Source, i);
		vFrames.push_back(std::move(Frame));
	}
	CFRelease(Source);
	return !vFrames.empty();
}
#else
bool MacDecodeImageFrames(const unsigned char *, size_t, int, int, size_t, bool, std::vector<SMacDecodedImageFrame> &vFrames)
{
	vFrames.clear();
	return false;
}
#endif
