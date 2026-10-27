/* Copyright (c) 2026 667.wtf (revo667) - Cecen Client. All rights reserved.
 * Using this code needs written permission: https://discord.gg/667, contact@revo667.com
 * CC-3415db95 */

#include "system_volume.h"

#include <algorithm>

#if defined(CONF_PLATFORM_MACOS)
#include <AudioToolbox/AudioServices.h>
#include <CoreAudio/CoreAudio.h>

static AudioObjectID DefaultOutputDevice()
{
	AudioObjectID Device = kAudioObjectUnknown;
	UInt32 Size = sizeof(Device);
	const AudioObjectPropertyAddress Address = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0};
	if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &Address, 0, nullptr, &Size, &Device) != noErr)
		return kAudioObjectUnknown;
	return Device;
}

static const AudioObjectPropertyAddress VOLUME_ADDRESS = {kAudioHardwareServiceDeviceProperty_VirtualMainVolume, kAudioDevicePropertyScopeOutput, 0};

bool SystemVolumeGet(float &Volume)
{
	const AudioObjectID Device = DefaultOutputDevice();
	if(Device == kAudioObjectUnknown || !AudioObjectHasProperty(Device, &VOLUME_ADDRESS))
		return false;
	Float32 Value = 0.0f;
	UInt32 Size = sizeof(Value);
	if(AudioObjectGetPropertyData(Device, &VOLUME_ADDRESS, 0, nullptr, &Size, &Value) != noErr)
		return false;
	Volume = std::clamp((float)Value, 0.0f, 1.0f);
	return true;
}

bool SystemVolumeSet(float Volume)
{
	const AudioObjectID Device = DefaultOutputDevice();
	if(Device == kAudioObjectUnknown || !AudioObjectHasProperty(Device, &VOLUME_ADDRESS))
		return false;
	Float32 Value = std::clamp(Volume, 0.0f, 1.0f);
	return AudioObjectSetPropertyData(Device, &VOLUME_ADDRESS, 0, nullptr, sizeof(Value), &Value) == noErr;
}
#elif defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <endpointvolume.h>
#include <mmdeviceapi.h>

template<typename TAction>
static bool WithEndpointVolume(TAction &&Action)
{
	const HRESULT InitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if(FAILED(InitResult) && InitResult != RPC_E_CHANGED_MODE)
		return false;
	const bool Uninit = SUCCEEDED(InitResult);

	bool Success = false;
	IMMDeviceEnumerator *pEnumerator = nullptr;
	if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator)))
	{
		IMMDevice *pDevice = nullptr;
		if(SUCCEEDED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)))
		{
			IAudioEndpointVolume *pVolume = nullptr;
			if(SUCCEEDED(pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&pVolume)) && pVolume)
			{
				Success = Action(pVolume);
				pVolume->Release();
			}
			pDevice->Release();
		}
		pEnumerator->Release();
	}

	if(Uninit)
		CoUninitialize();
	return Success;
}

bool SystemVolumeGet(float &Volume)
{
	return WithEndpointVolume([&](IAudioEndpointVolume *pVolume) {
		float Value = 0.0f;
		if(FAILED(pVolume->GetMasterVolumeLevelScalar(&Value)))
			return false;
		Volume = std::clamp(Value, 0.0f, 1.0f);
		return true;
	});
}

bool SystemVolumeSet(float Volume)
{
	return WithEndpointVolume([&](IAudioEndpointVolume *pVolume) {
		return SUCCEEDED(pVolume->SetMasterVolumeLevelScalar(std::clamp(Volume, 0.0f, 1.0f), nullptr));
	});
}
#else
bool SystemVolumeGet(float &Volume)
{
	(void)Volume;
	return false;
}

bool SystemVolumeSet(float Volume)
{
	(void)Volume;
	return false;
}
#endif
