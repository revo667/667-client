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

static IAudioEndpointVolume *DefaultEndpointVolume()
{
	const HRESULT InitResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if(FAILED(InitResult) && InitResult != RPC_E_CHANGED_MODE)
		return nullptr;

	IMMDeviceEnumerator *pEnumerator = nullptr;
	if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator)))
		return nullptr;

	IMMDevice *pDevice = nullptr;
	IAudioEndpointVolume *pVolume = nullptr;
	if(SUCCEEDED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)))
	{
		pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&pVolume);
		pDevice->Release();
	}
	pEnumerator->Release();
	return pVolume;
}

bool SystemVolumeGet(float &Volume)
{
	IAudioEndpointVolume *pVolume = DefaultEndpointVolume();
	if(!pVolume)
		return false;
	float Value = 0.0f;
	const bool Success = SUCCEEDED(pVolume->GetMasterVolumeLevelScalar(&Value));
	pVolume->Release();
	if(Success)
		Volume = std::clamp(Value, 0.0f, 1.0f);
	return Success;
}

bool SystemVolumeSet(float Volume)
{
	IAudioEndpointVolume *pVolume = DefaultEndpointVolume();
	if(!pVolume)
		return false;
	const bool Success = SUCCEEDED(pVolume->SetMasterVolumeLevelScalar(std::clamp(Volume, 0.0f, 1.0f), nullptr));
	pVolume->Release();
	return Success;
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
