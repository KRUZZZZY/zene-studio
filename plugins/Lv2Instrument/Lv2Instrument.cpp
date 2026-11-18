/*
 * Lv2Instrument.cpp - implementation of LV2 instrument
 *
 * Copyright (c) 2018-2023 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "Lv2Instrument.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QPushButton>

#include <stdexcept>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "Lv2Proc.h"
#include "Lv2SubPluginFeatures.h"
#include "StringPairDrag.h"
#include "Clipboard.h"

#include "embed.h"
#include "plugin_export.h"


namespace lmms
{

namespace
{

QString uriForKey(const Plugin::Descriptor::SubPluginFeatures::Key* key)
{
	if (key == nullptr || !key->isValid())
	{
		throw std::runtime_error("Cannot instantiate LV2 instrument: missing sub-plugin key");
	}

	const QString uri = key->attributes.value(QStringLiteral("uri"));
	if (uri.isEmpty())
	{
		throw std::runtime_error("Cannot instantiate LV2 instrument: sub-plugin key has no URI");
	}
	return uri;
}

} // namespace


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT lv2instrument_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"LV2",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"plugin for using arbitrary LV2 instruments inside Zene Studio."),
	"Johannes Lorenz <jlsf2013$$$users.sourceforge.net, $$$=@>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Lv2SubPluginFeatures(Plugin::Type::Instrument)
};

}




/*
	Lv2Instrument
*/


Lv2Instrument::Lv2Instrument(InstrumentTrack *instrumentTrackArg,
	Descriptor::SubPluginFeatures::Key *key) :
	Instrument(instrumentTrackArg, &lv2instrument_plugin_descriptor, key,
#ifdef LV2_INSTRUMENT_USE_MIDI
		Flag::IsSingleStreamed | Flag::IsMidiBased
#else
		Flag::IsSingleStreamed
#endif
	),
	Lv2ControlBase(this, uriForKey(key))
{
	clearRunningNotes();

	connect(instrumentTrack()->pitchRangeModel(), SIGNAL(dataChanged()),
		this, SLOT(updatePitchRange()), Qt::DirectConnection);
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged,
		this, &Lv2Instrument::onSampleRateChanged);

	publishOutputBuses();  // 0.5.0 MX-114

	// MX-157: the latency port is read off the audio thread, twice a second (Lv2Effect's interval).
	m_latencyPoll.setInterval(500);
	connect(&m_latencyPoll, &QTimer::timeout, this, &Lv2Instrument::checkLatency);
	m_latencyPoll.start();

	// now we need a play-handle which cares for calling play()
	auto iph = new InstrumentPlayHandle(this, instrumentTrackArg);
	Engine::audioEngine()->addPlayHandle(iph);
}




Lv2Instrument::~Lv2Instrument()
{
	Engine::audioEngine()->removePlayHandlesOfTypes(instrumentTrack(),
		PlayHandle::Type::NotePlayHandle | PlayHandle::Type::InstrumentPlayHandle);
}




void Lv2Instrument::reload()
{
	Lv2ControlBase::reload();
	clearRunningNotes();
	emit modelChanged();
}




void Lv2Instrument::checkLatency()
{
	const int frames = Lv2ControlBase::latencyFrames();
	if (frames == m_reportedLatency) { return; }
	m_reportedLatency = frames;
	reportLatency(frames);
}




void Lv2Instrument::clearRunningNotes()
{
#ifdef LV2_INSTRUMENT_USE_MIDI
	for (int i = 0; i < NumKeys; ++i) { m_runningNotes[i] = 0; }
#endif
}




void Lv2Instrument::onSampleRateChanged()
{
	// TODO: once lv2 options are implemented,
	//       plugins that support it might allow changing their samplerate
	//       through it instead of reloading
	reload();
}




void Lv2Instrument::saveSettings(QDomDocument &doc, QDomElement &that)
{
	Lv2ControlBase::saveSettings(doc, that);
}




void Lv2Instrument::loadSettings(const QDomElement &that)
{
	Lv2ControlBase::loadSettings(that);
}




void Lv2Instrument::loadFile(const QString &file)
{
	Lv2ControlBase::loadFile(file);
}




#ifdef LV2_INSTRUMENT_USE_MIDI
bool Lv2Instrument::handleMidiEvent(
	const MidiEvent &event, const TimePos &time, f_cnt_t offset)
{
	// this function can be called from GUI threads while the plugin is running
	// handleMidiInputEvent will use a thread-safe ringbuffer
	handleMidiInputEvent(event, time, offset);
	return true;
}
#endif




// not yet working
#ifndef LV2_INSTRUMENT_USE_MIDI
void Lv2Instrument::playNoteImpl(NotePlayHandle *nph, std::span<SampleFrame>)
{
}
#endif




