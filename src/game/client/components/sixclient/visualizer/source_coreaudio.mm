#include "analyzer.h"
#include "smoother.h"
#include "source.h"

#include <base/log.h>
#include <base/time.h>

#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <strings.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace SixClientVisualizer
{

namespace
{
constexpr int MAX_PENDING_SAMPLES = 48000;
constexpr int64_t SILENCE_FALLBACK_MS = 2500;
constexpr int64_t IDLE_TEARDOWN_MS = 8000;
constexpr int64_t RETRY_DELAY_MS = 5000;
constexpr AudioObjectPropertyElement ELEMENT_MAIN = 0;

AudioObjectID TranslatePidToProcessObject(pid_t Pid)
{
	const AudioObjectPropertyAddress Address = {kAudioHardwarePropertyTranslatePIDToProcessObject, kAudioObjectPropertyScopeGlobal, ELEMENT_MAIN};
	AudioObjectID Object = kAudioObjectUnknown;
	UInt32 Size = sizeof(Object);
	if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &Address, sizeof(Pid), &Pid, &Size, &Object) != noErr)
		return kAudioObjectUnknown;
	return Object;
}

std::vector<AudioObjectID> FindPlayerProcessObjects(const std::string &ServiceId)
{
	std::vector<AudioObjectID> vResult;
	if(ServiceId.empty())
		return vResult;

	const AudioObjectPropertyAddress ListAddress = {kAudioHardwarePropertyProcessObjectList, kAudioObjectPropertyScopeGlobal, ELEMENT_MAIN};
	UInt32 Size = 0;
	if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &ListAddress, 0, nullptr, &Size) != noErr || Size == 0)
		return vResult;
	std::vector<AudioObjectID> vProcesses(Size / sizeof(AudioObjectID));
	if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &ListAddress, 0, nullptr, &Size, vProcesses.data()) != noErr)
		return vResult;
	vProcesses.resize(Size / sizeof(AudioObjectID));

	const pid_t OwnPid = getpid();
	for(AudioObjectID Process : vProcesses)
	{
		const AudioObjectPropertyAddress PidAddress = {kAudioProcessPropertyPID, kAudioObjectPropertyScopeGlobal, ELEMENT_MAIN};
		pid_t Pid = -1;
		UInt32 PidSize = sizeof(Pid);
		if(AudioObjectGetPropertyData(Process, &PidAddress, 0, nullptr, &PidSize, &Pid) == noErr && Pid == OwnPid)
			continue;

		const AudioObjectPropertyAddress BundleAddress = {kAudioProcessPropertyBundleID, kAudioObjectPropertyScopeGlobal, ELEMENT_MAIN};
		CFStringRef BundleId = nullptr;
		UInt32 BundleSize = sizeof(BundleId);
		if(AudioObjectGetPropertyData(Process, &BundleAddress, 0, nullptr, &BundleSize, &BundleId) != noErr || !BundleId)
			continue;
		char aBundle[256] = "";
		const bool Converted = CFStringGetCString(BundleId, aBundle, sizeof(aBundle), kCFStringEncodingUTF8);
		CFRelease(BundleId);
		if(Converted && strncasecmp(aBundle, ServiceId.c_str(), ServiceId.size()) == 0)
			vResult.push_back(Process);
	}
	return vResult;
}

std::string DefaultOutputDeviceUid()
{
	const AudioObjectPropertyAddress DeviceAddress = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, ELEMENT_MAIN};
	AudioObjectID Device = kAudioObjectUnknown;
	UInt32 Size = sizeof(Device);
	if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &DeviceAddress, 0, nullptr, &Size, &Device) != noErr || Device == kAudioObjectUnknown)
		return {};
	const AudioObjectPropertyAddress UidAddress = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, ELEMENT_MAIN};
	CFStringRef Uid = nullptr;
	Size = sizeof(Uid);
	if(AudioObjectGetPropertyData(Device, &UidAddress, 0, nullptr, &Size, &Uid) != noErr || !Uid)
		return {};
	char aUid[512] = "";
	const bool Converted = CFStringGetCString(Uid, aUid, sizeof(aUid), kCFStringEncodingUTF8);
	CFRelease(Uid);
	return Converted ? aUid : std::string();
}

