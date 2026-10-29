/*
 * AuHost.h - one hosted Audio Unit (AUv2) component (Q050-491, macOS only)
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

#ifndef LMMS_AU_HOST_H
#define LMMS_AU_HOST_H

#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QList>
#include <QString>

#include "AuComponent.h"

namespace lmms::au
{

//! One parameter as kAudioUnitProperty_ParameterInfo describes it, in the unit's own (plain) values.
struct ParamInfo
{
	std::uint32_t id = 0;
	QString name;
	float minimum = 0.f;
	float maximum = 1.f;
	float defaultValue = 0.f;
	bool writable = true;
};

//! Every effect (aufx, aumf) or instrument (aumu) component the system registry lists. GUI thread.
QList<Component> listRegistry(bool instruments);

/*! What the parameter models need of a unit (AuParamModelList), apart from Apple's API: the hosted unit is one,
 *  and a test can supply another. setParam() is called on the audio thread and must be allocation and lock free. */
class ParamTarget
{
public:
	virtual ~ParamTarget() = default;
	virtual bool isLoaded() const = 0;
	virtual const std::vector<ParamInfo>& parameters() const = 0;
	//! GUI thread: the unit's current value of the parameter at @a index.
	virtual float paramValue(int index) const = 0;
	//! Audio thread (or any): the plain value @a value for the parameter at @a index, applied by the next block.
	virtual void setParam(int index, float value) = 0;
};

/*!
 * One Audio Unit instance. Free of Apple types so the effect and instrument classes are plain C++;
 * AuHost.cpp (compiled as Objective-C++) holds the AudioUnit, the render callback and the editor.
 *
 * Threads, the CLAP host's discipline:
 *  - load(), prepare(), release(), parameters(), paramValue(), saveState(), loadState() and the
 *    editor calls run on the GUI thread, and prepare()/loadState() only while the engine is held
 *    (AudioEngine::requestChangeInModel) or before the device is in a chain.
 *  - process(), queueMidi() and setParam() run on the audio thread (queueMidi() also on the MIDI
 *    thread). They allocate nothing, take no lock and never wait: the MIDI queue is the bounded
 *    MPSC ring the VST3 host uses, a parameter change is an atomic store the next block applies,
 *    and the render callback reads input pointers process() set before AudioUnitRender.
 */
class HostedUnit : public ParamTarget
{
public:
	HostedUnit();
	~HostedUnit() override;
	HostedUnit(const HostedUnit&) = delete;
	HostedUnit& operator=(const HostedUnit&) = delete;

	//! Find @a id in the registry and instantiate it. False with a typed error (instantiationError).
	bool load(const ComponentId& id, QString* error);
	bool isLoaded() const override;
	bool isInstrument() const;
	const ComponentId& componentId() const;
	QString name() const;

	//! Set the stream format (non-interleaved float, @a channels in and out), the largest block,
	//! and initialize. False with a typed error when the unit refuses.
	bool prepare(double sampleRate, int maxFrames, int channels, QString* error);
	bool isPrepared() const;
	void release();

	//! Audio thread. Renders @a frames (at most prepare()'s maxFrames) from @a in into @a out.
	bool process(const float* const* in, float* const* out, int inChannels, int outChannels, int frames);
	//! Audio or MIDI thread. One MIDI message for the next block; false when the queue is full.
	bool queueMidi(std::uint8_t status, std::uint8_t data1, std::uint8_t data2, int frameOffset);
	//! Any thread. The plain value @a value for the parameter at @a index, applied by the next block.
	void setParam(int index, float value) override;

	const std::vector<ParamInfo>& parameters() const override;
	//! GUI thread: the unit's current value (its own editor may have moved it).
	float paramValue(int index) const override;

	//! kAudioUnitProperty_ClassInfo as a binary property list.
	bool saveState(QByteArray* state) const;
	bool loadState(const QByteArray& state, QString* error);

	int latencyFrames() const;
	int tailFrames() const;

	//! The unit's Cocoa view (kAudioUnitProperty_CocoaUI, else CoreAudioKit's AUGenericView) added to
	//! the NSView @a parentView. Refused, typed, outside Qt's cocoa platform: no NSView to add it to.
	bool openEditor(void* parentView, QString* error);
	void closeEditor();
	void editorSize(int* width, int* height) const;
	bool editorOpen() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace lmms::au

#endif // LMMS_AU_HOST_H
