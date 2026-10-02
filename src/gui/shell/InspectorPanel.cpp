/*
 * InspectorPanel.cpp - the shell's Inspector: the selected track's and clip's properties
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
 *
 */

#include "InspectorPanel.h"

#include <functional>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "Mixer.h"
#include "MidiClip.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "ShellCommandMenus.h"
#include "ShellSelection.h"
#include "Song.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(id, args);
}

QLabel* caption(const QString& text, QWidget* parent)
{
	auto* label = new QLabel(text, parent);
	label->setObjectName(QStringLiteral("deviceCaption"));
	return label;
}

QLabel* sectionTitle(const QString& text, QWidget* parent)
{
	auto* label = new QLabel(text.toUpper(), parent);
	label->setObjectName(QStringLiteral("deviceSection"));
	return label;
}

/*! A section's title with its "More" button: a menu @a populate fills each time it opens
 *  (ShellCommandMenus - the W1 capabilities), so it always offers the current object's commands. */
QWidget* titleBar(const QString& text, QWidget* parent, const std::function<void(QMenu*)>& populate)
{
	auto* bar = new QWidget(parent);
	auto* row = new QHBoxLayout(bar);
	row->setContentsMargins(0, 0, 0, 0);
	row->addWidget(sectionTitle(text, bar));
	row->addStretch(1);
	auto* more = new QToolButton(bar);
	more->setText(QStringLiteral("⋯"));
	more->setObjectName(QStringLiteral("inspectorMore"));
	more->setAccessibleName(QObject::tr("More %1 actions").arg(text.toLower()));
	more->setToolTip(more->accessibleName());
	more->setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(more);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, populate] { menu->clear(); populate(menu); });
	more->setMenu(menu);
	row->addWidget(more);
	return bar;
}

//! "bar.beat" for a position, as the header's position readout writes it.
QString barBeat(const TimePos& pos)
{
	Song* song = Engine::getSong();
	const int perBar = std::max(1, song->ticksPerBar());
	const int perBeat = std::max(1, perBar / std::max(1, song->getTimeSigModel().getNumerator()));
	return QStringLiteral("%1.%2").arg(pos.getTicks() / perBar + 1).arg((pos.getTicks() % perBar) / perBeat + 1);
}

QString lengthText(const TimePos& length)
{
	const double bars = static_cast<double>(length.getTicks()) / std::max(1, Engine::getSong()->ticksPerBar());
	return QObject::tr("%1 bars").arg(bars, 0, 'g', 4);
}

//! The track's colour, as a square swatch before its name.
class Swatch : public QWidget
{
public:
	Swatch(const QColor& color, QWidget* parent) : QWidget(parent), m_color(color) { setFixedSize(14, 14); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(m_color);
		p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), tokens::radius::sm, tokens::radius::sm);
	}

private:
	QColor m_color;
};

QToolButton* toggle(const QString& text, const QString& tip, QWidget* parent)
{
	auto* button = new QToolButton(parent);
	button->setObjectName(QStringLiteral("inspectorToggle"));
	button->setText(text);
	button->setCheckable(true);
	button->setToolTip(tip);
	button->setAccessibleName(tip);
	button->setMinimumSize(3 * tokens::size::target, tokens::size::target);
	return button;
}

Knob* knob(FloatModel* model, const QString& label, QWidget* parent)
{
	auto* knob = new Knob(KnobType::Bright26, label, parent);
	knob->setFont(tokens::font(tokens::TextRole::Small));
	knob->setModel(model);
	knob->setHintText(label + QLatin1Char(':'), QString());
	return knob;
}

FloatModel* volumeOf(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->volumeModel(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->volumeModel(); }
	return nullptr;
}

FloatModel* panningOf(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->panningModel(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->panningModel(); }
	return nullptr;
}

