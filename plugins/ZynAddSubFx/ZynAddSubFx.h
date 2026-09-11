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

#include <globals.h>

#include "AudioPlugin.h"
#include "AutomatableModel.h"
#include "InstrumentView.h"
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

	auto processLock() -> bool override;
	void processUnlock() override;

	void initPlugin();
	void sendControlChange( MidiControllers midiCtl, float value );

	bool m_hasGUI;
	QMutex m_pluginMutex;
	LocalZynAddSubFx * m_localPlugin;
	ZynAddSubFxRemotePlugin * m_remotePlugin;

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
