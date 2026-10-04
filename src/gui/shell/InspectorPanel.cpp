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

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "ClipEdits.h"
#include "CommandForm.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "InspectorChannelRouting.h"
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
#include "TakeLanesPanel.h"
#include "TrackMixModels.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

//! Runs @a id with @a args at once from @a from; a refusal (an edit lock, a bad name) is shown as a floating message.
ControlResult invoke(QWidget* from, const QString& id, const QJsonObject& args)
{
	return runCommand(from->window(), CommandCall{id, args, {}, {}, false});
}

//! The user is typing into @a field (its editor has the focus): a refresh would not reach the text being typed.
bool typingInto(const QWidget* field)
{
	const QWidget* focus = QApplication::focusWidget();
	return focus != nullptr && focus != field && field->isAncestorOf(focus);
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
	more->setMinimumSize(tokens::size::target, tokens::size::target);  // the target size the toggles have
	auto* menu = new QMenu(more);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, populate] { menu->clear(); populate(menu); });
	more->setMenu(menu);
	row->addWidget(more);
	return bar;
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

Knob* knob(FloatModel* model, const QString& label, QWidget* parent, Knob* made = nullptr, const QString& unit = QString())
{
	auto* knob = made != nullptr ? made : new Knob(KnobType::Bright26, label, parent);
	knob->setFont(tokens::font(tokens::TextRole::Small));
	knob->setModel(model);
	knob->setHintText(label + QLatin1Char(':'), unit);
	return knob;
}

//! Theme 13: a volume as a musician reads it - in dB (VolumeKnob), its unity gain at @a unity on the model's scale
//! (100 for a track's 0..200 %, 1 for a channel's 0..2 gain); typing a value takes dB too.
Knob* volumeKnob(FloatModel* model, const QString& label, QWidget* parent, float unity)
{
	auto* volume = new VolumeKnob(KnobType::Bright26, label, parent);
	volume->setZeroDbfsPoint(unity);
	return knob(model, label, parent, volume);
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
	IntModel* assigned = track->mixerChannelModel();
	// Theme 13: "To <channel>" - a destination, never mistaken for the track's own name beside it.
	const auto show = [button](int index) { button->setText(QObject::tr("To %1").arg(channelLabel(index))); };
	show(assigned->value());
	// An assignment made elsewhere (an undo, a script, the track header) shows here too.
	QObject::connect(assigned, &Model::dataChanged, button, [show, assigned] { show(assigned->value()); });
	auto* menu = new QMenu(button);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, button, show, assigned = QPointer<IntModel>(assigned), id = control::trackIdOf(track)] {
		menu->clear();
		if (assigned.isNull()) { return; }  // the track went; the Inspector is about to redraw
		show(assigned->value());  // a channel renamed since
		for (int i = 0; i < static_cast<int>(Engine::mixer()->numChannels()); ++i)
		{
			const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(i));
			QAction* action = menu->addAction(channelLabel(i), button, [button, show, id, channel, i] {
				if (invoke(button, QStringLiteral("track.set_mixer_channel"), {{QStringLiteral("track"), id},
					{QStringLiteral("channel"), channel}}).ok) { show(i); }
			});
			action->setCheckable(true);
			action->setChecked(i == assigned->value());  // the current channel is marked
		}
	});
	button->setMenu(menu);
	return button;
}

//! Theme 13: the channel's name, edited in place - mixer.rename_channel, one undo step; a refused name springs back.
QLineEdit* channelNameField(MixerChannel* channel, const QString& id, QWidget* parent)
{
	auto* name = new QLineEdit(channel->m_name, parent);
	name->setObjectName(QStringLiteral("inspectorName"));
	name->setAccessibleName(QObject::tr("Channel name"));
	name->setPlaceholderText(id);
	name->setProperty("channel", id);
	QObject::connect(name, &QLineEdit::editingFinished, name, [name, id] {
		ControlResult error;
		const MixerChannel* now = control::resolveMixerChannel(id, &error);
		if (now == nullptr || name->text().trimmed() == now->m_name) { return; }
		if (!invoke(name, QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), id}, {QStringLiteral("name"), name->text()}}).ok)
		{
			name->setText(now->m_name);
		}
	});
	return name;
}

} // namespace