void Lv2Instrument::playImpl(std::span<SampleFrame> out)
{
	copyModelsFromLmms();

	run(out.size());

	copyModelsToLmms();
	copyBuffersToLmms(out.data(), out.size());
	deliverOutputBuses(static_cast<f_cnt_t>(out.size()));
}




void Lv2Instrument::publishOutputBuses()
{
	std::vector<InstrumentOutputBus> buses{{QStringLiteral("Main"), 2, 0}};
	const std::size_t extra = control(0) != nullptr ? control(0)->extraOutputCount() : 0;
	for (std::size_t port = 0; port < extra; port += 2)
	{
		const int channels = port + 1 < extra ? 2 : 1;
		buses.push_back({QStringLiteral("Aux %1").arg(buses.size()), channels, static_cast<int>(2 + port)});
	}
	m_outputBuses.setLayout(buses);
}




void Lv2Instrument::deliverOutputBuses(f_cnt_t frames)
{
	const Lv2Proc* proc = control(0);
	const bool muted = instrumentTrack() != nullptr && instrumentTrack()->isMuted();
	for (int bus = 1; bus < m_outputBuses.busCount(); ++bus)
	{
		SampleFrame* block = m_outputBuses.routedBuffer(bus, frames);
		if (block == nullptr || proc == nullptr) { continue; }
		const auto left = static_cast<std::size_t>(2 * (bus - 1));
		const auto right = m_outputBuses.bus(bus)->channels > 1 ? left + 1 : left;
		proc->copyExtraOutputToCore(left, block, 0, frames);
		proc->copyExtraOutputToCore(right, block, 1, frames);
		m_outputBuses.send(bus, frames, muted);
	}
}




gui::PluginView* Lv2Instrument::instantiateView(QWidget *parent)
{
	return new gui::Lv2InsView(this, parent);
}




void Lv2Instrument::updatePitchRange()
{
	qDebug() << "Zene Studio: Cannot update pitch range for lv2 plugin:"
				"not implemented yet";
}




QString Lv2Instrument::nodeName() const
{
	return Lv2ControlBase::nodeName();
}




namespace gui
{

/*
	Lv2InsView
*/


Lv2InsView::Lv2InsView(Lv2Instrument *_instrument, QWidget *_parent) :
	InstrumentView(_instrument, _parent),
	Lv2ViewBase(this, _instrument)
{
	setAutoFillBackground(true);
	if (m_reloadPluginButton) {
		connect(m_reloadPluginButton, &QPushButton::clicked,
			this, [this](){ this->castModel<Lv2Instrument>()->reload();} );
	}
	if (m_toggleUIButton) {
		connect(m_toggleUIButton, &QPushButton::toggled,
			this, [this](){ toggleUI(); });
	}
	if (m_helpButton) {
		connect(m_helpButton, &QPushButton::toggled,
			this, [this](bool visible){ toggleHelp(visible); });
	}
}




void Lv2InsView::dragEnterEvent(QDragEnterEvent *_dee)
{
	// For mimeType() and MimeType enum class
	using namespace Clipboard;

	void (QDragEnterEvent::*reaction)() = &QDragEnterEvent::ignore;

	if (_dee->mimeData()->hasFormat( mimeType( MimeType::StringPair )))
	{
		const QString txt =
			_dee->mimeData()->data( mimeType( MimeType::StringPair ) );
		if (txt.section(':', 0, 0) == "pluginpresetfile") {
			reaction = &QDragEnterEvent::acceptProposedAction;
		}
	}

	(_dee->*reaction)();
}




void Lv2InsView::dropEvent(QDropEvent *_de)
{
	const QString type = StringPairDrag::decodeKey(_de);
	const QString value = StringPairDrag::decodeValue(_de);
	if (type == "pluginpresetfile")
	{
		castModel<Lv2Instrument>()->loadFile(value);
		_de->accept();
		return;
	}
	_de->ignore();
}




void Lv2InsView::hideEvent(QHideEvent *event)
{
	closeHelpWindow();
	QWidget::hideEvent(event);
}




void Lv2InsView::modelChanged()
{
	Lv2ViewBase::modelChanged(castModel<Lv2Instrument>());
	connect(castModel<Lv2Instrument>(), &Lv2Instrument::modelChanged,
		this, [this](){ this->modelChanged();} );
}


} // namespace gui

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin *lmms_plugin_main(Model *_parent, void *_data)
{
	using KeyType = Plugin::Descriptor::SubPluginFeatures::Key;
	try {
		return new Lv2Instrument(static_cast<InstrumentTrack*>(_parent), static_cast<KeyType*>(_data));
	} catch (const std::runtime_error& e) {
		qCritical() << e.what();
		return nullptr;
	}
}

}


} // namespace lmms
