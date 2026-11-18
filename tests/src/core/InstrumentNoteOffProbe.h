/*
 * InstrumentNoteOffProbe.h - test instrument for observing base MIDI note-off delivery
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#pragma once

#include "Instrument.h"
#include "MidiEvent.h"

#define private public
#include "InstrumentTrack.h"
#undef private

namespace racktest
{

class NoteOffRecordingInstrument final : public lmms::Instrument
{
public:
	NoteOffRecordingInstrument(lmms::InstrumentTrack* track, const lmms::Plugin::Descriptor* descriptor) :
		Instrument(track, descriptor)
	{
	}

	QString nodeName() const override { return QStringLiteral("noteoffrecordinginstrument"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
	lmms::gui::PluginView* instantiateView(QWidget*) override { return nullptr; }
	bool handleMidiEvent(const lmms::MidiEvent& event, const lmms::TimePos&, lmms::f_cnt_t) override
	{
		if (event.type() == lmms::MidiNoteOff) { ++m_noteOffs; }
		return true;
	}
	int noteOffCount() const { return m_noteOffs; }

private:
	int m_noteOffs = 0;
};

class BaseInstrumentDeliveryProbe
{
public:
	explicit BaseInstrumentDeliveryProbe(lmms::InstrumentTrack* track) :
		m_track(track),
		m_original(track->instrument()),
		m_recorder(new NoteOffRecordingInstrument(track, m_original->descriptor()))
	{
		m_track->m_instrument = m_recorder;
	}
	~BaseInstrumentDeliveryProbe()
	{
		m_track->m_instrument = m_original;
		delete m_recorder;
	}
	int noteOffCount() const { return m_recorder->noteOffCount(); }

private:
	lmms::InstrumentTrack* m_track;
	lmms::Instrument* m_original;
	NoteOffRecordingInstrument* m_recorder;
};

} // namespace racktest