InspectorPanel::InspectorPanel(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("inspectorPanel"));
	setAccessibleName(tr("Inspector"));
	// The content scrolls: a channel's inserts, sends, strip and output can be taller than the region.
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	m_scroll = new QScrollArea(this);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFocusPolicy(Qt::NoFocus);  // Tab and F6 go to the controls, which the area scrolls into view
	m_scroll->viewport()->setAutoFillBackground(false);
	auto* body = new QWidget(m_scroll);
	body->setAutoFillBackground(false);
	m_scroll->setWidget(body);
	outer->addWidget(m_scroll);
	m_column = new QVBoxLayout(body);
	m_column->setContentsMargins(tokens::space::lg, tokens::space::lg, tokens::space::lg, tokens::space::lg);
	m_column->setSpacing(tokens::space::lg);
	// Theme 13: one content widget for the panel's life; its sections come and go inside it (InspectorSections.cpp).
	m_content = new QWidget(body);
	m_sections = new QVBoxLayout(m_content);
	m_sections->setContentsMargins(0, 0, 0, 0);
	m_sections->setSpacing(tokens::space::xl);
	m_column->addWidget(m_content);
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
		if (!invoke(m_name, QStringLiteral("track.rename"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), m_name->text()}}).ok)
		{
			m_name->setText(m_track->name());  // refused: the field shows the name the track kept
		}
	});
	nameRow->addWidget(m_name, 1);
	column->addLayout(nameRow);
	column->addWidget(caption(tr("%1 track - %2").arg(control::trackTypeNameOf(track->type()), control::trackIdOf(track)), section));
	column->addWidget(trackControls(track, section));
	// 0.5.0 L03 (ED-164): a track with take lanes shows them here, beside the track, not in a window.
	if (!track->takeLanes().isEmpty()) { column->addWidget(new TakeLanesPanel(control::trackIdOf(track), section)); }
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
	// A refused toggle springs back at once rather than at the next sync.
	connect(m_mute, &QToolButton::clicked, this, [mute = m_mute, id](bool on) {
		if (!invoke(mute, QStringLiteral("track.set_mute"), {{QStringLiteral("track"), id}, {QStringLiteral("muted"), on}}).ok) { mute->setChecked(!on); }
	});
	connect(m_solo, &QToolButton::clicked, this, [solo = m_solo, id](bool on) {
		if (!invoke(solo, QStringLiteral("track.set_solo"), {{QStringLiteral("track"), id}, {QStringLiteral("solo"), on}}).ok) { solo->setChecked(!on); }
	});
	auto* toggles = new QHBoxLayout;
	toggles->setSpacing(tokens::space::sm);
	toggles->addWidget(m_mute, 1);
	toggles->addWidget(m_solo, 1);
	column->addLayout(toggles);
	auto* mix = new QHBoxLayout;
	mix->setSpacing(tokens::space::lg);
	if (FloatModel* volume = trackVolumeModel(track)) { mix->addWidget(volumeKnob(volume, tr("Volume"), box, 100.f)); }
	if (FloatModel* panning = trackPanningModel(track)) { mix->addWidget(knob(panning, tr("Pan"), box, nullptr, QStringLiteral("%"))); }
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
	const QString id = control::clipIdOf(clip);
	addClipFields(grid, clip, id, section);
	if (auto* midi = dynamic_cast<MidiClip*>(clip))
	{
		addRow(grid, tr("Notes"), new QLabel(QString::number(midi->notes().size()), section));
		auto* edit = new QPushButton(tr("Edit notes"), section);
		edit->setAccessibleName(tr("Open the clip in the note editor"));
		connect(edit, &QPushButton::clicked, this, [edit, id] {
			invoke(edit, QStringLiteral("window.note_editor_set"), {{QStringLiteral("clip"), id}, {QStringLiteral("fit"), true}});
		});
		grid->addWidget(edit, grid->rowCount(), 0, 1, 2);
		grid->addWidget(notesSection(midi, id, section), grid->rowCount(), 0, 1, 2);
	}
	else if (auto* sample = dynamic_cast<SampleClip*>(clip))
	{
		auto* gain = new ReadoutField(tr("Gain"), section);
		const QPointer<SampleClip> guarded(sample);  // the clip can go while its fields are still shown
		gain->bindText([guarded] { return guarded ? QStringLiteral("%1 dB").arg(gainLinearToDb(guarded->sample().amplification()), 0, 'f', 1) : QString(); },
			[gain, id](const QString& typed) {
				bool ok = false;
				const double db = QString(typed).remove(QStringLiteral("dB")).trimmed().toDouble(&ok);
				return ok && invoke(gain, QStringLiteral("clip.set_gain"), {{QStringLiteral("clip"), id}, {QStringLiteral("gain_db"), db}}).ok;
			});
		grid->addWidget(gain, grid->rowCount(), 0, 1, 2);  // a readout field carries its own caption
		m_liveFields.append(gain);  // a gain set elsewhere (an undo, a script) shows here too
	}
	grid->setColumnStretch(1, 1);
	return section;
}

