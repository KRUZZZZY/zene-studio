/*
 * MidiPort.h - abstraction of MIDI ports which are part of LMMS' MIDI
 *              sequencing system
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
 *
 */

#ifndef LMMS_MIDI_PORT_H
#define LMMS_MIDI_PORT_H

#include <atomic>

#include <QString>
#include <QList>
#include <QMap>
#include <QVector>

#include "Midi.h"
#include "TimePos.h"
#include "AutomatableModel.h"

namespace lmms
{

class MidiClient;
class MidiEvent;
class MidiEventProcessor;

namespace gui
{

class MidiPortMenu;
class ControllerConnectionDialog;
class InstrumentMidiIOView;

}


// class for abstraction of MIDI-port
class MidiPort : public Model, public SerializingObject
{
	Q_OBJECT
	mapPropertyFromModel(int,inputChannel,setInputChannel,m_inputChannelModel);
	mapPropertyFromModel(int,outputChannel,setOutputChannel,m_outputChannelModel);
	mapPropertyFromModel(int,inputController,setInputController,m_inputControllerModel);
	mapPropertyFromModel(int,outputController,setOutputController,m_outputControllerModel);
	mapPropertyFromModel(int,fixedInputVelocity,setFixedInputVelocity,m_fixedInputVelocityModel);
	mapPropertyFromModel(int,fixedOutputVelocity,setFixedOutputVelocity,m_fixedOutputVelocityModel);
	mapPropertyFromModel(int,fixedOutputNote,setFixedOutputNote,m_fixedOutputNoteModel);
	mapPropertyFromModel(int,outputProgram,setOutputProgram,m_outputProgramModel);
	mapPropertyFromModel(int,outputBankMsb,setOutputBankMsb,m_outputBankMsbModel);
	mapPropertyFromModel(int,outputBankLsb,setOutputBankLsb,m_outputBankLsbModel);
	mapPropertyFromModel(int,baseVelocity,setBaseVelocity,m_baseVelocityModel);
	mapPropertyFromModel(bool,isReadable,setReadable,m_readableModel);
	mapPropertyFromModel(bool,isWritable,setWritable,m_writableModel);
public:
	using Map = QMap<QString, bool>;

	/*! Q050-252.1: one saved setting of the External MIDI device - what a hardware synth is told: the output
	 *  port, the channel, the bank (CC 0 / CC 32, -1 = "send none") and the program (1..128, as the PROG box
	 *  counts). Saved with the port, recalled through device.external_preset_recall. */
	struct DevicePreset
	{
		QString name;
		QString port;      //!< empty: no port named
		int channel = 1;   //!< 0..16 (0 keeps each note's own)
		int bankMsb = -1;  //!< -1 or 0..127
		int bankLsb = -1;  //!< -1 or 0..127
		int program = 1;   //!< 1..128
	};
	static constexpr int MaxDevicePresets = 64;
	static constexpr int MaxDevicePresetName = 64;

	enum class Mode
	{
		Disabled,	// don't route any MIDI-events (default)
		Input,		// from MIDI-client to MIDI-event-processor
		Output,		// from MIDI-event-processor to MIDI-client
		Duplex		// both directions
	} ;

	MidiPort( const QString& name,
			MidiClient* client,
			MidiEventProcessor* eventProcessor,
			Model* parent = nullptr,
			Mode mode = Mode::Disabled );
	~MidiPort() override;

	void setName( const QString& name );

	Mode mode() const
	{
		return m_mode;
	}

	void setMode( Mode mode );

	bool isInputEnabled() const
	{
		return mode() == Mode::Input || mode() == Mode::Duplex;
	}

	bool isOutputEnabled() const
	{
		return mode() == Mode::Output || mode() == Mode::Duplex;
	}

	/*! Output accounting, written on whatever thread calls processOutEvent()
	 *  (the audio thread for the note path, the caller for LED feedback) and
	 *  read by `controller.surface_state` and the controller tests.
	 *
	 *  `outputEventsOffered()` counts events handed to this port's output path;
	 *  `outputEventsWritten()` counts the ones MidiPort::processOutEvent() then
	 *  actually passed to the MIDI client - the client BOUNDARY, which is the
	 *  strongest thing an in-process test can measure with no hardware (the
	 *  client's own bytes go to sendByte(), a no-op in the dummy client).
	 *
	 *  Relaxed atomics: this is a measurement, not synchronization. Nothing
	 *  here allocates or locks, so the audio path stays realtime-safe. */
	quint64 outputEventsOffered() const
	{
		return m_outputEventsOffered.load(std::memory_order_relaxed);
	}
	quint64 outputEventsWritten() const
	{
		return m_outputEventsWritten.load(std::memory_order_relaxed);
	}