QString channelLabel(int index)
{
	MixerChannel* channel = index >= 0 && index < static_cast<int>(Engine::mixer()->numChannels())
		? Engine::mixer()->mixerChannel(index) : nullptr;
	if (channel == nullptr) { return QString::number(index); }
	return channel->m_name.isEmpty() ? (index == 0 ? QObject::tr("Master") : QString::number(index)) : channel->m_name;
}

//! The channel the track plays into, as a menu of every channel: choosing one is
//! track.set_mixer_channel (engine audit / L01), so a script and a click take one path.
QToolButton* mixerChannelMenu(Track* track, QWidget* parent)
{
	auto* button = new QToolButton(parent);
	button->setObjectName(QStringLiteral("inspectorChannel"));
	button->setPopupMode(QToolButton::InstantPopup);
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	button->setAccessibleName(QObject::tr("Mixer channel"));
	button->setToolTip(QObject::tr("The mixer channel this track plays into"));
	button->setText(channelLabel(track->mixerChannelModel()->value()));
	auto* menu = new QMenu(button);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, button, id = control::trackIdOf(track)] {
		menu->clear();
		for (int i = 0; i < static_cast<int>(Engine::mixer()->numChannels()); ++i)
		{
			const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(i));
			menu->addAction(channelLabel(i), button, [button, id, channel, i] {
				if (invoke(QStringLiteral("track.set_mixer_channel"), {{QStringLiteral("track"), id},
					{QStringLiteral("channel"), channel}}).ok) { button->setText(channelLabel(i)); }
			});
		}
	});
	button->setMenu(menu);
	return button;
}

} // namespace

InspectorPanel::InspectorPanel(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("inspectorPanel"));
	setAccessibleName(tr("Inspector"));
	m_column = new QVBoxLayout(this);
	m_column->setContentsMargins(tokens::space::lg, tokens::space::lg, tokens::space::lg, tokens::space::lg);
	m_column->setSpacing(tokens::space::lg);
	m_column->addStretch(1);
	connect(ShellSelection::instance(), &ShellSelection::changed, this, &InspectorPanel::scheduleRebuild);
	m_pending.setSingleShot(true);
	m_pending.setInterval(0);
	connect(&m_pending, &QTimer::timeout, this, &InspectorPanel::rebuild);
	m_sync.setInterval(250);
	connect(&m_sync, &QTimer::timeout, this, &InspectorPanel::syncToggles);
	rebuild();
}

void InspectorPanel::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	m_sync.start();
}

void InspectorPanel::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	m_sync.stop();
}

void InspectorPanel::dropContent()
{
	if (m_content == nullptr) { return; }
	m_column->removeWidget(m_content);
	m_content->hide();
	m_content->deleteLater();
	m_content = nullptr;
	m_name = nullptr;
	m_mute = nullptr;
	m_solo = nullptr;
}

void InspectorPanel::scheduleRebuild()
{
	dropContent();
	m_pending.start();
}

void InspectorPanel::rebuild()
{
	m_pending.stop();
	dropContent();
	if (!m_track.isNull()) { disconnect(m_track, nullptr, this, nullptr); }
	m_track = ShellSelection::instance()->track();
	m_clip = ShellSelection::instance()->clip();
	m_content = new QWidget(this);
	auto* column = new QVBoxLayout(m_content);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(tokens::space::xl);
	ControlResult error;
	const QString channelId = ShellSelection::instance()->channel();
	MixerChannel* channel = channelId.isEmpty() ? nullptr : control::resolveMixerChannel(channelId, &error);
	if (channel != nullptr) { column->addWidget(channelSection(channel, channelId)); }
	else if (m_track.isNull())
	{
		column->addWidget(caption(tr("Select a track, a clip or a mixer channel to see its properties here."), m_content));
		column->itemAt(0)->widget()->setProperty("wordWrap", true);
	}
	else
	{
		connect(m_track, &QObject::destroyed, this, &InspectorPanel::scheduleRebuild);
		column->addWidget(trackSection(m_track));
		if (!m_clip.isNull()) { column->addWidget(clipSection(m_clip)); }
	}
	m_column->insertWidget(0, m_content);
	m_content->show();
	syncToggles();
}