class CCoreAudioVisualizerSource final : public IVisualizerSource
{
	std::mutex m_Mutex;
	bool m_Shutdown = false;
	SVisualizerPlaybackHint m_Hint;
	int64_t m_HintRevision = 0;
	SVisualizerConfig m_Config;
	int64_t m_ConfigRevision = 0;
	SVisualizerFrame m_LatestFrame;

	std::mutex m_SampleMutex;
	std::vector<float> m_vPendingSamples;

	dispatch_queue_t m_Queue = nullptr;
	AudioObjectID m_TapId = kAudioObjectUnknown;
	AudioObjectID m_AggregateId = kAudioObjectUnknown;
	AudioDeviceIOProcID m_IoProcId = nullptr;
	int m_TapSampleRate = 0;
	std::string m_TapServiceId;

	std::thread m_Worker;

	void StoreFrame(const SVisualizerFrame &Frame)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_LatestFrame = Frame;
	}

	void StoreBackendState(EVisualizerBackendStatus Status)
	{
		SVisualizerFrame Frame;
		Frame.m_BackendStatus = Status;
		Frame.m_IsPassiveFallback = Status == EVisualizerBackendStatus::FALLBACK || Status == EVisualizerBackendStatus::UNAVAILABLE;
		StoreFrame(Frame);
	}

	void PushInput(const AudioBufferList *pInput)
	{
		if(!pInput || pInput->mNumberBuffers == 0)
			return;
		const AudioBuffer &First = pInput->mBuffers[0];
		const UInt32 FirstChannels = std::max<UInt32>(1, First.mNumberChannels);
		const UInt32 Frames = First.mDataByteSize / (sizeof(float) * FirstChannels);
		if(Frames == 0 || !First.mData)
			return;

		std::vector<float> vMono(Frames, 0.0f);
		UInt32 TotalChannels = 0;
		for(UInt32 b = 0; b < pInput->mNumberBuffers; b++)
		{
			const AudioBuffer &Buffer = pInput->mBuffers[b];
			const UInt32 Channels = std::max<UInt32>(1, Buffer.mNumberChannels);
			if(!Buffer.mData || Buffer.mDataByteSize < Frames * Channels * sizeof(float))
				continue;
			const float *pSamples = static_cast<const float *>(Buffer.mData);
			for(UInt32 f = 0; f < Frames; f++)
				for(UInt32 c = 0; c < Channels; c++)
					vMono[f] += pSamples[f * Channels + c];
			TotalChannels += Channels;
		}
		if(TotalChannels == 0)
			return;
		const float Norm = 1.0f / TotalChannels;
		for(float &Sample : vMono)
			Sample = std::clamp(Sample * Norm, -1.0f, 1.0f);

		std::lock_guard<std::mutex> Lock(m_SampleMutex);
		m_vPendingSamples.insert(m_vPendingSamples.end(), vMono.begin(), vMono.end());
		if((int)m_vPendingSamples.size() > MAX_PENDING_SAMPLES)
			m_vPendingSamples.erase(m_vPendingSamples.begin(), m_vPendingSamples.end() - MAX_PENDING_SAMPLES);
	}

	void DestroyTap()
	{
		if(@available(macOS 14.2, *))
		{
			if(m_AggregateId != kAudioObjectUnknown)
			{
				if(m_IoProcId)
				{
					AudioDeviceStop(m_AggregateId, m_IoProcId);
					AudioDeviceDestroyIOProcID(m_AggregateId, m_IoProcId);
				}
				AudioHardwareDestroyAggregateDevice(m_AggregateId);
			}
			if(m_TapId != kAudioObjectUnknown)
				AudioHardwareDestroyProcessTap(m_TapId);
		}
		m_IoProcId = nullptr;
		m_AggregateId = kAudioObjectUnknown;
		m_TapId = kAudioObjectUnknown;
		m_TapSampleRate = 0;
		m_TapServiceId.clear();
		if(m_Queue)
			dispatch_sync(m_Queue, ^{});
		std::lock_guard<std::mutex> Lock(m_SampleMutex);
		m_vPendingSamples.clear();
	}

	bool CreateTap(const std::string &ServiceId) API_AVAILABLE(macos(14.2))
	{
		@autoreleasepool
		{
			const std::vector<AudioObjectID> vPlayer = FindPlayerProcessObjects(ServiceId);
			NSMutableArray<NSNumber *> *pProcesses = [NSMutableArray array];
			CATapDescription *pDescription = nil;
			if(!vPlayer.empty())
			{
				for(AudioObjectID Process : vPlayer)
					[pProcesses addObject:@(Process)];
				pDescription = [[CATapDescription alloc] initStereoMixdownOfProcesses:pProcesses];
			}
			else
			{
				const AudioObjectID Own = TranslatePidToProcessObject(getpid());
				if(Own != kAudioObjectUnknown)
					[pProcesses addObject:@(Own)];
				pDescription = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:pProcesses];
			}
			if(!pDescription)
				return false;
			[pDescription setName:@"Cecen Client visualizer"];
			[pDescription setPrivate:YES];
			[pDescription setMuteBehavior:CATapUnmuted];

			AudioObjectID TapId = kAudioObjectUnknown;
			const OSStatus TapStatus = AudioHardwareCreateProcessTap(pDescription, &TapId);
			NSString *pTapUid = [[[pDescription UUID] UUIDString] copy];
			[pDescription release];
			if(TapStatus != noErr || TapId == kAudioObjectUnknown)
			{
				log_warn("visualizer", "creating the system audio tap failed (status %d)", (int)TapStatus);
				[pTapUid release];
				return false;
			}
			m_TapId = TapId;

			NSMutableDictionary *pAggregate = [NSMutableDictionary dictionary];
			pAggregate[@kAudioAggregateDeviceNameKey] = @"Cecen Client visualizer";
			pAggregate[@kAudioAggregateDeviceUIDKey] = [[NSUUID UUID] UUIDString];
			pAggregate[@kAudioAggregateDeviceIsPrivateKey] = @YES;
			pAggregate[@kAudioAggregateDeviceIsStackedKey] = @NO;
			pAggregate[@kAudioAggregateDeviceTapAutoStartKey] = @YES;
			pAggregate[@kAudioAggregateDeviceTapListKey] = @[@{@kAudioSubTapUIDKey: pTapUid, @kAudioSubTapDriftCompensationKey: @YES}];
			const std::string OutputUid = DefaultOutputDeviceUid();
			if(!OutputUid.empty())
			{
				NSString *pOutputUid = [NSString stringWithUTF8String:OutputUid.c_str()];
				pAggregate[@kAudioAggregateDeviceMainSubDeviceKey] = pOutputUid;
				pAggregate[@kAudioAggregateDeviceSubDeviceListKey] = @[@{@kAudioSubDeviceUIDKey: pOutputUid}];
			}
			[pTapUid release];

			AudioObjectID AggregateId = kAudioObjectUnknown;
			if(AudioHardwareCreateAggregateDevice((CFDictionaryRef)pAggregate, &AggregateId) != noErr || AggregateId == kAudioObjectUnknown)
			{
				DestroyTap();
				return false;
			}
			m_AggregateId = AggregateId;

			const AudioObjectPropertyAddress FormatAddress = {kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal, ELEMENT_MAIN};
			AudioStreamBasicDescription Format = {};
			UInt32 FormatSize = sizeof(Format);
			if(AudioObjectGetPropertyData(m_TapId, &FormatAddress, 0, nullptr, &FormatSize, &Format) != noErr ||
				Format.mFormatID != kAudioFormatLinearPCM || !(Format.mFormatFlags & kAudioFormatFlagIsFloat) || Format.mBitsPerChannel != 32 || Format.mSampleRate < 8000.0)
			{
				DestroyTap();
				return false;
			}
			m_TapSampleRate = (int)std::lround(Format.mSampleRate);

			const OSStatus ProcStatus = AudioDeviceCreateIOProcIDWithBlock(&m_IoProcId, m_AggregateId, m_Queue,
				^(const AudioTimeStamp *, const AudioBufferList *pInputData, const AudioTimeStamp *, AudioBufferList *, const AudioTimeStamp *) {
					PushInput(pInputData);
				});
			if(ProcStatus != noErr || !m_IoProcId || AudioDeviceStart(m_AggregateId, m_IoProcId) != noErr)
			{
				DestroyTap();
				return false;
			}
			m_TapServiceId = ServiceId;
			log_info("visualizer", "capturing %s at %d Hz", vPlayer.empty() ? "all system audio" : ServiceId.c_str(), m_TapSampleRate);
			return true;
		}
	}

	void WorkerMain()
	{
		if(@available(macOS 14.2, *))
		{
		}
		else
		{
			StoreBackendState(EVisualizerBackendStatus::UNAVAILABLE);
			return;
		}

		CVisualizerAnalyzer Analyzer;
		CVisualizerSmoother Smoother;
		std::vector<float> vSamples;
		int64_t AppliedConfigRevision = -1;
		int AppliedSampleRate = 0;
		int64_t LastSignalTick = 0;
		int64_t LastPlayingTick = 0;
		int64_t NextRetryTick = 0;
		int ActiveReads = 0;
		int SilentReads = 0;
		bool LatchedSignal = false;
		bool LoggedSignal = false;

		StoreBackendState(EVisualizerBackendStatus::FALLBACK);
		while(true)
		{
			SVisualizerPlaybackHint Hint;
			SVisualizerConfig Config;
			int64_t ConfigRevision;
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				if(m_Shutdown)
					break;
				Hint = m_Hint;
				Config = m_Config;
				ConfigRevision = m_ConfigRevision;
			}

			const int64_t Now = time_get();
			const int64_t Freq = time_freq();
			if(Hint.m_Playing)
				LastPlayingTick = Now;
			const bool Idle = !Hint.m_Playing && (LastPlayingTick == 0 || Now - LastPlayingTick > IDLE_TEARDOWN_MS * Freq / 1000);
			const bool PlayerChanged = m_AggregateId != kAudioObjectUnknown && !Hint.m_ServiceId.empty() && Hint.m_ServiceId != m_TapServiceId;
			if(m_AggregateId != kAudioObjectUnknown && (Idle || PlayerChanged))
			{
				DestroyTap();
				LatchedSignal = false;
				StoreBackendState(EVisualizerBackendStatus::FALLBACK);
			}

			if(m_AggregateId == kAudioObjectUnknown)
			{
				if(Hint.m_Playing && Now >= NextRetryTick)
				{
					bool Created = false;
					if(@available(macOS 14.2, *))
						Created = CreateTap(Hint.m_ServiceId);
					if(!Created)
						NextRetryTick = Now + RETRY_DELAY_MS * Freq / 1000;
					LastSignalTick = Now;
					ActiveReads = SilentReads = 0;
				}
				if(m_AggregateId == kAudioObjectUnknown)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}
			}

			if(ConfigRevision != AppliedConfigRevision || m_TapSampleRate != AppliedSampleRate)
			{
				SVisualizerConfig RuntimeConfig = Config;
				RuntimeConfig.m_SampleRate = m_TapSampleRate;
				Analyzer.Configure(RuntimeConfig);
				Smoother.Configure(RuntimeConfig);
				AppliedConfigRevision = ConfigRevision;
				AppliedSampleRate = m_TapSampleRate;
			}

			{
				std::lock_guard<std::mutex> Lock(m_SampleMutex);
				vSamples.swap(m_vPendingSamples);
			}
			if(vSamples.empty())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(8));
				continue;
			}
			Analyzer.PushMonoSamples(vSamples.data(), (int)vSamples.size());
			vSamples.clear();

			SVisualizerFrame RawFrame;
			Analyzer.Analyze(RawFrame);
			if(RawFrame.m_HasRealtimeSignal)
			{
				ActiveReads = std::min(ActiveReads + 1, 32);
				SilentReads = 0;
				LastSignalTick = Now;
			}
			else
			{
				ActiveReads = 0;
				SilentReads++;
			}
			if(!LatchedSignal && ActiveReads >= 2)
			{
				LatchedSignal = true;
				if(!LoggedSignal)
					log_info("visualizer", "receiving audio");
				LoggedSignal = true;
			}
			else if(LatchedSignal && SilentReads >= 10)
				LatchedSignal = false;

			const bool LongSilence = Hint.m_Playing && Now - LastSignalTick > SILENCE_FALLBACK_MS * Freq / 1000;
			RawFrame.m_HasRealtimeSignal = LatchedSignal;
			RawFrame.m_BackendStatus = LatchedSignal ? EVisualizerBackendStatus::LIVE : (LongSilence ? EVisualizerBackendStatus::FALLBACK : EVisualizerBackendStatus::SILENT);
			RawFrame.m_IsPassiveFallback = RawFrame.m_BackendStatus == EVisualizerBackendStatus::FALLBACK;

			SVisualizerFrame SmoothedFrame;
			Smoother.Process(RawFrame, SmoothedFrame);
			SmoothedFrame.m_BackendStatus = RawFrame.m_BackendStatus;
			SmoothedFrame.m_IsPassiveFallback = RawFrame.m_IsPassiveFallback;
			StoreFrame(SmoothedFrame);

			std::this_thread::sleep_for(std::chrono::milliseconds(8));
		}

		DestroyTap();
	}

