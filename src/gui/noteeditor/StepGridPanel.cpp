/*
 * StepGridPanel.cpp - the melodic step grid's panel (include/StepGridPanel.h)
 *
 * A thin view over two registered commands: note.step_get_state is everything it shows and
 * note.step_set is its only writer - the same pair a headless agent dispatches. Nothing here
 * adds, moves or changes a note directly, so every gesture is one command and one undo step.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "StepGridPanel.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <utility>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "MidiClip.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const char* command, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

//! The resolutions the box offers, in ticks (192 a bar): 1/32, 1/16 triplet, 1/16, 1/8, 1/4.
constexpr int Resolutions[] = {6, 8, 12, 24, 48};
const char* const ResolutionNames[] = {"1/32", "1/16 T", "1/16", "1/8", "1/4"};
constexpr int DefaultStepTicks = 12;
//! Rows an empty clip opens on: two octaves from C4 (key 48).
constexpr int EmptyLow = 48;
constexpr int Octave = 12;

QStringList rootNames()
{
	return {QStringLiteral("C"), QStringLiteral("C#"), QStringLiteral("D"), QStringLiteral("D#"), QStringLiteral("E"),
		QStringLiteral("F"), QStringLiteral("F#"), QStringLiteral("G"), QStringLiteral("G#"), QStringLiteral("A"),
		QStringLiteral("A#"), QStringLiteral("B")};
}

QJsonArray notesOf(const QJsonObject& state) { return state.value(QStringLiteral("notes")).toArray(); }

//! The cell form of a step_get_state note, as window.step_grid reports it.
QJsonObject cellOf(const QJsonObject& note)
{
	QJsonObject cell;
	for (const char* field : {"note", "step", "key", "velocity", "chance", "ratchet", "length", "in_scale"})
	{
		cell.insert(QLatin1String(field), note.value(QLatin1String(field)));
	}
	return cell;
}

QSpinBox* spinBox(QWidget* parent, const char* name, const QString& label, std::pair<int, int> range)
{
	auto* box = new QSpinBox(parent);
	box->setObjectName(QLatin1String(name));
	box->setAccessibleName(label);
	box->setRange(range.first, range.second);
	// One edit per committed value: typing "150" is one note.step_set, not three.
	box->setKeyboardTracking(false);
	return box;
}

} // namespace

StepGridPanel::StepGridPanel(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Step Grid"));
	setAccessibleName(tr("Melodic step grid"));
	auto* layout = new QVBoxLayout(this);
	buildHeader(layout);
	buildFills(layout);
	m_canvas = new StepGridCanvas(this);
	auto* scroll = new QScrollArea(this);
	scroll->setObjectName(QStringLiteral("stepGridScroll"));
	scroll->setWidget(m_canvas);
	layout->addWidget(scroll, 1);
	buildInspector(layout);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("stepGridStatus"));
	m_status->setWordWrap(true);
	m_status->setVisible(false);
	layout->addWidget(m_status);
	resize(720, 480);
	fillScales();
	showSelected();
}

void StepGridPanel::buildHeader(QVBoxLayout* layout)
{
	m_title = new QLabel(tr("No clip open"), this);
	m_title->setObjectName(QStringLiteral("stepGridTitle"));
	m_resolution = new QComboBox(this);
	m_resolution->setObjectName(QStringLiteral("stepGridResolution"));
	m_resolution->setAccessibleName(tr("Step length"));
	for (std::size_t i = 0; i < std::size(Resolutions); ++i) { m_resolution->addItem(QLatin1String(ResolutionNames[i]), Resolutions[i]); }
	m_resolution->setCurrentIndex(m_resolution->findData(DefaultStepTicks));
	m_scale = new QComboBox(this);
	m_scale->setObjectName(QStringLiteral("stepGridScale"));
	m_scale->setAccessibleName(tr("Scale"));
	m_root = new QComboBox(this);
	m_root->setObjectName(QStringLiteral("stepGridRoot"));
	m_root->setAccessibleName(tr("Key"));
	m_root->addItems(rootNames());
	m_rowsOnly = new QCheckBox(tr("Scale rows only"), this);
	m_rowsOnly->setObjectName(QStringLiteral("stepGridScaleRows"));
	connect(m_resolution, QOverload<int>::of(&QComboBox::activated), this, [this] { refresh(); });
	connect(m_scale, QOverload<int>::of(&QComboBox::activated), this, [this] { refresh(); });
	connect(m_root, QOverload<int>::of(&QComboBox::activated), this, [this] { refresh(); });
	connect(m_rowsOnly, &QCheckBox::toggled, this, [this](bool on) { setScaleRowsOnly(on); });
	auto* row = new QHBoxLayout;
	row->addWidget(m_title, 1);
	for (auto [text, widget] : {std::pair<QString, QWidget*>{tr("Step"), m_resolution}, {tr("Scale"), m_scale}, {tr("Key"), m_root}})
	{
		auto* label = new QLabel(text, this);
		label->setBuddy(widget);
		row->addWidget(label);
		row->addWidget(widget);
	}
	row->addWidget(m_rowsOnly);
	layout->addLayout(row);
}

void StepGridPanel::buildInspector(QVBoxLayout* layout)
{
	m_key = new ScaleKeySpinBox(this);
	m_key->setObjectName(QStringLiteral("stepGridKey"));
	m_key->setAccessibleName(tr("Pitch"));
	m_key->setRange(0, 127);
	m_key->setKeyboardTracking(false);
	m_velocity = spinBox(this, "stepGridVelocity", tr("Velocity"), {0, 200});
	m_chance = spinBox(this, "stepGridChance", tr("Chance"), {0, 100});
	m_chance->setSuffix(QStringLiteral(" %"));
	m_ratchet = spinBox(this, "stepGridRatchet", tr("Ratchet"), {1, 16});
	m_remove = new QPushButton(tr("Remove"), this);
	m_remove->setObjectName(QStringLiteral("stepGridRemove"));
	// Return commits a field; in a dialog it would also press an auto-default button and remove the note.
	m_remove->setAutoDefault(false);
	for (QWidget* control : std::initializer_list<QWidget*>{m_key, m_velocity, m_chance, m_ratchet, m_remove})
	{
		control->setProperty("controlCommand", QStringLiteral("note.step_set"));
	}
	const auto edit = [this](const char* field) {
		return [this, field](int value) {
			if (!m_filling) { editSelected({{QLatin1String(field), value}}); }
		};
	};
	connect(m_key, QOverload<int>::of(&QSpinBox::valueChanged), this, edit("key"));
	connect(m_velocity, QOverload<int>::of(&QSpinBox::valueChanged), this, edit("velocity"));
	connect(m_ratchet, QOverload<int>::of(&QSpinBox::valueChanged), this, edit("ratchet"));
	// The box shows percent; the wire's chance is 0..1.
	connect(m_chance, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int percent) {
		if (!m_filling) { editSelected({{QStringLiteral("chance"), percent / 100.0}}); }
	});
	connect(m_remove, &QPushButton::clicked, this, [this] { editSelected({{QStringLiteral("remove"), true}}); });
	auto* row = new QHBoxLayout;
	for (auto [text, widget] : {std::pair<QString, QWidget*>{tr("Pitch"), m_key}, {tr("Velocity"), m_velocity},
			 {tr("Chance"), m_chance}, {tr("Ratchet"), m_ratchet}})
	{
		auto* label = new QLabel(text, this);
		label->setBuddy(widget);
		row->addWidget(label);
		row->addWidget(widget);
	}
	row->addStretch();
	row->addWidget(m_remove);
	layout->addLayout(row);
}

void StepGridPanel::buildFills(QVBoxLayout* layout)
{
	m_fillKind = new QComboBox(this);
	m_fillKind->setObjectName(QStringLiteral("stepGridFillKind"));
	m_fillKind->setAccessibleName(tr("Shape"));
	m_fillKind->addItem(tr("Shape"), QStringLiteral("shape"));
	m_fillKind->addItem(tr("Random"), QStringLiteral("random"));
	m_fillContour = new QComboBox(this);
	m_fillContour->setObjectName(QStringLiteral("stepGridFillContour"));
	m_fillContour->setAccessibleName(tr("Contour"));
	for (const auto& [text, data] : {std::pair<QString, QString>{tr("Walk"), QStringLiteral("walk")},
			 {tr("Rise"), QStringLiteral("rise")}, {tr("Fall"), QStringLiteral("fall")},
			 {tr("Arch"), QStringLiteral("arch")}, {tr("Valley"), QStringLiteral("valley")}})
	{
		m_fillContour->addItem(text, data);
	}
	m_fillContour->setCurrentIndex(m_fillContour->findData(QStringLiteral("arch")));
	m_fillSeed = new QSpinBox(this);
	m_fillSeed->setObjectName(QStringLiteral("stepGridFillSeed"));
	m_fillSeed->setAccessibleName(tr("Seed"));
	m_fillSeed->setRange(0, std::numeric_limits<int>::max());
	m_fillSeed->setKeyboardTracking(false);
	m_fillSpread = spinBox(this, "stepGridFillSpread", tr("Spread"), {0, 12});
	m_fillSpread->setValue(2);
	m_fillDensity = spinBox(this, "stepGridFillDensity", tr("Density"), {1, 100});
	m_fillDensity->setValue(100);
	m_fillDensity->setSuffix(QStringLiteral(" %"));
	m_fill = new QPushButton(tr("Fill"), this);
	m_fill->setObjectName(QStringLiteral("stepGridFill"));
	m_fill->setAccessibleName(tr("Fill"));
	// As the Remove button: Return in a dialog must not press the fill.
	m_fill->setAutoDefault(false);
	for (QWidget* control : std::initializer_list<QWidget*>{m_fillKind, m_fillContour, m_fillSeed, m_fillSpread, m_fillDensity, m_fill})
	{
		control->setProperty("controlCommand", QStringLiteral("note.step_fill"));
	}
	connect(m_fillKind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { showFill(); });
	connect(m_fill, &QPushButton::clicked, this, [this] { fillSteps(); });
	auto* row = new QHBoxLayout;
	for (auto [text, widget] : {std::pair<QString, QWidget*>{tr("Shape"), m_fillKind}, {tr("Contour"), m_fillContour},
			 {tr("Seed"), m_fillSeed}, {tr("Spread"), m_fillSpread}, {tr("Density"), m_fillDensity}})
	{
		auto* label = new QLabel(text, this);
		label->setBuddy(widget);
		row->addWidget(label);
		row->addWidget(widget);
	}
	row->addStretch();
	row->addWidget(m_fill);
	layout->addLayout(row);
	showFill();
}

void StepGridPanel::showFill()
{
	const bool open = !m_clipId.isEmpty();
	const bool shape = m_fillKind->currentData().toString() == QStringLiteral("shape");
	m_fillKind->setEnabled(open);
	m_fillSeed->setEnabled(open);
	m_fillSpread->setEnabled(open);
	m_fill->setEnabled(open);
	m_fillContour->setEnabled(open && shape);
	m_fillDensity->setEnabled(open && !shape);
}

void StepGridPanel::fillSteps()
{
	if (m_clipId.isEmpty()) { say(tr("Open a MIDI clip first.")); return; }
	QJsonObject args = readArgs();
	args.insert(QStringLiteral("kind"), m_fillKind->currentData().toString());
	args.insert(QStringLiteral("contour"), m_fillContour->currentData().toString());
	args.insert(QStringLiteral("seed"), m_fillSeed->value());
	args.insert(QStringLiteral("spread"), m_fillSpread->value());
	args.insert(QStringLiteral("density"), m_fillDensity->value());
	// The command is the whole edit and its own checkpoint; the clip's change repaints the grid through watchClip.
	const ControlResult written = run("note.step_fill", args);
	if (!written.ok) { say(written.errorMessage); return; }
	say(QString());
}

void StepGridPanel::fillScales()
{
	m_scale->addItem(tr("Project Key"), QString());
	m_scale->addItem(tr("No Scale"), QStringLiteral("none"));
	const ControlResult listed = run("scale.list", {});
	for (const QJsonValue& entry : listed.result.value(QStringLiteral("scales")).toArray())
	{
		const QString name = entry.toObject().value(QStringLiteral("name")).toString();
		m_scale->addItem(name, name);
	}
	m_scale->setCurrentIndex(0);
}

QString StepGridPanel::setClip(const QString& clipId)
{
	ControlResult error;
	control::ClipRef ref;
	MidiClip* clip = control::resolveMidiClip(clipId, &ref, &error);
	if (clip == nullptr) { return error.errorMessage; }
	m_clipId = clipId;
	m_selected.clear();
	watchClip(clip);
	refresh();
	return {};
}

void StepGridPanel::watchClip(MidiClip* clip)
{
	if (m_clip != nullptr) { disconnect(m_clip, nullptr, this, nullptr); }
	m_clip = clip;
	// An agent's note.step_set, a piano-roll edit and an undo all end in dataChanged: the grid re-reads.
	connect(clip, &MidiClip::dataChanged, this, [this] { refresh(); });
	// A deleted clip leaves nothing to read: the grid empties without asking the registry mid-destruction.
	connect(clip, &QObject::destroyed, this, [this] {
		m_clipId.clear();
		m_selected.clear();
		refresh();
	});
}

int StepGridPanel::stepTicks() const { return m_resolution->currentData().toInt(); }

bool StepGridPanel::setStepTicks(int ticks)
{
	if (ticks < 1) { return false; }
	if (m_resolution->findData(ticks) < 0) { m_resolution->addItem(tr("%1 ticks").arg(ticks), ticks); }
	m_resolution->setCurrentIndex(m_resolution->findData(ticks));
	refresh();
	return true;
}

bool StepGridPanel::setScaleChoice(const QString& scale, int root)
{
	const int index = m_scale->findData(scale);
	if (index < 0) { return false; }
	m_scale->setCurrentIndex(index);
	m_root->setCurrentIndex(std::clamp(root, 0, Octave - 1));
	refresh();
	return true;
}

QString StepGridPanel::scaleChoice() const { return m_scale->currentData().toString(); }

void StepGridPanel::setScaleRowsOnly(bool only)
{
	m_scaleRowsOnly = only;
	if (m_rowsOnly->isChecked() != only) { m_rowsOnly->setChecked(only); }
	computeRows();
	m_canvas->relayout();
}

//! The arguments every read and write carries: the clip, the resolution and the scale choice.
QJsonObject StepGridPanel::readArgs() const
{
	QJsonObject args{{QStringLiteral("clip"), m_clipId}, {QStringLiteral("step_ticks"), stepTicks()}};
	const QString scale = scaleChoice();
	if (scale == QStringLiteral("none")) { args.insert(QStringLiteral("scale"), QString()); }
	else if (!scale.isEmpty())
	{
		args.insert(QStringLiteral("scale"), scale);
		args.insert(QStringLiteral("root"), m_root->currentIndex());
	}
	return args;
}

void StepGridPanel::refresh()
{
	m_state = QJsonObject();
	m_classes.clear();
	m_root->setEnabled(scaleChoice() != QStringLiteral("none") && !scaleChoice().isEmpty());
	if (!m_clipId.isEmpty())
	{
		const ControlResult read = run("note.step_get_state", readArgs());
		if (read.ok) { m_state = read.result; } else { say(read.errorMessage); }
	}
	const QJsonObject scale = m_state.value(QStringLiteral("scale")).toObject();
	for (const QJsonValue& pc : scale.value(QStringLiteral("pitch_classes")).toArray()) { m_classes.append(pc.toInt()); }
	m_key->setPitchClasses(m_classes);
	m_title->setText(m_clipId.isEmpty() ? tr("No clip open") : tr("Clip %1").arg(m_clipId));
	computeRows();
	// The selection is kept by id even while its note is absent: an undo restores a clip by clearing it
	// first (one empty change) and then reloading every note with its id, and the selection comes back with it.
	showSelected();
	m_canvas->relayout();
}

//! Two octaves from the lowest note's C (C4 for an empty clip), widened to every note's key, highest first.
void StepGridPanel::computeRows()
{
	int low = EmptyLow;
	int high = EmptyLow + 2 * Octave - 1;
	const QJsonArray notes = notesOf(m_state);
	QVector<int> keys;
	for (const QJsonValue& note : notes) { keys.append(note.toObject().value(QStringLiteral("key")).toInt()); }
	if (!keys.isEmpty())
	{
		const auto [lowest, highest] = std::minmax_element(keys.begin(), keys.end());
		low = *lowest / Octave * Octave;
		high = std::min(127, std::max(low + 2 * Octave - 1, *highest));
	}
	m_rows.clear();
	for (int key = high; key >= low; --key)
	{
		if (!m_scaleRowsOnly || inScale(key) || keys.contains(key)) { m_rows.append(key); }
	}
}

int StepGridPanel::steps() const { return std::max(1, m_state.value(QStringLiteral("steps")).toInt()); }

bool StepGridPanel::inScale(int key) const { return m_classes.isEmpty() || m_classes.contains(key % Octave); }

QJsonObject StepGridPanel::noteAt(int step, int key) const
{
	for (const QJsonValue& value : notesOf(m_state))
	{
		const QJsonObject note = value.toObject();
		if (note.value(QStringLiteral("step")).toInt() == step && note.value(QStringLiteral("key")).toInt() == key) { return note; }
	}
	return {};
}

bool StepGridPanel::selectNote(const QString& noteId)
{
	for (const QJsonValue& value : notesOf(m_state))
	{
		if (value.toObject().value(QStringLiteral("note")).toString() != noteId) { continue; }
		m_selected = noteId;
		showSelected();
		m_canvas->update();
		return true;
	}
	return false;
}

//! The inspector follows the selected note; with none it is disabled.
void StepGridPanel::showSelected()
{
	QJsonObject note;
	for (const QJsonValue& value : notesOf(m_state))
	{
		if (value.toObject().value(QStringLiteral("note")).toString() == m_selected) { note = value.toObject(); }
	}
	m_filling = true;
	for (QWidget* control : std::initializer_list<QWidget*>{m_key, m_velocity, m_chance, m_ratchet, m_remove})
	{
		control->setEnabled(!note.isEmpty());
	}
	if (!note.isEmpty())
	{
		m_key->setValue(note.value(QStringLiteral("key")).toInt());
		m_velocity->setValue(note.value(QStringLiteral("velocity")).toInt());
		m_chance->setValue(static_cast<int>(std::lround(note.value(QStringLiteral("chance")).toDouble() * 100.0)));
		m_ratchet->setValue(note.value(QStringLiteral("ratchet")).toInt());
	}
	m_filling = false;
	showFill();
}

QString StepGridPanel::runSet(const QJsonArray& edits, QJsonObject* reply)
{
	if (m_clipId.isEmpty()) { const QString refusal = tr("Open a MIDI clip first."); say(refusal); return refusal; }
	QJsonObject args = readArgs();
	args.insert(QStringLiteral("edits"), edits);
	const ControlResult written = run("note.step_set", args);
	if (!written.ok) { say(written.errorMessage); return written.errorMessage; }
	say(QString());
	if (reply != nullptr) { *reply = written.result; }
	return {};
}

QString StepGridPanel::clickCell(int step, int key)
{
	const QJsonObject note = noteAt(step, key);
	if (!note.isEmpty())
	{
		selectNote(note.value(QStringLiteral("note")).toString());
		return {};
	}
	QJsonObject reply;
	const QString refusal = runSet({QJsonObject{{QStringLiteral("step"), step}, {QStringLiteral("key"), key}}}, &reply);
	const QJsonArray added = reply.value(QStringLiteral("notes")).toArray();
	if (refusal.isEmpty() && !added.isEmpty()) { selectNote(added.first().toString()); }
	return refusal;
}

QString StepGridPanel::removeCell(int step, int key)
{
	const QJsonObject note = noteAt(step, key);
	if (note.isEmpty()) { return tr("No note in that cell."); }
	return runSet({QJsonObject{{QStringLiteral("note"), note.value(QStringLiteral("note"))}, {QStringLiteral("remove"), true}}});
}

QString StepGridPanel::editSelected(const QJsonObject& fields)
{
	if (m_selected.isEmpty()) { const QString refusal = tr("Select a note first."); say(refusal); return refusal; }
	QJsonObject entry = fields;
	entry.insert(QStringLiteral("note"), m_selected);
	return runSet({entry});
}

QJsonObject StepGridPanel::describe() const
{
	QJsonArray cells;
	for (const QJsonValue& note : notesOf(m_state)) { cells.append(cellOf(note.toObject())); }
	QJsonArray rows;
	for (int key : m_rows) { rows.append(key); }
	return {{QStringLiteral("shown"), isVisible()}, {QStringLiteral("clip"), m_clipId},
		{QStringLiteral("step_ticks"), stepTicks()}, {QStringLiteral("steps"), steps()},
		{QStringLiteral("scale_choice"), scaleChoice()}, {QStringLiteral("scale_rows_only"), m_scaleRowsOnly},
		{QStringLiteral("scale"), m_state.value(QStringLiteral("scale"))}, {QStringLiteral("rows"), rows},
		{QStringLiteral("cells"), cells}, {QStringLiteral("selected"), m_selected}};
}

void StepGridPanel::say(const QString& message)
{
	m_status->setText(message);
	m_status->setVisible(!message.isEmpty());
}

} // namespace lmms::gui