void InspectorPanel::addRow(QGridLayout* grid, const QString& text, QWidget* value)
{
	const int row = grid->rowCount();
	grid->addWidget(caption(text, grid->parentWidget()), row, 0, Qt::AlignLeft | Qt::AlignVCenter);
	grid->addWidget(value, row, 1);
}

QWidget* InspectorPanel::trackSection(Track* track)
{
	auto* section = new QWidget(m_content);
	auto* column = new QVBoxLayout(section);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(tokens::space::md);
	column->addWidget(titleBar(tr("Track"), section, [track = QPointer<Track>(track)](QMenu* menu) {
		ShellCommandMenus::populateTrack(menu, track.data());
	}));
	auto* nameRow = new QHBoxLayout;
	nameRow->setSpacing(tokens::space::sm);
	nameRow->addWidget(new Swatch(track->color().value_or(tokens::palette().clipMidi), section));
	m_name = new QLineEdit(track->name(), section);
	m_name->setObjectName(QStringLiteral("inspectorName"));
	m_name->setAccessibleName(tr("Track name"));
	connect(m_name, &QLineEdit::editingFinished, this, [this, id = control::trackIdOf(track)] {
		if (m_name == nullptr || m_track.isNull() || m_name->text() == m_track->name()) { return; }
		invoke(QStringLiteral("track.rename"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), m_name->text()}});
	});
	nameRow->addWidget(m_name, 1);
	column->addLayout(nameRow);
	column->addWidget(caption(tr("%1 track - %2").arg(control::trackTypeNameOf(track->type()), control::trackIdOf(track)), section));
	column->addWidget(trackControls(track, section));
	return section;
}

QWidget* InspectorPanel::trackControls(Track* track, QWidget* parent)
{
	auto* box = new QWidget(parent);
	auto* column = new QVBoxLayout(box);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(tokens::space::md);
	const QString id = control::trackIdOf(track);
	m_mute = toggle(tr("Mute"), tr("Mute the track"), box);
	m_solo = toggle(tr("Solo"), tr("Solo the track"), box);
	connect(m_mute, &QToolButton::clicked, this, [id](bool on) {
		invoke(QStringLiteral("track.set_mute"), {{QStringLiteral("track"), id}, {QStringLiteral("muted"), on}});
	});
	connect(m_solo, &QToolButton::clicked, this, [id](bool on) {
		invoke(QStringLiteral("track.set_solo"), {{QStringLiteral("track"), id}, {QStringLiteral("solo"), on}});
	});
	auto* toggles = new QHBoxLayout;
	toggles->setSpacing(tokens::space::sm);
	toggles->addWidget(m_mute, 1);
	toggles->addWidget(m_solo, 1);
	column->addLayout(toggles);
	auto* mix = new QHBoxLayout;
	mix->setSpacing(tokens::space::lg);
	if (FloatModel* volume = volumeOf(track)) { mix->addWidget(knob(volume, tr("Volume"), box)); }
	if (FloatModel* panning = panningOf(track)) { mix->addWidget(knob(panning, tr("Pan"), box)); }
	if (track->mixerChannelModel() != nullptr) { mix->addWidget(mixerChannelMenu(track, box)); }
	mix->addStretch(1);
	column->addLayout(mix);
	return box;
}