	int realOutputChannel() const
	{
		// There's a possibility of outputChannel being 0 ("--"), which is used to keep all
		// midi channels when forwarding. In that case, realOutputChannel will return the
		// default channel 1 (whose value is 0).
		return outputChannel() ? outputChannel() - 1 : 0;
	}

	void processInEvent( const MidiEvent& event, const TimePos& time = TimePos() );
	//! @a offset: frames into the engine period the event was produced at (0.5.0 W4), for a client
	//! that places events inside its cycle (MidiClient::processOutEventAt).
	//! Exported: the VST3 instrument plug-in writes its output events straight to the port.
	LMMS_EXPORT void processOutEvent( const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0 );


	//! The client this port is registered with (the engine's, unless a test rebound it).
	MidiClient* midiClient() const { return m_midiClient; }
	/*! Test seam (Q050-252.1): registers this port with @a client instead, so a test can give it a client with
	 *  ports and a record of what it was sent. The product never calls it. */
	LMMS_EXPORT void rebindClientForTest( MidiClient* client );

	/*! Sets the output bank (-1: send none) and program together and sends them to the output ONCE (bank
	 *  select CC 0 / CC 32, then the program change), instead of once per model as three separate edits
	 *  would - a hardware synth must not step through the patches in between. Sends when anything changed
	 *  or @a force is set. @return whether it sent. Main thread (it emits the models' signals). */
	LMMS_EXPORT bool setOutputPatch( int bankMsb, int bankLsb, int program, bool force = false );
	//! Sends the current bank select and program change to the output (what the three models do on a change).
	LMMS_EXPORT void sendOutputPatch();

	const QVector<DevicePreset>& devicePresets() const { return m_devicePresets; }
	//! The preset called @a name, or nullptr.
	LMMS_EXPORT const DevicePreset* findDevicePreset( const QString& name ) const;
	//! Adds @a preset, or replaces the one of that name; false when the list is full (MaxDevicePresets).
	LMMS_EXPORT bool setDevicePreset( const DevicePreset& preset );
	LMMS_EXPORT bool removeDevicePreset( const QString& name );

	void saveSettings( QDomDocument& doc, QDomElement& thisElement ) override;
	void loadSettings( const QDomElement& thisElement ) override;

	QString nodeName() const override
	{
		return "midiport";
	}

	void subscribeReadablePort( const QString& port, bool subscribe = true );
	void subscribeWritablePort( const QString& port, bool subscribe = true );

	const Map& readablePorts() const
	{
		return m_readablePorts;
	}

	const Map& writablePorts() const
	{
		return m_writablePorts;
	}

	void invalidateCilent();

	gui::MidiPortMenu* m_readablePortsMenu;
	gui::MidiPortMenu* m_writablePortsMenu;


public slots:
	void updateMidiPortMode();


private slots:
	void updateReadablePorts();
	void updateWritablePorts();
	void updateOutputProgram();  //!< a bank or program model changed: send the patch


private:
	MidiClient* m_midiClient;
	MidiEventProcessor* m_midiEventProcessor;

	Mode m_mode;

	IntModel m_inputChannelModel;
	IntModel m_outputChannelModel;
	IntModel m_inputControllerModel;
	IntModel m_outputControllerModel;
	IntModel m_fixedInputVelocityModel;
	IntModel m_fixedOutputVelocityModel;
	IntModel m_fixedOutputNoteModel;
	IntModel m_outputProgramModel;
	IntModel m_outputBankMsbModel;  //!< -1 = send no bank select (every project before Q050-252.1)
	IntModel m_outputBankLsbModel;
	IntModel m_baseVelocityModel;
	BoolModel m_readableModel;
	BoolModel m_writableModel;

	Map m_readablePorts;
	Map m_writablePorts;

	QVector<DevicePreset> m_devicePresets;
	bool m_patchBatch = false;  //!< setOutputPatch() is applying: the models' changes do not send on their own

	//! Output accounting - see the accessors' comment above. Zero-initialized
	//! atomics, so a port that never writes costs nothing.
	std::atomic<quint64> m_outputEventsOffered{0};
	std::atomic<quint64> m_outputEventsWritten{0};


	friend class gui::ControllerConnectionDialog;
	friend class gui::InstrumentMidiIOView;


signals:
	void readablePortsChanged();
	void writablePortsChanged();
	void modeChanged();

} ;

using MidiPortList = QList<MidiPort*>;

} // namespace lmms

#endif // LMMS_MIDI_PORT_H