public:
	CCoreAudioVisualizerSource()
	{
		m_Queue = dispatch_queue_create("cecenclient.visualizer.tap", DISPATCH_QUEUE_SERIAL);
		m_Worker = std::thread([this]() { WorkerMain(); });
	}

	~CCoreAudioVisualizerSource() override
	{
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Shutdown = true;
		}
		if(m_Worker.joinable())
			m_Worker.join();
		if(m_Queue)
			dispatch_release(m_Queue);
	}

	void SetPlaybackHint(const SVisualizerPlaybackHint &Hint) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Hint.m_ServiceId != Hint.m_ServiceId || m_Hint.m_Title != Hint.m_Title || m_Hint.m_Artist != Hint.m_Artist || m_Hint.m_Playing != Hint.m_Playing)
		{
			m_Hint = Hint;
			++m_HintRevision;
		}
	}

	void SetConfig(const SVisualizerConfig &Config) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_Config.m_SampleRate == Config.m_SampleRate && m_Config.m_BandCount == Config.m_BandCount &&
			m_Config.m_LowCutHz == Config.m_LowCutHz && m_Config.m_HighCutHz == Config.m_HighCutHz &&
			m_Config.m_BassSplitHz == Config.m_BassSplitHz && m_Config.m_NoiseReduction == Config.m_NoiseReduction &&
			m_Config.m_Sensitivity == Config.m_Sensitivity && m_Config.m_ChannelMode == Config.m_ChannelMode)
			return;
		m_Config = Config;
		++m_ConfigRevision;
	}

	bool PollFrame(SVisualizerFrame &OutFrame) override
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		OutFrame = m_LatestFrame;
		return OutFrame.m_BackendStatus == EVisualizerBackendStatus::LIVE || OutFrame.m_HasRealtimeSignal || OutFrame.m_Rms > 0.0f || OutFrame.m_Peak > 0.0f;
	}
};
} // namespace

std::unique_ptr<IVisualizerSource> CreateCoreAudioVisualizerSource()
{
	return std::make_unique<CCoreAudioVisualizerSource>();
}

} // namespace SixClientVisualizer