QWidget* InspectorPanel::clipSection(Clip* clip)
{
	auto* section = new QWidget(m_content);
	auto* grid = new QGridLayout(section);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(tokens::space::lg);
	grid->setVerticalSpacing(tokens::space::sm);
	grid->addWidget(titleBar(tr("Clip"), section, [clip = QPointer<Clip>(clip)](QMenu* menu) {
		ShellCommandMenus::populateClip(menu, clip.data());
	}), 0, 0, 1, 2);
	addRow(grid, tr("Name"), new QLabel(clip->name().isEmpty() ? tr("(untitled)") : clip->name(), section));
	addRow(grid, tr("Start"), new QLabel(barBeat(clip->startPosition()), section));
	addRow(grid, tr("Length"), new QLabel(lengthText(clip->length()), section));
	const QString id = control::clipIdOf(clip);
	if (auto* midi = dynamic_cast<MidiClip*>(clip))
	{
		addRow(grid, tr("Notes"), new QLabel(QString::number(midi->notes().size()), section));
		auto* edit = new QPushButton(tr("Edit notes"), section);
		edit->setAccessibleName(tr("Open the clip in the note editor"));
		connect(edit, &QPushButton::clicked, this, [id] {
			invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("clip"), id}, {QStringLiteral("fit"), true}});
		});
		grid->addWidget(edit, grid->rowCount(), 0, 1, 2);
	}
	else if (auto* sample = dynamic_cast<SampleClip*>(clip))
	{
		auto* gain = new ReadoutField(tr("Gain"), section);
		gain->bindText([sample] { return QStringLiteral("%1 dB").arg(gainLinearToDb(sample->sample().amplification()), 0, 'f', 1); },
			[id](const QString& typed) {
				bool ok = false;
				const double db = QString(typed).remove(QStringLiteral("dB")).trimmed().toDouble(&ok);
				return ok && invoke(QStringLiteral("clip.set_gain"), {{QStringLiteral("clip"), id}, {QStringLiteral("gain_db"), db}}).ok;
			});
		addRow(grid, tr("Gain"), gain);
	}
	grid->setColumnStretch(1, 1);
	return section;
}

QWidget* InspectorPanel::channelSection(MixerChannel* channel, const QString& id)
{
	// A channel is not a QObject: its models' own deletion drops the section before a control
	// could paint a deleted model.
	connect(&channel->m_volumeModel, &QObject::destroyed, this, &InspectorPanel::scheduleRebuild);
	auto* section = new QWidget(m_content);
	auto* column = new QVBoxLayout(section);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(tokens::space::md);
	column->addWidget(titleBar(tr("Mixer channel"), section, [id](QMenu* menu) { ShellCommandMenus::populateChannel(menu, id); }));
	auto* name = new QLabel(channel->m_name.isEmpty() ? id : channel->m_name, section);
	name->setObjectName(QStringLiteral("inspectorName"));
	column->addWidget(name);
	column->addWidget(caption(id, section));
	auto* row = new QHBoxLayout;
	row->setSpacing(tokens::space::md);
	auto* mute = new LedCheckBox(tr("Mute"), section);
	mute->setModel(&channel->m_muteModel);
	auto* solo = new LedCheckBox(tr("Solo"), section);
	solo->setModel(&channel->m_soloModel);
	Knob* volume = knob(&channel->m_volumeModel, tr("Volume"), section);
	Knob* pan = knob(&channel->m_panModel, tr("Pan"), section);
	for (QWidget* w : std::initializer_list<QWidget*>{mute, solo, volume, pan}) { row->addWidget(w); }
	row->addStretch(1);
	column->addLayout(row);
	return section;
}

void InspectorPanel::syncToggles()
{
	if (m_track.isNull() || m_mute == nullptr) { return; }
	m_mute->setChecked(m_track->isMuted());
	m_solo->setChecked(m_track->isSolo());
	if (m_name != nullptr && !m_name->hasFocus() && m_name->text() != m_track->name()) { m_name->setText(m_track->name()); }
}

QJsonObject InspectorPanel::describe() const
{
	QJsonObject out{{QStringLiteral("track"), control::trackIdOf(m_track)}, {QStringLiteral("clip"), control::clipIdOf(m_clip)},
		{QStringLiteral("channel"), ShellSelection::instance()->channel()}};
	if (!m_track.isNull())
	{
		out.insert(QStringLiteral("name"), m_track->name());
		out.insert(QStringLiteral("muted"), m_track->isMuted());
		out.insert(QStringLiteral("soloed"), m_track->isSolo());
	}
	return out;
}

} // namespace lmms::gui