QWidget* InspectorPanel::channelSection(MixerChannel* channel, const QString& id)
{
	// A channel is not a QObject: its models' own deletion drops the section before a control
	// could paint a deleted model.
	auto* section = new QWidget(m_content);
	// Theme 13: tied to this section (a kept-then-replaced section must not retire its successor).
	connect(&channel->m_volumeModel, &QObject::destroyed, section, [this, section] {
		if (m_channelSection.widget == section) { retireSection(m_channelSection); }
		scheduleRebuild();
	});
	auto* column = new QVBoxLayout(section);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(tokens::space::md);
	column->addWidget(titleBar(tr("Mixer channel"), section, [id](QMenu* menu) { ShellCommandMenus::populateChannel(menu, id); }));
	m_channelName = channelNameField(channel, id, section);
	column->addWidget(m_channelName);
	column->addWidget(caption(tr("Mixer channel - %1").arg(id), section));
	auto* row = new QHBoxLayout;
	row->setSpacing(tokens::space::md);
	auto* mute = new LedCheckBox(tr("Mute"), section);
	mute->setModel(&channel->m_muteModel);
	auto* solo = new LedCheckBox(tr("Solo"), section);
	solo->setModel(&channel->m_soloModel);
	Knob* volume = volumeKnob(&channel->m_volumeModel, tr("Volume"), section, 1.f);
	Knob* pan = knob(&channel->m_panModel, tr("Pan"), section);
	for (QWidget* w : std::initializer_list<QWidget*>{mute, solo, volume, pan}) { row->addWidget(w); }
	row->addStretch(1);
	column->addLayout(row);
	// MX-012 (0.5.0 L08): the inserts, sends and strip settings beside the fader.
	column->addWidget(inspectorrouting::routingSection(channel, id, section, [this] { refreshChannelSection(); }));
	return section;
}

void InspectorPanel::syncToggles()
{
	// The live fields (clip and note values) follow edits made elsewhere - the arranger, the note editor -
	// focused or not, so Enter on a focused field opens on the current value; only one being typed into waits.
	for (const QPointer<ReadoutField>& field : m_liveFields)
	{
		if (!field.isNull() && !typingInto(field)) { field->refresh(); }
	}
	syncChannelName();
	if (m_track.isNull() || m_mute == nullptr) { return; }
	m_mute->setChecked(m_track->isMuted());
	m_solo->setChecked(m_track->isSolo());
	if (m_name != nullptr && !m_name->hasFocus() && m_name->text() != m_track->name()) { m_name->setText(m_track->name()); }
}

} // namespace lmms::gui
