/*
 * AuHost.cpp - one hosted Audio Unit (AUv2) component (Q050-491, macOS only)
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

// Compiled as Objective-C++ (CMakeLists.txt: -x objective-c++), without ARC: every Objective-C object
// this file keeps is retained and released by hand.

#include "AuHost.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <QDebug>
#include <QDomDocument>
#include <QDomElement>
#include <QGuiApplication>

#import <AppKit/AppKit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AUCocoaUIView.h>
#import <CoreAudioKit/AUGenericView.h>

#include "AuParameter.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Vst3MidiQueue.h"

namespace lmms::au
{

namespace
{

AudioComponentDescription descriptionOf(const ComponentId& id)
{
	AudioComponentDescription description{};
	description.componentType = id.type;
	description.componentSubType = id.subtype;
	description.componentManufacturer = id.manufacturer;
	return description;
}

QString fromCF(CFStringRef text)
{
	return text != nullptr ? QString::fromCFString(text) : QString();
}

QString registryName(AudioComponent component)
{
	CFStringRef name = nullptr;
	if (AudioComponentCopyName(component, &name) != noErr) { return {}; }
	const QString out = fromCF(name);
	if (name != nullptr) { CFRelease(name); }
	return out;
}

void appendType(OSType type, QList<Component>* out)
{
	AudioComponentDescription wanted{};
	wanted.componentType = type;
	AudioComponent component = nullptr;
	while ((component = AudioComponentFindNext(component, &wanted)) != nullptr)
	{
		AudioComponentDescription found{};
		if (AudioComponentGetDescription(component, &found) != noErr) { continue; }
		UInt32 version = 0;
		AudioComponentGetVersion(component, &version);
		const ComponentId id{found.componentType, found.componentSubType, found.componentManufacturer};
		out->append(splitComponentName(id, registryName(component), version));
	}
}

//! The view the factory class @a info names makes for @a unit, retained; nil when the class is not one.
NSView* factoryView(AudioUnit unit, const AudioUnitCocoaViewInfo& info)
{
	NSURL* location = (NSURL*)info.mCocoaAUViewBundleLocation;
	NSString* className = (NSString*)info.mCocoaAUViewClass[0];
	NSBundle* bundle = location != nil ? [NSBundle bundleWithURL:location] : nil;
	Class factoryClass = bundle != nil && className != nil ? [bundle classNamed:className] : nil;
	if (factoryClass == nil || ![factoryClass conformsToProtocol:@protocol(AUCocoaUIBase)]) { return nil; }
	// AUCocoaUIBase does not adopt <NSObject>: the factory is typed as the NSObject it is, for -release.
	NSObject<AUCocoaUIBase>* factory = (NSObject<AUCocoaUIBase>*)[[factoryClass alloc] init];
	NSView* view = [[factory uiViewForAudioUnit:unit withSize:NSZeroSize] retain];
	[factory release];
	return view;
}

//! kAudioUnitProperty_CocoaUI hands over one reference to the URL and to each class name of its @a size bytes.
void releaseViewInfo(AudioUnitCocoaViewInfo* info, UInt32 size)
{
	const auto classes = (size - offsetof(AudioUnitCocoaViewInfo, mCocoaAUViewClass)) / sizeof(CFStringRef);
	if (info->mCocoaAUViewBundleLocation != nullptr) { CFRelease(info->mCocoaAUViewBundleLocation); }
	for (std::size_t i = 0; i < classes; ++i)
	{
		if (info->mCocoaAUViewClass[i] != nullptr) { CFRelease(info->mCocoaAUViewClass[i]); }
	}
}

//! The unit's own Cocoa view (kAudioUnitProperty_CocoaUI), retained; nil when it has none.
NSView* customView(AudioUnit unit)
{
	UInt32 size = 0;
	Boolean writable = false;
	if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) != noErr
		|| size < sizeof(AudioUnitCocoaViewInfo))
	{
		return nil;
	}
	std::vector<std::uint8_t> raw(size);
	auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(raw.data());
	if (AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size) != noErr) { return nil; }
	NSView* view = factoryView(unit, *info);
	releaseViewInfo(info, size);
	return view;
}

} // namespace

QList<Component> listRegistry(bool instruments)
{
	QList<Component> out;
	if (instruments)
	{
		appendType(kAudioUnitType_MusicDevice, &out);
		return out;
	}
	appendType(kAudioUnitType_Effect, &out);
	appendType(kAudioUnitType_MusicEffect, &out);
	return out;
}

struct HostedUnit::Impl
{
	AudioComponentInstance unit = nullptr;
	ComponentId id;
	QString name;
	bool instrument = false;
	bool initialized = false;
	double sampleRate = 0.0;
	int maxFrames = 0;
	int channels = 2;
	AudioBufferList* output = nullptr;   //!< prepare(): one buffer per channel, pointed at the caller's
	std::vector<float> scratch;          //!< prepare(): a channel the caller did not supply, and silence
	const float* const* input = nullptr; //!< process(): what the render callback copies from
	int inputChannels = 0;
	Float64 sampleTime = 0.0;
	vst3::MidiQueue midi;
	std::vector<ParamInfo> params;
	std::unique_ptr<std::atomic<float>[]> pendingValues;
	std::unique_ptr<std::atomic<bool>[]> pendingDirty;
	NSView* view = nil;

	//! The effect's input bus: AudioUnitRender asks for it inside process(), on the audio thread.
	static OSStatus renderInput(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
		AudioBufferList* io)
	{
		auto* self = static_cast<Impl*>(ref);
		const float* silence = self->scratch.data();
		for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
		{
			AudioBuffer& buffer = io->mBuffers[b];
			const bool fed = self->input != nullptr && static_cast<int>(b) < self->inputChannels;
			const float* source = fed ? self->input[b] : silence;
			if (buffer.mData == nullptr) { buffer.mData = const_cast<float*>(source); }
			else { std::memcpy(buffer.mData, source, frames * sizeof(float)); }
			buffer.mDataByteSize = frames * sizeof(float);
		}
		return noErr;
	}

	void readParameters()
	{
		params.clear();
		UInt32 size = 0;
		Boolean writable = false;
		if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &writable) == noErr
			&& size > 0)
		{
			std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
			if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size) != noErr)
			{
				ids.clear();
			}
			for (const AudioUnitParameterID parameter : ids) { readParameter(parameter); }
		}
		pendingValues = std::make_unique<std::atomic<float>[]>(params.size());
		pendingDirty = std::make_unique<std::atomic<bool>[]>(params.size());
		for (std::size_t i = 0; i < params.size(); ++i) { pendingDirty[i].store(false, std::memory_order_relaxed); }
	}

	void readParameter(AudioUnitParameterID parameter)
	{
		AudioUnitParameterInfo info{};
		UInt32 size = sizeof(info);
		if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, parameter, &info, &size) != noErr)
		{
			return;
		}
		ParamInfo out;
		out.id = parameter;
		if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0 && info.cfNameString != nullptr)
		{
			out.name = fromCF(info.cfNameString);
			if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0) { CFRelease(info.cfNameString); }
		}
		else { out.name = QString::fromUtf8(info.name, static_cast<int>(qstrnlen(info.name, sizeof(info.name)))); }
		out.minimum = info.minValue;
		out.maximum = info.maxValue > info.minValue ? info.maxValue : info.minValue + 1.f;
		out.defaultValue = std::clamp(info.defaultValue, out.minimum, out.maximum);
		out.writable = (info.flags & kAudioUnitParameterFlag_IsWritable) != 0;
		params.push_back(out);
	}

	bool setFormat(AudioUnitScope scope, double rate, QString* error)
	{
		AudioStreamBasicDescription format{};
		format.mSampleRate = rate;
		format.mFormatID = kAudioFormatLinearPCM;
		format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
		format.mBytesPerPacket = sizeof(float);
		format.mFramesPerPacket = 1;
		format.mBytesPerFrame = sizeof(float);
		format.mChannelsPerFrame = static_cast<UInt32>(channels);
		format.mBitsPerChannel = 32;
		const OSStatus status = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, scope, 0, &format, sizeof(format));
		if (status == noErr) { return true; }
		*error = instantiationError(id, name, QStringLiteral("it refused %1-channel float %2 at %3 Hz")
			.arg(channels).arg(scope == kAudioUnitScope_Input ? QStringLiteral("input") : QStringLiteral("output"))
			.arg(rate), status);
		return false;
	}

	//! Audio thread: the parameter values set since the last block.
	void applyPendingParameters()
	{
		for (std::size_t i = 0; i < params.size(); ++i)
		{
			if (pendingDirty[i].exchange(false, std::memory_order_acq_rel))
			{
				AudioUnitSetParameter(unit, params[i].id, kAudioUnitScope_Global, 0,
					pendingValues[i].load(std::memory_order_relaxed), 0);
			}
		}
	}

	//! Audio thread: the queued MIDI, each message at its frame inside this block.
	void deliverMidi(int frames)
	{
		if (!instrument) { return; }
		vst3::MidiEventIn event;
		while (midi.pop(&event))
		{
			MusicDeviceMIDIEvent(unit, event.type, event.data0, event.data1,
				static_cast<UInt32>(std::clamp(event.frameOffset, 0, frames - 1)));
		}
	}

	//! Audio thread: the output buffers at the caller's channels, scratch for any it did not supply.
	void pointOutputs(float* const* out, int outChannels, UInt32 bytes)
	{
		for (UInt32 c = 0; c < output->mNumberBuffers; ++c)
		{
			AudioBuffer& buffer = output->mBuffers[c];
			buffer.mNumberChannels = 1;
			buffer.mData = static_cast<int>(c) < outChannels ? out[c]
				: scratch.data() + static_cast<std::size_t>(maxFrames) * (c + 1);
			buffer.mDataByteSize = bytes;
		}
	}

	void freeBuffers()
	{
		std::free(output);
		output = nullptr;
	}
};

HostedUnit::HostedUnit() : m_impl(std::make_unique<Impl>()) {}

HostedUnit::~HostedUnit()
{
	closeEditor();
	release();
	if (m_impl->unit != nullptr) { AudioComponentInstanceDispose(m_impl->unit); }
}

bool HostedUnit::load(const ComponentId& id, QString* error)
{
	Impl& d = *m_impl;
	d.id = id;
	if (!id.isValid())
	{
		*error = instantiationError(id, {}, QStringLiteral("its key names no component"), 0);
		return false;
	}
	const AudioComponentDescription wanted = descriptionOf(id);
	AudioComponent component = AudioComponentFindNext(nullptr, &wanted);
	if (component == nullptr)
	{
		*error = instantiationError(id, {}, QStringLiteral("the system component registry has no such component"), 0);
		return false;
	}
	d.name = splitComponentName(id, registryName(component), 0).name;
	const OSStatus status = AudioComponentInstanceNew(component, &d.unit);
	if (status != noErr || d.unit == nullptr)
	{
		d.unit = nullptr;
		*error = instantiationError(id, d.name, QStringLiteral("AudioComponentInstanceNew failed"), status);
		return false;
	}
	d.instrument = kindOf(id.type) == Kind::Instrument;
	d.readParameters();
	return true;
}

bool HostedUnit::isLoaded() const { return m_impl->unit != nullptr; }
bool HostedUnit::isInstrument() const { return m_impl->instrument; }
const ComponentId& HostedUnit::componentId() const { return m_impl->id; }
QString HostedUnit::name() const { return m_impl->name; }
bool HostedUnit::isPrepared() const { return m_impl->initialized; }
const std::vector<ParamInfo>& HostedUnit::parameters() const { return m_impl->params; }
bool HostedUnit::editorOpen() const { return m_impl->view != nil; }

bool HostedUnit::prepare(double sampleRate, int maxFrames, int channels, QString* error)
{
	Impl& d = *m_impl;
	if (d.unit == nullptr)
	{
		*error = instantiationError(d.id, d.name, QStringLiteral("it is not loaded"), 0);
		return false;
	}
	release();
	d.channels = std::max(1, channels);
	d.sampleRate = sampleRate;
	d.maxFrames = std::max(1, maxFrames);
	if (!d.setFormat(kAudioUnitScope_Output, sampleRate, error)) { return false; }
	if (!d.instrument)
	{
		if (!d.setFormat(kAudioUnitScope_Input, sampleRate, error)) { return false; }
		AURenderCallbackStruct callback{&Impl::renderInput, &d};
		const OSStatus status = AudioUnitSetProperty(d.unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
			&callback, sizeof(callback));
		if (status != noErr)
		{
			*error = instantiationError(d.id, d.name, QStringLiteral("it refused the host's input callback"), status);
			return false;
		}
	}
	UInt32 frames = static_cast<UInt32>(d.maxFrames);
	AudioUnitSetProperty(d.unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &frames, sizeof(frames));
	const OSStatus status = AudioUnitInitialize(d.unit);
	if (status != noErr)
	{
		*error = instantiationError(d.id, d.name, QStringLiteral("AudioUnitInitialize failed"), status);
		return false;
	}
	d.output = static_cast<AudioBufferList*>(std::calloc(1,
		offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * static_cast<std::size_t>(d.channels)));
	d.output->mNumberBuffers = static_cast<UInt32>(d.channels);
	// One channel's worth of silence first, then a block per channel for outputs the caller has no buffer for.
	d.scratch.assign(static_cast<std::size_t>(d.maxFrames) * static_cast<std::size_t>(d.channels + 1), 0.f);
	d.sampleTime = 0.0;
	// The MIDI queue is not reset: the MIDI thread may be pushing live input into it while the engine is held for
	// a re-prepare, and the engine's hold does not stop that thread. What is queued plays on the next block.
	d.initialized = true;
	return true;
}

void HostedUnit::release()
{
	Impl& d = *m_impl;
	if (d.initialized) { AudioUnitUninitialize(d.unit); }
	d.initialized = false;
	d.freeBuffers();
}

bool HostedUnit::process(const float* const* in, float* const* out, int inChannels, int outChannels, int frames)
{
	Impl& d = *m_impl;
	if (!d.initialized || frames <= 0 || frames > d.maxFrames) { return false; }
	d.applyPendingParameters();
	d.deliverMidi(frames);
	const auto bytes = static_cast<UInt32>(frames) * sizeof(float);
	d.pointOutputs(out, outChannels, bytes);
	d.input = in;
	d.inputChannels = inChannels;
	AudioUnitRenderActionFlags flags = 0;
	AudioTimeStamp timeStamp{};
	timeStamp.mSampleTime = d.sampleTime;
	timeStamp.mFlags = kAudioTimeStampSampleTimeValid;
	const OSStatus status = AudioUnitRender(d.unit, &flags, &timeStamp, 0, static_cast<UInt32>(frames), d.output);
	d.input = nullptr;
	d.sampleTime += frames;
	if (status != noErr) { return false; }
	const int rendered = std::min(outChannels, static_cast<int>(d.output->mNumberBuffers));
	for (int c = 0; c < rendered; ++c)
	{
		// A unit may answer with buffers of its own instead of filling ours.
		const void* data = d.output->mBuffers[c].mData;
		if (data != out[c]) { std::memcpy(out[c], data, bytes); }
	}
	for (int c = rendered; c < outChannels; ++c) { std::fill_n(out[c], frames, 0.f); }
	return true;
}

bool HostedUnit::queueMidi(std::uint8_t status, std::uint8_t data1, std::uint8_t data2, int frameOffset)
{
	if (!m_impl->instrument) { return true; }
	vst3::MidiEventIn event;
	event.type = status;
	event.channel = status & 0x0f;
	event.data0 = data1;
	event.data1 = data2;
	event.frameOffset = frameOffset;
	return m_impl->midi.push(event);
}

void HostedUnit::setParam(int index, float value)
{
	Impl& d = *m_impl;
	if (index < 0 || static_cast<std::size_t>(index) >= d.params.size()) { return; }
	d.pendingValues[index].store(value, std::memory_order_relaxed);
	d.pendingDirty[index].store(true, std::memory_order_release);
}

float HostedUnit::paramValue(int index) const
{
	const Impl& d = *m_impl;
	if (d.unit == nullptr || index < 0 || static_cast<std::size_t>(index) >= d.params.size()) { return 0.f; }
	AudioUnitParameterValue value = 0.f;
	if (AudioUnitGetParameter(d.unit, d.params[index].id, kAudioUnitScope_Global, 0, &value) != noErr)
	{
		return d.params[index].defaultValue;
	}
	return value;
}

bool HostedUnit::saveState(QByteArray* state) const
{
	const Impl& d = *m_impl;
	if (d.unit == nullptr) { return false; }
	CFPropertyListRef plist = nullptr;
	UInt32 size = sizeof(plist);
	if (AudioUnitGetProperty(d.unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size) != noErr
		|| plist == nullptr)
	{
		return false;
	}
	CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, plist, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
	CFRelease(plist);
	if (data == nullptr) { return false; }
	*state = QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), static_cast<int>(CFDataGetLength(data)));
	CFRelease(data);
	return true;
}

bool HostedUnit::loadState(const QByteArray& state, QString* error)
{
	Impl& d = *m_impl;
	if (d.unit == nullptr)
	{
		*error = instantiationError(d.id, d.name, QStringLiteral("it is not loaded"), 0);
		return false;
	}
	CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(state.constData()), state.size());
	CFPropertyListRef plist = data != nullptr
		? CFPropertyListCreateWithData(kCFAllocatorDefault, data, kCFPropertyListImmutable, nullptr, nullptr) : nullptr;
	if (data != nullptr) { CFRelease(data); }
	if (plist == nullptr)
	{
		*error = QStringLiteral("the saved Audio Unit state of %1 is not a property list").arg(componentKey(d.id));
		return false;
	}
	const OSStatus status = AudioUnitSetProperty(d.unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
		&plist, sizeof(plist));
	CFRelease(plist);
	if (status != noErr)
	{
		*error = QStringLiteral("the Audio Unit %1 refused its saved state (status %2, %3)")
			.arg(componentKey(d.id)).arg(status).arg(statusName(status));
		return false;
	}
	return true;
}

int HostedUnit::latencyFrames() const
{
	const Impl& d = *m_impl;
	Float64 seconds = 0.0;
	UInt32 size = sizeof(seconds);
	if (d.unit == nullptr
		|| AudioUnitGetProperty(d.unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &seconds, &size) != noErr)
	{
		return 0;
	}
	return static_cast<int>(std::clamp(seconds * d.sampleRate, 0.0, 1.0e7));
}

int HostedUnit::tailFrames() const
{
	const Impl& d = *m_impl;
	Float64 seconds = 0.0;
	UInt32 size = sizeof(seconds);
	if (d.unit == nullptr
		|| AudioUnitGetProperty(d.unit, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &seconds, &size) != noErr)
	{
		return 0;
	}
	return static_cast<int>(std::clamp(seconds * d.sampleRate, 0.0, 1.0e7));
}

bool HostedUnit::openEditor(void* parentView, QString* error)
{
	Impl& d = *m_impl;
	if (d.unit == nullptr)
	{
		*error = instantiationError(d.id, d.name, QStringLiteral("it is not loaded"), 0);
		return false;
	}
	const QString platform = QGuiApplication::platformName();
	if (platform != QLatin1String("cocoa"))
	{
		// Under any other Qt platform the window id is not an NSView, and adding a view to it would crash.
		*error = QStringLiteral("the Audio Unit %1 cannot embed on macOS here: its editor is an NSView and this "
			"session runs Qt's '%2' platform, which has no NSView (window API cocoa)").arg(componentKey(d.id), platform);
		return false;
	}
	closeEditor();
	@autoreleasepool
	{
		NSView* view = customView(d.unit);
		if (view == nil) { view = [[AUGenericView alloc] initWithAudioUnit:d.unit]; }
		if (view == nil)
		{
			*error = QStringLiteral("the Audio Unit %1 has no Cocoa view and CoreAudioKit's generic view could not be "
				"made for it").arg(componentKey(d.id));
			return false;
		}
		if ([view isKindOfClass:[AUGenericView class]]) { [(AUGenericView*)view setShowsExpertParameters:YES]; }
		[view setFrameOrigin:NSMakePoint(0, 0)];
		[(NSView*)parentView addSubview:view];
		d.view = view;
	}
	return true;
}

void HostedUnit::closeEditor()
{
	Impl& d = *m_impl;
	if (d.view == nil) { return; }
	[d.view removeFromSuperview];
	[d.view release];
	d.view = nil;
}

void HostedUnit::editorSize(int* width, int* height) const
{
	NSView* view = m_impl->view;
	*width = view != nil ? static_cast<int>([view frame].size.width) : 0;
	*height = view != nil ? static_cast<int>([view frame].size.height) : 0;
}

} // namespace lmms::au

namespace lmms
{

void saveAuState(au::HostedUnit* unit, QDomDocument& doc, QDomElement& element)
{
	QByteArray state;
	if (!unit->saveState(&state)) { return; }
	auto stateElement = doc.createElement(QStringLiteral("state"));
	stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
	element.appendChild(stateElement);
}

bool loadAuState(au::HostedUnit* unit, const QDomElement& element)
{
	const auto stateElement = element.firstChildElement(QStringLiteral("state"));
	if (stateElement.isNull() || !unit->isLoaded()) { return false; }
	AudioEngine* engine = Engine::audioEngine();
	// Not while the unit renders: the engine is held for the property write.
	if (engine != nullptr) { engine->requestChangeInModel(); }
	QString error;
	const bool loaded = unit->loadState(QByteArray::fromBase64(stateElement.text().toLatin1()), &error);
	if (engine != nullptr) { engine->doneChangeInModel(); }
	if (!loaded) { qWarning() << "AU:" << error; }
	return loaded;
}

} // namespace lmms
