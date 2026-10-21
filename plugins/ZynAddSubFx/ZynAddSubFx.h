/*
 * ZynAddSubFx.h - ZynAddSubFX-embedding plugin
 *
 * Copyright (c) 2008-2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef ZYNADDSUBFX_H
#define ZYNADDSUBFX_H

#include <QMap>
#include <QMutex>

#include <functional>

#include <globals.h>

#include "AudioPlugin.h"
#include "AutomatableModel.h"
#include "InstrumentView.h"
#include "MidiHandoffQueue.h"
#include "RemotePlugin.h"
#include "RemotePluginAudioPorts.h"

class QPushButton;

namespace lmms
{


class LocalZynAddSubFx;
class NotePlayHandle;

namespace gui
{
class Knob;
class LedCheckBox;
class ZynAddSubFxView;
}

class ZynAddSubFxRemotePlugin : public RemotePlugin
{
	Q_OBJECT
public:
	ZynAddSubFxRemotePlugin(RemotePluginAudioPortsController& audioPorts);

	bool processMessage( const message & _m ) override;


signals:
	void clickedCloseButton();

} ;



/**
 * The instrument runs either the remote ZynAddSubFx client process or the
 * in-process LocalZynAddSubFx synth, so its buffers are configurable at runtime:
 * the audio ports switch between the remote plugin's shared block and a local
 * buffer with `setBufferType()`/`activate()` (#589).
 *
 * Which of the two runs is a per-instance choice: the default is the in-process
 * synth, and the `separateprocess` attribute opts the instance into the client
 * process (see m_separateProcessModel). Showing the Zyn GUI still requires the
 * client, because the GUI is the client's own window.
 */
class ZynAddSubFxInstrument
	: public AudioPluginExt<Instrument, AudioPortsSettings {
			.kind = AudioDataKind::F32,
			.interleaved = false,
			.inputs = 0,
			.outputs = 2
		}, DefaultConfigurableAudioPorts>
{
	Q_OBJECT
public:
	ZynAddSubFxInstrument( InstrumentTrack * _instrument_track );
	~ZynAddSubFxInstrument() override;

	//! How this instrument is hosted right now: "in-process" (the local synth),
	//! "separate-process" (the RemoteZynAddSubFx client process is running) or
	//! "separate-process-exited" (the client was started and has since gone
	//! away). Q_INVOKABLE because a test host has no header for this class -
	//! it is a plugin module.
	Q_INVOKABLE QString hostingState() const;

	//! pid of the running client process, or 0 when the in-process synth is the
	//! one running (feature row 80, board card #670). Q_INVOKABLE so the
	//! control surface's `oop.*` group reports the client the way the platform
	//! sees it and a test host needs no header for this class.
	Q_INVOKABLE qint64 hostingProcessId() const;

	//! Choose the hosting mode: true re-hosts this instance in its own client
	//! process (RemoteZynAddSubFx), false back to the in-process synth. Returns
	//! whether the mode changed (a call that asks for the mode already in force
	//! changes nothing and says so). This is the ONE settle point for the
	//! choice - the instrument view's checkbox, a project's `separateprocess`
	//! attribute and the control surface's oop.set_mode all end here - and it
	//! re-instantiates rather than switching live, because the shared audio
	//! block and the synth state do not survive a switch mid-render.
	Q_INVOKABLE bool setHostingMode( bool separateProcess );

	bool handleMidiEvent( const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0 ) override;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	void loadFile( const QString & _file ) override;


	QString nodeName() const override;

	gui::PluginView* instantiateView( QWidget * _parent ) override;


private slots:
	void reloadPlugin();

	void updatePitchRange();

	void updatePortamento();
	void updateFilterFreq();
	void updateFilterQ();
	void updateBandwidth();
	void updateFmGain();
	void updateResCenterFreq();
	void updateResBandwidth();


private:
	auto processImpl(PlanarBufferView<const float, 0> in, PlanarBufferView<float, 2> out) -> ProcessStatus override;

	void initPlugin();
	void sendControlChange( MidiControllers midiCtl, float value );

	//! Test seam: called where a regression test has to look at the plugin lock - the
	//! choice of a plugin and its use, and the hosting getters' read of the plugin
	//! pointer (see tests/src/plugins/ZynAddSubFxInstrumentTest.cpp). It does nothing
	//! in a build without the test seams.
	void testHook() const;

protected:
	auto processLock() -> bool override;
	void processUnlock() override;

	//! Forwards the MIDI events that were queued since the last period to the plugin that
	//! is running, in order. Called by processImpl() on the audio thread.
	void forwardQueuedMidi();

	bool m_hasGUI;
	//! The plugin mutex. It guards the plugin instances: their creation, their use and
	//! their deletion, and the state the instrument keeps with them. A reader that wants
	//! to know which plugin is running has to hold it, because the instance it points at
	//! can be deleted and replaced by a hosting-mode switch at any time.
	mutable QMutex m_pluginMutex;

	//! MIDI from any thread (processInEvent() runs on the GUI, the MIDI input and the audio thread), waiting
	//! for the next processed period. handleMidiEvent() only pushes here; processImpl() forwards it under
	//! processLock(), so the audio thread never waits for the plugin mutex to queue an event.
	MidiHandoffQueue<256> m_midiHandoff;
	//! Channels whose events the full queue refused; sent all-notes-off once the queue has drained.
	MidiChannelOverflow m_midiOverflow;
	LocalZynAddSubFx * m_localPlugin;
	ZynAddSubFxRemotePlugin * m_remotePlugin;

#ifdef LMMS_TESTING
	//! Test seam: the hook a regression test installs to observe the plugin lock.
	std::function<void()> m_testHook;
	//! Test seam: makes the lock policy take the export path, so a regression test can
	//! drive it without running a render.
	bool m_testForceExport = false;
	//! Test seam: the offset of the last MIDI event forwardQueuedMidi() handed over.
	int m_testLastForwardedOffset = -1;
#endif

	FloatModel m_portamentoModel;
	FloatModel m_filterFreqModel;
	FloatModel m_filterQModel;
	FloatModel m_bandwidthModel;
	FloatModel m_fmGainModel;
	FloatModel m_resCenterFreqModel;
	FloatModel m_resBandwidthModel;
	BoolModel m_forwardMidiCcModel;

	//! Per-instance opt-in for the separate-process path (RemoteZynAddSubFx):
	//! stored as the `separateprocess` attribute of this plugin's element, the
	//! same BoolModel convention m_forwardMidiCcModel uses for `forwardmidicc`.
	//! Default false, i.e. the in-process synth, which is what every project
	//! written before this setting existed gets.
	BoolModel m_separateProcessModel;

	QMap<int, bool> m_modifiedControllers;

	friend class gui::ZynAddSubFxView;


signals:
	void settingsChanged();

} ;


namespace gui
{


class ZynAddSubFxView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	ZynAddSubFxView( Instrument * _instrument, QWidget * _parent );


protected:
	void dragEnterEvent( QDragEnterEvent * _dee ) override;
	void dropEvent( QDropEvent * _de ) override;


private:
	void modelChanged() override;

	QPushButton * m_toggleUIButton;
	Knob * m_portamento;
	Knob * m_filterFreq;
	Knob * m_filterQ;
	Knob * m_bandwidth;
	Knob * m_fmGain;
	Knob * m_resCenterFreq;
	Knob * m_resBandwidth;
	LedCheckBox * m_forwardMidiCC;
	LedCheckBox * m_separateProcess;


private slots:
	void toggleUI();
	void separateProcessToggled();

} ;


} // namespace gui

} // namespace lmms

#endif
